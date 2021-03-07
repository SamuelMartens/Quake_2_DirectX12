#include "dx_app.h"

#include <cassert>
#include <string>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <d3dcompiler.h>
#include <DirectXColors.h>
#include <memory>
#include <tuple>


#include "../win32/winquake.h"
#include "dx_utils.h"
#include "dx_shaderdefinitions.h"
#include "dx_glmodel.h"
#include "dx_camera.h"
#include "dx_diagnostics.h"
#include "dx_infrastructure.h"
#include "dx_settings.h"
#include "dx_framegraphbuilder.h"
#include "dx_resourcemanager.h"
#include "dx_memorymanager.h"
#include "dx_jobmultithreading.h"
#include "dx_rendercallbacks.h"
#include "dx_descriptorheap.h"

#ifdef max
#undef max
#endif


namespace
{
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

			assert(vertCount * vertCount >= 9 && "Weird vert count, during dynamic geom transform");

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

		assert(originalInd.size() % 3 == 0 && "Invalid indices in Tex Coord normalization");

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

	void AppendNormalizedVertexData(const std::vector<int>& normalizedVertIndices, const std::vector<XMFLOAT4>& unnormalizedVertices,
		std::vector<XMFLOAT4>& normalizedVertices)
	{
		for (int i = 0; i < normalizedVertIndices.size(); ++i)
		{
			const int unnormalizedVertInd = normalizedVertIndices[i];

			if (unnormalizedVertInd == -1)
			{
				assert(false && "Uninitialized vert ind");
				normalizedVertices.push_back(XMFLOAT4(0.0f, 0.0f, 0.0f, -1.0f));
			}
			else
			{
				normalizedVertices.push_back(unnormalizedVertices[unnormalizedVertInd]);
			}
		}
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

	windowClass.style			= 0;
	windowClass.lpfnWndProc	= WindowProc;
	windowClass.cbClsExtra		= 0;
	windowClass.cbWndExtra		= 0;
	windowClass.hInstance		= hInstance;
	windowClass.hIcon			= 0;
	windowClass.hCursor		= LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_GRAYTEXT);
	windowClass.lpszMenuName	= 0;
	windowClass.lpszClassName  = static_cast<LPCSTR>(windowsClassName.c_str());

	ATOM classReg = RegisterClass(&windowClass);
	assert(classReg != 0 && "Failed to register win class.");

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

	assert(hWindows && "Failed to create windows");

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
	
	CreateSwapChainBuffersAndViews();

	CreateTextureSampler();

	CreateFences(fence);

	InitCommandListsBuffer();

	InitUtils();

	// ------- Frames init -----

	InitFrames();

	// ------- Open init command list -----
	AcquireMainThreadFrame();
	GPUJobContext initContext = CreateContext(GetMainThreadFrame());

	initContext.commandList.Open();

	// -- Steps that require command list --

	MemoryManager::Inst().Init(initContext);

	// ------- Close and execute init command list -----

	initContext.commandList.Close();

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
	assert(Settings::SWAP_CHAIN_BUFFER_COUNT == Settings::FRAMES_NUM && "Swap chain buffer count shall be equal to frames num");

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
	rtvHeap = std::make_unique<std::remove_reference_t<decltype(*rtvHeap)>>
		(GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	dsvHeap = std::make_unique<std::remove_reference_t<decltype(*dsvHeap)>>
		(GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV), D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	cbvSrvHeap = std::make_unique<std::remove_reference_t<decltype(*cbvSrvHeap)>>
		(GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	samplerHeap = std::make_unique<std::remove_reference_t<decltype(*samplerHeap)>>
		(GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
}

void Renderer::CreateSwapChainBuffersAndViews()
{
	for (int i = 0; i < Settings::SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		AssertBufferAndView& buffView = swapChainBuffersAndViews[i];

		// Get i-th buffer in a swap chain
		ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffView.buffer)));

		buffView.viewIndex = rtvHeap->Allocate(buffView.buffer.Get());
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
	assert(MSQualityLevels > 0 && "Unexpected MSAA quality levels");
}

void Renderer::CreateCommandQueue()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
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
	depthStencilDesc.Format = Settings::DEPTH_STENCIL_FORMAT;
	depthStencilDesc.SampleDesc.Count = GetMSAASampleCount();
	depthStencilDesc.SampleDesc.Quality = GetMSAAQuality();
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optimizedClearVal;
	optimizedClearVal.Format = Settings::DEPTH_STENCIL_FORMAT;
	optimizedClearVal.DepthStencil.Depth = 1.0f;
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

void Renderer::RebuildFrameGraph()
{
	Logs::Log(Logs::Category::Parser, "RebuildFrameGraph");

	FlushAllFrames();

	// Delete old framegraph
	for (int i = 0; i < frames.size(); ++i)
	{
		frames[i].frameGraph.reset(nullptr);
	}

	FrameGraphBuilder::Inst().BuildFrameGraph(frames[0].frameGraph);

	// Frame graph has changed new frame graph is stored in the first frame,
	// populate this change to other frames
	for (int i = 1; i < frames.size(); ++i)
	{
		frames[i].frameGraph = std::make_unique<FrameGraph>(FrameGraph(*frames[0].frameGraph));
	}
}

Frame& Renderer::GetMainThreadFrame()
{
	ASSERT_MAIN_THREAD;
	assert(currentFrameIndex != Const::INVALID_INDEX && "Trying to get current frame which is invalid");

	return frames[currentFrameIndex];
}

void Renderer::SubmitFrame(Frame& frame)
{
	assert(frame.acquiredCommandListsIndices.empty() == false && "Trying to execute empty command lists");

	std::vector<ID3D12CommandList*> commandLists(frame.acquiredCommandListsIndices.size(), nullptr);

	for (int i = 0; i < frame.acquiredCommandListsIndices.size(); ++i)
	{
		const int commandListIndex = frame.acquiredCommandListsIndices[i];
		commandLists[i] = commandListBuffer.commandLists[commandListIndex].GetGPUList();
	}
	
	commandQueue->ExecuteCommandLists(commandLists.size(), commandLists.data());

	assert(frame.executeCommandListFenceValue == -1 && frame.executeCommandListEvenHandle == INVALID_HANDLE_VALUE &&
		"Trying to set up sync primitives for frame that already has it");

	frame.executeCommandListFenceValue = GenerateFenceValue();
	frame.executeCommandListEvenHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

	commandQueue->Signal(fence.Get(), frame.executeCommandListFenceValue);
	ThrowIfFailed(fence->SetEventOnCompletion(frame.executeCommandListFenceValue, frame.executeCommandListEvenHandle));

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with frameNumber %d submitted", frame.frameNumber);
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
	Logs::Logf(Logs::Category::FrameSubmission, "Frame with frameNumber %d releases resources", frame.frameNumber);

	for (int acquiredCommandListIndex : frame.acquiredCommandListsIndices)
	{
		commandListBuffer.allocator.Delete(acquiredCommandListIndex);
	}
	frame.acquiredCommandListsIndices.clear();

	frame.colorBufferAndView = nullptr;
	frame.frameNumber = Const::INVALID_INDEX;

	DO_IN_LOCK(frame.uploadResources, clear());

	auto& uploadMemory = 
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	frame.entities.clear();
	frame.particles.clear();

	frame.texCreationRequests.clear();

	// Remove used draw calls
	frame.uiDrawCalls.clear();

	if (frame.frameGraph != nullptr)
	{
		frame.frameGraph->ReleasePerFrameResources();
	}
}

void Renderer::AcquireMainThreadFrame()
{
	ASSERT_MAIN_THREAD;

	assert(currentFrameIndex == Const::INVALID_INDEX && "Trying to acquire frame, while there is already frame acquired.");

	// m_currentFrameIndex - is basically means index of frame that is used by main thread.
	//						 and also indicates if new frame shall be used
	// isInUse - is in general means, if any thread is using this frame.

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

			Logs::Logf(Logs::Category::FrameSubmission, "Frame with index %d acquired", currentFrameIndex);

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

	assert(frameIt != frames.end() && "Can't find free frame");

	OpenFrame(*frameIt);
	currentFrameIndex = std::distance(frames.begin(), frameIt);

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with index %d acquired", currentFrameIndex);
}

void Renderer::DetachMainThreadFrame()
{
	ASSERT_MAIN_THREAD;

	assert(currentFrameIndex != Const::INVALID_INDEX && "Trying to detach frame. But there is nothing to detach.");

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with index %d and frameNumber %d detached", currentFrameIndex, frames[currentFrameIndex].frameNumber);

	currentFrameIndex = Const::INVALID_INDEX;
}

void Renderer::ReleaseFrame(Frame& frame)
{
	frame.Release();

	Logs::Log(Logs::Category::FrameSubmission, "Frame released");
}

void Renderer::WaitForFrame(Frame& frame) const
{
	assert(frame.executeCommandListFenceValue != -1 && frame.executeCommandListEvenHandle != INVALID_HANDLE_VALUE &&
		"Trying to wait for frame that has invalid sync primitives.");

	if (fence->GetCompletedValue() < frame.executeCommandListFenceValue)
	{
		DWORD res = WaitForSingleObject(frame.executeCommandListEvenHandle, INFINITE);

		assert(res == WAIT_OBJECT_0 && "Frame wait ended in unexpected way.");
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

void Renderer::CreateTextureSampler()
{
	Descriptor_t samplerDesc = D3D12_SAMPLER_DESC{};
	D3D12_SAMPLER_DESC& samplerDescRef = std::get<D3D12_SAMPLER_DESC>(samplerDesc);
	samplerDescRef.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDescRef.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescRef.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescRef.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescRef.MinLOD = 0;
	samplerDescRef.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDescRef.MipLODBias = 1;
	samplerDescRef.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	samplerHeap->Allocate(nullptr, &samplerDesc);
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

		regContext.commandList.Open();
		regContext.frame.frameGraph->RegisterObjects(staticObjects, regContext);
		regContext.commandList.Close();

		CloseFrame(regContext.frame);
		ReleaseFrameResources(regContext.frame);

		// Detach but not release, so we will be forced to use all frames
		DetachMainThreadFrame();
	}

	assert(frameIndex == frames.size() && "Not all frames registered objects");

	// Now when registration is over release everything
	for (Frame& frame : frames)
	{
		frame.Release();
	}
}

std::vector<int> Renderer::BuildObjectsInFrustumList(const Camera& camera, const std::vector<Utils::AABB>& objCulling) const
{
	Utils::AABB cameraAABB = camera.GetAABB();

	const FXMVECTOR sseCameraBBMin = XMLoadFloat4(&cameraAABB.bbMin);
	const FXMVECTOR sseCameraBBMax = XMLoadFloat4(&cameraAABB.bbMax);

	std::size_t currentIndex = 0;

	std::vector<int> res;
	// Just heuristic 
	res.reserve(objCulling.size() / 2);

	std::for_each(objCulling.cbegin(), objCulling.cend(), 
		[sseCameraBBMin, 
		sseCameraBBMax,
		&currentIndex,
		&res](const Utils::AABB& objCulling) 
	{
		if (XMVector4LessOrEqual(XMLoadFloat4(&objCulling.bbMin), sseCameraBBMax) &&
			XMVector4GreaterOrEqual(XMLoadFloat4(&objCulling.bbMax), sseCameraBBMin))
		{
			res.push_back(currentIndex);
		}
	
		++currentIndex;
	});

	res.shrink_to_fit();
	return res;
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

void Renderer::EndFrameJob(GPUJobContext& context)
{
	Logs::Logf(Logs::Category::Job, "EndFrame job started frame %d", context.frame.frameNumber);

	context.commandList.Open();

	Diagnostics::BeginEvent(context.commandList.GetGPUList(), "End Frame");

	Frame& frame = context.frame;

	CD3DX12_RESOURCE_BARRIER resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		frame.colorBufferAndView->buffer.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	// This is stupid. I use separate command list just for a few commands, bruh
	context.commandList.GetGPUList()->ResourceBarrier(
		1,
		&resourceBarrier
	);

	Diagnostics::EndEvent(context.commandList.GetGPUList());

	context.commandList.Close();

	// Make sure everything else is done
	context.WaitDependency();

	// We can't submit command lists attached to render target that is not current back buffer,
	// so we need to wait until previous frame is done
	WaitForPrevFrame(context.frame);

	CloseFrame(frame);

	PresentAndSwapBuffers(frame);

	ReleaseFrameResources(frame);
	
	ReleaseFrame(frame);

	ThreadingUtils::AssertUnlocked(context.frame.uploadResources);
	
	// Delete shared resources marked for deletion
	ResourceManager::Inst().DeleteRequestedResources();

	Logs::Log(Logs::Category::Job, "EndFrame job ended");
}

void Renderer::BeginFrameJob(GPUJobContext& context)
{
	Logs::Logf(Logs::Category::Job, "BeginFrame job started frame %d", context.frame.frameNumber);

	JOB_GUARD(context);
	
	CommandList& commandList = context.commandList;
	Frame& frame = context.frame;

	CD3DX12_RESOURCE_BARRIER resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		frame.colorBufferAndView->buffer.Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	// Indicate buffer transition to write state
	commandList.GetGPUList()->ResourceBarrier(
		1,
		&resourceBarrier
	);

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = rtvHeap->GetHandleCPU(frame.colorBufferAndView->viewIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE depthTargetView = dsvHeap->GetHandleCPU(frame.depthBufferViewIndex);

	// Clear back buffer and depth buffer
	commandList.GetGPUList()->ClearRenderTargetView(renderTargetView, DirectX::Colors::Black, 0, nullptr);
	commandList.GetGPUList()->ClearDepthStencilView(
		depthTargetView,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);

	Logs::Logf(Logs::Category::Job, "BeginFrame job ended frame %d", frame.frameNumber);
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
	assert(aliasHeader != nullptr && "Alias header for dynamic object is not found.");

	// Header data
	object.headerData.animFrameSizeInBytes = aliasHeader->framesize;

	// Allocate buffers on CPU, that we will use for transferring our stuff
	std::vector<int> unnormalizedIndexBuffer;
	std::vector<XMFLOAT2> unnormalizedTexCoords;
	// This is just heuristic guess of how much memory we will need.
	unnormalizedIndexBuffer.reserve(aliasHeader->num_xyz);
	unnormalizedTexCoords.reserve(aliasHeader->num_xyz);

	// Get texture coords and indices in one buffer
	const int* order = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(aliasHeader) + aliasHeader->ofs_glcmds);
	UnwindDynamicGeomIntoTriangleList(order, unnormalizedIndexBuffer, unnormalizedTexCoords);

	auto[normalizedIndexBuffer, normalizedTexCoordsBuffer, normalizedVertexIndices] =
		NormalizedDynamGeomVertTexCoord(unnormalizedIndexBuffer, unnormalizedTexCoords);

	object.headerData.animFrameVertsNum = normalizedVertexIndices.size();
	object.headerData.indicesNum = normalizedIndexBuffer.size();

	const int verticesNum = aliasHeader->num_frames * object.headerData.animFrameVertsNum;
	std::vector<XMFLOAT4> vertexBuffer;
	vertexBuffer.reserve(verticesNum);

	std::vector<XMFLOAT4> singleFrameVertexBuffer;
	singleFrameVertexBuffer.reserve(object.headerData.animFrameVertsNum);

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
		AppendNormalizedVertexData(normalizedVertexIndices, singleFrameVertexBuffer, vertexBuffer);

		// Get next frame
		currentFrame = reinterpret_cast<const daliasframe_t*>(
			(reinterpret_cast<const byte*>(currentFrame) + aliasHeader->framesize));

	}

	auto& defaultMemory =
		MemoryManager::Inst().GetBuff<DefaultBuffer_t>();

	// Load GPU buffers
	const int vertexBufferSize = vertexBuffer.size() * sizeof(XMFLOAT4);
	const int indexBufferSize = normalizedIndexBuffer.size() * sizeof(int);
	const int texCoordsBufferSize = normalizedTexCoordsBuffer.size() * sizeof(XMFLOAT2);

	object.vertices = defaultMemory.Allocate(vertexBufferSize);
	object.indices = defaultMemory.Allocate(indexBufferSize);
	object.textureCoords = defaultMemory.Allocate(texCoordsBufferSize);

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
	// Fill up vertex buffer
	std::vector<ShDef::Vert::PosTexCoord> vertices;
	for (const glpoly_t* poly = surf.polys; poly != nullptr; poly = poly->chain)
	{
		constexpr int vertexsize = 7;

		// xyz s1t1 s2t2
		const float* glVert = poly->verts[0];
		for (int i = 0; i < poly->numverts; ++i, glVert += vertexsize)
		{
			ShDef::Vert::PosTexCoord dxVert;
			dxVert.position = { glVert[0], glVert[1], glVert[2], 1.0f };
			dxVert.texCoord = { glVert[3], glVert[4] };

			vertices.push_back(std::move(dxVert));
		}
	}

	if (vertices.empty())
	{
		return;
	}

	StaticObject& obj = staticObjects.emplace_back(StaticObject());
	Utils::AABB& objCulling = staticObjectsAABB.emplace_back();

	auto& defaultMemory =
		MemoryManager::Inst().GetBuff<DefaultBuffer_t>();

	// Set the texture name
	obj.textureKey = surf.texinfo->image->name;

	obj.verticesSizeInBytes = sizeof(ShDef::Vert::PosTexCoord) * vertices.size();
	obj.vertices = defaultMemory.Allocate(obj.verticesSizeInBytes);

	FArg::UpdateDefaultHeapBuff updateBuffArg;
	updateBuffArg.buffer = defaultMemory.GetGpuBuffer();
	updateBuffArg.offset = defaultMemory.GetOffset(obj.vertices);
	updateBuffArg.byteSize = obj.verticesSizeInBytes;
	updateBuffArg.data = vertices.data();
	updateBuffArg.alignment = 0;
	updateBuffArg.context = &context;

	ResourceManager::Inst().UpdateDefaultHeapBuff(updateBuffArg);

	static uint64_t allocSize = 0;
	uint64_t size = sizeof(ShDef::Vert::PosTexCoord) * vertices.size();

	// Fill up index buffer
	std::vector<uint32_t> indices;
	indices = Utils::GetIndicesListForTrianglelistFromPolygonPrimitive(vertices.size());

	obj.indicesSizeInBytes = sizeof(uint32_t) * indices.size();
	obj.indices = defaultMemory.Allocate(obj.indicesSizeInBytes);

	updateBuffArg.buffer = defaultMemory.GetGpuBuffer();
	updateBuffArg.offset = defaultMemory.GetOffset(obj.indices);
	updateBuffArg.byteSize = obj.indicesSizeInBytes;
	updateBuffArg.data = indices.data();
	updateBuffArg.alignment = 0;
	updateBuffArg.context = &context;

	ResourceManager::Inst().UpdateDefaultHeapBuff(updateBuffArg);

	std::vector<XMFLOAT4> verticesPos;
	verticesPos.reserve(vertices.size());

	for (const ShDef::Vert::PosTexCoord& vertex : vertices)
	{
		verticesPos.push_back(vertex.position);
	}

	const auto [bbMin, bbMax] = obj.GenerateAABB(verticesPos);
	
	objCulling.bbMin = bbMin;
	objCulling.bbMax = bbMax;
}

void Renderer::DecomposeGLModelNode(const model_t& model, const mnode_t& node, GPUJobContext& context)
{
	// Looks like if leaf return, leafs don't contain any geom
	if (node.contents != -1)
	{
		return;
	}

	// This is intermediate node, keep going for a leafs
	DecomposeGLModelNode(model, *node.children[0], context);
	DecomposeGLModelNode(model, *node.children[1], context);

	// Each surface inside node represents stand alone object with its own texture

	const unsigned int lastSurfInd = node.firstsurface + node.numsurfaces;
	const msurface_t* surf = &model.surfaces[node.firstsurface];

	for (unsigned int surfInd = node.firstsurface;
		surfInd < lastSurfInd;
		++surfInd, ++surf)
	{
		assert(surf != nullptr && "Error during graphical objects generation");

		CreateGraphicalObjectFromGLSurface(*surf, context);
	}
}

GPUJobContext Renderer::CreateContext(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	const int commandListIndex = commandListBuffer.allocator.Allocate();
	frame.acquiredCommandListsIndices.push_back(commandListIndex);

	return GPUJobContext(frame, commandListBuffer.commandLists[commandListIndex]);
}

void Renderer::GetDrawAreaSize(int* Width, int* Height)
{

	char modeVarName[] = "gl_mode";
	char modeVarVal[] = "3";
	cvar_t* mode = GetRefImport().Cvar_Get(modeVarName, modeVarVal, CVAR_ARCHIVE);

	assert(mode);
	GetRefImport().Vid_GetModeInfo(Width, Height, static_cast<int>(mode->value));
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
	min(scaledWidth, maxSize);

	for (scaledHeight = 1; scaledHeight < height; scaledHeight <<= 1);
	min(scaledHeight, maxSize);
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
	Logs::Logf(Logs::Category::Generic, "API: Get draw texture size %s", name);

	std::array<char, MAX_QPATH> texFullName;
	ResourceManager::Inst().GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	const Texture* tex = ResourceManager::Inst().FindTexture(texFullName.data());

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
	Logs::Log(Logs::Category::Generic, "API: End level loading");

	Frame& frame = dynamicModelRegContext->frame;

	dynamicModelRegContext->commandList.Close();
	
	GPUJobContext createDeferredTextureContext = CreateContext(frame);
	ResourceManager::Inst().CreateDeferredTextures(createDeferredTextureContext);
	
	CloseFrame(frame);

	ReleaseFrameResources(frame);

	ReleaseFrame(frame);

	if (std::shared_ptr<Semaphore> staticModelRegSemaphore = staticModelRegContext->frame.GetFinishSemaphore())
	{
		staticModelRegSemaphore->Wait();
	}

	staticModelRegContext = nullptr;
	dynamicModelRegContext = nullptr;

	RegisterObjectsAtFrameGraphs();

	Mod_FreeAll();

	Logs::Log(Logs::Category::Generic, "End level loading");
}

void Renderer::AddDrawCall_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data)
{
	Logs::Logf(Logs::Category::Generic, "API: AddDrawCall_RawPic");

	DrawCall_StretchRaw drawCall;
	drawCall.x = x;
	drawCall.y = y;
	drawCall.quadWidth = quadWidth;
	drawCall.quadHeight = quadHeight;
	
	// To enforce order of texture create/update, this check must be done in main thread,
	// so worker threads can run independently
	Texture* rawTex = ResourceManager::Inst().FindTexture(Texture::RAW_TEXTURE_NAME);

	if (rawTex == nullptr 
		|| rawTex->desc.width != textureWidth
		|| rawTex->desc.height != textureHeight)
	{
		TextureDesc desc = { textureWidth, textureHeight, DXGI_FORMAT_R8G8B8A8_UNORM };
		rawTex = ResourceManager::Inst().CreateTextureFromDataDeferred(data, desc, Texture::RAW_TEXTURE_NAME, GetMainThreadFrame());
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
	Logs::Logf(Logs::Category::Generic, "API: AddDrawCall_Pic %s", name);

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

	// Frame graph processing

	if (FrameGraphBuilder::Inst().IsSourceChanged() == true)
	{
		RebuildFrameGraph();
	}

	// Start work on the current frame
	AcquireMainThreadFrame();
	Frame& frame = GetMainThreadFrame();

	frame.colorBufferAndView = &GetNextSwapChainBufferAndView();
	frame.scissorRect = scissorRect;

	frame.frameNumber = frameCounter;

	++frameCounter;
}

void Renderer::EndFrame()
{
	Logs::Log(Logs::Category::Generic, "API: EndFrame");

	// All heavy lifting is here

	Frame& frame = GetMainThreadFrame();

	if (frame.texCreationRequests.empty() == false)
	{
		GPUJobContext createDeferredTextureContext = CreateContext(frame);
		ResourceManager::Inst().CreateDeferredTextures(createDeferredTextureContext);
	}

	// Proceed to next frame
	DetachMainThreadFrame();

	// Some preparations
	PreRenderSetUpFrame(frame);

	frame.frameGraph->Execute(frame);
}

void Renderer::PreRenderSetUpFrame(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	// Some preparations
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&frame.uiViewMat, tempMat);
	tempMat = XMMatrixOrthographicRH(frame.camera.width, frame.camera.height, 0.0f, 1.0f);

	XMStoreFloat4x4(&frame.uiProjectionMat, tempMat);
	
	// Static objects
	frame.visibleStaticObjectsIndices = BuildObjectsInFrustumList(frame.camera, staticObjectsAABB);

	// Dynamic objects
	frame.visibleEntitiesIndices = BuildVisibleDynamicObjectsList(frame.camera, frame.entities);
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

Texture* Renderer::RegisterDrawPic(const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: Register draw pic %s", name);

	// If dynamic model registration context exists, then we are in the middle of the level loading. At this point
	// current frame is most likely invalid
	Frame& frame = dynamicModelRegContext != nullptr ? dynamicModelRegContext->frame : GetMainThreadFrame();
	ResourceManager& resMan = ResourceManager::Inst();

	std::array<char, MAX_QPATH> texFullName;
	resMan.GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	Texture* newTex = resMan.CreateTextureFromFileDeferred(texFullName.data(), frame);

	return newTex;
}

void Renderer::RegisterWorldModel(const char* model)
{
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

	context.commandList.Open();

	char fullName[MAX_QPATH];
	Utils::Sprintf(fullName, sizeof(fullName), "maps/%s.bsp", model);

	char varName[] = "flushmap";
	char varDefVal[] = "0";
	cvar_t* flushMap = GetRefImport().Cvar_Get(varName, varDefVal, 0);

	if (strcmp(mod_known[0].name, fullName) || flushMap->value)
	{
		Mod_Free(&mod_known[0]);
	}

	// Create new world model
	model_t* mapModel = Mod_ForName(fullName, qTrue, context);

	// Legacy from quake 2 model handling system
	r_worldmodel = mapModel;

	DecomposeGLModelNode(*mapModel, *mapModel->nodes, context);

	// Submit frame
	context.commandList.Close();
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
	Logs::Logf(Logs::Category::Generic, "API: Begin level loading %s", mapName);

	RegisterWorldModel(mapName);

	AcquireMainThreadFrame();
	Frame& frame = GetMainThreadFrame();

	dynamicModelRegContext = std::make_unique<GPUJobContext>(CreateContext(frame));
	dynamicModelRegContext->commandList.Open();

	DetachMainThreadFrame();
}

model_s* Renderer::RegisterModel(const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: Register model %s", name);

	std::string modelName = name;

	Frame& frame = dynamicModelRegContext->frame;
	
	model_t* mod = Mod_ForName(modelName.data(), qFalse, *dynamicModelRegContext);

	if (mod)
	{

		switch (mod->type)
		{
		case mod_sprite:
		{
			//#TODO implement sprites 
			//dsprite_t* sprites = reinterpret_cast<dsprite_t *>(mod->extradata);
			//for (int i = 0; i < sprites->numframes; ++i)
			//{
			//	mod->skins[i] = FindOrCreateTexture(sprites->frames[i].name);
			//}

			//m_dynamicGraphicalObjects[mod] = CreateDynamicGraphicObjectFromGLModel(mod);

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
				mod->skins[i] = ResourceManager::Inst().FindOrCreateTexture(imageName, *dynamicModelRegContext);
			}

			dynamicObjectsModels[mod] = CreateDynamicGraphicObjectFromGLModel(mod, *dynamicModelRegContext);

			break;
		}
		case mod_brush:
		{
			//#TODO implement brush
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
