#include "dx_app.h"

#include <numeric>

#include "Lib/imgui/backends/imgui_impl_dx12.h"
#include "Lib/imgui/backends/imgui_impl_win32.h"
#include "../win32/winquake.h"
#include "dx_framegraphbuilder.h"
#include "dx_jobmultithreading.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace
{
	std::mutex drawDebugGuiMutex;

	inline void PushBackUvInd(const float* uvInd, std::vector<int>& indices, std::vector<XMFLOAT2>& texCoords)
	{
		texCoords.emplace_back(XMFLOAT2(uvInd[0], uvInd[1]));

		const int index = (reinterpret_cast<const int*>(uvInd))[2];
		indices.push_back(index);
	}

	void UnwindDynamicGeomIntoTriangleList(const int* order, std::vector<int>& indices, std::vector<XMFLOAT2>& texCoords)
	{
		while (true)
		{
			int vertCount = *order++;

			if (vertCount == 0)
			{
				break;
			}

			DX_ASSERT(vertCount * vertCount >= 9 && "Weird vert count, during dynamic geom transform");

			if (vertCount < 0)
			{
				// Negative vert count means treat vertices like triangle fan
				vertCount *= -1;

				const float* firstUvInd = reinterpret_cast<const float*>(order);
				const float* uvInd = firstUvInd + 3;
				for (int i = 1; i < vertCount - 1; ++i)
				{
					PushBackUvInd(firstUvInd, indices, texCoords);

					PushBackUvInd(uvInd, indices, texCoords);

					uvInd += 3;
					PushBackUvInd(uvInd, indices, texCoords);
				}
			}
			else
			{
				// Positive vert count means treat vertices like triangle strip
				const float* uvInd = reinterpret_cast<const float*>(order);

				bool swapOrder = true;

				for (int i = 0; i < vertCount - 2; ++i)
				{
					if (swapOrder)
					{
						PushBackUvInd(uvInd + 0 * 3, indices, texCoords);
						PushBackUvInd(uvInd + 1 * 3, indices, texCoords);
						PushBackUvInd(uvInd + 2 * 3, indices, texCoords);
					}
					else
					{
						PushBackUvInd(uvInd + 0 * 3, indices, texCoords);
						PushBackUvInd(uvInd + 2 * 3, indices, texCoords);
						PushBackUvInd(uvInd + 1 * 3, indices, texCoords);
					}

					uvInd += 3;
					swapOrder = !swapOrder;
				}

			}
			
			order += 3 * vertCount;
		}
	}

	void AddProbeDebugRaySegments(std::vector<DebugObject_t>& debugObjects, const std::vector<DiffuseProbe>& probeData, int probeIndex)
	{
		// NOTE: most likely you need to lock mutex for probe data outside of this function

		const DiffuseProbe& probe = probeData[probeIndex];

		if (probe.pathTracingSegments.has_value() == false)
		{
			return;
		}

		for (int segmentIndex = 0; segmentIndex < probe.pathTracingSegments->size(); ++segmentIndex)
		{
			DebugObject_ProbePathSegment object;
			object.probeIndex = probeIndex;
			object.segmentIndex = segmentIndex;
			object.bounce = probe.pathTracingSegments->at(segmentIndex).bounce;

			debugObjects.push_back(object);
		}
	}

	void AddPointLightSampleSegments(std::vector<DebugObject_t>& debugObjects, 
		const std::vector<DiffuseProbe>& probeData,
		int probeIndex,
		int pathIndex,
		int pointIndex,
		Renderer::DrawPathLightSampleMode_Type type)
	{
		// NOTE: most likely you need to lock mutex for probe data outside of this function

		const DiffuseProbe& probe = probeData[probeIndex];

		if (probe.lightSamples.has_value() == false)
		{
			return;
		}

		const LightSamplePoint& point = probe.lightSamples->at(pathIndex)[pointIndex];

		for (int sampleIndex = 0; sampleIndex < point.samples.size(); ++sampleIndex)
		{
			const LightSamplePoint::Sample& sample = point.samples[sampleIndex];

			if (type == Renderer::DrawPathLightSampleMode_Type::Point &&
				sample.lightType != Light::Type::Point)
			{
				continue;
			}

			if (type == Renderer::DrawPathLightSampleMode_Type::Area &&
				sample.lightType != Light::Type::Area)
			{
				continue;
			}

			DebugObject_ProbeLightSample object;
			object.probeIndex = probeIndex;
			object.pathIndex = pathIndex;
			object.pathPointIndex = pointIndex;		
			object.sampleIndex = sampleIndex;
			object.radiance = sample.radiance;

			debugObjects.push_back(object);
		}
	}

	// indices , texCoords, vertices reorder ind
	using NormalizedDynamGeom_t = std::tuple<std::vector<int>, std::vector<XMFLOAT2>, std::vector<int>>;

	NormalizedDynamGeom_t NormalizedDynamGeomVertTexCoord(const std::vector<int>& originalInd, const std::vector<XMFLOAT2>& originalTexCoord)
	{
		std::vector<int> reorderInd;
		std::vector<XMFLOAT2> reorderTexCoord;
		std::vector<int> reorderVert;

		if (originalInd.empty())
		{
			return std::make_tuple(reorderInd, reorderTexCoord, reorderVert);
		}

		DX_ASSERT(originalInd.size() % 3 == 0 && "Invalid indices in Tex Coord normalization");

		// Find max index
		int maxIndex = 0;
		for (int index : originalInd)
		{
			maxIndex = std::max(index, maxIndex);
		}

		const XMFLOAT2 initTexCoords = XMFLOAT2(-1.0f, -1.0f);
		const int initReorderVert = -1;

		reorderInd = originalInd;
		reorderTexCoord.resize(maxIndex + 1, initTexCoords);
		reorderVert.resize(maxIndex + 1, initReorderVert);

		for (int i = 0; i < originalInd.size(); ++i)
		{
			const int vertInd = originalInd[i];

			if (!Utils::VecEqual(initTexCoords, reorderTexCoord[vertInd]) &&
				!Utils::VecEqual(reorderTexCoord[vertInd], originalTexCoord[i]))
			{
				// Not uninitialized, and not the same tex coords. Means collision detected
				
				// Append original vert ind to the end. 
				reorderVert.push_back(vertInd);
				// Append new tex coord to the end
				reorderTexCoord.push_back(originalTexCoord[i]);
				// Now make index buffer to point on the end of our texCoord, vertexBuffers
				reorderInd[i] = reorderVert.size() - 1;
			}
			else
			{
				// No collision case
				// Vertex that is occupied initially should use this spot, as intended
				reorderVert[vertInd] = vertInd;
				reorderTexCoord[vertInd] = originalTexCoord[i];
				reorderInd[i] = vertInd;
			}

		}

		return std::make_tuple(reorderInd, reorderTexCoord, reorderVert);
	}

	std::vector<XMFLOAT4> NormalizeSingleFrameVertexData(const std::vector<int>& normalizedIndices, const std::vector<XMFLOAT4>& unnormalizedVertices)
	{
		std::vector<XMFLOAT4> normalizedVertices;
		normalizedVertices.reserve(normalizedIndices.size());

		for (int i = 0; i < normalizedIndices.size(); ++i)
		{
			const int unnormalizedVertInd = normalizedIndices[i];

			if (unnormalizedVertInd == -1)
			{
				DX_ASSERT(false && "Uninitialized vert ind");
				normalizedVertices.push_back(XMFLOAT4(0.0f, 0.0f, 0.0f, -1.0f));
			}
			else
			{
				normalizedVertices.push_back(unnormalizedVertices[unnormalizedVertInd]);
			}
		}

		return normalizedVertices;
	}
}

void Renderer::Init(WNDPROC WindowProc, HINSTANCE hInstance)
{
	InitWin32(WindowProc, hInstance);

	EnableDebugLayer();

	Infr::Inst().Init();

	InitDx();

	Load8To24Table();
}

void Renderer::InitWin32(WNDPROC WindowProc, HINSTANCE hInstance)
{
	if (hWindows)
	{
		ShutdownWin32();
	}

	const std::string windowsClassName = "Quake 2";

	int width = 0;
	int height = 0;
	GetDrawAreaSize(&width, &height);

	WNDCLASS windowClass;
	RECT	 screenRect;
	
	standardWndProc = WindowProc;
	
	windowClass.style			= 0;
	windowClass.lpfnWndProc	= MainWndProcWrapper;
	windowClass.cbClsExtra		= 0;
	windowClass.cbWndExtra		= 0;
	windowClass.hInstance		= hInstance;
	windowClass.hIcon			= 0;
	windowClass.hCursor		= LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_GRAYTEXT);
	windowClass.lpszMenuName	= 0;
	windowClass.lpszClassName  = static_cast<LPCSTR>(windowsClassName.c_str());

	ATOM classReg = RegisterClass(&windowClass);
	DX_ASSERT(classReg != 0 && "Failed to register win class.");

	screenRect.left = 0;
	screenRect.top = 0;
	screenRect.right = width;
	screenRect.bottom = height;

	// For now always run in windows mode (not fullscreen)
	const int exStyleBist = 0;
	const int styleBits = WINDOW_STYLE;

	AdjustWindowRect(&screenRect, styleBits, FALSE);

	const int adjustedWidth = screenRect.right - screenRect.left;
	const int adjustedHeight = screenRect.bottom - screenRect.top;

	char xPosVarName[] = "vid_xpos";
	char xPosVarVal[] = "0";
	cvar_t* vid_xpos = GetRefImport().Cvar_Get(xPosVarName, xPosVarVal, 0);
	char yPosVarName[] = "vid_ypos";
	char yPosVarVal[] = "0";
	cvar_t* vid_ypos = GetRefImport().Cvar_Get(yPosVarName, yPosVarVal, 0);
	const int x = vid_xpos->value;
	const int y = vid_ypos->value;

	 hWindows = CreateWindowEx(
		exStyleBist,
		windowsClassName.c_str(),
		windowsClassName.c_str(),
		styleBits,
		x, y, adjustedWidth, adjustedHeight,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	DX_ASSERT(hWindows && "Failed to create windows");

	ShowWindow(hWindows, SW_SHOW);
	UpdateWindow(hWindows);

	SetForegroundWindow(hWindows);
	SetFocus(hWindows);

	GetRefImport().Vid_NewWindow(width, height);
}

void Renderer::InitDx()
{
	Logs::Log(Logs::Category::Generic, "Dx init started");

	// ------ Pre frames init -----
	// Stuff that doesn't require frames for initialization
	SetDebugMessageFilter();

	CreateCommandQueue();

	CheckMSAAQualitySupport();

	CreateSwapChain();

	CreateDescriptorHeaps();
	
	CreateDescriptorHeapsAllocators();

	InitDescriptorSizes();

	CreateSwapChainBuffersAndViews();

	CreateTextureSampler();

	CreateFences(fence);

	InitCommandListsBuffer();

	InitUtils();

	InitDebugGui();

	// ------- Frames init -----

	InitFrames();

	// ------- Open init command list -----
	AcquireMainThreadFrame();
	GPUJobContext initContext = CreateContext(GetMainThreadFrame());

	initContext.commandList->Open();

	// -- Steps that require command list --

	MemoryManager::Inst().Init(initContext);

	// ------- Close and execute init command list -----

	initContext.commandList->Close();

	CloseFrame(initContext.frame);

	ReleaseFrameResources(initContext.frame);

	// We are done with that frame
	ReleaseFrame(initContext.frame);
	DetachMainThreadFrame();

	Logs::Log(Logs::Category::Generic, "Dx Init finished");
}

void Renderer::EnableDebugLayer()
{
	if (Settings::DEBUG_LAYER_ENABLED == false)
	{
		return;
	}

	ComPtr<ID3D12Debug> debugController;

	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	debugController->EnableDebugLayer();
}

void Renderer::SetDebugMessageFilter()
{
	if constexpr (!Settings::DEBUG_LAYER_ENABLED || !Settings::DEBUG_MESSAGE_FILTER_ENABLED)
	{
		return;
	}

	ComPtr<ID3D12InfoQueue> infoQueue;

	ComPtr<ID3D12Device> device = Infr::Inst().GetDevice();

	ThrowIfFailed(device.As(&infoQueue));

	D3D12_MESSAGE_ID denyId[] =
	{
		D3D12_MESSAGE_ID_CORRUPTED_PARAMETER2
	};

	D3D12_INFO_QUEUE_FILTER filter = {};
	filter.DenyList.NumIDs = _countof(denyId);
	filter.DenyList.pIDList = denyId;

	ThrowIfFailed(infoQueue->PushStorageFilter(&filter));

}

void Renderer::InitUtils()
{
	// Generate utils matrices
	int drawAreaWidth = 0;
	int drawAreaHeight = 0;

	GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);

	XMMATRIX sseResultMatrix = XMMatrixIdentity();
	sseResultMatrix.r[1] = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	sseResultMatrix = XMMatrixTranslation(-drawAreaWidth / 2, -drawAreaHeight / 2, 0.0f) * sseResultMatrix;
	XMStoreFloat4x4(&m_yInverseAndCenterMatrix, sseResultMatrix);

	// Init raw palette with 0
	std::fill(rawPalette.begin(), rawPalette.end(), 0);

	JobSystem::Inst().Init();

	InitScissorRect();

	ThreadingUtils::Init();

	LightBaker::Inst().Init();
}

void Renderer::InitScissorRect()
{
	int drawAreaWidth = 0;
	int drawAreaHeight = 0;

	GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);
	//#INFO scissor rectangle needs to be reset, every time command list is reset
	// Set scissor
	scissorRect = { 0, 0, drawAreaWidth, drawAreaHeight };
}

void Renderer::InitFrames()
{
	DX_ASSERT(Settings::SWAP_CHAIN_BUFFER_COUNT == Settings::FRAMES_NUM && "Swap chain buffer count shall be equal to frames num");

	for (int i = 0; i < Settings::FRAMES_NUM; ++i)
	{
		Frame& frame = frames[i];
		frame.Init(i);
	}
}

void Renderer::InitCommandListsBuffer()
{
	for (CommandList& commandList : commandListBuffer.commandLists)
	{
		commandList.Init();
		// We expect this command list to be closed after initialization
		commandList.Close();
	}
}

void Renderer::CreateDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc;

	heapDesc.NumDescriptors = Settings::RTV_DTV_DESCRIPTOR_HEAP_SIZE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heapDesc.NodeMask = 0;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(rtvHeap.GetAddressOf())));

	heapDesc.NumDescriptors = Settings::RTV_DTV_DESCRIPTOR_HEAP_SIZE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(dsvHeap.GetAddressOf())));

	heapDesc.NumDescriptors = Settings::CBV_SRV_DESCRIPTOR_HEAP_SIZE +
		Settings::FRAMES_NUM * Settings::FRAME_STREAMING_CBV_SRV_DESCRIPTOR_HEAP_SIZE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(cbvSrvHeap.GetAddressOf())));

	heapDesc.NumDescriptors = Settings::SAMPLER_DESCRIPTOR_HEAP_SIZE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(samplerHeap.GetAddressOf())));
}

void Renderer::InitDescriptorSizes()
{
	rtvDescriptorSize = GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	dsvDescriptorSize = GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	
	cbvSrvDescriptorSize = GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	samplerDescriptorSize = GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void Renderer::CreateDescriptorHeapsAllocators()
{
	rtvHeapAllocator = std::make_unique<std::remove_reference_t<decltype(*rtvHeapAllocator)>>(0);

	dsvHeapAllocator = std::make_unique<std::remove_reference_t<decltype(*dsvHeapAllocator)>>(0);

	cbvSrvHeapAllocator = std::make_unique<std::remove_reference_t<decltype(*cbvSrvHeapAllocator)>>(0);

	samplerHeapAllocator = std::make_unique<std::remove_reference_t<decltype(*samplerHeapAllocator)>>(0);
}

void Renderer::CreateSwapChainBuffersAndViews()
{
	for (int i = 0; i < Settings::SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		AssertBufferAndView& buffView = swapChainBuffersAndViews[i];

		// Get i-th buffer in a swap chain
		ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffView.buffer)));
		buffView.viewIndex = rtvHeapAllocator->Allocate(buffView.buffer.Get());

		Diagnostics::SetResourceName(buffView.buffer.Get(), 
			"Back buffer Index :" + std::to_string(i) +
			" ,View Index :" + std::to_string(buffView.viewIndex));

	}
}

void Renderer::CreateSwapChain()
{
	// Create swap chain
	swapChain.Reset();

	int drawAreaWidth = 0;
	int drawAreaHeight = 0;

	GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferDesc.Width = drawAreaWidth;
	swapChainDesc.BufferDesc.Height = drawAreaHeight;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = Settings::BACK_BUFFER_FORMAT;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = GetMSAASampleCount();
	swapChainDesc.SampleDesc.Quality = GetMSAAQuality();
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = Settings::SWAP_CHAIN_BUFFER_COUNT;
	swapChainDesc.OutputWindow = hWindows;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(Infr::Inst().GetFactory()->CreateSwapChain(commandQueue.Get(),
		&swapChainDesc,
		swapChain.GetAddressOf()));
}

void Renderer::CheckMSAAQualitySupport()
{
	if (Settings::MSAA_ENABLED == false)
		return;

	// Check 4X MSAA Quality Support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Format = Settings::BACK_BUFFER_FORMAT;
	qualityLevels.SampleCount = Settings::MSAA_SAMPLE_COUNT;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(Infr::Inst().GetDevice()->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&qualityLevels,
		sizeof(qualityLevels)
	));

	MSQualityLevels = qualityLevels.NumQualityLevels;
	DX_ASSERT(MSQualityLevels > 0 && "Unexpected MSAA quality levels");
}

void Renderer::CreateCommandQueue()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
	Diagnostics::SetResourceName(commandQueue.Get(), "Main Graphics Command Queue");
}

void Renderer::CreateFences(ComPtr<ID3D12Fence>& fence)
{
	// Create fence
	ThrowIfFailed(Infr::Inst().GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
}

void Renderer::CreateDepthStencilBuffer(ComPtr<ID3D12Resource>& buffer)
{
	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);

	// Create the depth/stencil buffer
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = DrawAreaWidth;
	depthStencilDesc.Height = DrawAreaHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthStencilDesc.SampleDesc.Count = GetMSAASampleCount();
	depthStencilDesc.SampleDesc.Quality = GetMSAAQuality();
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optimizedClearVal;
	optimizedClearVal.Format = Settings::DEPTH_STENCIL_FORMAT;
	optimizedClearVal.DepthStencil.Depth = 0.0f;
	optimizedClearVal.DepthStencil.Stencil = 0;

	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&optimizedClearVal,
		IID_PPV_ARGS(buffer.GetAddressOf())));
}

int Renderer::GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const
{
	return Infr::Inst().GetDevice()->GetDescriptorHandleIncrementSize(descriptorHeapType);
}

std::unique_ptr<FrameGraph> Renderer::RebuildFrameGraph(std::vector<FrameGraphSource::FrameGraphResourceDecl>& internalResourceDecl)
{
	Logs::Log(Logs::Category::Parser, "RebuildFrameGraph");

	FlushAllFrames();

	std::unique_ptr<FrameGraph> newFrameGraph = nullptr;

	try
	{
		FrameGraphBuilder::Inst().BuildFrameGraph(newFrameGraph, internalResourceDecl);
	}
	catch (Utils::Exception e)
	{
		frameGraphBuildMessage = "Frame Graph Build Failed!";

		newFrameGraph.reset(nullptr);
	}

	return newFrameGraph;
}

void Renderer::ReplaceFrameGraphAndCreateFrameGraphResources(const std::vector<FrameGraphSource::FrameGraphResourceDecl> internalResourceDecl, std::unique_ptr<FrameGraph>& newFrameGraph)
{
	DX_ASSERT(newFrameGraph != nullptr);

	// Delete old framegraph
	for (int i = 0; i < frames.size(); ++i)
	{
		frames[i].frameGraph.reset(nullptr);
	}

	// Now create new frame graph resources. It is important to do after deletion of the old
	// frame graph, because otherwise while destroyed old frame graph might delete new ones resources,
	// if they have the same name

	FrameGraphBuilder::Inst().CreateFrameGraphResources(internalResourceDecl, *newFrameGraph);

	for (int i = 0; i < frames.size(); ++i)
	{
		frames[i].frameGraph = std::make_unique<FrameGraph>(FrameGraph(*newFrameGraph));
	}
}

ID3D12DescriptorHeap* Renderer::GetRtvHeap()
{
	return rtvHeap.Get();
}

ID3D12DescriptorHeap* Renderer::GetDsvHeap()
{
	return dsvHeap.Get();
}

ID3D12DescriptorHeap* Renderer::GetCbvSrvHeap()
{
	return cbvSrvHeap.Get();
}

ID3D12DescriptorHeap* Renderer::GetSamplerHeap()
{
	return samplerHeap.Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Renderer::GetRtvHandleCPU(int index) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), index, rtvDescriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Renderer::GetRtvHandleGPU(int index) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(rtvHeap->GetGPUDescriptorHandleForHeapStart(), index, rtvDescriptorSize);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Renderer::GetDsvHandleCPU(int index) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvHeap->GetCPUDescriptorHandleForHeapStart(), index, dsvDescriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Renderer::GetDsvHandleGPU(int index) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(dsvHeap->GetGPUDescriptorHandleForHeapStart(), index, dsvDescriptorSize);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Renderer::GetCbvSrvHandleCPU(int index) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), index, cbvSrvDescriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Renderer::GetCbvSrvHandleGPU(int index) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), index, cbvSrvDescriptorSize);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Renderer::GetSamplerHandleCPU(int index) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(samplerHeap->GetCPUDescriptorHandleForHeapStart(), index, samplerDescriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Renderer::GetSamplerHandleGPU(int index) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(samplerHeap->GetGPUDescriptorHandleForHeapStart(), index, samplerDescriptorSize);
}

Frame& Renderer::GetMainThreadFrame()
{
	ASSERT_MAIN_THREAD;
	DX_ASSERT(currentFrameIndex != Const::INVALID_INDEX && "Trying to get current frame which is invalid");

	return frames[currentFrameIndex];
}

void Renderer::SubmitFrame(Frame& frame)
{
	DX_ASSERT(frame.acquiredCommandListsIndices.empty() == false && "Trying to execute empty command lists");

	std::vector<ID3D12CommandList*> commandLists(frame.acquiredCommandListsIndices.size(), nullptr);

	for (int i = 0; i < frame.acquiredCommandListsIndices.size(); ++i)
	{
		const int commandListIndex = frame.acquiredCommandListsIndices[i];
		commandLists[i] = commandListBuffer.commandLists[commandListIndex].GetGPUList();
	}

	commandQueue->ExecuteCommandLists(commandLists.size(), commandLists.data());

	DX_ASSERT(frame.executeCommandListFenceValue == -1 && frame.executeCommandListEvenHandle == INVALID_HANDLE_VALUE &&
		"Trying to set up sync primitives for frame that already has it");

	frame.executeCommandListFenceValue = GenerateFenceValue();
	frame.executeCommandListEvenHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

	commandQueue->Signal(fence.Get(), frame.executeCommandListFenceValue);
	ThrowIfFailed(fence->SetEventOnCompletion(frame.executeCommandListFenceValue, frame.executeCommandListEvenHandle));

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with frameNumber {} submitted", frame.frameNumber);
}

void Renderer::OpenFrame(Frame& frame) const
{
	frame.Acquire();
}

void Renderer::CloseFrame(Frame& frame)
{
	commandListBuffer.ValidateListsClosed(frame.acquiredCommandListsIndices);

	SubmitFrame(frame);
	WaitForFrame(frame);

	frame.ResetSyncData();
}

void Renderer::ReleaseFrameResources(Frame& frame)
{
	Logs::Logf(Logs::Category::FrameSubmission, "Frame with frameNumber {} releases resources", frame.frameNumber);

	for (int acquiredCommandListIndex : frame.acquiredCommandListsIndices)
	{
		commandListBuffer.allocator.Delete(acquiredCommandListIndex);
	}
	frame.acquiredCommandListsIndices.clear();

	frame.colorBufferAndView = nullptr;

	DO_IN_LOCK(frame.uploadResources, clear());

	frame.entities.clear();
	frame.particles.clear();

	frame.resourceCreationRequests.clear();

	// Remove used draw calls
	frame.uiDrawCalls.clear();
	frame.streamingCbvSrvAllocator->Reset();

	// Release debug objects
	frame.debugObjects.clear();

	// Release readback related stuff
	ResourceManager& resMan = ResourceManager::Inst();

	for (const ResourceReadBackRequest& request : frame.resourceReadBackRequests)
	{
		resMan.DeleteResource(Resource::GetReadbackResourceNameFromRequest(request, frame.frameNumber).c_str());
	}

	frame.resourceReadBackRequests.clear();

	// Release frame graph resource
	if (frame.frameGraph != nullptr)
	{
		frame.frameGraph->ReleasePerFrameResources(frame);
	}

	// Should always be last
	frame.frameNumber = Const::INVALID_INDEX;
}

void Renderer::AcquireMainThreadFrame()
{
	ASSERT_MAIN_THREAD;

	DX_ASSERT(currentFrameIndex == Const::INVALID_INDEX && "Trying to acquire frame, while there is already frame acquired.");

	// currentFrameIndex - index of frame that is used by main thread.
	//						 and also indicates if new frame shall be used
	// isInUse - if any thread is using this frame.

	std::vector <std::shared_ptr<Semaphore>> framesFinishedSemaphores;

	// Try to find free frame
	auto frameIt = frames.begin();
	for (; frameIt != frames.end(); ++frameIt)
	{
		// It is important to grab that before isInUse check, so we never end up in the situation where semaphore
		// was deleted right after check, but before we manage to pull it from frameIt
		std::shared_ptr<Semaphore> frameFinishedSemaphore = frameIt->GetFinishSemaphore();
		
		if (frameIt->GetIsInUse() == false)
		{
			OpenFrame(*frameIt);
			currentFrameIndex = std::distance(frames.begin(), frameIt);

			Logs::Logf(Logs::Category::FrameSubmission, "Frame with index {} acquired", currentFrameIndex);

			return;
		}
		
		framesFinishedSemaphores.push_back(std::move(frameFinishedSemaphore));
	}

	if (framesFinishedSemaphores.empty() == false)
	{
		Semaphore::WaitForMultipleAny(framesFinishedSemaphores);
	}

	// Try again after wait 
	frameIt = frames.begin();
	for (; frameIt != frames.end(); ++frameIt)
	{
		if (frameIt->GetIsInUse() == false)
		{
			break;
		}
	}

	DX_ASSERT(frameIt != frames.end() && "Can't find free frame");

	OpenFrame(*frameIt);
	currentFrameIndex = std::distance(frames.begin(), frameIt);

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with index {} acquired", currentFrameIndex);
}

void Renderer::DetachMainThreadFrame()
{
	ASSERT_MAIN_THREAD;

	DX_ASSERT(currentFrameIndex != Const::INVALID_INDEX && "Trying to detach frame. But there is nothing to detach.");

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with index {} and frameNumber {} detached", currentFrameIndex, frames[currentFrameIndex].frameNumber);

	currentFrameIndex = Const::INVALID_INDEX;
}

void Renderer::ReleaseFrame(Frame& frame)
{
	frame.Release();

	Logs::Log(Logs::Category::FrameSubmission, "Frame released Index: " + std::to_string(frame.GetArrayIndex()));
}

void Renderer::WaitForFrame(Frame& frame) const
{
	DX_ASSERT(frame.executeCommandListFenceValue != -1 && frame.executeCommandListEvenHandle != INVALID_HANDLE_VALUE &&
		"Trying to wait for frame that has invalid sync primitives.");

	if (fence->GetCompletedValue() < frame.executeCommandListFenceValue)
	{
		DWORD res = WaitForSingleObject(frame.executeCommandListEvenHandle, INFINITE);

		DX_ASSERT(res == WAIT_OBJECT_0 && "Frame wait ended in unexpected way.");
	}
}

void Renderer::WaitForPrevFrame(Frame& frame) const
{
	// Find prev frame
	const auto frameIt = std::find_if(frames.cbegin(), frames.cend(), 
		[&frame](const Frame& f)
	{
		return f.GetIsInUse() == true && f.frameNumber == frame.frameNumber - 1;
	});

	if (frameIt == frames.cend())
	{
		return;
	}

	if (std::shared_ptr<Semaphore> s = frameIt->GetFinishSemaphore())
	{
		s->Wait();
	}
}

int Renderer::GenerateFenceValue()
{
	return fenceValue++;
}

int Renderer::GetFenceValue() const
{
	return fenceValue;
}

void Renderer::SwitchToRequestedState()
{
	DX_ASSERT(requestedState.has_value() == true && "Can't switch to requested state it's empty");

	OnStateEnd(currentState);
	OnStateStart(*requestedState);

	currentState = *requestedState;

	requestedState.reset();
}

void Renderer::OnStateEnd(State state)
{
	switch (state)
	{
	case Renderer::State::Rendering:
		FlushAllFrames();
		break;
	case Renderer::State::LightBaking:
		LightBaker::Inst().PostBake();
		break;
	case State::LoadLightBakingFromFile:
		Renderer::Inst().ConsumeDiffuseIndirectLightingBakingResult(LightBaker::Inst().TransferBakingResult());
		break;
	case State::LevelLoading:
		break;
	default:
		break;
	}
}

void Renderer::OnStateStart(State state)
{
	switch(state)
	{
	case Renderer::State::Rendering:
		break;
	case Renderer::State::LightBaking:
	{
		// Prepare for baking
		LightBaker::Inst().PreBake();

		// Now I need to populate system with baking jobs, but make sure one thread remains free,
		// cause I still have some stuff to render
		const int workerThreasNum = JobSystem::Inst().GetWorkerThreadsNum();
		constexpr int reservedThreadsNum = 1;

		JobQueue& jobQueue = JobSystem::Inst().GetJobQueue();

		for (int i = 0; i < workerThreasNum - reservedThreadsNum; ++i)
		{
			jobQueue.Enqueue(Job([]() 
			{
				LightBaker::Inst().BakeJob();
			}));
		}

		break;
	}
	case Renderer::State::LoadLightBakingFromFile:
	{

		JobSystem::Inst().GetJobQueue().Enqueue(Job([]() 
		{
			LightBaker::Inst().LoadBakingResultsFromFileJob();
		}));

		break;
	}
	case State::LevelLoading:
		break;
	default:
		break;
	}
}

void Renderer::CreateTextureSampler()
{
	ViewDescription_t samplerDesc = D3D12_SAMPLER_DESC{};
	D3D12_SAMPLER_DESC& samplerDescRef = std::get<D3D12_SAMPLER_DESC>(samplerDesc);
	samplerDescRef.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDescRef.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescRef.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescRef.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescRef.MinLOD = 0;
	samplerDescRef.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDescRef.MipLODBias = 1;
	samplerDescRef.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	samplerHeapAllocator->Allocate(nullptr, &samplerDesc);
}

void Renderer::InitDebugGui()
{
	DX_ASSERT(hWindows != nullptr &&
		cbvSrvHeap != nullptr &&
		Infr::Inst().GetDevice() != nullptr &&
	"ImGUI init error. Some required components are not initialized");

	imGuiFontTexDescHandle = cbvSrvHeapAllocator->Allocate();

	ImGui::CreateContext();

	ImGui::StyleColorsLight();
	
	ImGui_ImplWin32_Init(hWindows);
	ImGui_ImplDX12_Init(
		Infr::Inst().GetDevice().Get(),
		Settings::FRAMES_NUM,
		Settings::BACK_BUFFER_FORMAT,
		cbvSrvHeap.Get(),
		GetCbvSrvHandleCPU(imGuiFontTexDescHandle),
		GetCbvSrvHandleGPU(imGuiFontTexDescHandle));
}

int Renderer::GetMSAASampleCount() const
{
	return Settings::MSAA_ENABLED ? Settings::MSAA_SAMPLE_COUNT : 1;
}

int Renderer::GetMSAAQuality() const
{
	return Settings::MSAA_ENABLED ? (MSQualityLevels - 1) : 0;
}

AssertBufferAndView& Renderer::GetNextSwapChainBufferAndView()
{
	AssertBufferAndView& buffAndView = swapChainBuffersAndViews[currentBackBuffer];
	currentBackBuffer = (currentBackBuffer + 1) % Settings::SWAP_CHAIN_BUFFER_COUNT;

	buffAndView.Lock();

	return buffAndView;
}

void Renderer::PresentAndSwapBuffers(Frame& frame)
{
	ThrowIfFailed(swapChain->Present(0, 0));

	frame.colorBufferAndView->Unlock();

}

void Renderer::RegisterObjectsAtFrameGraphs()
{
	// Explicitly register static geometry to passes
	// potentially dynamic will do the same

	// although it is not needed to acquire/release frames here, but for consistency all frames that 
	// are in use should go through this

	FlushAllFrames();

	// Acquire all frames and do registration 
	int frameIndex = 0;
	for(; frameIndex < frames.size(); ++frameIndex)
	{
		AcquireMainThreadFrame();

		GPUJobContext regContext = CreateContext(GetMainThreadFrame());

		regContext.commandList->Open();
		regContext.frame.frameGraph->RegisterStaticObjects(staticObjects, regContext);
		regContext.commandList->Close();

		CloseFrame(regContext.frame);
		ReleaseFrameResources(regContext.frame);

		// Detach but not release, so we will be forced to use all frames
		DetachMainThreadFrame();
	}

	DX_ASSERT(frameIndex == frames.size() && "Not all frames registered objects");

	// Now when registration is over release everything
	for (Frame& frame : frames)
	{
		frame.Release();
	}
}

void Renderer::CreateIndirectLightResources(GPUJobContext& context)
{
	CommandListRAIIGuard_t commandListGuard(*context.commandList);
	ResourceManager& resMan = ResourceManager::Inst();

	DX_ASSERT(lightBakingResult.obj.probes.empty() == false && "Can't create resource from empty lightprobes");
	DX_ASSERT(lightBakingResultGPUVersion < LightBaker::Inst().GetBakeVersion() && "We already have current version, why update?");

	// Probe data
	{
		if (resMan.FindResource(Resource::PROBE_STRUCTURED_BUFFER_NAME) != nullptr)
		{
			resMan.DeleteResource(Resource::PROBE_STRUCTURED_BUFFER_NAME);
		}

		int probeVectorSize = 0;
		std::vector<XMFLOAT4> probeGPUData;

		{
			std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);

			probeVectorSize = lightBakingResult.obj.probes.size() * sizeof(DiffuseProbe::DiffuseSH_t) / sizeof(XMFLOAT4);
			probeGPUData.reserve(probeVectorSize);

			for (const DiffuseProbe& probe : lightBakingResult.obj.probes)
			{
				for (const XMFLOAT4& probeCoeff : probe.radianceSh)
				{
					probeGPUData.push_back(probeCoeff);
				}
			}
		}

		ResourceDesc desc;
		desc.width = probeVectorSize * sizeof(XMFLOAT4);
		desc.height = 1;
		desc.format = DXGI_FORMAT_UNKNOWN;
		desc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.flags = D3D12_RESOURCE_FLAG_NONE;


		FArg::CreateStructuredBuffer args;
		args.context = &context;
		args.desc = &desc;
		args.data = reinterpret_cast<std::byte*>(probeGPUData.data());
		args.name = Resource::PROBE_STRUCTURED_BUFFER_NAME;

		resMan.CreateStructuredBuffer(args);
	}
	
	// Clusters Grid Probe Info
	{
		if (resMan.FindResource(Resource::CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME) != nullptr)
		{
			resMan.DeleteResource(Resource::CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME);
		}

		std::vector<ClusterProbeGridInfo> probeGridInfo = Renderer::Inst().GenBakeClusterProbeGridInfo();

		DX_ASSERT(probeGridInfo.empty() == false && "Can't generate cluster probe grid info. It's empty");

		ResourceDesc desc;
		desc.width = probeGridInfo.size() * sizeof(ClusterProbeGridInfo);
		desc.height = 1;
		desc.format = DXGI_FORMAT_UNKNOWN;
		desc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.flags = D3D12_RESOURCE_FLAG_NONE;

		FArg::CreateStructuredBuffer args;
		args.context = &context;
		args.desc = &desc;
		args.data = reinterpret_cast<std::byte*>(probeGridInfo.data());
		args.name = Resource::CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME;

		resMan.CreateStructuredBuffer(args);
	}

	lightBakingResultGPUVersion = lightBakingResultCPUVersion;
}

void Renderer::CreateClusteredLightingResources(GPUJobContext& context)
{
	CommandListRAIIGuard_t commandListGuard(*context.commandList);
	ResourceManager& resMan = ResourceManager::Inst();

	DX_ASSERT(resMan.FindResource(Resource::FRUSTUM_CLUSTERS_AABB_NAME) == nullptr &&
	"Trying create frustum cluster resources that already exist");

	const std::vector<Utils::AABB> frustumClusters = context.frame.camera.GenerateFrustumClusterInViewSpace(
		Camera::FRUSTUM_TILE_WIDTH,
		Camera::FRUSTUM_TILE_HEIGHT,
		Camera::FRUSTUM_CLUSTER_SLICES);

	ResourceDesc desc;
	desc.width = frustumClusters.size() * sizeof(Utils::AABB);
	desc.height = 1;
	desc.format = DXGI_FORMAT_UNKNOWN;
	desc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.flags = D3D12_RESOURCE_FLAG_NONE;

	FArg::CreateStructuredBuffer args;
	args.context = &context;
	args.desc = &desc;
	args.data = reinterpret_cast<const std::byte*>(frustumClusters.data());
	args.name = Resource::FRUSTUM_CLUSTERS_AABB_NAME;

	resMan.CreateStructuredBuffer(args);
}

void Renderer::CreateLightResources(const std::vector<GPULight>& gpuLights, const std::vector<GPULightBoundingVolume>& gpuBoundingVolumes, const std::vector<uint32_t>& pickedLightList, int frustumClustersNum, GPUJobContext& context) const
{
	CommandListRAIIGuard_t commandListGuard(*context.commandList);

	// Bounding volumes buffer
	DX_ASSERT(ResourceManager::Inst().FindResource(Resource::LIGHT_BOUNDING_VOLUME_LIST_NAME) == nullptr &&
		"Light bounding volume resource already exists");

	DX_ASSERT(gpuBoundingVolumes.empty() == false && "Trying to create resource with empty gpu bounding volumes lights list");

	ResourceDesc volumesBufferDesc;
	volumesBufferDesc.width = gpuBoundingVolumes.size() * sizeof(std::decay_t<decltype(gpuBoundingVolumes)>::value_type);
	volumesBufferDesc.height = 1;
	volumesBufferDesc.format = DXGI_FORMAT_UNKNOWN;
	volumesBufferDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	volumesBufferDesc.flags = D3D12_RESOURCE_FLAG_NONE;

	FArg::CreateStructuredBuffer volumeBufferArgs;
	volumeBufferArgs.context = &context;
	volumeBufferArgs.desc = &volumesBufferDesc;
	volumeBufferArgs.data = reinterpret_cast<const std::byte*>(gpuBoundingVolumes.data());
	volumeBufferArgs.name = Resource::LIGHT_BOUNDING_VOLUME_LIST_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(volumeBufferArgs);

	// Lights buffer
	DX_ASSERT(ResourceManager::Inst().FindResource(Resource::LIGHT_LIST_NAME) == nullptr &&
		"Light list resource already exists");

	DX_ASSERT(gpuLights.empty() == false && "Trying to create resource with empty gpu lights list");

	ResourceDesc lightBufferDesc;
	lightBufferDesc.width = gpuLights.size() * sizeof(std::decay_t<decltype(gpuLights)>::value_type);
	lightBufferDesc.height = 1;
	lightBufferDesc.format = DXGI_FORMAT_UNKNOWN;
	lightBufferDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	lightBufferDesc.flags = D3D12_RESOURCE_FLAG_NONE;

	FArg::CreateStructuredBuffer lightBufferArgs;
	lightBufferArgs.context = &context;
	lightBufferArgs.desc = &lightBufferDesc;
	lightBufferArgs.data = reinterpret_cast<const std::byte*>(gpuLights.data());
	lightBufferArgs.name = Resource::LIGHT_LIST_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(lightBufferArgs);

	DX_ASSERT(pickedLightList.empty() == false && "Forgot to init light picked data");

	ResourceDesc debugPickedLightsListDesc;
	debugPickedLightsListDesc.width = pickedLightList.size() * sizeof(std::decay_t<decltype(pickedLightList)>::value_type);
	debugPickedLightsListDesc.height = 1;
	debugPickedLightsListDesc.format = DXGI_FORMAT_UNKNOWN;
	debugPickedLightsListDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	debugPickedLightsListDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	FArg::CreateStructuredBuffer debugPickedLightsListArgs;
	debugPickedLightsListArgs.context = &context;
	debugPickedLightsListArgs.desc = &debugPickedLightsListDesc;
	debugPickedLightsListArgs.data = reinterpret_cast<const std::byte*>(pickedLightList.data());
	debugPickedLightsListArgs.name = Resource::DEBUG_PICKED_LIGHTS_LIST_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(debugPickedLightsListArgs);

	ResourceDesc clusteredLighting_globalLightIndicesDesc;
	clusteredLighting_globalLightIndicesDesc.width = Light::ClusteredLighting_GetGlobalLightIndicesElementsNum(gpuLights.size()) * sizeof(uint32_t);
	clusteredLighting_globalLightIndicesDesc.height = 1;
	clusteredLighting_globalLightIndicesDesc.format = DXGI_FORMAT_UNKNOWN;
	clusteredLighting_globalLightIndicesDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	clusteredLighting_globalLightIndicesDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	FArg::CreateStructuredBuffer clusteredLighting_globalLightIndicesArgs;
	clusteredLighting_globalLightIndicesArgs.context = &context;
	clusteredLighting_globalLightIndicesArgs.desc = &clusteredLighting_globalLightIndicesDesc;
	clusteredLighting_globalLightIndicesArgs.data = nullptr;
	clusteredLighting_globalLightIndicesArgs.name = Resource::CLUSTERED_LIGHTING_LIGHT_INDEX_GLOBAL_LIST_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(clusteredLighting_globalLightIndicesArgs);

	ResourceDesc clusteredLighting_perClusterLightDataDesc;
	clusteredLighting_perClusterLightDataDesc.width = frustumClustersNum * sizeof(Light::ClusterLightData);
	clusteredLighting_perClusterLightDataDesc.height = 1;
	clusteredLighting_perClusterLightDataDesc.format = DXGI_FORMAT_UNKNOWN;
	clusteredLighting_perClusterLightDataDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	clusteredLighting_perClusterLightDataDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	FArg::CreateStructuredBuffer clusteredLighting_perClusterLightDataArgs;
	clusteredLighting_perClusterLightDataArgs.context = &context;
	clusteredLighting_perClusterLightDataArgs.desc = &clusteredLighting_perClusterLightDataDesc;
	clusteredLighting_perClusterLightDataArgs.data = nullptr;
	clusteredLighting_perClusterLightDataArgs.name = Resource::CLUSTERED_LIGHTING_PER_CLUSTER_LIGH_DATA_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(clusteredLighting_perClusterLightDataArgs);

	ResourceDesc clusteredLighting_lightCullingDataDesc;
	clusteredLighting_lightCullingDataDesc.width = sizeof(std::decay_t<decltype(clusteredLighting_lightCullingData)>);
	clusteredLighting_lightCullingDataDesc.height = 1;
	clusteredLighting_lightCullingDataDesc.format = DXGI_FORMAT_UNKNOWN;
	clusteredLighting_lightCullingDataDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	clusteredLighting_lightCullingDataDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	FArg::CreateStructuredBuffer clusteredLighting_lightCullingDataArgs;
	clusteredLighting_lightCullingDataArgs.context = &context;
	clusteredLighting_lightCullingDataArgs.desc = &clusteredLighting_lightCullingDataDesc;
	clusteredLighting_lightCullingDataArgs.data = reinterpret_cast<const std::byte*>(&clusteredLighting_lightCullingData);
	clusteredLighting_lightCullingDataArgs.name = Resource::CLUSTERED_LIGHTING_LIGHT_CULLING_DATA_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(clusteredLighting_lightCullingDataArgs);
}

void Renderer::CreatePBRResources(GPUJobContext& context) const
{
	CommandListRAIIGuard_t commandListGuard(*context.commandList);

	DX_ASSERT(ResourceManager::Inst().FindResource(Resource::MATERIAL_LIST_NAME) == nullptr &&
		"Material list resource already exist");

	ResourceDesc resourceDesc;
	resourceDesc.width = Material::IDMaterials.size() * sizeof(Material);
	resourceDesc.height = 1;
	resourceDesc.format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.flags = D3D12_RESOURCE_FLAG_NONE;

	FArg::CreateStructuredBuffer bufferArgs;
	bufferArgs.context = &context;
	bufferArgs.desc = &resourceDesc;
	bufferArgs.data = reinterpret_cast<const std::byte*>(Material::IDMaterials.data());
	bufferArgs.name = Resource::MATERIAL_LIST_NAME;

	ResourceManager::Inst().CreateStructuredBuffer(bufferArgs);
}

void Renderer::CopyFromReadBackResourcesToCPUMemory(Frame& frame)
{
	ResourceManager& resMan = ResourceManager::Inst();

	for (ResourceReadBackRequest& request : frame.resourceReadBackRequests)
	{
		Resource* readBackResource = resMan.FindResource(Resource::GetReadbackResourceNameFromRequest(request, frame.frameNumber));

		DX_ASSERT(readBackResource != nullptr && "Can't find readback resource");
		DX_ASSERT(readBackResource->desc.height == 1 &&
			readBackResource->desc.format == DXGI_FORMAT_UNKNOWN &&
			"Invalid readback resource data");

		const int readBackSize = readBackResource->desc.width;
		std::byte* readBackData = nullptr;

		D3D12_RANGE readBackRange{ 0, static_cast<SIZE_T>(readBackSize) };
		ThrowIfFailed(readBackResource->gpuBuffer->Map(
			0,
			&readBackRange,
			reinterpret_cast<void**>(&readBackData)));

		memcpy(request.readBackCPUMemory, readBackData, readBackSize);

		// Do this, to indicate that CPU didn't write anything to the resource
		readBackRange.End = readBackRange.Begin;
		readBackResource->gpuBuffer->Unmap(0, &readBackRange);
	}
}

void Renderer::GenerateStaticLightBoundingVolumes(const std::vector<GPULight>& gpuLights)
{
	DX_ASSERT(
		staticAreaLights.empty() == false &&
		staticPointLights.empty() == false &&
		"Can't generate light bounding volumes, lights list is empty");

	DX_ASSERT(staticLightsBoundingVolumes.empty() == true && 
		"Static light bounding volumes list was never cleaned up");

	staticLightsBoundingVolumes.reserve(staticAreaLights.size() + staticPointLights.size());

	// NOTE: Lights must be the same order as in GenerateGPULightList() and GetLightIndexInStaticLightList()
	for (int i = 0; i < staticPointLights.size(); ++i)
	{
		LightBoundingVolume volume;
		volume.type = Light::Type::Point;
		volume.shape = PointLight::GetBoundingSphere(staticPointLights[i]);
		volume.sourceIndex = i;

		staticLightsBoundingVolumes.push_back(volume);
	}

	ResourceManager& resMan = ResourceManager::Inst();

	for (int i =0; i < staticAreaLights.size(); ++i)
	{
		const GPULight& gpuLight = gpuLights[GetLightIndexInStaticLightList(i, Light::Type::Area)];

		const XMFLOAT4 origin = XMFLOAT4(
			gpuLight.worldTransform.m[3][0],
			gpuLight.worldTransform.m[3][1],
			gpuLight.worldTransform.m[3][2],
			gpuLight.worldTransform.m[3][3]);

		LightBoundingVolume volume;
		volume.type = Light::Type::Area;
		volume.shape = AreaLight::GetBoundingSphere(staticAreaLights[i], 
			gpuLight.extends,
			origin);
		volume.sourceIndex = i;

		staticLightsBoundingVolumes.push_back(volume);
	}
}

void Renderer::CreateStaticLightDebugData(const std::vector<GPULight>& gpuLights)
{
	debugPickedStaticLights = std::vector<uint32_t>(gpuLights.size(), 0);
}

void Renderer::CreateClusteredLightData()
{
	clusteredLighting_lightCullingData = Light::ClusteredLighting_LightCullingData{};
}

int Renderer::GetGpuLightsNum() const
{
	return staticPointLights.size() + staticAreaLights.size();
}

std::vector<GPULight> Renderer::GenerateGPULightList() const
{
	DX_ASSERT(
		staticAreaLights.empty() == false &&
		staticPointLights.empty() == false &&
		"Can't generate GPU lights, lights list is empty");

	std::vector<GPULight> gpuLights;
	gpuLights.reserve(staticAreaLights.size() + staticPointLights.size());

	// NOTE: Lights must be the same order as in GenerateStaticLightBoundingVolumes() and GetLightIndexInStaticLightList()
	for (int i = 0; i < staticPointLights.size(); ++i)
	{
		gpuLights.push_back(PointLight::ToGPULight(staticPointLights[i]));
	}

	ResourceManager& resMan = ResourceManager::Inst();

	for (int i = 0; i < staticAreaLights.size(); ++i)
	{
		gpuLights.push_back(AreaLight::ToGPULight(staticAreaLights[i]));
	}

	DX_ASSERT(gpuLights.size() == GetGpuLightsNum() && "Mismatch between gpu lights list generation function and size function");

	return gpuLights;
}

std::vector<GPULightBoundingVolume> Renderer::GenerateGPULightBoundingVolumesList() const
{
	DX_ASSERT(staticLightsBoundingVolumes.empty() == false &&
		"Can't create light resources ");

	// Generate GPU light bounding volumes
	std::vector<GPULightBoundingVolume> gpuBoundingVolumes;
	gpuBoundingVolumes.reserve(staticLightsBoundingVolumes.size());

	for (const LightBoundingVolume& volume : staticLightsBoundingVolumes)
	{
		GPULightBoundingVolume gpuVolume;

		gpuVolume.origin = volume.shape.origin;
		gpuVolume.radius = volume.shape.radius;

		gpuBoundingVolumes.push_back(gpuVolume);
	}

	return gpuBoundingVolumes;
}

void Renderer::InitStaticLighting()
{
	for (AreaLight& light : staticAreaLights)
	{
		AreaLight::InitIfValid(light);
	}

	// Remove lights with 0.0 area or intensity
	staticAreaLights.erase(std::remove_if(staticAreaLights.begin(), staticAreaLights.end(), 
		[](const AreaLight& light) 
	{
		return light.area == 0.0f || 
		light.radiance == 0.0f;
	}), staticAreaLights.end());
}


Utils::MouseInput Renderer::GetMouseInput() const
{
	std::scoped_lock<std::mutex> lock(drawDebugGuiMutex);

	const ImGuiIO& io = ImGui::GetIO();

	Utils::MouseInput mouseInput;
	mouseInput.position.x = io.MousePos.x;
	mouseInput.position.y = io.MousePos.y;

	mouseInput.leftButtonClicked = io.MouseClicked[0];

	return mouseInput;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LONG WINAPI Renderer::MainWndProcWrapper(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if constexpr (Settings::DEBUG_GUI_ENABLED == true)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam) == 1)
		{
			return 1;
		}
	}

	DX_ASSERT(Renderer::Inst().standardWndProc != nullptr && "Standard input function is not found");

	return Renderer::Inst().standardWndProc(hWnd, uMsg, wParam, lParam);
}

std::vector<int> Renderer::BuildVisibleDynamicObjectsList(const Camera& camera, const std::vector<entity_t>& entities) const
{
	std::vector<int> visibleObjects;

	for (int i = 0; i < entities.size(); ++i)
	{
		const entity_t& entity = entities[i];

		constexpr int SKIP_FLAGS = RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM;

		if (entity.model != nullptr &&
			(entity.flags & SKIP_FLAGS) == 0 &&
			IsVisible(entity, camera) == true)
		{
			visibleObjects.push_back(i);
		}
	}

	return visibleObjects;
}

void Renderer::SetUpFrameDebugData(Frame& frame)
{
	if (debugSettings.fixFrustumClustersInPlace == false)
	{
		XMVECTOR sseDeterminant;
		XMMATRIX sseInvertedViewMatrix = XMMatrixInverse(&sseDeterminant,
			frame.camera.GenerateViewMatrix());

		DX_ASSERT(XMVectorGetX(sseDeterminant) != 0.0f && "Invalid matrix determinant");

		XMStoreFloat4x4(&debugSettings.frustumClustersInverseViewTransform, sseInvertedViewMatrix);

		// Active Clusters Readback 
		const int frustumClustersNum = frame.camera.GetFrustumClustersNum();

		if (frustumClustersNum > 0)
		{
			if (debugSettings.showActiveFrustumClusters == true)
			{
				debugSettings.activeFrustumClusters.resize(frustumClustersNum);

				ResourceReadBackRequest readBackRequest;
				readBackRequest.readBackCPUMemory = reinterpret_cast<std::byte*>(debugSettings.activeFrustumClusters.data());
				readBackRequest.targetPassName = "ClusteredLighting_MarkActiveClusters";
				readBackRequest.targetResourceName = "ClusteredLight_ActiveClusters";

				frame.resourceReadBackRequests.push_back(readBackRequest);
			}

			if (debugSettings.showFrustumClustersAffectedByPickedLights == true || debugSettings.showClustersAffectedByAnyLight == true)
			{
				const int gpuLightsNum = GetGpuLightsNum();

				debugSettings.clusteredLighting_globalLightIndices.resize(Light::ClusteredLighting_GetGlobalLightIndicesElementsNum(gpuLightsNum));
				debugSettings.clusteredLighting_perClusterLightData.resize(frustumClustersNum);

				ResourceReadBackRequest globalLightIndicesRequest;
				globalLightIndicesRequest.readBackCPUMemory = reinterpret_cast<std::byte*>(debugSettings.clusteredLighting_globalLightIndices.data());
				globalLightIndicesRequest.targetPassName = "ClusteredLighting_CreateClusterLightList";
				globalLightIndicesRequest.targetResourceName = Resource::CLUSTERED_LIGHTING_LIGHT_INDEX_GLOBAL_LIST_NAME;

				frame.resourceReadBackRequests.push_back(globalLightIndicesRequest);

				ResourceReadBackRequest perClusterLightRequest;
				perClusterLightRequest.readBackCPUMemory = reinterpret_cast<std::byte*>(debugSettings.clusteredLighting_perClusterLightData.data());
				perClusterLightRequest.targetPassName = "ClusteredLighting_CreateClusterLightList";
				perClusterLightRequest.targetResourceName = Resource::CLUSTERED_LIGHTING_PER_CLUSTER_LIGH_DATA_NAME;

				frame.resourceReadBackRequests.push_back(perClusterLightRequest);
			}
		}
	}

	frame.debugFrustumClusterInverseViewMat = debugSettings.frustumClustersInverseViewTransform;
	frame.debugEnableLightSourcePicker = debugSettings.enableLightSourcePicker;

	frame.mouseInput = GetMouseInput();

	frame.roughnessOverride = debugSettings.roughness;
	frame.metalinessOverride = debugSettings.metalness;
	frame.reflectanceOverride = debugSettings.reflectance;

	frame.useMaterialOverride = debugSettings.useMaterialOverrides;

	if (debugSettings.enableLightSourcePicker == true)
	{
		if (frame.mouseInput.leftButtonClicked == true)
		{
			ResourceReadBackRequest readBackRequest;
			readBackRequest.readBackCPUMemory = reinterpret_cast<std::byte*>(debugPickedStaticLights.data());
			readBackRequest.targetPassName = "Debug_LightSourcePicker";
			readBackRequest.targetResourceName = Resource::DEBUG_PICKED_LIGHTS_LIST_NAME;

			frame.resourceReadBackRequests.push_back(readBackRequest);
		}
	}
}

std::vector<DebugObject_t> Renderer::GenerateFrameDebugObjects(const Camera& camera)
{
	ASSERT_MAIN_THREAD;

	std::vector<DebugObject_t> debugObjects;
	// NOTE: order of registering here should match FrameGraph::RegisterGlobalObjectsResDebug
	if (debugSettings.drawLightProbesDebugGeometry == true)
	{
		// Figure out all clusters we want to generate for
		if (debugSettings.fixLightProbesDebugGeometryInTheSameCluster == false ||
			debugSettings.lightProbesDebugGeometryDisplayCluster == Const::INVALID_INDEX)
		{
			const BSPNode& cameraNode = Renderer::Inst().GetBSPTree().GetNodeWithPoint(camera.position);
			DX_ASSERT(cameraNode.cluster != Const::INVALID_INDEX && "Camera is located in invalid BSP node.");

			debugSettings.lightProbesDebugGeometryDisplayCluster = cameraNode.cluster;
		}

		const std::vector<XMFLOAT4> bakePoints = LightBaker::Inst().GenerateClusterBakePoints(debugSettings.lightProbesDebugGeometryDisplayCluster);

		DX_ASSERT(bakePoints.empty() == false && "Bake points are empty. Is this alright?");

		debugObjects.reserve(bakePoints.size());

		const BakingData& bakeResult = lightBakingResult.obj;

		const bool hasProbeDataOnGPU =
			ResourceManager::Inst().FindResource(Resource::PROBE_STRUCTURED_BUFFER_NAME) != nullptr;

		for (int i = 0; i < bakePoints.size(); ++i)
		{
			DebugObject_LightProbe object;

			object.probeIndex = Const::INVALID_INDEX;
			object.position = bakePoints[i];

			if (hasProbeDataOnGPU)
			{
				switch (*bakeResult.bakingMode)
				{
				case LightBakingMode::CurrentPositionCluster:
				{
					DX_ASSERT(bakeResult.bakingCluster.has_value() == true && "Baking result should have value");

					if (*bakeResult.bakingCluster == debugSettings.lightProbesDebugGeometryDisplayCluster)
					{
						object.probeIndex = i;
					}
				}
				break;
				case LightBakingMode::AllClusters:
				case LightBakingMode::CurrentAndNeighbouringClusters:
				{
					DX_ASSERT(bakeResult.clusterFirstProbeIndices.empty() == false && "Cluster sizes should have value");

					const int clusterFirstProbeIndex = bakeResult.clusterFirstProbeIndices[debugSettings.lightProbesDebugGeometryDisplayCluster];

					if (clusterFirstProbeIndex == Const::INVALID_INDEX)
					{
						object.probeIndex = Const::INVALID_INDEX;
					}
					else
					{
						object.probeIndex = i + clusterFirstProbeIndex;
					}
				}
				break;
				default:
					DX_ASSERT(false && "Unknown baking mode");
					break;
				}
			}

			debugObjects.push_back(object);
		}
	}

	if (debugSettings.drawLightSourcesDebugGeometry == true) 
	{
		for (int i = 0; i < staticPointLights.size(); ++i)
		{
			DebugObject_LightSource object;
			object.sourceIndex = i;
			object.type = Light::Type::Point;
			object.showRadius = debugSettings.drawPointLightObjectRadius;
			object.showApproximation = false;

			debugObjects.push_back(object);
		}

		for (int i = 0; i < staticAreaLights.size(); ++i)
		{
			DebugObject_LightSource object;
			object.sourceIndex = i;
			object.type = Light::Type::Area;
			object.showRadius = false;
			object.showApproximation = debugSettings.drawAreaLightApproximation;

			debugObjects.push_back(object);
		}
	}

	if (debugSettings.drawPointLightBoundingVolume)
	{
		if (debugSettings.enableLightSourcePicker)
		{
			for (int i = 0; i < debugPickedStaticLights.size(); ++i)
			{
				if (debugPickedStaticLights[i] == 0)
				{
					continue;
				}

				const LightBoundingVolume& volume = staticLightsBoundingVolumes[i];

				if (volume.type == Light::Type::Point)
				{
					DebugObject_LightBoundingVolume object;
					object.sourceIndex = i;
					object.type = volume.type;

					debugObjects.push_back(object);
				}
			}
		}
		else
		{
			for (int i = 0; i < staticLightsBoundingVolumes.size(); ++i)
			{
				const LightBoundingVolume& volume = staticLightsBoundingVolumes[i];

				if (volume.type == Light::Type::Point)
				{
					DebugObject_LightBoundingVolume object;
					object.sourceIndex = i;
					object.type = volume.type;

					debugObjects.push_back(object);
				}
			}
		}
	}

	if (debugSettings.drawAreaLightBoundingVolume)
	{
		for (int i = 0; i < staticLightsBoundingVolumes.size(); ++i)
		{
			const LightBoundingVolume& volume = staticLightsBoundingVolumes[i];

			if (volume.type == Light::Type::Area)
			{
				DebugObject_LightBoundingVolume object;
				object.sourceIndex = i;
				object.type = volume.type;

				debugObjects.push_back(object);
			}
		}
	}

	if (debugSettings.drawBakeRayPaths == true)
	{
		std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);

		if (lightBakingResult.obj.probes.empty() == false)
		{
			switch (debugSettings.drawBakeRayPathsMode)
			{
			case Renderer::DrawRayPathMode::AllClusterProbes:
			{
				for (int probeIndex = 0; probeIndex < lightBakingResult.obj.probes.size(); ++probeIndex)
				{
					AddProbeDebugRaySegments(debugObjects, lightBakingResult.obj.probes, probeIndex);
				}

				break;
			}
			case Renderer::DrawRayPathMode::SingleProbe:
			{
				DX_ASSERT(debugSettings.drawBakeRayPathsProbeIndex < lightBakingResult.obj.probes.size() && "Invalid probe index");

				AddProbeDebugRaySegments(debugObjects, lightBakingResult.obj.probes, debugSettings.drawBakeRayPathsProbeIndex);

				break;
			}
			default:
				DX_ASSERT(false && "Invalid draw ray path mode");
				break;
			}
		}
	}

	if (debugSettings.drawLightPathSamples == true)
	{
		std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);

		if (lightBakingResult.obj.probes.empty() == false)
		{
			switch (debugSettings.drawPathLightSampleMode_Scale)
			{
			case Renderer::DrawPathLightSampleMode_Scale::AllSamples:
			{
				for (int probeIndex = 0; probeIndex < lightBakingResult.obj.probes.size(); ++probeIndex)
				{
					const DiffuseProbe& probe = lightBakingResult.obj.probes[probeIndex];

					if (probe.lightSamples.has_value() == false)
					{
						continue;
					}

					for (int pathIndex = 0; pathIndex < probe.lightSamples->size(); ++pathIndex)
					{
						const PathLightSampleInfo_t& path = probe.lightSamples->at(pathIndex);

						for (int pointIndex = 0; pointIndex < path.size(); ++pointIndex)
						{
							AddPointLightSampleSegments(
								debugObjects,
								lightBakingResult.obj.probes,
								probeIndex,
								pathIndex,
								pointIndex,
								debugSettings.drawPathLightSampleMode_Type);
						}
					}
				}

				break;
			}
			case Renderer::DrawPathLightSampleMode_Scale::ProbeSamples:
			{
				const DiffuseProbe& probe = lightBakingResult.obj.probes[debugSettings.drawPathLightSamples_ProbeIndex];

				if (probe.lightSamples.has_value() == true)
				{
					for (int pathIndex = 0; pathIndex < probe.lightSamples->size(); ++pathIndex)
					{
						const PathLightSampleInfo_t& path = probe.lightSamples->at(pathIndex);

						for (int pointIndex = 0; pointIndex < path.size(); ++pointIndex)
						{
							AddPointLightSampleSegments(
								debugObjects, 
								lightBakingResult.obj.probes,
								debugSettings.drawPathLightSamples_ProbeIndex,
								pathIndex,
								pointIndex,
								debugSettings.drawPathLightSampleMode_Type);
						}
					}
				}

				break;
			}
			case Renderer::DrawPathLightSampleMode_Scale::PathSamples:
			{
				const DiffuseProbe& probe = lightBakingResult.obj.probes[debugSettings.drawPathLightSamples_ProbeIndex];

				if (probe.lightSamples.has_value() == true)
				{
					const PathLightSampleInfo_t& path = probe.lightSamples->at(debugSettings.drawPathLightSamples_PathIndex);

					for (int pointIndex = 0; pointIndex < path.size(); ++pointIndex)
					{
						AddPointLightSampleSegments(
							debugObjects,
							lightBakingResult.obj.probes,
							debugSettings.drawPathLightSamples_ProbeIndex,
							debugSettings.drawPathLightSamples_PathIndex,
							pointIndex,
							debugSettings.drawPathLightSampleMode_Type);
					}
				}

				break;
			}
			case Renderer::DrawPathLightSampleMode_Scale::PointSamples:
			{
				const DiffuseProbe& probe = lightBakingResult.obj.probes[debugSettings.drawPathLightSamples_ProbeIndex];

				if (probe.lightSamples.has_value() == true)
				{
					const PathLightSampleInfo_t& path = probe.lightSamples->at(debugSettings.drawPathLightSamples_PathIndex);

					AddPointLightSampleSegments(
						debugObjects,
						lightBakingResult.obj.probes,
						debugSettings.drawPathLightSamples_ProbeIndex,
						debugSettings.drawPathLightSamples_PathIndex,
						debugSettings.drawPathLightSamples_PointIndex,
						debugSettings.drawPathLightSampleMode_Type);
				}

				break;
			}
			default:
				DX_ASSERT(false && "Invalid draw light sample mode");
				break;
			}
		}
	}

	if (debugSettings.drawFrustumClusters == true)
	{
		const int numTilesX = camera.width / Camera::FRUSTUM_TILE_WIDTH;
		const int numTilesY = camera.height / Camera::FRUSTUM_TILE_HEIGHT;
		const int numTilesZ = Camera::FRUSTUM_CLUSTER_SLICES;

		const int numCluster = numTilesX * numTilesY *
			numTilesZ;

		std::vector<uint32_t> frustumClustersAffectedByLightsIndices;

		if (debugSettings.showFrustumClustersAffectedByPickedLights == true)
		{
			// Find clusters affected by picked light
 			for (int pickedLightIndex = 0; pickedLightIndex < debugPickedStaticLights.size(); ++pickedLightIndex)
			{
				if (debugPickedStaticLights[pickedLightIndex] == false)
				{
					continue;
				}

				for (int clusterIndex = 0; clusterIndex < debugSettings.clusteredLighting_perClusterLightData.size(); ++clusterIndex)
				{
					const Light::ClusterLightData& perClusterLightData = debugSettings.clusteredLighting_perClusterLightData[clusterIndex];

					const auto beginSearchItr = debugSettings.clusteredLighting_globalLightIndices.cbegin() + perClusterLightData.offset;
					const auto endSearchItr = beginSearchItr + perClusterLightData.count;

					const auto lightIndexItr = std::find(beginSearchItr, endSearchItr, pickedLightIndex);

					if (lightIndexItr != endSearchItr)
					{
						frustumClustersAffectedByLightsIndices.push_back(clusterIndex);
					}
				}
			}
		}

		std::vector<uint32_t> frustumClustersAffectedByAnyLightsIndices;

		if (debugSettings.showClustersAffectedByAnyLight)
		{
			for (int clusterIndex = 0; clusterIndex < debugSettings.clusteredLighting_perClusterLightData.size(); ++clusterIndex)
			{
				const Light::ClusterLightData& perClusterLightData = debugSettings.clusteredLighting_perClusterLightData[clusterIndex];

				if (perClusterLightData.count > 0)
				{
					frustumClustersAffectedByAnyLightsIndices.push_back(clusterIndex);
				}
			}
		}

		for (int i = 0; i < numCluster; ++i)
		{
			DebugObject_FrustumCluster object;
			object.index = i;

			if (debugSettings.activeFrustumClusters.empty() == false && debugSettings.showActiveFrustumClusters == true)
			{
				DX_ASSERT(debugSettings.activeFrustumClusters.size() > object.index && "Invalid active cluster array size");
				object.isActive = static_cast<bool>(debugSettings.activeFrustumClusters[object.index]);
			}
			else
			{
				object.isActive = false;
			}

			if (frustumClustersAffectedByLightsIndices.empty() == false && debugSettings.showFrustumClustersAffectedByPickedLights == true)
			{
				const auto clusterIt = std::find(frustumClustersAffectedByLightsIndices.cbegin(), frustumClustersAffectedByLightsIndices.cend(), i);
				object.isAffectedByLight = clusterIt != frustumClustersAffectedByLightsIndices.cend();
			}
			else
			{
				object.isAffectedByLight = false;
			}

			if (frustumClustersAffectedByAnyLightsIndices.empty() == false && debugSettings.showClustersAffectedByAnyLight == true)
			{
				const auto clusterIt = std::find(frustumClustersAffectedByAnyLightsIndices.cbegin(), frustumClustersAffectedByAnyLightsIndices.cend(), i);
				object.isAffectedByAnyLight = clusterIt != frustumClustersAffectedByAnyLightsIndices.cend();
			}
			else
			{
				object.isAffectedByAnyLight = false;
			}

			debugObjects.push_back(object);
		}
	}

	return debugObjects;
}

const std::vector<SourceStaticObject>& Renderer::GetSourceStaticObjects() const
{
	return sourceStaticObjects;
}

const std::vector<AreaLight>& Renderer::GetStaticAreaLights() const
{
	return staticAreaLights;
}

const std::vector<PointLight>& Renderer::GetStaticPointLights() const
{
	return staticPointLights;
}

const std::vector<LightBoundingVolume>& Renderer::GetLightBoundingVolumes() const
{
	return staticLightsBoundingVolumes;
}

void Renderer::RequestStateChange(State state)
{
	DX_ASSERT(requestedState.has_value() == false && "State Already requested");

	requestedState = state;
}

Renderer::State Renderer::GetState() const
{
	return currentState;
}

void Renderer::EndFrameJob(GPUJobContext& context)
{
	Frame& frame = context.frame;
	// Make sure everything else is done
	context.WaitDependency();

	// We can't submit command lists attached to render target that is not current back buffer,
	// so we need to wait until previous frame is done
	WaitForPrevFrame(context.frame);

	CloseFrame(frame);

	PresentAndSwapBuffers(frame);

	CopyFromReadBackResourcesToCPUMemory(frame);

	ReleaseFrameResources(frame);
	
	ReleaseFrame(frame);

	ThreadingUtils::AssertUnlocked(context.frame.uploadResources);

	Logs::Log(Logs::Category::Job, "EndFrame job ended");
}

void Renderer::DrawDebugGuiJob(GPUJobContext& context)
{
	JOB_GUARD(context);

	context.WaitDependency();

	if constexpr (Settings::DEBUG_GUI_ENABLED == false)
	{
		return;
	}

	LightBaker& lightBaker = LightBaker::Inst();

	ID3D12GraphicsCommandList* gpuCommanList = context.commandList->GetGPUList();

	Diagnostics::BeginEvent(gpuCommanList, "Debug GUI");
	Logs::Log(Logs::Category::Job, "DrawDebugGui job started");

	// ImGui is not thread safe so I have to block here. Another choice would be to create a separate context
	// for every frame
	std::scoped_lock<std::mutex> lock(drawDebugGuiMutex);
	
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	{
		{
			ImGui::Begin("Debug GUI");

			if (GetState() == State::Rendering)
			{
				if (ImGui::CollapsingHeader("Debug geometry"))
				{
					ImGui::Indent();

					ImGui::Checkbox("Light Probes", &debugSettings.drawLightProbesDebugGeometry);

					if (debugSettings.drawLightProbesDebugGeometry == true)
					{
						ImGui::Indent();
						ImGui::Checkbox("Fix in place", &debugSettings.fixLightProbesDebugGeometryInTheSameCluster);
						ImGui::Unindent();
					}

					ImGui::Checkbox("Light Sources", &debugSettings.drawLightSourcesDebugGeometry);

					if (debugSettings.drawLightSourcesDebugGeometry == true)
					{
						ImGui::Indent();
						ImGui::Checkbox("Enable light source picker", &debugSettings.enableLightSourcePicker);
						ImGui::NewLine();

						ImGui::Checkbox("Point light object physical radius", &debugSettings.drawPointLightObjectRadius);
						ImGui::Checkbox("Area light approximation", &debugSettings.drawAreaLightApproximation);

						ImGui::NewLine();

						{
							std::string pointLightBoundingVolumeLabel = debugSettings.enableLightSourcePicker == true ?
								"Point light bounding volumes (for selected light)" : "Point light bounding volumes";

							ImGui::Checkbox(pointLightBoundingVolumeLabel.c_str(), &debugSettings.drawPointLightBoundingVolume);
							ImGui::Checkbox("Area light bounding volumes", &debugSettings.drawAreaLightBoundingVolume);
						}

						ImGui::Unindent();
					}

					ImGui::Checkbox("Frustum clusters", &debugSettings.drawFrustumClusters);

					if (debugSettings.drawFrustumClusters == true)
					{
						ImGui::Indent();
						ImGui::Checkbox("Fix in place", &debugSettings.fixFrustumClustersInPlace);
						ImGui::Checkbox("Show active clusters", &debugSettings.showActiveFrustumClusters);
						ImGui::Checkbox("Show clusters affected by any light", &debugSettings.showClustersAffectedByAnyLight);

						if (debugSettings.enableLightSourcePicker == true)
						{
							ImGui::Indent();

							ImGui::Checkbox("Show frustum clusters affected by picked light", &debugSettings.showFrustumClustersAffectedByPickedLights);

							ImGui::Unindent();
						}

						ImGui::Unindent();
					}

					ImGui::Unindent();
				}

				ImGui::Separator();

				if (ImGui::CollapsingHeader("Light baking debug display settings"))
				{
					ImGui::Indent();
					if (ImGui::CollapsingHeader("Path segments display settings"))
					{
						ImGui::Indent();

						{
							int drawRayMode = static_cast<int>(debugSettings.drawBakeRayPathsMode);

							ImGui::RadioButton("Single probe", &drawRayMode, static_cast<int>(DrawRayPathMode::SingleProbe));
							ImGui::SameLine();
							ImGui::RadioButton("All probes", &drawRayMode, static_cast<int>(DrawRayPathMode::AllClusterProbes));

							debugSettings.drawBakeRayPathsMode = static_cast<DrawRayPathMode>(drawRayMode);
						}

						ImGui::Text("Probes num: %d", lightBakingResult.obj.probes.size());
						ImGui::InputInt("Probe ray index", &debugSettings.drawBakeRayPathsProbeIndex);
						debugSettings.drawBakeRayPathsProbeIndex = std::max(0, std::min<int>(lightBakingResult.obj.probes.size() - 1, 
							debugSettings.drawBakeRayPathsProbeIndex));

						ImGui::Checkbox("Show Ray Paths", &debugSettings.drawBakeRayPaths);

						ImGui::Unindent();
					}

					ImGui::Separator();

					if (ImGui::CollapsingHeader("Light sampling display settings"))
					{
						ImGui::Indent();

						{
							int drawLightSamplesMode = static_cast<int>(debugSettings.drawPathLightSampleMode_Scale);

							ImGui::RadioButton("Single Probe", &drawLightSamplesMode, static_cast<int>(DrawPathLightSampleMode_Scale::ProbeSamples));
							ImGui::SameLine();
							ImGui::RadioButton("Single Path", &drawLightSamplesMode, static_cast<int>(DrawPathLightSampleMode_Scale::PathSamples));
							ImGui::SameLine();
							ImGui::RadioButton("Single Point", &drawLightSamplesMode, static_cast<int>(DrawPathLightSampleMode_Scale::PointSamples));
							ImGui::SameLine();
							ImGui::RadioButton("All Samples", &drawLightSamplesMode, static_cast<int>(DrawPathLightSampleMode_Scale::AllSamples));

							debugSettings.drawPathLightSampleMode_Scale = static_cast<DrawPathLightSampleMode_Scale>(drawLightSamplesMode);
						}

						{
							int drawLightSampleMode = static_cast<int>(debugSettings.drawPathLightSampleMode_Type);

							ImGui::RadioButton("Point", &drawLightSampleMode, static_cast<int>(DrawPathLightSampleMode_Type::Point));
							ImGui::SameLine();
							ImGui::RadioButton("Area", &drawLightSampleMode, static_cast<int>(DrawPathLightSampleMode_Type::Area));
							ImGui::SameLine();
							ImGui::RadioButton("All", &drawLightSampleMode, static_cast<int>(DrawPathLightSampleMode_Type::All));

							debugSettings.drawPathLightSampleMode_Type = static_cast<DrawPathLightSampleMode_Type>(drawLightSampleMode);
						}

						ImGui::Text("Probes num: %d", lightBakingResult.obj.probes.size());
						ImGui::InputInt("Probe index", &debugSettings.drawPathLightSamples_ProbeIndex);
						debugSettings.drawPathLightSamples_ProbeIndex = std::max(0, std::min<int>(lightBakingResult.obj.probes.size() - 1, 
							debugSettings.drawPathLightSamples_ProbeIndex));

						const bool isLightSamplePathDataExists = lightBakingResult.obj.probes.empty() == false &&
							lightBakingResult.obj.probes[debugSettings.drawPathLightSamples_ProbeIndex].lightSamples.has_value();

						const int lightSamplePathNum = isLightSamplePathDataExists ?
							lightBakingResult.obj.probes[debugSettings.drawPathLightSamples_ProbeIndex].lightSamples->size() : 0;

						ImGui::Text("Paths num: %d", lightSamplePathNum);
						ImGui::InputInt("Path index", &debugSettings.drawPathLightSamples_PathIndex);
						debugSettings.drawPathLightSamples_PathIndex = std::max(0, std::min<int>(lightSamplePathNum - 1, debugSettings.drawPathLightSamples_PathIndex));

						const int lightSamplePointsNum = isLightSamplePathDataExists ?
							lightBakingResult.obj.probes[debugSettings.drawPathLightSamples_ProbeIndex].lightSamples->at(debugSettings.drawPathLightSamples_PathIndex).size() : 0;

						ImGui::Text("Sample points num: %d", lightSamplePointsNum);
						ImGui::InputInt("Sample point index ", &debugSettings.drawPathLightSamples_PointIndex);
						debugSettings.drawPathLightSamples_PointIndex = std::max(0, std::min<int>(lightSamplePointsNum - 1, debugSettings.drawPathLightSamples_PointIndex));

						ImGui::Checkbox("Show Light Sample Rays", &debugSettings.drawLightPathSamples);

						ImGui::Unindent();
					}
					
					ImGui::Unindent();
				}

				ImGui::Separator();

				if (ImGui::CollapsingHeader("Light Baking"))
				{
					ImGui::Indent();

					{
						ImGui::Text("Bake Options");

						ImGui::Text("Only use this option if baking for camera cluster");

						bool saveBakeRayPaths = lightBaker.GetBakeFlag(BakeFlags::SaveRayPath);

						if (ImGui::Checkbox("Save Bake Ray Paths", &saveBakeRayPaths))
						{
							lightBaker.SetBakeFlag(BakeFlags::SaveRayPath, saveBakeRayPaths);
						}

						bool saveLightSamples = lightBaker.GetBakeFlag(BakeFlags::SaveLightSampling);

						if (ImGui::Checkbox("Save Light Samples", &saveLightSamples))
						{
							lightBaker.SetBakeFlag(BakeFlags::SaveLightSampling, saveLightSamples);
						}

						bool ignoreDirectLighting = lightBaker.GetBakeFlag(BakeFlags::IgnoreDirectLighting);

						if (ImGui::Checkbox("Ignore Direct Lighting", &ignoreDirectLighting))
						{
							lightBaker.SetBakeFlag(BakeFlags::IgnoreDirectLighting, ignoreDirectLighting);
						}

						{
							int bakeMode = static_cast<int>(lightBaker.GetBakingMode());

							ImGui::RadioButton("Camera cluster", &bakeMode, static_cast<int>(LightBakingMode::CurrentPositionCluster));
							ImGui::SameLine();
							ImGui::RadioButton("All clusters", &bakeMode, static_cast<int>(LightBakingMode::AllClusters));
							ImGui::SameLine();
							ImGui::RadioButton("Current and Neighbouring clusters", &bakeMode, static_cast<int>(LightBakingMode::CurrentAndNeighbouringClusters));

							lightBaker.SetBakingMode(static_cast<LightBakingMode>(bakeMode));
						}
					}

					ImGui::Separator();

					{
						ImGui::Text("Light Sampling settings");

						bool samplePointLights = lightBaker.GetBakeFlag(BakeFlags::SamplePointLights);

						if (ImGui::Checkbox("Sample Point Lights", &samplePointLights))
						{
							lightBaker.SetBakeFlag(BakeFlags::SamplePointLights, samplePointLights);
						}

						ImGui::SameLine();

						bool sampleAreaLights = lightBaker.GetBakeFlag(BakeFlags::SampleAreaLights);

						if (ImGui::Checkbox("Sample Area Lights", &sampleAreaLights))
						{
							lightBaker.SetBakeFlag(BakeFlags::SampleAreaLights, sampleAreaLights);
						}
					}

					ImGui::Separator();

					{
						bool saveToFileAfterBake = lightBaker.GetBakeFlag(BakeFlags::SaveToFileAfterBake);

						if (ImGui::Checkbox("Save to file after bake", &saveToFileAfterBake))
						{
							lightBaker.SetBakeFlag(BakeFlags::SaveToFileAfterBake, saveToFileAfterBake);
						}
					}

					ImGui::Separator();

					{
						if (ImGui::Button("Start bake"))
						{
							if (lightBaker.GetBakingMode() == LightBakingMode::CurrentPositionCluster || 
								lightBaker.GetBakingMode() == LightBakingMode::CurrentAndNeighbouringClusters)
							{
								lightBaker.SetBakePosition(context.frame.camera.position);
							}
							else
							{
								// Enforce false, because certain flags can only be used for certain modes
								lightBaker.SetBakeFlag(BakeFlags::SaveRayPath, false);
								lightBaker.SetBakeFlag(BakeFlags::SaveLightSampling, false);
							}

							RequestStateChange(State::LightBaking);
						}
					}

					ImGui::Separator();

					{
						if (ImGui::Button("Load light baking data from file"))
						{
							RequestStateChange(State::LoadLightBakingFromFile);
						}
					}

					ImGui::Unindent();
				}

				ImGui::Separator();

				if (ImGui::CollapsingHeader("PBR"))
				{
					ImGui::Indent();

					ImGui::Checkbox("Override Materials", &debugSettings.useMaterialOverrides);
					ImGui::NewLine();

					ImGui::SliderFloat("Roughness", &debugSettings.roughness, 0.0001f, 1.0f);
					ImGui::SliderFloat("Metaliness", &debugSettings.metalness, 0.0f, 1.0f);
					ImGui::SliderFloat("Reflectance", &debugSettings.reflectance, 0.0f, 1.0f);

					ImGui::Unindent();
				}
			}
			else if (GetState() == State::LightBaking)
			{
				ImGui::Text("Baking progress: %d / %d", lightBaker.GetBakedProbesNum(), lightBaker.GetTotalProbesNum());
			}
			else if (GetState() == State::LoadLightBakingFromFile)
			{
				ImGui::Text("Loading light baking result from file...");
			}

			ImGui::End();
		}

		{
			if (frameGraphBuildMessage.empty() == false)
			{
				ImGui::Begin("Frame Graph Build Res");

				ImGui::Text(frameGraphBuildMessage.c_str());

				if (ImGui::Button("Close"))
				{
					frameGraphBuildMessage.clear();
				}

				ImGui::End();
			}
		}

	}
	
	ImGui::Render();

	// This job should be not intrusive, i.e. there should be no problems to remove it.
	// That's why this weird resource transition is here. Alternatively I can make this job a pass
	CD3DX12_RESOURCE_BARRIER toRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		context.frame.colorBufferAndView->buffer.Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	gpuCommanList->ResourceBarrier(1, &toRenderTargetBarrier);

	D3D12_CPU_DESCRIPTOR_HANDLE colorRenderTargetView = GetRtvHandleCPU(context.frame.colorBufferAndView->viewIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE depthRenderTargetView = GetDsvHandleCPU(context.frame.depthBufferViewIndex);

	gpuCommanList->OMSetRenderTargets(1, &colorRenderTargetView, true, &depthRenderTargetView);


	ID3D12DescriptorHeap* descriptorHeaps[] = { GetCbvSrvHeap(), GetSamplerHeap() };
	gpuCommanList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gpuCommanList);

	CD3DX12_RESOURCE_BARRIER toDefaultStateBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		context.frame.colorBufferAndView->buffer.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);
	gpuCommanList->ResourceBarrier(1, &toDefaultStateBarrier);

	Logs::Log(Logs::Category::Job, "DrawDebugGui job ended");
	Diagnostics::EndEvent(gpuCommanList);
}

void Renderer::ShutdownWin32()
{
	DestroyWindow(hWindows);
	hWindows = NULL;
}

DynamicObjectModel Renderer::CreateDynamicGraphicObjectFromGLModel(const model_t* model, GPUJobContext& context)
{
	DynamicObjectModel object;

	object.name = model->name;

	const dmdl_t* aliasHeader = reinterpret_cast<dmdl_t*>(model->extradata);
	DX_ASSERT(aliasHeader != nullptr && "Alias header for dynamic object is not found.");

	// Allocate buffers on CPU, that we will use for transferring our stuff
	std::vector<int> unnormalizedIndexBuffer;
	std::vector<XMFLOAT2> unnormalizedTexCoords;
	// This is just heuristic guess of how much memory we will need.
	unnormalizedIndexBuffer.reserve(aliasHeader->num_xyz);
	unnormalizedTexCoords.reserve(aliasHeader->num_xyz);

	// Get texture coords and indices in one buffer
	const int* order = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(aliasHeader) + aliasHeader->ofs_glcmds);
	UnwindDynamicGeomIntoTriangleList(order, unnormalizedIndexBuffer, unnormalizedTexCoords);
	
	// NOTE: normalizedVertexIndices is the indices used to normalize (i.e. remove duplicates) of vertices
	// later on. This is NOT indices used for geometry rendering.
	auto[normalizedIndexBuffer, normalizedTexCoordsBuffer, normalizedVertexIndices] =
		NormalizedDynamGeomVertTexCoord(unnormalizedIndexBuffer, unnormalizedTexCoords);

	object.headerData.animFrameVertsNum = normalizedVertexIndices.size();
	object.headerData.animFrameNormalsNum = normalizedVertexIndices.size();
	object.headerData.indicesNum = normalizedIndexBuffer.size();

	// Reserver space for all vertices
	const int verticesNum = aliasHeader->num_frames * object.headerData.animFrameVertsNum;
	std::vector<XMFLOAT4> vertexBuffer;
	vertexBuffer.reserve(verticesNum);

	const int normalsNum = aliasHeader->num_frames * object.headerData.animFrameNormalsNum;
	std::vector<XMFLOAT4> normalBuffer;
	normalBuffer.reserve(normalsNum);

	// Reserve space for single frame of vertices
	std::vector<XMFLOAT4> singleFrameVertexBuffer;
	singleFrameVertexBuffer.reserve(object.headerData.animFrameVertsNum);

	// Reserve space for single frame of normals
	std::vector<XMFLOAT4> singleFrameNormalBuffer;
	singleFrameNormalBuffer.reserve(object.headerData.animFrameNormalsNum);

	// Animation frames data
	object.animationFrames.reserve(aliasHeader->num_frames);
	const daliasframe_t* currentFrame = reinterpret_cast<const daliasframe_t*>(
		(reinterpret_cast<const byte*>(aliasHeader) + aliasHeader->ofs_frames));

	for (int i = 0; i < aliasHeader->num_frames; ++i)
	{
		DynamicObjectModel::AnimFrame& animFrame = object.animationFrames.emplace_back(DynamicObjectModel::AnimFrame());

		animFrame.name = currentFrame->name;
		animFrame.scale = XMFLOAT4(currentFrame->scale[0], currentFrame->scale[1], currentFrame->scale[2], 0.0f);
		animFrame.translate = XMFLOAT4(currentFrame->translate[0], currentFrame->translate[1], currentFrame->translate[2], 0.0f);

		// Fill up one frame vertices (unnormalized)
		singleFrameVertexBuffer.clear();

		for (int j = 0; j < aliasHeader->num_xyz; ++j)
		{
			const byte* currentVert = currentFrame->verts[j].v;
			singleFrameVertexBuffer.push_back(XMFLOAT4(
				currentVert[0],
				currentVert[1],
				currentVert[2],
				1.0f));
		}

		singleFrameVertexBuffer = NormalizeSingleFrameVertexData(normalizedVertexIndices, singleFrameVertexBuffer);
		singleFrameNormalBuffer = Utils::GenerateNormals(singleFrameVertexBuffer, normalizedIndexBuffer);

		vertexBuffer.insert(vertexBuffer.end(), singleFrameVertexBuffer.cbegin(), singleFrameVertexBuffer.cend());
		normalBuffer.insert(normalBuffer.end(), singleFrameNormalBuffer.cbegin(), singleFrameNormalBuffer.cend());

		// Get next frame
		currentFrame = reinterpret_cast<const daliasframe_t*>(
			(reinterpret_cast<const byte*>(currentFrame) + aliasHeader->framesize));

	}

	auto& defaultMemory =
		MemoryManager::Inst().GetBuff<DefaultBuffer_t>();

	// Load GPU buffers
	const int vertexBufferSize = vertexBuffer.size() * sizeof(XMFLOAT4);
	const int normalBufferSize = normalBuffer.size() * sizeof(XMFLOAT4);

	const int indexBufferSize = normalizedIndexBuffer.size() * sizeof(int);
	const int texCoordsBufferSize = normalizedTexCoordsBuffer.size() * sizeof(XMFLOAT2);

	object.vertices = defaultMemory.Allocate(vertexBufferSize);
	object.indices = defaultMemory.Allocate(indexBufferSize);
	object.textureCoords = defaultMemory.Allocate(texCoordsBufferSize);
	object.normals = defaultMemory.Allocate(normalBufferSize);

	ResourceManager& resMan = ResourceManager::Inst();

	// Get vertices in
	FArg::UpdateDefaultHeapBuff updateArgs;
	updateArgs.alignment = 0;
	updateArgs.buffer = defaultMemory.GetGpuBuffer();
	updateArgs.byteSize = vertexBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(vertexBuffer.data());
	updateArgs.offset = defaultMemory.GetOffset(object.vertices);
	updateArgs.context = &context;
	resMan.UpdateDefaultHeapBuff(updateArgs);

	// Get normals in
	updateArgs.alignment = 0;
	updateArgs.buffer = defaultMemory.GetGpuBuffer();
	updateArgs.byteSize = normalBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalBuffer.data());
	updateArgs.offset = defaultMemory.GetOffset(object.normals);
	updateArgs.context = &context;
	resMan.UpdateDefaultHeapBuff(updateArgs);

	// Get indices in
	updateArgs.alignment = 0;
	updateArgs.buffer = defaultMemory.GetGpuBuffer();
	updateArgs.byteSize = indexBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalizedIndexBuffer.data());
	updateArgs.offset = defaultMemory.GetOffset(object.indices);
	updateArgs.context = &context;
	resMan.UpdateDefaultHeapBuff(updateArgs);

	// Get tex coords in
	updateArgs.alignment = 0;
	updateArgs.buffer = defaultMemory.GetGpuBuffer();
	updateArgs.byteSize = texCoordsBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalizedTexCoordsBuffer.data());
	updateArgs.offset = defaultMemory.GetOffset(object.textureCoords);
	updateArgs.context = &context;
	resMan.UpdateDefaultHeapBuff(updateArgs);

	// Get textures in
	object.textures.reserve(aliasHeader->num_skins);

	for (int i = 0; i < aliasHeader->num_skins; ++i)
	{
		object.textures.push_back(model->skins[i]->name);
	}

	return object;
}

void Renderer::CreateGraphicalObjectFromGLSurface(const msurface_t& surf, GPUJobContext& context)
{
	// Generate vertices
	std::vector<ShDef::Vert::StaticObjVert_t> vertices;
	for (const glpoly_t* poly = surf.polys; poly != nullptr; poly = poly->chain)
	{
		constexpr int vertexsize = 7;

		// xyz s1t1 s2t2
		const float* glVert = poly->verts[0];
		for (int i = 0; i < poly->numverts; ++i, glVert += vertexsize)
		{
			ShDef::Vert::StaticObjVert_t dxVert;
			dxVert.position = { glVert[0], glVert[1], glVert[2], 1.0f };
			dxVert.texCoord = { glVert[3], glVert[4] };

			vertices.push_back(std::move(dxVert));
		}
	}

	// Remove duplicating vertices
	for (int i = vertices.size() - 1; i > 0; --i)
	{
		const ShDef::Vert::StaticObjVert_t& currentVert = vertices[i];

		const auto similarVertIter = std::find_if(vertices.cbegin(), vertices.cbegin() + i,
			[&currentVert](const ShDef::Vert::StaticObjVert_t& vert)
			{
				constexpr float epsilon = 0.00001f;
				
				const bool similarPosition = XMVector3NearEqual(
					XMLoadFloat4(&currentVert.position), 
					XMLoadFloat4(&vert.position),
					XMVectorSet(epsilon, epsilon, epsilon, epsilon));

				const bool similarTexCoord = XMVector2NearEqual(
					XMLoadFloat2(&currentVert.texCoord),
					XMLoadFloat2(&vert.texCoord),
					XMVectorSet(epsilon, epsilon, epsilon, epsilon));

				return similarPosition && similarTexCoord;
			});

		if (similarVertIter != vertices.cbegin() + i)
		{
			vertices.erase(vertices.begin() + i);
		}
	}

	DX_ASSERT(vertices.empty() == false && "Static object cannot be created from empty vertices");

	// Generate indices
	std::vector<int> indices;
	indices = Utils::GetIndicesListForTrianglelistFromPolygonPrimitive(vertices.size());

	// Generate normals
	std::vector<XMFLOAT4> vertPos;
	vertPos.reserve(vertices.size());

	std::transform(vertices.cbegin(), vertices.cend(), std::back_inserter(vertPos),
		[](const ShDef::Vert::StaticObjVert_t& vert)
	{
		return vert.position;
	});

	
	std::vector<int> degenerateTrianglesIndices;
	std::vector<XMFLOAT4> normals = Utils::GenerateNormals(vertPos, indices, &degenerateTrianglesIndices);

	DX_ASSERT(normals.size() == vertices.size() && "Vertex size should match generated normals");

	for (int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].normal = normals[i];
	}

	// Write textrue coords
	std::vector<XMFLOAT2> textureCoords;
	textureCoords.reserve(vertices.size());

	std::transform(vertices.cbegin(), vertices.cend(), std::back_inserter(textureCoords),
		[](const ShDef::Vert::StaticObjVert_t& vert)
	{
		return vert.texCoord;
	});

	// If true, it means entire mesh consists of degenerate triangles. 
	const bool isDegenerateMesh = degenerateTrianglesIndices.size() * 3 == indices.size();

	// Get rid of degenerate triangles 
	// Ideally, I can skip creating of degenerate meshes at all
	// But in reality, my bsp stores mesh indices and if I mess up indices then
	// culling will be busted.
	if (degenerateTrianglesIndices.empty() == false && isDegenerateMesh == false)
	{
		// 1) Delete indices
		for (int i = degenerateTrianglesIndices.size() - 1; i >= 0; --i)
		{
			const int triangleIndex = degenerateTrianglesIndices[i];
			indices.erase(indices.begin() + triangleIndex * 3, indices.begin() + (triangleIndex + 1) * 3);
		}
		
		// 2) Detect vertices that are not used anymore
		std::vector<bool> usedVertices(vertices.size(), false);

		for (const int index : indices)
		{
			usedVertices[index] = true;
		}
		 
		// 3) Delete those vertices along with normals and texCoords
		for (int i = usedVertices.size() - 1; i >= 0 ; --i)
		{
			if (usedVertices[i] == true)
			{
				continue;
			}

			vertices.erase(vertices.begin() + i);
			vertPos.erase(vertPos.begin() + i);
			textureCoords.erase(textureCoords.begin() + i);
			normals.erase(normals.begin() + i);

			// Patch up indices
			for (int& index : indices)
			{
				if (index > i)
				{
					index -= 1;
				}

				DX_ASSERT(index >= 0 && "Invalid result index value");
			}
		}
	}

	// If this is a light surface, add it to the list
	if ((surf.texinfo->flags & SURF_WARP) == 0 && 
		(surf.texinfo->flags & SURF_SKY) == 0 &&
		(surf.texinfo->flags & SURF_LIGHT) != 0 &&
		surf.texinfo->image->desc.surfaceProperties->irradiance > 0 &&
		isDegenerateMesh == false)
	{
		staticAreaLights.emplace_back(AreaLight{ 
			static_cast<int>(staticObjects.size()) - 1 });
	}

	auto& defaultMemory =
		MemoryManager::Inst().GetBuff<DefaultBuffer_t>();


	StaticObject& obj = staticObjects.emplace_back(StaticObject());

	// Set the texture name
	obj.textureKey = surf.texinfo->image->name;
	
	// Assign material ID
	obj.materialID = Material::FindMaterialMatchFromTextureName(obj.textureKey);

	// Fill up vertex buffer
	obj.verticesSizeInBytes = sizeof(ShDef::Vert::StaticObjVert_t) * vertices.size();
	obj.vertices = defaultMemory.Allocate(obj.verticesSizeInBytes);

	FArg::UpdateDefaultHeapBuff updateBuffArg;
	updateBuffArg.buffer = defaultMemory.GetGpuBuffer();
	updateBuffArg.offset = defaultMemory.GetOffset(obj.vertices);
	updateBuffArg.byteSize = obj.verticesSizeInBytes;
	updateBuffArg.data = vertices.data();
	updateBuffArg.alignment = 0;
	updateBuffArg.context = &context;

	ResourceManager::Inst().UpdateDefaultHeapBuff(updateBuffArg);

	// Fill up index buffer
	obj.indicesSizeInBytes = sizeof(int) * indices.size();
	obj.indices = defaultMemory.Allocate(obj.indicesSizeInBytes);

	updateBuffArg.buffer = defaultMemory.GetGpuBuffer();
	updateBuffArg.offset = defaultMemory.GetOffset(obj.indices);
	updateBuffArg.byteSize = obj.indicesSizeInBytes;
	updateBuffArg.data = indices.data();
	updateBuffArg.alignment = 0;
	updateBuffArg.context = &context;

	ResourceManager::Inst().UpdateDefaultHeapBuff(updateBuffArg);

	// Handle source static objects
	SourceStaticObject sourceObject;

	sourceObject.textureKey = obj.textureKey;
	sourceObject.materialID = obj.materialID;
	sourceObject.verticesPos = std::move(vertPos);
	sourceObject.normals = std::move(normals);
	sourceObject.textureCoords = std::move(textureCoords);
	
	std::transform(indices.cbegin(), indices.cend(), std::back_inserter(sourceObject.indices),
		[](const uint32_t index) 
	{
		return index;
	});

	sourceObject.aabb = Utils::ConstructAABB(sourceObject.verticesPos);

	sourceStaticObjects.push_back(std::move(sourceObject));
}

void Renderer::DecomposeGLModelNode(const model_t& model, const mnode_t& node, GPUJobContext& context)
{
	// WARNING: this function should process surfaces in the same order as
	// BSPTree::AddNode
	if (Node_IsLeaf(&node))
	{
		// Each surface inside node represents stand alone object with its own texture
		const mleaf_t& leaf = reinterpret_cast<const mleaf_t&>(node);

		const msurface_t* const * surf = leaf.firstmarksurface;

		for (int i = 0; i < leaf.nummarksurfaces; ++i, ++surf)
		{
			if (Surf_IsEmpty(*surf) == false)
			{
				CreateGraphicalObjectFromGLSurface(**surf, context);
			}
		}
	}
	else
	{

		// This is intermediate node, keep going for a leafs
		DecomposeGLModelNode(model, *node.children[0], context);
		DecomposeGLModelNode(model, *node.children[1], context);
	}
}

GPUJobContext Renderer::CreateContext(Frame& frame, bool acquireCommandList)
{
	ASSERT_MAIN_THREAD;

	CommandList* commandList = nullptr;

	if (acquireCommandList)
	{
		const int commandListIndex = commandListBuffer.allocator.Allocate();
		frame.acquiredCommandListsIndices.push_back(commandListIndex);

		commandList = &commandListBuffer.commandLists[commandListIndex];
	}
	
	return GPUJobContext(frame, commandList);
}

void Renderer::GetDrawAreaSize(int* Width, int* Height)
{

	char modeVarName[] = "gl_mode";
	char modeVarVal[] = "3";
	cvar_t* mode = GetRefImport().Cvar_Get(modeVarName, modeVarVal, CVAR_ARCHIVE);

	DX_ASSERT(mode);
	//#SWITCH resolution
	//GetRefImport().Vid_GetModeInfo(Width, Height, static_cast<int>(mode->value));
    *Width = 1024;
	*Height = 768;
}

const BSPTree& Renderer::GetBSPTree() const
{
	return bspTree;
}

int Renderer::GetCurrentFrameCounter() const
{
	return frameCounter;
}

int Renderer::GetLightIndexInStaticLightList(int lightTypeArrayIndex, Light::Type type) const
{
	if (type == Light::Type::Point)
	{
		return lightTypeArrayIndex;
	}

	if (type == Light::Type::Area)
	{
		return staticPointLights.size() + lightTypeArrayIndex;
	}

	DX_ASSERT(false && "Invalid light type, so index in light list can't be derived");
	return Const::INVALID_INDEX;
}

const Renderer::DebugSettings& Renderer::GetDebugSettings() const
{
	return debugSettings;
}

void Renderer::ConsumeDiffuseIndirectLightingBakingResult(BakingData&& results)
{
	ASSERT_MAIN_THREAD;

	DX_ASSERT(results.probes.empty() == false && "Can't consume empty probe data");

	std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);
	lightBakingResult.obj = std::move(results);

	lightBakingResultCPUVersion = LightBaker::Inst().GetBakeVersion();
}

std::vector<std::vector<XMFLOAT4>> Renderer::GenProbePathSegmentsVertices() const
{
	std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);

	std::vector<std::vector<XMFLOAT4>> vertices;
	vertices.reserve(lightBakingResult.obj.probes.size());

	std::vector<XMFLOAT4> singleProbeVertices;

	for (const DiffuseProbe& probe : lightBakingResult.obj.probes)
	{
		DX_ASSERT(probe.pathTracingSegments.has_value() && "Can't generate path segment vertices. No source data");

		singleProbeVertices.clear();

		for (const PathSegment& seg : *probe.pathTracingSegments)
		{
			singleProbeVertices.push_back(seg.v0);
			singleProbeVertices.push_back(seg.v1);
		}

		vertices.push_back(singleProbeVertices);
	}

	return vertices;
}

std::vector<std::vector<std::vector<std::vector<XMFLOAT4>>>> Renderer::GenLightSampleVertices() const
{
	std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);

	std::vector<std::vector<std::vector<std::vector<XMFLOAT4>>>> vertices;
	vertices.reserve(lightBakingResult.obj.probes.size());

	std::vector<std::vector<std::vector<XMFLOAT4>>> singleProbeVerts;
	std::vector<std::vector<XMFLOAT4>> singlePathVert;
	std::vector<XMFLOAT4> singlePointVert;

	for (const DiffuseProbe& probe : lightBakingResult.obj.probes)
	{
		DX_ASSERT(probe.lightSamples.has_value() && "Can't generate debug light segments. No source data");

		singleProbeVerts.clear();

		for (const PathLightSampleInfo_t& path : *probe.lightSamples)
		{
			singlePathVert.clear();

			for (const LightSamplePoint& point : path)
			{
				singlePointVert.clear();

				for (const LightSamplePoint::Sample& sample : point.samples)
				{
					singlePointVert.push_back(point.position);
					singlePointVert.push_back(sample.position);
				}

				singlePathVert.push_back(singlePointVert);
			}

			singleProbeVerts.push_back(singlePathVert);
		}

		vertices.push_back(singleProbeVerts);
	}

	return vertices;
}

std::vector<ClusterProbeGridInfo> Renderer::GenBakeClusterProbeGridInfo() const
{
	std::scoped_lock<std::mutex> lock(lightBakingResult.mutex);

	const BakingData& bakeData = lightBakingResult.obj;

	if (bakeData.probes.empty() == true)
	{
		return {};
	}

	std::vector<ClusterProbeGridInfo> probeGridInfoArray;
	probeGridInfoArray.reserve(bakeData.clusterFirstProbeIndices.size());

	DX_ASSERT(bakeData.clusterFirstProbeIndices.size() == bakeData.clusterProbeGridSizes.size() &&
		"Cluster First probe indices should be the same size as cluster grid probe sizes");

	DX_ASSERT(bakeData.bakingMode.has_value() == true);

	switch (*bakeData.bakingMode)
	{
	case LightBakingMode::AllClusters:
	{
		for (int i = 0; i < bakeData.clusterFirstProbeIndices.size(); ++i)
		{
			ClusterProbeGridInfo probeGridInfo;
			probeGridInfo.SizeX = bakeData.clusterProbeGridSizes[i].x;
			probeGridInfo.SizeY = bakeData.clusterProbeGridSizes[i].y;
			probeGridInfo.SizeZ = bakeData.clusterProbeGridSizes[i].z;

			probeGridInfo.StartIndex = bakeData.clusterFirstProbeIndices[i];

			probeGridInfoArray.push_back(probeGridInfo);
		}
	}
	break;
	case LightBakingMode::CurrentAndNeighbouringClusters:
	{
		// Get clusters num
		const int clustersNum = Renderer::Inst().GetBSPTree().GetClustersSet().size();

		// We have data for a few clusters. So fill out everything as empty.
		probeGridInfoArray.resize(clustersNum);

		for (int i = 0; i < bakeData.clusterFirstProbeIndices.size(); ++i)
		{
			ClusterProbeGridInfo probeGridInfo;
			probeGridInfo.SizeX = bakeData.clusterProbeGridSizes[i].x;
			probeGridInfo.SizeY = bakeData.clusterProbeGridSizes[i].y;
			probeGridInfo.SizeZ = bakeData.clusterProbeGridSizes[i].z;

			probeGridInfo.StartIndex = bakeData.clusterFirstProbeIndices[i];

			probeGridInfoArray[i] = probeGridInfo;
		}
	}
	break;
	case LightBakingMode::CurrentPositionCluster: 
	{
		// Get clusters num
		const int clustersNum = Renderer::Inst().GetBSPTree().GetClustersSet().size();

		// We have data only for one cluster. So fill out everything as empty.
		probeGridInfoArray.resize(clustersNum);

		DX_ASSERT(bakeData.bakingCluster.has_value() == true);

		// Now write data only for one cluster, which should be in the end
		ClusterProbeGridInfo& clusterWithData = probeGridInfoArray[*bakeData.bakingCluster];
		clusterWithData.SizeX = bakeData.clusterProbeGridSizes[*bakeData.bakingCluster].x;
		clusterWithData.SizeY = bakeData.clusterProbeGridSizes[*bakeData.bakingCluster].y;
		clusterWithData.SizeZ = bakeData.clusterProbeGridSizes[*bakeData.bakingCluster].z;

		clusterWithData.StartIndex = bakeData.clusterFirstProbeIndices[*bakeData.bakingCluster];
	};
	break;
	default:
		DX_ASSERT(false);
		break;
	}

	return probeGridInfoArray;
}

const std::array<unsigned int, 256>& Renderer::GetRawPalette() const
{
	return rawPalette;
}

const std::array<unsigned int, 256>& Renderer::GetTable8To24() const
{
	return Table8To24;
}

const std::vector<StaticObject>& Renderer::GetStaticObjects() const
{
	return staticObjects;
}

const std::unordered_map<model_t*, DynamicObjectModel>& Renderer::GetDynamicModels() const
{
	return dynamicObjectsModels;
}

void Renderer::Load8To24Table()
{
	char colorMapFilename[] = "pics/colormap.pcx";

	int width = 0;
	int height = 0;

	std::byte* image = nullptr;
	std::byte* palette = nullptr;

	Utils::LoadPCX(colorMapFilename, &image, &palette, &width, &height);

	if (palette == nullptr)
	{
		char errorMsg[] = "Couldn't load pics/colormap.pcx";
		GetRefImport().Sys_Error(ERR_FATAL, errorMsg);
	}

	for (int i = 0; i < Table8To24.size(); ++i)
	{
		int r = static_cast<int>(palette[i * 3 + 0]);
		int g = static_cast<int>(palette[i * 3 + 1]);
		int b = static_cast<int>(palette[i * 3 + 2]);

		int v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
		Table8To24[i] = static_cast<unsigned int>(LittleLong(v));
	}

	Table8To24[Table8To24.size() - 1] &= LittleLong(0xffffff);	// 255 is transparent

	free(image);
	free(palette);
}

void Renderer::ImageBpp8To32(const std::byte* data, int width, int height, unsigned int* out) const
{
	const int size = width * height;

	for (int i = 0; i < size; ++i)
	{
		// Cause m_8To24Table is in bytes we keep all channels in one number
		int p = static_cast<int>(data[i]);

		out[i] = Table8To24[p];

		if (p != Settings::TRANSPARENT_TABLE_VAL)
		{
			continue;
		}

		// Transparent pixel stays transparent, but with proper color to blend
		// no bleeding!		
		if (i > width && static_cast<int>(data[i - width]) != Settings::TRANSPARENT_TABLE_VAL)
		{
			// if this is not first row and pixel above has value, pick it
			p = static_cast<int>(data[i - width]);
		}
		else if (i < size - width && static_cast<int>(data[i + width]) != Settings::TRANSPARENT_TABLE_VAL)
		{
			// if this is not last row and pixel below has value, pick it
			p = static_cast<int>(data[i + width]);
		}
		else if (i > 0 && static_cast<int>(data[i - 1]) != Settings::TRANSPARENT_TABLE_VAL)
		{
			// if pixel on left has value pick it
			p = static_cast<int>(data[i - 1]);
		}
		else if (i < size - 1 && static_cast<int>(data[i + 1]) != Settings::TRANSPARENT_TABLE_VAL)
		{
			// if pixel on right has value pick it
			p = static_cast<int>(data[i + 1]);
		}
		else
			p = 0;

		reinterpret_cast<std::byte*>(&out[i])[0] = reinterpret_cast<const std::byte*>(&Table8To24[p])[0];
		reinterpret_cast<std::byte*>(&out[i])[1] = reinterpret_cast<const std::byte*>(&Table8To24[p])[1];
		reinterpret_cast<std::byte*>(&out[i])[2] = reinterpret_cast<const std::byte*>(&Table8To24[p])[2];
	}
}

void Renderer::FindImageScaledSizes(int width, int height, int& scaledWidth, int& scaledHeight) const
{
	constexpr int maxSize = 256;

	for (scaledWidth = 1; scaledWidth < width; scaledWidth <<= 1);	
	scaledWidth = std::min(scaledWidth, maxSize);

	for (scaledHeight = 1; scaledHeight < height; scaledHeight <<= 1);
	scaledHeight = std::min(scaledHeight, maxSize);
}

bool Renderer::IsVisible(const entity_t& entity, const Camera& camera) const
{
	const XMFLOAT4 entityPos = XMFLOAT4(entity.origin[0], entity.origin[1], entity.origin[2], 1.0f);

	const FXMVECTOR sseEntityPos = XMLoadFloat4(&entityPos);
	const FXMVECTOR sseCameraToEntity = XMVectorSubtract(sseEntityPos, XMLoadFloat4(&camera.position));
	const float dist = XMVectorGetX(XMVector4Length(sseCameraToEntity));

	constexpr float closeVisibilityDist = 100.0f;
	constexpr float farVisibilityDist = 800.0f;

	if (dist < closeVisibilityDist)
	{
		// When objects are too close field of view culling doesn't really work. Object origin might be behind
		// but object might be seen just fine
		return true;
	}
	else if (dist < farVisibilityDist)
	{
		const auto[yaw, pitch, roll] = camera.GetBasis();

		const float dot = XMVectorGetX(XMVector4Dot(XMLoadFloat4(&roll), XMVector4Normalize(sseCameraToEntity)));
		// Adding this extension coefficient, because object position is in the middle, so if only a piece of object
		// in the view, it will not disappear. Also corners need a bit bigger angle 
		constexpr float viewExtensionCoefficient = 1.15f;
		// Multiply by 0.5f because we need only half of fov
		const float cameraViewAngle = XMConvertToRadians(std::max(camera.fov.y, camera.fov.x) * 0.5f * viewExtensionCoefficient);
		return std::acos(dot) <= cameraViewAngle;
	}

	return false;
}

void Renderer::DeleteDefaultMemoryBuffer(BufferHandler handler)
{
	MemoryManager::Inst().GetBuff<DefaultBuffer_t>().Delete(handler);
}

void Renderer::DeleteUploadMemoryBuffer(BufferHandler handler)
{
	MemoryManager::Inst().GetBuff<UploadBuffer_t>().Delete(handler);
}

void Renderer::GetDrawTextureSize(int* x, int* y, const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: Get draw texture size {}", name);

	std::array<char, MAX_QPATH> texFullName;
	ResourceManager::Inst().GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	const Resource* tex = ResourceManager::Inst().FindResource(texFullName.data());

	if (tex != nullptr)
	{
		*x = tex->desc.width;
		*y = tex->desc.height;
	}
	else
	{
		*x = -1;
		*y = -1;
	}
}

void Renderer::SetPalette(const unsigned char* palette)
{
	Logs::Log(Logs::Category::Generic, "API: Set Palette");

	unsigned char* rawPal = reinterpret_cast<unsigned char *>(rawPalette.data());

	if (palette)
	{
		for (int i = 0; i < 256; i++)
		{
			rawPal[i * 4 + 0] = palette[i * 3 + 0];
			rawPal[i * 4 + 1] = palette[i * 3 + 1];
			rawPal[i * 4 + 2] = palette[i * 3 + 2];
			rawPal[i * 4 + 3] = 0xff;
		}
	}
	else
	{
		for (int i = 0; i < 256; i++)
		{
			rawPal[i * 4 + 0] = Table8To24[i] & 0xff;
			rawPal[i * 4 + 1] = (Table8To24[i] >> 8) & 0xff;
			rawPal[i * 4 + 2] = (Table8To24[i] >> 16) & 0xff;
			rawPal[i * 4 + 3] = 0xff;
		}
	}
}

void Renderer::EndLevelLoading()
{
	if (GetState() != State::LevelLoading)
	{
		return;
	}
	
	constexpr State nextState = Settings::LOAD_LIGHT_BAKING_DATA_ON_START_UP ? State::LoadLightBakingFromFile : State::Rendering;

	RequestStateChange(nextState);
	SwitchToRequestedState();

	Logs::Log(Logs::Category::Generic, "API: End level loading");

	Frame& frame = dynamicModelRegContext->frame;

	dynamicModelRegContext->commandList->Close();
	
	GPUJobContext postLoadingResourcesCreationContext = CreateContext(frame);
	CreatePBRResources(postLoadingResourcesCreationContext);

	GPUJobContext createDeferredTextureContext = CreateContext(frame);
	ResourceManager::Inst().CreateDeferredResource(createDeferredTextureContext);
	
	CloseFrame(frame);

	ReleaseFrameResources(frame);

	ReleaseFrame(frame);

	if (std::shared_ptr<Semaphore> staticModelRegSemaphore = staticModelRegContext->frame.GetFinishSemaphore())
	{
		staticModelRegSemaphore->Wait();
	}

	staticModelRegContext = nullptr;
	dynamicModelRegContext = nullptr;

	InitStaticLighting();

	RegisterObjectsAtFrameGraphs();

	Mod_FreeAll();

	Logs::Log(Logs::Category::Generic, "End level loading");
}

void Renderer::AddDrawCall_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data)
{
	Logs::Log(Logs::Category::Generic, "API: AddDrawCall_RawPic");

	DrawCall_StretchRaw drawCall;
	drawCall.x = x;
	drawCall.y = y;
	drawCall.quadWidth = quadWidth;
	drawCall.quadHeight = quadHeight;
	
	// To enforce order of texture create/update, this check must be done in main thread,
	// so worker threads can run independently
	Resource* rawTex = ResourceManager::Inst().FindResource(Resource::RAW_TEXTURE_NAME);

	if (rawTex == nullptr 
		|| rawTex->desc.width != textureWidth
		|| rawTex->desc.height != textureHeight)
	{
		ResourceDesc desc;
		desc.width = textureWidth;
		desc.height = textureHeight;
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		// Get tex actual color
		const int textureSize = desc.width * desc.height;
		std::vector<unsigned int> texture(textureSize, 0);

		const auto& rawPalette = Renderer::Inst().GetRawPalette();
		for (int i = 0; i < textureSize; ++i)
		{
			texture[i] = rawPalette[std::to_integer<int>(data[i])];
		}

		FArg::CreateResourceFromDataDeferred createTexArgs;
		createTexArgs.data = reinterpret_cast<std::byte*>(texture.data());
		createTexArgs.desc = &desc;
		createTexArgs.name = Resource::RAW_TEXTURE_NAME;
		createTexArgs.frame = &GetMainThreadFrame();
		createTexArgs.saveResourceInCPUMemory = false;

		rawTex = ResourceManager::Inst().CreateResourceFromDataDeferred(createTexArgs);
	}
	else
	{
		drawCall.textureWidth = textureWidth;
		drawCall.textureHeight = textureHeight;

		const int textureSize = textureWidth * textureHeight;
		drawCall.data.resize(textureSize);
		memcpy(drawCall.data.data(), data, textureSize);
	}

	GetMainThreadFrame().uiDrawCalls.push_back(std::move(drawCall));
}

void Renderer::AddDrawCall_Pic(int x, int y, const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: AddDrawCall_Pic {}", name);

	GetMainThreadFrame().uiDrawCalls.emplace_back(DrawCall_Pic{ x, y, std::string(name) });
}

void Renderer::AddDrawCall_Char(int x, int y, int num)
{
	Logs::Log(Logs::Category::Generic, "API: AddDrawCall_Char");


	if ((num & 127) == 32)
	{
		return;		// space
	}

	if (y <= -Settings::CHAR_SIZE)
	{
		return;		// totally off screen
	}

	GetMainThreadFrame().uiDrawCalls.emplace_back(DrawCall_Char{ x, y, num });
}

void Renderer::BeginFrame()
{
	Logs::Log(Logs::Category::Generic, "API: Begin frame");

	// That might flush all frames
	ResourceManager::Inst().DeleteRequestedResources();

	// Frame graph processing

	std::vector<FrameGraphSource::FrameGraphResourceDecl> internalResourceDecl;
	std::unique_ptr<FrameGraph> newFrameGraph = nullptr;
	if (FrameGraphBuilder::Inst().IsSourceChanged() == true)
	{
		newFrameGraph = RebuildFrameGraph(internalResourceDecl);
	}

	// State switch 

	if (requestedState.has_value() == true)
	{
		SwitchToRequestedState();
	}

	// Start work on the current frame
	AcquireMainThreadFrame();

	// If we successfully compiled new framegraph replace old one
	if (newFrameGraph != nullptr)
	{
		ReplaceFrameGraphAndCreateFrameGraphResources(internalResourceDecl, newFrameGraph);
	}

	Frame& frame = GetMainThreadFrame();
	frame.colorBufferAndView = &GetNextSwapChainBufferAndView();
	frame.scissorRect = scissorRect;

	frame.frameNumber = frameCounter;

	++frameCounter;
}

void Renderer::EndFrame()
{
	Logs::Log(Logs::Category::Generic, "API: EndFrame");

	if (GetState() == State::LightBaking || 
		GetState() == State::LoadLightBakingFromFile)
	{
		const LightBaker& lightBaker = LightBaker::Inst();

		if (lightBakingResultGPUVersion < lightBaker.GetBakeVersion())
		{
			RequestStateChange(State::Rendering);
		}
	}

	// All heavy lifting is here

	Frame& frame = GetMainThreadFrame();

	// Create some permanent resources
	if (frame.resourceCreationRequests.empty() == false)
	{
		GPUJobContext createDeferredTextureContext = CreateContext(frame);
		ResourceManager::Inst().CreateDeferredResource(createDeferredTextureContext);
	}
	
	if (lightBakingResultGPUVersion < lightBakingResultCPUVersion)
	{
		GPUJobContext indirectResUpdateContext = CreateContext(frame);
		CreateIndirectLightResources(indirectResUpdateContext);
	}

	const bool cameraIsInitalized = frame.camera.fov.x != 0 && frame.camera.fov.y != 0;
	if (ResourceManager::Inst().FindResource(Resource::FRUSTUM_CLUSTERS_AABB_NAME) == nullptr &&
		cameraIsInitalized)
	{
		GPUJobContext clusteredLightingResourcesContext = CreateContext(frame);
		CreateClusteredLightingResources(clusteredLightingResourcesContext);
	}

	if (staticLightsBoundingVolumes.empty() == true &&
		staticAreaLights.empty() == false &&
		staticPointLights.empty() == false)
	{
		GPUJobContext createLightingResourcesContext = CreateContext(frame);

		// Init light Resources
		const std::vector<GPULight> gpuLights = GenerateGPULightList();

		GenerateStaticLightBoundingVolumes(gpuLights);
		const std::vector<GPULightBoundingVolume> gpuBoundingVolumes = GenerateGPULightBoundingVolumesList();

		CreateStaticLightDebugData(gpuLights);

		CreateClusteredLightData();

		CreateLightResources(gpuLights, 
			gpuBoundingVolumes,
			debugPickedStaticLights,
			frame.camera.GetFrustumClustersNum(),
			createLightingResourcesContext);
	}

	// Proceed to next frame
	DetachMainThreadFrame();

	// Renderer preparations
	PreRenderSetUpFrame(frame);

	// Frame Graph preparations
	frame.frameGraph->AddResourceReadbackCallbacks(frame);

	if (frame.frameGraph->IsResourceProxiesCreationRequired())
	{
		frame.frameGraph->CreateResourceProxies();
	}

	frame.frameGraph->Execute(frame);
}

void Renderer::PreRenderSetUpFrame(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	// UI Proj matrix
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&frame.uiViewMat, tempMat);
	tempMat = XMMatrixOrthographicRH(frame.camera.width, frame.camera.height, 1.0f, 0.0f);

	XMStoreFloat4x4(&frame.uiProjectionMat, tempMat);
	
	SetUpFrameDebugData(frame);

	// Static objects
	frame.visibleStaticObjectsIndices = bspTree.GetCameraVisibleObjectsIndices(frame.camera);

	// Dynamic objects
	frame.visibleEntitiesIndices = BuildVisibleDynamicObjectsList(frame.camera, frame.entities);

	// Debug objects
	frame.debugObjects = GenerateFrameDebugObjects(frame.camera); 
}

void Renderer::FlushAllFrames() const
{
	ASSERT_MAIN_THREAD;

	std::vector<std::shared_ptr<Semaphore>> frameFinishSemaphores;

	for (const Frame& frame : frames)
	{
		std::shared_ptr<Semaphore> fSemaphore = frame.GetFinishSemaphore();

		if (fSemaphore != nullptr)
		{
			frameFinishSemaphores.push_back(std::move(fSemaphore));
		}
	}

	if (frameFinishSemaphores.empty() == false)
	{
		Semaphore::WaitForMultipleAll(std::move(frameFinishSemaphores));
	}
}

// This seems to take 188Kb of memory. Which is not that bad, so I will leave it
// as it is for now. I believe there is other places to fix memory issues
#define	MAX_MOD_KNOWN	512
extern model_t	mod_known[MAX_MOD_KNOWN];
extern model_t* r_worldmodel;

Resource* Renderer::RegisterDrawPic(const char* name)
{
	ResourceManager& resMan = ResourceManager::Inst();

	if (GetState() != State::LevelLoading)
	{
		// Try to find existing 
		return resMan.FindResource(name);
	}

	Logs::Logf(Logs::Category::Generic, "API: Register draw pic {}", name);

	// If dynamic model registration context exists, then we are in the middle of the level loading. At this point
	// current frame is most likely invalid
	Frame& frame = dynamicModelRegContext != nullptr ? dynamicModelRegContext->frame : GetMainThreadFrame();

	std::array<char, MAX_QPATH> texFullName;
	resMan.GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	FArg::CreateTextureFromFileDeferred texCreateArgs;
	texCreateArgs.name = texFullName.data();
	texCreateArgs.frame = &frame;
	texCreateArgs.saveResourceInCPUMemory = false;

	Resource* newTex = resMan.CreateTextureFromFileDeferred(texCreateArgs);

	return newTex;
}

void Renderer::RegisterWorldModel(const char* model)
{
	DX_ASSERT(GetState() == State::LevelLoading && "Can only register world model with appropriate state");

	//#TODO I need to manage map model lifetime. So for example in this function
	// I would need to delete old map model and related to it objects before loading
	// a new one. (make sure to handle properly when some object is needed in the new
	// model, so you don't load it twice). Currently it doesn't make sense to do anything
	// with this, because I will have other models in game and I want to handle world model 
	// as part of this system. Maybe I should leave it as it is and just use old system?
	AcquireMainThreadFrame();
	Frame& frame = GetMainThreadFrame();

	GPUJobContext context = CreateContext(frame);
	staticModelRegContext = std::make_unique<GPUJobContext>(context);

	context.commandList->Open();

	std::string fullName = std::format("maps/{}.bsp", model);

	char varName[] = "flushmap";
	char varDefVal[] = "0";
	cvar_t* flushMap = GetRefImport().Cvar_Get(varName, varDefVal, 0);

	if (strcmp(mod_known[0].name, fullName.c_str()) || flushMap->value)
	{
		Mod_Free(&mod_known[0]);
	}

	// Create new world model
	model_t* mapModel = Mod_ForName(fullName.data(), qTrue, context);

	// Read static point lights
	int numStaticPointLights = 0;
	const PointLight* pointLights = Mod_StaticPointLights(&numStaticPointLights);

	staticPointLights.reserve(numStaticPointLights);
	for (int i = 0; i < numStaticPointLights; ++pointLights, ++i)
	{
		if (pointLights->objectPhysicalRadius == 0.0f)
		{
			// Ignore disabled lights. Usually means they are within the wall.
			// Look EnsurePointLightDoesNotIntersectWalls() for more details.
			continue;
		}

		staticPointLights.push_back(*pointLights);
	}

	Mod_FreeStaticPointLights();

	DecomposeGLModelNode(*mapModel, *mapModel->nodes, context);

	// Init bsp
 	bspTree.Create(*mapModel->nodes);
	bspTree.InitClusterVisibility(*mapModel->vis, mapModel->vissize);

	// Submit frame
	context.commandList->Close();
	DetachMainThreadFrame();

	JobSystem::Inst().GetJobQueue().Enqueue(Job(
		[context, this] () mutable 
	{
		Logs::Log(Logs::Category::Job, "Register world model job started");

		Frame& frame = context.frame;

		CloseFrame(frame);

		ReleaseFrameResources(frame);

		ReleaseFrame(frame);

		Logs::Log(Logs::Category::Job, "Register world model job ended");
	}));
}

void Renderer::BeginLevelLoading(const char* mapName)
{
	if (loadedMapName == mapName)
	{
		return;
	}

	loadedMapName = mapName;

	RequestStateChange(State::LevelLoading);
	SwitchToRequestedState();

	Logs::Logf(Logs::Category::Generic, "API: Begin level loading {}", mapName);

	RegisterWorldModel(mapName);

	AcquireMainThreadFrame();
	Frame& frame = GetMainThreadFrame();

	dynamicModelRegContext = std::make_unique<GPUJobContext>(CreateContext(frame));
	dynamicModelRegContext->commandList->Open();

	DetachMainThreadFrame();
}

model_s* Renderer::RegisterModel(const char* name)
{
	if (GetState() != State::LevelLoading)
	{
		return NULL;
	}

	Logs::Logf(Logs::Category::Generic, "API: Register model {}", name);

	std::string modelName = name;

	Frame& frame = dynamicModelRegContext->frame;
	
	model_t* mod = Mod_ForName(modelName.data(), qFalse, *dynamicModelRegContext);

	if (mod)
	{

		switch (mod->type)
		{
		case mod_sprite:
		{
			// Remove after sprites implemented
			Mod_Free(mod);
			mod = NULL;
			break;
		}
		case mod_alias:
		{
			dmdl_t* pheader = reinterpret_cast<dmdl_t*>(mod->extradata);
			for (int i = 0; i < pheader->num_skins; ++i)
			{
				char* imageName = reinterpret_cast<char*>(pheader) + pheader->ofs_skins + i * MAX_SKINNAME;
				mod->skins[i] = ResourceManager::Inst().FindOrCreateResource(imageName, *dynamicModelRegContext, false);
			}

			dynamicObjectsModels[mod] = CreateDynamicGraphicObjectFromGLModel(mod, *dynamicModelRegContext);

			break;
		}
		case mod_brush:
		{
			mod = NULL;
			break;
		}
		default:
			break;
		}
	}

	return mod;
}

void Renderer::UpdateFrame(const refdef_t& updateData)
{
	Logs::Log(Logs::Category::Generic, "API: Update frame");

	Frame& frame = GetMainThreadFrame();

	frame.camera.Update(updateData);
	frame.camera.GenerateViewProjMat();
	
	frame.entities.resize(updateData.num_entities);
	memcpy(frame.entities.data(), updateData.entities, sizeof(entity_t) * updateData.num_entities);

	frame.particles.resize(updateData.num_particles);
	memcpy(frame.particles.data(), updateData.particles, sizeof(particle_t) * updateData.num_particles);
}
