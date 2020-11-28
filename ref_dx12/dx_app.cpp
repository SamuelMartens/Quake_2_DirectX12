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
#include "dx_materialcompiler.h"

#ifdef max
#undef max
#endif

// Order is extremely important. Consider construction/destruction order
// when adding something
#define JOB_GUARD( context ) \
	DependenciesRAIIGuard_t dependenciesGuard(context); \
	CommandListRAIIGuard_t commandListGuard(context.commandList)

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


Renderer::Renderer()
{
}

Renderer& Renderer::Inst()
{
	static Renderer* app = nullptr;

	if (app == nullptr)
	{
		app = new Renderer();
	}

	return *app;
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
	if (m_hWindows)
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

	 m_hWindows = CreateWindowEx(
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

	assert(m_hWindows && "Failed to create windows");

	ShowWindow(m_hWindows, SW_SHOW);
	UpdateWindow(m_hWindows);

	SetForegroundWindow(m_hWindows);
	SetFocus(m_hWindows);

	GetRefImport().Vid_NewWindow(width, height);
}

void Renderer::InitDx()
{
	Logs::Log(Logs::Category::Generic, "Dx init started");

	// ------ Pre frames init -----
	// Stuff that doesn't require frames for initialization
	SetDebugMessageFilter();

	InitDescriptorSizes();

	CreateCommandQueue();

	CheckMSAAQualitySupport();

	CreateSwapChain();

	CreateDescriptorHeaps();
	
	CreateSwapChainBuffersAndViews();

	CreateCompiledMaterials();

	CreateTextureSampler();

	CreateFences(m_fence);

	InitCommandListsBuffer();

	// ------- Frames init -----

	InitFrames();

	// ------- After frames init -----

	InitUtils();

	// ------- Open init command list -----
	AcquireCurrentFrame();
	Context initContext = CreateContext(GetCurrentFrame());

	initContext.commandList.Open();

	// -- Steps that require command list --

	InitMemory(initContext);

	// ------- Close and execute init command list -----

	initContext.commandList.Close();

	CloseFrame(initContext.frame);

	ReleaseFrameResources(initContext.frame);

	// We are done with that frame
	ReleaseFrame(initContext.frame);
	DetachCurrentFrame();

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
	if (!Settings::DEBUG_LAYER_ENABLED || !Settings::DEBUG_MESSAGE_FILTER_ENABLED)
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
	std::fill(m_rawPalette.begin(), m_rawPalette.end(), 0);

	// Init dynamic objects constant buffers pool
	m_dynamicObjectsConstBuffersPool.obj.resize(Settings::DYNAM_OBJECT_CONST_BUFFER_POOL_SIZE);

	m_jobSystem.Init();

	InitScissorRect();

	ThreadingUtils::Init();

	//#DEBUG
	MaterialCompiler::Inst().GenerateMaterial();
	//END
}

void Renderer::InitMemory(Context& context)
{
	// Create default memory buffer
	m_defaultMemoryBuffer.allocBuffer.gpuBuffer = CreateDefaultHeapBuffer(nullptr, Settings::DEFAULT_MEMORY_BUFFER_SIZE, context);
	Diagnostics::SetResourceName(m_defaultMemoryBuffer.allocBuffer.gpuBuffer.Get(), "DefaultMemoryHeap");

	// Create upload memory buffer
	m_uploadMemoryBuffer.allocBuffer.gpuBuffer = CreateUploadHeapBuffer(Settings::UPLOAD_MEMORY_BUFFER_SIZE);
	Diagnostics::SetResourceName(m_uploadMemoryBuffer.allocBuffer.gpuBuffer.Get(), "UploadMemoryHeap");
}

void Renderer::InitScissorRect()
{
	int drawAreaWidth = 0;
	int drawAreaHeight = 0;

	GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);
	//#INFO scissor rectangle needs to be reset, every time command list is reset
	// Set scissor
	m_scissorRect = { 0, 0, drawAreaWidth, drawAreaHeight };
}

void Renderer::InitFrames()
{
	assert(Settings::SWAP_CHAIN_BUFFER_COUNT == Settings::FRAMES_NUM && "Swap chain buffer count shall be equal to frames num");

	for (int i = 0; i < Settings::FRAMES_NUM; ++i)
	{
		m_frames[i].Init(i);
	}
}

void Renderer::InitCommandListsBuffer()
{
	for (CommandList& commandList : m_commandListBuffer.commandLists)
	{
		commandList.Init();
		// We expect this command list to be closed after initialization
		commandList.Close();
	}
}

void Renderer::CreateDescriptorHeaps()
{
	rtvHeap = std::make_unique<DescriptorHeap>(Settings::RTV_DTV_DESCRIPTOR_HEAP_SIZE, 
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	dsvHeap = std::make_unique<DescriptorHeap>(Settings::RTV_DTV_DESCRIPTOR_HEAP_SIZE, 
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	cbvSrvHeap = std::make_unique<DescriptorHeap>(Settings::CBV_SRV_DESCRIPTOR_HEAP_SIZE,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	// Create sampler heap
	D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc;
	samplerHeapDesc.NumDescriptors = 1;
	samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	samplerHeapDesc.NodeMask = 0;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
		&samplerHeapDesc,
		IID_PPV_ARGS(m_samplerHeap.GetAddressOf())));

}

void Renderer::CreateSwapChainBuffersAndViews()
{
	for (int i = 0; i < Settings::SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		AssertBufferAndView& buffView = m_swapChainBuffersAndViews[i];

		// Get i-th buffer in a swap chain
		ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&buffView.buffer)));

		buffView.viewIndex = rtvHeap->Allocate(buffView.buffer);
	}
}

void Renderer::CreateSwapChain()
{
	// Create swap chain
	m_swapChain.Reset();

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
	swapChainDesc.OutputWindow = m_hWindows;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(Infr::Inst().GetFactory()->CreateSwapChain(m_commandQueue.Get(),
		&swapChainDesc,
		m_swapChain.GetAddressOf()));
}

void Renderer::CheckMSAAQualitySupport()
{
	if (Settings::MSAA_ENABLED == false)
		return;

	// Check 4X MSAA Quality Support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS MSQualityLevels;
	MSQualityLevels.Format = Settings::BACK_BUFFER_FORMAT;
	MSQualityLevels.SampleCount = Settings::MSAA_SAMPLE_COUNT;
	MSQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	MSQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(Infr::Inst().GetDevice()->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&MSQualityLevels,
		sizeof(MSQualityLevels)
	));

	m_MSQualityLevels = MSQualityLevels.NumQualityLevels;
	assert(m_MSQualityLevels > 0 && "Unexpected MSAA quality levels");
}

void Renderer::CreateCommandQueue()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
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

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
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

void Renderer::InitDescriptorSizes()
{
	ComPtr<ID3D12Device>& device = Infr::Inst().GetDevice();

	// Get descriptors sizes
	m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_cbvSrbDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_samplerDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

ComPtr<ID3D12RootSignature> Renderer::SerializeAndCreateRootSigFromRootDesc(const CD3DX12_ROOT_SIGNATURE_DESC& rootSigDesc) const
{
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	ThrowIfFailed(D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()));

	ComPtr<ID3D12RootSignature> resultRootSig;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&resultRootSig)
	));

	return resultRootSig;
}

void Renderer::CreateCompiledMaterials()
{
	std::vector<MaterialSource> materialsSource = MaterialSource::ConstructSourceMaterials();

	for (const MaterialSource& matSource : materialsSource)
	{
		m_materials.push_back(CompileMaterial(matSource));
	}
}

Material Renderer::CompileMaterial(const MaterialSource& materialSourse) const
{
	Material materialCompiled;

	materialCompiled.name = materialSourse.name;

	// Compiler root signature
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		materialSourse.rootParameters.size(),
		materialSourse.rootParameters.data(),
		materialSourse.staticSamplers.size(),
		materialSourse.staticSamplers.empty() ? nullptr : materialSourse.staticSamplers.data(),
		materialSourse.rootSignatureFlags);

	materialCompiled.rootSingature = SerializeAndCreateRootSigFromRootDesc(rootSigDesc);

	// Compile shaders
	 std::array<ComPtr<ID3DBlob>, MaterialSource::ShaderType::SIZE> compiledShaders;

	for (int i = 0; i < MaterialSource::ShaderType::SIZE; ++i)
	{
		if (materialSourse.shaders[i].empty())
		{
			continue;
		}

		compiledShaders[i] = LoadCompiledShader(materialSourse.shaders[i]);
	}

	// Create pso 
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc(materialSourse.psoDesc);

	psoDesc.pRootSignature = materialCompiled.rootSingature.Get();
	psoDesc.InputLayout = { materialSourse.inputLayout.data(), static_cast<UINT>(materialSourse.inputLayout.size())};

	ID3DBlob* currentBlob = nullptr;
	for (int i = 0; i < MaterialSource::ShaderType::SIZE; ++i)
	{
		MaterialSource::ShaderType shaderType = static_cast<MaterialSource::ShaderType>(i);
		currentBlob = compiledShaders[shaderType].Get();

		switch (shaderType)
		{
		case MaterialSource::ShaderType::Vs:
			{
				assert(currentBlob != nullptr && "Empty vertex shader blob");
				psoDesc.VS =
				{
					reinterpret_cast<BYTE*>(currentBlob->GetBufferPointer()),
					currentBlob->GetBufferSize()
				};
				break;
			}
		case MaterialSource::ShaderType::Ps:
			{
				assert(currentBlob != nullptr && "Empty pixel shader blob");
				psoDesc.PS =
				{
					reinterpret_cast<BYTE*>(currentBlob->GetBufferPointer()),
					currentBlob->GetBufferSize()
				};
				break;
			}
		case MaterialSource::ShaderType::Gs:
			{
				if (currentBlob != nullptr)
				{
					psoDesc.GS = 
					{
						reinterpret_cast<BYTE*>(currentBlob->GetBufferPointer()),
						currentBlob->GetBufferSize()
					};
				}
				break;
			}
		default:
			assert(false && "Shader compilation failed. Unknown shader type");
			break;
		}
	}

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&materialCompiled.pipelineState)));

	materialCompiled.primitiveTopology = materialSourse.primitiveTopology;

	return materialCompiled;
}

void Renderer::SetMaterialAsync(const std::string& name, CommandList& commandList)
{
	auto materialIt = std::find_if(m_materials.begin(), m_materials.end(), [name](const Material& mat)
	{
		return mat.name == name;
	});

	assert(materialIt != m_materials.end() && "Can't set requested material. It's not found");

	commandList.commandList->SetGraphicsRootSignature(materialIt->rootSingature.Get());
	commandList.commandList->SetPipelineState(materialIt->pipelineState.Get());
	commandList.commandList->IASetPrimitiveTopology(materialIt->primitiveTopology);
}

void Renderer::SetNonMaterialState(Context& context) const
{
	CommandList& commandList = context.commandList;
	Frame& frame = context.frame;

	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = static_cast<float>(frame.camera.width);
	viewport.Height = static_cast<float>(frame.camera.height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	commandList.commandList->RSSetViewports(1, &viewport);
	// Resetting scissor is mandatory 
	commandList.commandList->RSSetScissorRects(1, &frame.scissorRect);

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = rtvHeap->GetHandleCPU(frame.colorBufferAndView->viewIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE depthTargetView = dsvHeap->GetHandleCPU(frame.depthBufferViewIndex);

	// Specify buffer we are going to render to
	commandList.commandList->OMSetRenderTargets(1, &renderTargetView, true, &depthTargetView);

	ID3D12DescriptorHeap* descriptorHeaps[] = { cbvSrvHeap->GetHeapResource(), m_samplerHeap.Get() };
	commandList.commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
}

Frame& Renderer::GetCurrentFrame()
{
	ASSERT_MAIN_THREAD;
	assert(m_currentFrameIndex != Const::INVALID_INDEX && "Trying to get current frame which is invalid");

	return m_frames[m_currentFrameIndex];
}

void Renderer::SubmitFrame(Frame& frame)
{
	assert(frame.acquiredCommandListsIndices.empty() == false && "Trying to execute empty command lists");

	std::vector<ID3D12CommandList*> commandLists(frame.acquiredCommandListsIndices.size(), nullptr);

	for (int i = 0; i < frame.acquiredCommandListsIndices.size(); ++i)
	{
		const int commandListIndex = frame.acquiredCommandListsIndices[i];
		commandLists[i] = m_commandListBuffer.commandLists[commandListIndex].commandList.Get();
	}
	
	m_commandQueue->ExecuteCommandLists(commandLists.size(), commandLists.data());

	assert(frame.executeCommandListFenceValue == -1 && frame.executeCommandListEvenHandle == INVALID_HANDLE_VALUE &&
		"Trying to set up sync primitives for frame that already has it");

	frame.executeCommandListFenceValue = GenerateFenceValue();
	frame.executeCommandListEvenHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

	m_commandQueue->Signal(m_fence.Get(), frame.executeCommandListFenceValue);
	ThrowIfFailed(m_fence->SetEventOnCompletion(frame.executeCommandListFenceValue, frame.executeCommandListEvenHandle));

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with frameNumber %d submitted", frame.frameNumber);
}

void Renderer::OpenFrame(Frame& frame) const
{
	frame.Acquire();
}

void Renderer::CloseFrame(Frame& frame)
{
	SubmitFrame(frame);
	WaitForFrame(frame);

	frame.ResetSyncData();
}

void Renderer::ReleaseFrameResources(Frame& frame)
{
	Logs::Logf(Logs::Category::FrameSubmission, "Frame with frameNumber %d releases resources", frame.frameNumber);

	for (int acquiredCommandListIndex : frame.acquiredCommandListsIndices)
	{
		m_commandListBuffer.allocator.Delete(acquiredCommandListIndex);
	}

	frame.acquiredCommandListsIndices.clear();

	frame.colorBufferAndView = nullptr;
	frame.frameNumber = Const::INVALID_INDEX;

	DO_IN_LOCK(frame.uploadResources, clear());

	// Streaming drawing stuff
	frame.streamingObjectsHandlers.mutex.lock();
	for (BufferHandler handler : frame.streamingObjectsHandlers.obj)
	{
		m_uploadMemoryBuffer.Delete(handler);
	}
	frame.streamingObjectsHandlers.obj.clear();
	frame.streamingObjectsHandlers.mutex.unlock();


	// We are done with dynamic objects rendering. It's safe
	// to delete them
	frame.dynamicObjects.clear();

	frame.entitiesToDraw.clear();
	frame.particlesToDraw.clear();

	frame.texCreationRequests.clear();

	// Remove used draw calls
	frame.uiDrawCalls.clear();
}

void Renderer::AcquireCurrentFrame()
{
	ASSERT_MAIN_THREAD;

	assert(m_currentFrameIndex == Const::INVALID_INDEX && "Trying to acquire frame, while there is already frame acquired.");

	//#TODO this is bad. Acquire release frame should be explicit, if it is possible.
	// Also, is there better way than just manipulating m_currentFrameIndex and isInUse? 
	// Remember meaning of these things ( maybe I need at at least rename it?)
	// m_currentFrameIndex - is basically means index of frame that is used by main thread.
	//						 and also indicates if new frame shall be used
	// isInUse - is in general means, if any thread is using this frame.

	std::vector <std::shared_ptr<Semaphore>> framesFinishedSemaphores;

	// Try to find free frame
	auto frameIt = m_frames.begin();
	for (; frameIt != m_frames.end(); ++frameIt)
	{
		// It is important to grab that before isInUse check, so we never end up in the situation where semaphore
		// was deleted right after check, but before we manage to pull it from frameIt
		std::shared_ptr<Semaphore> frameFinishedSemaphore = frameIt->GetFinishSemaphore();
		
		if (frameIt->GetIsInUse() == false)
		{
			OpenFrame(*frameIt);
			m_currentFrameIndex = std::distance(m_frames.begin(), frameIt);

			Logs::Logf(Logs::Category::FrameSubmission, "Frame with index %d acquired", m_currentFrameIndex);

			return;
		}
		
		framesFinishedSemaphores.push_back(std::move(frameFinishedSemaphore));
	}

	if (framesFinishedSemaphores.empty() == false)
	{
		Semaphore::WaitForMultipleAny(framesFinishedSemaphores);
	}

	// Try again after wait 
	frameIt = m_frames.begin();
	for (; frameIt != m_frames.end(); ++frameIt)
	{
		if (frameIt->GetIsInUse() == false)
		{
			break;
		}
	}

	assert(frameIt != m_frames.end() && "Can't find free frame");

	OpenFrame(*frameIt);
	m_currentFrameIndex = std::distance(m_frames.begin(), frameIt);

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with index %d acquired", m_currentFrameIndex);
}

void Renderer::DetachCurrentFrame()
{
	ASSERT_MAIN_THREAD;

	assert(m_currentFrameIndex != Const::INVALID_INDEX && "Trying to detach frame. But there is nothing to detach.");

	Logs::Logf(Logs::Category::FrameSubmission, "Frame with index %d and frameNumber %d detached", m_currentFrameIndex, m_frames[m_currentFrameIndex].frameNumber);

	m_currentFrameIndex = Const::INVALID_INDEX;
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

	if (m_fence->GetCompletedValue() < frame.executeCommandListFenceValue)
	{
		DWORD res = WaitForSingleObject(frame.executeCommandListEvenHandle, INFINITE);

		assert(res == WAIT_OBJECT_0 && "Frame wait ended in unexpected way.");
	}
}

void Renderer::WaitForPrevFrame(Frame& frame) const
{
	// Find prev frame
	const auto frameIt = std::find_if(m_frames.cbegin(), m_frames.cend(), 
		[&frame](const Frame& f)
	{
		return f.GetIsInUse() == true && f.frameNumber == frame.frameNumber - 1;
	});

	if (frameIt == m_frames.cend())
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
	return m_fenceValue++;
}

int Renderer::GetFenceValue() const
{
	return m_fenceValue;
}

void Renderer::CreateTextureSampler()
{
	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	Infr::Inst().GetDevice()->CreateSampler(&samplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
}

int Renderer::GetMSAASampleCount() const
{
	return Settings::MSAA_ENABLED ? Settings::MSAA_SAMPLE_COUNT : 1;
}

int Renderer::GetMSAAQuality() const
{
	return Settings::MSAA_ENABLED ? (m_MSQualityLevels - 1) : 0;
}

AssertBufferAndView& Renderer::GetNextSwapChainBufferAndView()
{
	AssertBufferAndView& buffAndView = m_swapChainBuffersAndViews[m_currentBackBuffer];
	m_currentBackBuffer = (m_currentBackBuffer + 1) % Settings::SWAP_CHAIN_BUFFER_COUNT;

	buffAndView.Lock();

	return buffAndView;
}

void Renderer::PresentAndSwapBuffers(Frame& frame)
{
	ThrowIfFailed(m_swapChain->Present(0, 0));

	frame.colorBufferAndView->Unlock();

}

Texture* Renderer::CreateTextureFromFileDeferred(const char* name, Frame& frame)
{
	std::scoped_lock<std::mutex> lock(m_textures.mutex);

	Texture tex;
	tex.name = name;

	Texture* result = &m_textures.obj.insert_or_assign(tex.name, std::move(tex)).first->second;

	TexCreationRequest_FromFile texRequest(*result);
	frame.texCreationRequests.push_back(std::move(texRequest));

	return result;
}

Texture* Renderer::CreateTextureFromFile(const char* name, Context& context)
{
	std::scoped_lock<std::mutex> lock(m_textures.mutex);

	return _CreateTextureFromFile(name, context);
}

Texture* Renderer::CreateTextureFromDataDeferred(const std::byte* data, int width, int height, int bpp, const char* name, Frame& frame)
{
	std::scoped_lock<std::mutex> lock(m_textures.mutex);

	Texture tex;
	tex.name = name;
	tex.width = width;
	tex.height = height;
	tex.bpp = bpp;

	Texture* result = &m_textures.obj.insert_or_assign(tex.name, std::move(tex)).first->second;

	TexCreationRequest_FromData texRequest(*result);
	const int texSize = width * height * bpp / 8;

	texRequest.data.resize(texSize);
	memcpy(texRequest.data.data(), data, texSize);

	frame.texCreationRequests.push_back(std::move(texRequest));

	return result;
}

Texture* Renderer::_CreateTextureFromFile(const char* name, Context& context)
{
	if (name == nullptr)
		return nullptr;

	std::array<char, MAX_QPATH> nonConstName;
	// Some old functions access only non const pointer, that's just work around
	char* nonConstNamePtr = nonConstName.data();
	strcpy(nonConstNamePtr, name);


	constexpr int fileExtensionLength = 4;
	const char* texFileExtension = nonConstNamePtr + strlen(nonConstNamePtr) - fileExtensionLength;

	std::byte* image = nullptr;
	std::byte* palette = nullptr;
	int width = 0;
	int height = 0;
	int bpp = 0;

	if (strcmp(texFileExtension, ".pcx") == 0)
	{
		bpp = 8;
		Utils::LoadPCX(nonConstNamePtr, &image, &palette, &width, &height);
	}
	else if (strcmp(texFileExtension, ".wal") == 0)
	{
		bpp = 8;
		Utils::LoadWal(nonConstNamePtr, &image, &width, &height);
	}
	else if (strcmp(texFileExtension, ".tga") == 0)
	{
		bpp = 32;
		Utils::LoadTGA(nonConstNamePtr, &image, &width, &height);
	}
	else
	{
		assert(false && "Invalid texture file extension");
		return nullptr;
	}

	if (image == nullptr)
	{
		assert(false && "Failed to create texture from file");
		return nullptr;
	}

	unsigned int* image32 = nullptr;
	constexpr int maxImageSize = 512 * 256;
	// This would be great to have this on heap, but it's too big and
	// cause stack overflow immediately on entrance in function
	static unsigned int* fixedImage = new unsigned int[maxImageSize];
#ifdef _DEBUG
	memset(fixedImage, 0, maxImageSize * sizeof(unsigned int));
#endif

	//#TODO I ignore texture type (which is basically represents is this texture sky or
	// or something else. It's kind of important here, as we need to handle pictures
	// differently sometimes depending on type. I will need to take care of it later.
	if (bpp == 8)
	{
		ImageBpp8To32(image, width, height, fixedImage);
		bpp = 32;

		image32 = fixedImage;
	}
	else
	{
		image32 = reinterpret_cast<unsigned int*>(image);
	}

	// I might need this later
	//int scaledWidth = 0;
	//int scaledHeight = 0;

	//FindImageScaledSizes(width, height, scaledWidth, scaledHeight);

	//std::vector<unsigned int> resampledImage(maxImageSize, 0);

	//if (scaledWidth != width || scaledHeight != height)
	//{
	//	ResampleTexture(image32, width, height, resampledImage.data(), scaledHeight, scaledWidth);
	//	image32 = resampledImage.data();
	//}

	Texture* createdTex = _CreateTextureFromData(reinterpret_cast<std::byte*>(image32), width, height, bpp, name, context);

	if (image != nullptr)
	{
		free(image);
	}

	if (palette != nullptr)
	{
		free(palette);
	}

	return createdTex;
}

void Renderer::_CreateGpuTexture(const unsigned int* raw, int width, int height, int bpp, Context& context, Texture& outTex)
{
	CommandList& commandList = context.commandList;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	// Create destination texture
	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&outTex.buffer)));

	// Count alignment and go for what we need
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(outTex.buffer.Get(), 0, 1);

	ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);
	Diagnostics::SetResourceNameWithAutoId(textureUploadBuffer.Get(), "TextureUploadBuffer_CreateTexture");

	DO_IN_LOCK(context.frame.uploadResources, push_back(textureUploadBuffer));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = raw;
	// Divide by 8 cause bpp is bits per pixel, not bytes
	textureData.RowPitch = width * bpp / 8;
	// Not SlicePitch but texture size in our case
	textureData.SlicePitch = textureData.RowPitch * height;

	UpdateSubresources(commandList.commandList.Get(), outTex.buffer.Get(), textureUploadBuffer.Get(), 0, 0, 1, &textureData);
	commandList.commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		outTex.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));


	DescriptorHeap::Desc_t srvDescription = D3D12_SHADER_RESOURCE_VIEW_DESC{};
	D3D12_SHADER_RESOURCE_VIEW_DESC& srvDescriptionRef = std::get<D3D12_SHADER_RESOURCE_VIEW_DESC>(srvDescription);
	srvDescriptionRef.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescriptionRef.Format = textureDesc.Format;
	srvDescriptionRef.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescriptionRef.Texture2D.MostDetailedMip = 0;
	srvDescriptionRef.Texture2D.MipLevels = 1;
	srvDescriptionRef.Texture2D.ResourceMinLODClamp = 0.0f;

	outTex.texViewIndex = cbvSrvHeap->Allocate(outTex.buffer, &srvDescription);
}

Texture* Renderer::_CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, Context& context)
{
	Logs::Logf(Logs::Category::Textures, "Create texture %s", name);

	Texture tex;
	_CreateGpuTexture(reinterpret_cast<const unsigned int*>(data), width, height, bpp, context, tex);

	tex.width = width;
	tex.height = height;
	tex.bpp = bpp;

	tex.name = name;

	Diagnostics::SetResourceName(tex.buffer.Get(), tex.name);

	return &m_textures.obj.insert_or_assign(tex.name, std::move(tex)).first->second;
}

Texture* Renderer::CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, Context& context)
{
	std::scoped_lock<std::mutex> lock(m_textures.mutex);

	return _CreateTextureFromData(data, width, height, bpp, name, context);
}

ComPtr<ID3D12Resource> Renderer::CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, Context& context)
{
	// Create actual buffer 
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = byteSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.SampleDesc.Quality = 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ComPtr<ID3D12Resource> buffer;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer)
	));

	ComPtr<ID3D12GraphicsCommandList>& commandList = context.commandList.commandList;

	if (data != nullptr)
	{
		// Create upload buffer
		ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(byteSize);
		Diagnostics::SetResourceNameWithAutoId(uploadBuffer.Get(), "UploadBuffer_CreateDefaultHeap");

		DO_IN_LOCK(context.frame.uploadResources, push_back(uploadBuffer));

		// Describe upload resource data 
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data;
		subResourceData.RowPitch = byteSize;
		subResourceData.SlicePitch = subResourceData.RowPitch;

		UpdateSubresources(commandList.Get(), buffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	}

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ
	));

	return buffer;
}

ComPtr<ID3D12Resource> Renderer::CreateUploadHeapBuffer(UINT64 byteSize) const
{
	ComPtr<ID3D12Resource> uploadHeapBuffer;

	// Create actual buffer 
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = byteSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.SampleDesc.Quality = 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadHeapBuffer)
	));

	return uploadHeapBuffer;
}

void Renderer::UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const
{
	assert(args.buffer != nullptr &&
		args.alignment != -1 &&
		args.byteSize != Const::INVALID_SIZE &&
		args.data != nullptr &&
		args.offset != Const::INVALID_OFFSET && "Uninitialized arguments in update upload buff");

	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;

	BYTE* mappedMemory = nullptr;
	// This parameter indicates the range that CPU might read. If begin and end are equal, we promise
	// that CPU will never try to read from this memory
	D3D12_RANGE mappedRange = { 0, 0 };

	ThrowIfFailed(args.buffer->Map(0, &mappedRange, reinterpret_cast<void**>(&mappedMemory)));

	memcpy(mappedMemory + args.offset, args.data, dataSize);

	args.buffer->Unmap(0, &mappedRange);
}

void Renderer::UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args)
{
	assert(args.buffer != nullptr &&
		args.alignment != -1 &&
		args.byteSize != Const::INVALID_SIZE &&
		args.data != nullptr &&
		args.offset != Const::INVALID_OFFSET &&
		args.context != nullptr &&
		"Uninitialized arguments in update default buff");

	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;

	Frame& frame = args.context->frame;
	CommandList& commandList = args.context->commandList;

	// Create upload buffer
	ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(args.byteSize);
	Diagnostics::SetResourceNameWithAutoId(uploadBuffer.Get(), "UploadBuffer_UpdateDefaultHeap");

	DO_IN_LOCK(frame.uploadResources, push_back(uploadBuffer));

	FArg::UpdateUploadHeapBuff uploadHeapBuffArgs;
	uploadHeapBuffArgs.alignment = 0;
	uploadHeapBuffArgs.buffer = uploadBuffer;
	uploadHeapBuffArgs.byteSize = args.byteSize;
	uploadHeapBuffArgs.data = args.data;
	uploadHeapBuffArgs.offset = 0;
	UpdateUploadHeapBuff(uploadHeapBuffArgs);

	commandList.commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		args.buffer.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_COPY_DEST
	));

	// Last argument is intentionally args.byteSize, cause that's how much data we pass to this function
	// we don't want to read out of range
	commandList.commandList->CopyBufferRegion(args.buffer.Get(), args.offset, uploadBuffer.Get(), 0, args.byteSize);

	commandList.commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		args.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ
	));
}

DynamicObjectConstBuffer& Renderer::FindDynamicObjConstBuffer()
{
	m_dynamicObjectsConstBuffersPool.mutex.lock();
	auto resIt = std::find_if(m_dynamicObjectsConstBuffersPool.obj.begin(), m_dynamicObjectsConstBuffersPool.obj.end(), 
		[](const DynamicObjectConstBuffer& buff) 
	{
		return buff.isInUse == false;
	});

	assert(resIt != m_dynamicObjectsConstBuffersPool.obj.end() && "Can't find free dynamic object const buffer");
	resIt->isInUse = true;
	
	m_dynamicObjectsConstBuffersPool.mutex.unlock();


	if (resIt->constantBufferHandler == BufConst::INVALID_BUFFER_HANDLER)
	{
		// This buffer doesn't have any memory allocated. Do it now
		// Allocate constant buffer
		static const unsigned int DynamicObjectConstSize =
			Utils::Align(sizeof(ShDef::ConstBuff::AnimInterpTranstMap), Settings::CONST_BUFFER_ALIGNMENT);

		resIt->constantBufferHandler = m_uploadMemoryBuffer.Allocate(DynamicObjectConstSize);
	}

	return *resIt;
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

void Renderer::EndFrameJob(Context& context)
{
	Logs::Logf(Logs::Category::Job, "EndFrame job started frame %d", context.frame.frameNumber);

	context.commandList.Open();

	Frame& frame = context.frame;
	// This is stupid. I use separate command list just for a few commands, bruh
	context.commandList.commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			frame.colorBufferAndView->buffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	context.commandList.Close();

	// Make sure everything else is done
	context.waitDependancy->Wait();

	// We can't submit command lists attached to render target that is not current back buffer,
	// so we need to wait until previous frame is done
	WaitForPrevFrame(context.frame);

	CloseFrame(frame);

	PresentAndSwapBuffers(frame);

	ReleaseFrameResources(frame);

	ReleaseFrame(frame);

	ThreadingUtils::AssertUnlocked(context.frame.streamingObjectsHandlers);
	ThreadingUtils::AssertUnlocked(context.frame.uploadResources);
	
	// Delete shared resources marked for deletion
	DeleteRequestedResources();

	Logs::Log(Logs::Category::Job, "EndFrame job ended");
}

void Renderer::BeginFrameJob(Context& context)
{
	Logs::Logf(Logs::Category::Job, "BeginFrame job started frame %d", context.frame.frameNumber);

	JOB_GUARD(context);

	CommandList& commandList = context.commandList;
	Frame& frame = context.frame;

	// Indicate buffer transition to write state
	commandList.commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			frame.colorBufferAndView->buffer.Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = rtvHeap->GetHandleCPU(frame.colorBufferAndView->viewIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE depthTargetView = dsvHeap->GetHandleCPU(frame.depthBufferViewIndex);

	// Clear back buffer and depth buffer
	commandList.commandList->ClearRenderTargetView(renderTargetView, DirectX::Colors::Black, 0, nullptr);
	commandList.commandList->ClearDepthStencilView(
		depthTargetView,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);

	Logs::Logf(Logs::Category::Job, "BeginFrame job ended frame %d", frame.frameNumber);
}

void Renderer::DrawUIJob(Context& context)
{
	JOB_GUARD(context);
	Logs::Logf(Logs::Category::Job, "DrawUI job started frame %d", context.frame.frameNumber);

	Frame& frame = context.frame;

	if (frame.uiDrawCalls.empty())
	{
		Logs::Log(Logs::Category::Job, "DrawUI job started. No draw calls");
		return;
	}
	// This is ugly :( I use this for both Constant buffer and Vertex buffer. As a result,
	// both should be aligned for constant buffers.
	const int perDrawCallMemoryRequired = Utils::Align(
		Utils::Align(sizeof(ShDef::ConstBuff::TransMat), Settings::CONST_BUFFER_ALIGNMENT) + 
		6 * sizeof(ShDef::Vert::PosTexCoord),
		Settings::CONST_BUFFER_ALIGNMENT);

	// Calculate amount of memory required for all draw calls
	const int requiredMemoryAlloc = context.frame.uiDrawCalls.size() * perDrawCallMemoryRequired;

	BufferHandler memoryBufferHandle = m_uploadMemoryBuffer.Allocate(requiredMemoryAlloc);
	DO_IN_LOCK(frame.streamingObjectsHandlers, push_back(memoryBufferHandle));

	BufferPiece currentBufferPiece = {memoryBufferHandle, 0};

	// Set some matrices
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&frame.uiViewMat, tempMat);

	tempMat = XMMatrixOrthographicRH(frame.camera.width, frame.camera.height, 0.0f, 1.0f);
	XMStoreFloat4x4(&frame.uiProjectionMat, tempMat);

	Diagnostics::BeginEvent(context.commandList.commandList.Get(), "UI drawing");

	SetNonMaterialState(context);

	SetMaterialAsync(MaterialSource::STATIC_MATERIAL_NAME, context.commandList);

	for (const DrawCall_UI_t& dc : context.frame.uiDrawCalls)
	{
		std::visit([&context, &currentBufferPiece, this](auto&& drawCall)
		{
			using T = std::decay_t<decltype(drawCall)>;

			if constexpr (std::is_same_v<T, DrawCall_Char>)
			{
				Draw_Char(drawCall.x, drawCall.y, drawCall.num, currentBufferPiece, context);
			}
			else if constexpr (std::is_same_v<T, DrawCall_Pic>)
			{
				Draw_Pic(drawCall.x, drawCall.y, drawCall.name.c_str(), currentBufferPiece, context);
			}
			else if constexpr (std::is_same_v<T, DrawCall_StretchRaw>)
			{
				Draw_RawPic(drawCall, currentBufferPiece, context);
			}
			else
			{
				static_assert(false, "Invalid class in draw UI");
			}
		}
		, dc);

		currentBufferPiece.offset += perDrawCallMemoryRequired;

	}

	Diagnostics::EndEvent(context.commandList.commandList.Get());

	Logs::Logf(Logs::Category::Job, "DrawUI job ended frame %d", frame.frameNumber);
}

void Renderer::DrawStaticGeometryJob(Context& context)
{
	JOB_GUARD(context);

	Logs::Logf(Logs::Category::Job, "Static job started frame %d", context.frame.frameNumber);

	CommandList& commandList = context.commandList;

	// Static geometry 
	Diagnostics::BeginEvent(commandList.commandList.Get(), "Static materials");

	SetNonMaterialState(context);
	SetMaterialAsync(MaterialSource::STATIC_MATERIAL_NAME, commandList);
	
	std::vector<int> visibleStaticObj = BuildObjectsInFrustumList(context.frame.camera, m_staticObjectsAABB);

	for (int i = 0; i < visibleStaticObj.size(); ++i)
	{
		const StaticObject& obj = m_staticObjects[visibleStaticObj[i]];

		UpdateStaticObjectConstantBuffer(obj, context);

		if (obj.indices != BufConst::INVALID_BUFFER_HANDLER)
		{
			DrawIndiced(obj, context);
		}
		else
		{
			Draw(obj, context);
		}
	}

	Diagnostics::EndEvent(commandList.commandList.Get());

	Logs::Logf(Logs::Category::Job, "Static job ended frame %d", context.frame.frameNumber);
}

void Renderer::DrawDynamicGeometryJob(Context& context)
{
	JOB_GUARD(context);

	Logs::Logf(Logs::Category::Job, "Dynamic job started frame %d", context.frame.frameNumber);

	CommandList& commandList = context.commandList;
	Frame& frame = context.frame;
	const std::vector<entity_t>& entitiesToDraw = context.frame.entitiesToDraw;

	// Dynamic geometry
	Diagnostics::BeginEvent(commandList.commandList.Get(), "Dynamic materials");

	SetNonMaterialState(context);
	SetMaterialAsync(MaterialSource::DYNAMIC_MATERIAL_NAME, commandList);

	for (int i = 0; i < entitiesToDraw.size(); ++i)
	{
		const entity_t& entity = (entitiesToDraw[i]);

		if (entity.model == nullptr ||
			entity.flags  & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) ||
			IsVisible(entity, context.frame.camera) == false)
		{
			continue;
		}
		
		assert(m_dynamicObjectsModels.find(entity.model) != m_dynamicObjectsModels.end()
			&& "Cannot render dynamic graphical object. Such model is not found");


		DynamicObjectModel& model = m_dynamicObjectsModels[entity.model];
		DynamicObjectConstBuffer& constBuffer = FindDynamicObjConstBuffer();

		// Const buffer should be a separate component, because if we don't do this different entities
		// will use the same model, but with different transformation
		DynamicObject& object = frame.dynamicObjects.emplace_back(DynamicObject(&model, &constBuffer));

		UpdateDynamicObjectConstantBuffer(object, entity, context);
		DrawIndiced(object, entity, context);
	}

	Diagnostics::EndEvent(commandList.commandList.Get());

	Logs::Logf(Logs::Category::Job, "Dynamic job ended frame %d", context.frame.frameNumber);
}

void Renderer::DrawParticleJob(Context& context)
{
	JOB_GUARD(context);

	Logs::Logf(Logs::Category::Job, "Particle job started frame %d", context.frame.frameNumber);

	CommandList& commandList = context.commandList;
	const std::vector<particle_t>& particlesToDraw = context.frame.particlesToDraw;

	Diagnostics::BeginEvent(commandList.commandList.Get(), "Particles");

	if (particlesToDraw.empty() == false)
	{
		SetNonMaterialState(context);
		SetMaterialAsync(MaterialSource::PARTICLE_MATERIAL_NAME, commandList);

		// Particles share the same constant buffer, so we only need to update it once
		const BufferHandler particleConstantBufferHandler = UpdateParticleConstantBuffer(context);

		// Preallocate vertex buffer for particles
		constexpr int singleParticleSize = sizeof(ShDef::Vert::PosCol);
		const int vertexBufferSize = singleParticleSize * particlesToDraw.size();
		const BufferHandler particleVertexBufferHandler = m_uploadMemoryBuffer.Allocate(vertexBufferSize);
		DO_IN_LOCK(context.frame.streamingObjectsHandlers, push_back(particleVertexBufferHandler));

		assert(particleVertexBufferHandler != BufConst::INVALID_BUFFER_HANDLER && "Failed to allocate particle vertex buffer");

		// Gather all particles, and do one draw call for everything at once
		for (int i = 0, currentVertexBufferOffset = 0; i < particlesToDraw.size(); ++i)
		{
			AddParticleToDrawList(particlesToDraw[i], particleVertexBufferHandler, currentVertexBufferOffset);

			currentVertexBufferOffset += singleParticleSize;
		}

		DrawParticleDrawList(particleVertexBufferHandler, vertexBufferSize, particleConstantBufferHandler, context);
	}

	Diagnostics::EndEvent(commandList.commandList.Get());

	Logs::Logf(Logs::Category::Job, "Particle job ended frame %d", context.frame.frameNumber);
}

void Renderer::CreateDeferredTextures(Context& context)
{
	CommandListRAIIGuard_t commandListGuard(context.commandList);

	std::scoped_lock<std::mutex> lock(m_textures.mutex);

	for (const TextureCreationRequest_t& tr : context.frame.texCreationRequests)
	{
		std::visit([&context, this](auto&& texRequest) 
		{
			using T = std::decay_t<decltype(texRequest)>;

			if constexpr (std::is_same_v<T, TexCreationRequest_FromFile>)
			{
				std::string name = texRequest.texture.name;
				_CreateTextureFromFile(name.c_str(), context);
			}
			else if constexpr (std::is_same_v<T, TexCreationRequest_FromData>)
			{
				// Get tex actual color
				const int textureSize = texRequest.texture.width * texRequest.texture.height;
				std::vector<unsigned int> texture(textureSize, 0);
				for (int i = 0; i < textureSize; ++i)
				{
					texture[i] = m_rawPalette[std::to_integer<int>(texRequest.data[i])];
				}

				std::string name = texRequest.texture.name;
				_CreateTextureFromData(
					reinterpret_cast<std::byte*>(texture.data()),
					texRequest.texture.width,
					texRequest.texture.height,
					texRequest.texture.bpp,
					name.c_str(),
					context);
			}
			else
			{
				static_assert(false, "Invalid class in Deferred Tex creation");
			}
		}
		, tr);
	}
}

ComPtr<ID3DBlob> Renderer::LoadCompiledShader(const std::string& filename) const
{
	std::ifstream fin(filename, std::ios::binary);

	fin.seekg(0, std::ios_base::end);
	const std::ifstream::pos_type size = static_cast<int>(fin.tellg());
	fin.seekg(0, std::ios::beg);

	ComPtr<ID3DBlob> blob = nullptr;

	ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

	fin.read(static_cast<char*>(blob->GetBufferPointer()), size);
	fin.close();

	return blob;
}

void Renderer::ShutdownWin32()
{
	DestroyWindow(m_hWindows);
	m_hWindows = NULL;
}

DynamicObjectModel Renderer::CreateDynamicGraphicObjectFromGLModel(const model_t* model, Context& context)
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

	// Load GPU buffers
	const int vertexBufferSize = vertexBuffer.size() * sizeof(XMFLOAT4);
	const int indexBufferSize = normalizedIndexBuffer.size() * sizeof(int);
	const int texCoordsBufferSize = normalizedTexCoordsBuffer.size() * sizeof(XMFLOAT2);

	object.vertices = m_defaultMemoryBuffer.Allocate(vertexBufferSize);
	object.indices = m_defaultMemoryBuffer.Allocate(indexBufferSize);
	object.textureCoords = m_defaultMemoryBuffer.Allocate(texCoordsBufferSize);

	// Get vertices in
	FArg::UpdateDefaultHeapBuff updateArgs;
	updateArgs.alignment = 0;
	updateArgs.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateArgs.byteSize = vertexBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(vertexBuffer.data());
	updateArgs.offset = m_defaultMemoryBuffer.GetOffset(object.vertices);
	updateArgs.context = &context;
	UpdateDefaultHeapBuff(updateArgs);

	// Get indices in
	updateArgs.alignment = 0;
	updateArgs.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateArgs.byteSize = indexBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalizedIndexBuffer.data());
	updateArgs.offset = m_defaultMemoryBuffer.GetOffset(object.indices);
	updateArgs.context = &context;
	UpdateDefaultHeapBuff(updateArgs);

	// Get tex coords in
	updateArgs.alignment = 0;
	updateArgs.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateArgs.byteSize = texCoordsBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalizedTexCoordsBuffer.data());
	updateArgs.offset = m_defaultMemoryBuffer.GetOffset(object.textureCoords);
	updateArgs.context = &context;
	UpdateDefaultHeapBuff(updateArgs);

	// Get textures in
	object.textures.reserve(aliasHeader->num_skins);

	for (int i = 0; i < aliasHeader->num_skins; ++i)
	{
		object.textures.push_back(model->skins[i]->name);
	}

	return object;
}

void Renderer::CreateGraphicalObjectFromGLSurface(const msurface_t& surf, Context& context)
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

	StaticObject& obj = m_staticObjects.emplace_back(StaticObject());
	Utils::AABB& objCulling = m_staticObjectsAABB.emplace_back();

	// Set the texture name
	obj.textureKey = surf.texinfo->image->name;

	obj.verticesSizeInBytes = sizeof(ShDef::Vert::PosTexCoord) * vertices.size();
	obj.vertices = m_defaultMemoryBuffer.Allocate(obj.verticesSizeInBytes);

	FArg::UpdateDefaultHeapBuff updateBuffArg;
	updateBuffArg.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateBuffArg.offset = m_defaultMemoryBuffer.GetOffset(obj.vertices);
	updateBuffArg.byteSize = obj.verticesSizeInBytes;
	updateBuffArg.data = vertices.data();
	updateBuffArg.alignment = 0;
	updateBuffArg.context = &context;

	UpdateDefaultHeapBuff(updateBuffArg);

	static uint64_t allocSize = 0;
	uint64_t size = sizeof(ShDef::Vert::PosTexCoord) * vertices.size();

	// Fill up index buffer
	std::vector<uint32_t> indices;
	indices = Utils::GetIndicesListForTrianglelistFromPolygonPrimitive(vertices.size());

	obj.indicesSizeInBytes = sizeof(uint32_t) * indices.size();
	obj.indices = m_defaultMemoryBuffer.Allocate(obj.indicesSizeInBytes);

	updateBuffArg.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateBuffArg.offset = m_defaultMemoryBuffer.GetOffset(obj.indices);
	updateBuffArg.byteSize = obj.indicesSizeInBytes;
	updateBuffArg.data = indices.data();
	updateBuffArg.alignment = 0;
	updateBuffArg.context = &context;

	UpdateDefaultHeapBuff(updateBuffArg);

	const unsigned int PictureObjectConstSize = Utils::Align(sizeof(ShDef::ConstBuff::TransMat), 
		Settings::CONST_BUFFER_ALIGNMENT);

	// Init frame data
	for (int i = 0; i < obj.frameData.size(); ++i)
	{
		obj.frameData[i].constantBufferHandler = m_uploadMemoryBuffer.Allocate(PictureObjectConstSize);
	}

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

void Renderer::DecomposeGLModelNode(const model_t& model, const mnode_t& node, Context& context)
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

Context Renderer::CreateContext(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	const int commandListIndex = m_commandListBuffer.allocator.Allocate();
	frame.acquiredCommandListsIndices.push_back(commandListIndex);

	return Context(frame, m_commandListBuffer.commandLists[commandListIndex]);
}

void Renderer::Draw(const StaticObject& object, Context& context)
{
	CommandList& commandList = context.commandList;

	// Set vertex buffer
	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = m_defaultMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() + m_defaultMemoryBuffer.GetOffset(object.vertices);
	vertBuffView.StrideInBytes = sizeof(ShDef::Vert::PosTexCoord);
	vertBuffView.SizeInBytes = object.verticesSizeInBytes;

	commandList.commandList->IASetVertexBuffers(0, 1, &vertBuffView);

	// Binding root signature params

	// 1)
	const Texture& texture = *FindTexture(object.textureKey);

	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle = cbvSrvHeap->GetHandleGPU(texture.texViewIndex);

	commandList.commandList->SetGraphicsRootDescriptorTable(0, texHandle);


	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texture.samplerInd, m_samplerDescriptorSize);

	commandList.commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	BufferHandler constBufferHandler = object.frameData[context.frame.GetArrayIndex()].constantBufferHandler;
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constBufferHandler);

	commandList.commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	commandList.commandList->DrawInstanced(vertBuffView.SizeInBytes / vertBuffView.StrideInBytes, 1, 0, 0);
}

void Renderer::DrawIndiced(const StaticObject& object, Context& context)
{
	assert(object.indices != BufConst::INVALID_BUFFER_HANDLER && "Trying to draw indexed object without index buffer");

	CommandList& commandList = context.commandList;

	// Set vertex buffer
	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = m_defaultMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() + m_defaultMemoryBuffer.GetOffset(object.vertices);
	vertBuffView.StrideInBytes = sizeof(ShDef::Vert::PosTexCoord);
	vertBuffView.SizeInBytes = object.verticesSizeInBytes;

	commandList.commandList->IASetVertexBuffers(0, 1, &vertBuffView);

	// Set index buffer
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	indexBufferView.BufferLocation = m_defaultMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() + m_defaultMemoryBuffer.GetOffset(object.indices);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = object.indicesSizeInBytes;

	commandList.commandList->IASetIndexBuffer(&indexBufferView);


	// Binding root signature params

	// 1)
	const Texture& texture = *FindTexture(object.textureKey);

	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle = cbvSrvHeap->GetHandleGPU(texture.texViewIndex);;

	commandList.commandList->SetGraphicsRootDescriptorTable(0, texHandle);

	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texture.samplerInd, m_samplerDescriptorSize);

	commandList.commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	BufferHandler constBufferHandler = object.frameData[context.frame.GetArrayIndex()].constantBufferHandler;
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constBufferHandler);

	commandList.commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	commandList.commandList->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
}

void Renderer::DrawIndiced(const DynamicObject& object, const entity_t& entity, Context& context)
{
	CommandList& commandList = context.commandList;

	const DynamicObjectModel& model = *object.model;
	const DynamicObjectConstBuffer& constBuffer = *object.constBuffer;

	// Set vertex buffer views
	const int vertexBufferStart = m_defaultMemoryBuffer.GetOffset(model.vertices);
	const D3D12_GPU_VIRTUAL_ADDRESS defaultMemBuffVirtAddress = m_defaultMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();

	constexpr int vertexSize = sizeof(XMFLOAT4);
	const int frameSize = vertexSize * model.headerData.animFrameVertsNum;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[3];


	// Position0
	vertexBufferViews[0].BufferLocation = defaultMemBuffVirtAddress +
		vertexBufferStart + frameSize * entity.oldframe;
	vertexBufferViews[0].StrideInBytes = vertexSize;
	vertexBufferViews[0].SizeInBytes = frameSize;

	// Position1
	vertexBufferViews[1].BufferLocation = defaultMemBuffVirtAddress +
		vertexBufferStart + frameSize * entity.frame;
	vertexBufferViews[1].StrideInBytes = vertexSize;
	vertexBufferViews[1].SizeInBytes = frameSize;

	// TexCoord
	constexpr int texCoordStrideSize = sizeof(XMFLOAT2);

	vertexBufferViews[2].BufferLocation = defaultMemBuffVirtAddress +
		m_defaultMemoryBuffer.GetOffset(model.textureCoords);
	vertexBufferViews[2].StrideInBytes = texCoordStrideSize;
	vertexBufferViews[2].SizeInBytes = texCoordStrideSize * model.headerData.animFrameVertsNum;

	commandList.commandList->IASetVertexBuffers(0, _countof(vertexBufferViews), vertexBufferViews);


	// Set index buffer
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	indexBufferView.BufferLocation = defaultMemBuffVirtAddress +
		m_defaultMemoryBuffer.GetOffset(model.indices);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = model.headerData.indicesNum * sizeof(uint32_t);
	
	commandList.commandList->IASetIndexBuffer(&indexBufferView);

	// Pick texture
	assert(entity.skin == nullptr && "Custom skin. I am not prepared for this");

	Texture* tex = nullptr;

	if (entity.skinnum >= MAX_MD2SKINS)
	{
		tex = FindTexture(model.textures[0]);
	}
	else
	{
		tex = FindTexture(model.textures[entity.skinnum]);

		if (tex == nullptr)
		{
			tex = FindTexture(model.textures[0]);
		}
	}

	assert(tex != nullptr && "Not texture found for dynamic object rendering. Implement fall back");

	// Binding root signature params

	// 1)
	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle = cbvSrvHeap->GetHandleGPU(tex->texViewIndex);;

	commandList.commandList->SetGraphicsRootDescriptorTable(0, texHandle);

	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(tex->samplerInd, m_samplerDescriptorSize);

	commandList.commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constBuffer.constantBufferHandler);

	commandList.commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	commandList.commandList->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
}

void Renderer::DrawStreaming(const FArg::DrawStreaming& args)
{
	assert(args.vertices != nullptr &&
		args.verticesSizeInBytes != Const::INVALID_SIZE &&
		args.verticesStride != -1 &&
		args.texName != nullptr &&
		args.pos != nullptr &&
		args.bufferPiece != nullptr &&
		args.context != nullptr &&
		"DrawStrwaming args are not initialized");

	Frame& frame = args.context->frame;
	CommandList& commandList = args.context->commandList;

	UpdateStreamingConstantBuffer(*args.pos, { 1.0f, 1.0f, 1.0f, 0.0f }, *args.bufferPiece, *args.context);

	const int vertexBufferOffset = m_uploadMemoryBuffer.GetOffset(args.bufferPiece->handler) +
		args.bufferPiece->offset +
		Utils::Align(sizeof(ShDef::ConstBuff::TransMat), Settings::CONST_BUFFER_ALIGNMENT);

	FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
	updateVertexBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateVertexBufferArgs.offset = vertexBufferOffset;
	updateVertexBufferArgs.data = args.vertices;
	updateVertexBufferArgs.byteSize = args.verticesSizeInBytes;
	updateVertexBufferArgs.alignment = 0;

	UpdateUploadHeapBuff(updateVertexBufferArgs);

	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() + vertexBufferOffset;
	vertBuffView.StrideInBytes = args.verticesStride;
	vertBuffView.SizeInBytes = args.verticesSizeInBytes;

	commandList.commandList->IASetVertexBuffers(0, 1, &vertBuffView);

	// Binding root signature params

	// 1)
	const Texture& texture = *FindTexture(args.texName);

	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle = cbvSrvHeap->GetHandleGPU(texture.texViewIndex);;

	commandList.commandList->SetGraphicsRootDescriptorTable(0, texHandle);

	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texture.samplerInd, m_samplerDescriptorSize);

	commandList.commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();

	cbAddress += m_uploadMemoryBuffer.GetOffset(args.bufferPiece->handler) +
		args.bufferPiece->offset;

	commandList.commandList->SetGraphicsRootConstantBufferView(2, cbAddress);


	commandList.commandList->DrawInstanced(args.verticesSizeInBytes / args.verticesStride, 1, 0, 0);
}

void Renderer::AddParticleToDrawList(const particle_t& particle, BufferHandler vertexBufferHandler, int vertexBufferOffset)
{

	unsigned char color[4];
	*reinterpret_cast<int *>(color) = m_8To24Table[particle.color];

	ShDef::Vert::PosCol particleGpuData = {
		XMFLOAT4(particle.origin[0], particle.origin[1], particle.origin[2], 1.0f),
		XMFLOAT4(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, particle.alpha)
	};

	// Deal with vertex buffer
	FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
	updateVertexBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateVertexBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(vertexBufferHandler) + vertexBufferOffset;
	updateVertexBufferArgs.data = &particleGpuData;
	updateVertexBufferArgs.byteSize = sizeof(particleGpuData);
	updateVertexBufferArgs.alignment = 0;
	UpdateUploadHeapBuff(updateVertexBufferArgs);
}

void Renderer::DrawParticleDrawList(BufferHandler vertexBufferHandler, int vertexBufferSizeInBytes, BufferHandler constBufferHandler, Context& context)
{
	CommandList& commandList = context.commandList;
	constexpr int vertexStrideInBytes = sizeof(ShDef::Vert::PosCol);

	D3D12_VERTEX_BUFFER_VIEW vertBufferView;
	vertBufferView.BufferLocation = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() +
		m_uploadMemoryBuffer.GetOffset(vertexBufferHandler);
	vertBufferView.StrideInBytes = vertexStrideInBytes;
	vertBufferView.SizeInBytes = vertexBufferSizeInBytes;

	commandList.commandList->IASetVertexBuffers(0, 1, &vertBufferView);

	// Binding root signature params

	// 1)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constBufferHandler);

	commandList.commandList->SetGraphicsRootConstantBufferView(0, cbAddress);

	// Draw
	commandList.commandList->DrawInstanced(vertexBufferSizeInBytes / vertexStrideInBytes, 1, 0, 0);
}

void Renderer::GetDrawAreaSize(int* Width, int* Height)
{

	char modeVarName[] = "gl_mode";
	char modeVarVal[] = "3";
	cvar_t* mode = GetRefImport().Cvar_Get(modeVarName, modeVarVal, CVAR_ARCHIVE);

	assert(mode);
	GetRefImport().Vid_GetModeInfo(Width, Height, static_cast<int>(mode->value));
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

	for (int i = 0; i < m_8To24Table.size(); ++i)
	{
		int r = static_cast<int>(palette[i * 3 + 0]);
		int g = static_cast<int>(palette[i * 3 + 1]);
		int b = static_cast<int>(palette[i * 3 + 2]);

		int v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
		m_8To24Table[i] = static_cast<unsigned int>(LittleLong(v));
	}

	m_8To24Table[m_8To24Table.size() - 1] &= LittleLong(0xffffff);	// 255 is transparent

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

		out[i] = m_8To24Table[p];

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

		reinterpret_cast<std::byte*>(&out[i])[0] = reinterpret_cast<const std::byte*>(&m_8To24Table[p])[0];
		reinterpret_cast<std::byte*>(&out[i])[1] = reinterpret_cast<const std::byte*>(&m_8To24Table[p])[1];
		reinterpret_cast<std::byte*>(&out[i])[2] = reinterpret_cast<const std::byte*>(&m_8To24Table[p])[2];
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
	const FXMVECTOR sseEntityPos = XMLoadFloat4(&XMFLOAT4(entity.origin[0], entity.origin[1], entity.origin[2], 1.0f));
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

void Renderer::RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete)
{
	std::scoped_lock<std::mutex> lock(m_resourcesToDelete.mutex);

	m_resourcesToDelete.obj.push_back(resourceToDelete);
}

void Renderer::DeleteRequestedResources()
{
	std::scoped_lock<std::mutex> lock(m_resourcesToDelete.mutex);

	m_resourcesToDelete.obj.clear();
}

void Renderer::DeleteDefaultMemoryBuffer(BufferHandler handler)
{
	m_defaultMemoryBuffer.Delete(handler);
}

void Renderer::DeleteUploadMemoryBuffer(BufferHandler handler)
{
	m_uploadMemoryBuffer.Delete(handler);
}

void Renderer::UpdateStreamingConstantBuffer(XMFLOAT4 position, XMFLOAT4 scale, BufferPiece bufferPiece, Context& context)
{
	assert(bufferPiece.handler != BufConst::INVALID_BUFFER_HANDLER &&
		bufferPiece.offset != Const::INVALID_OFFSET &&
		"Can't update constant buffer, invalid offset.");

	// Update transformation mat
	ShDef::ConstBuff::TransMat transMat;
	XMMATRIX modelMat = XMMatrixScaling(scale.x, scale.y, scale.z);

	modelMat = modelMat * XMMatrixTranslation(
		position.x,
		position.y,
		position.z
	);

	Frame& frame = context.frame;

	XMMATRIX sseViewMat = XMLoadFloat4x4(&frame.uiViewMat);
	XMMATRIX sseProjMat = XMLoadFloat4x4(&frame.uiProjectionMat);
	XMMATRIX sseYInverseAndCenterMat = XMLoadFloat4x4(&m_yInverseAndCenterMatrix);

	XMMATRIX sseMvpMat = modelMat * sseYInverseAndCenterMat * sseViewMat * sseProjMat;


	XMStoreFloat4x4(&transMat.transformationMat, sseMvpMat);

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(bufferPiece.handler) + bufferPiece.offset;
	updateConstBufferArgs.data = &transMat;
	updateConstBufferArgs.byteSize = sizeof(transMat);
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;
	// Update our constant buffer
	UpdateUploadHeapBuff(updateConstBufferArgs);
}

void Renderer::UpdateStaticObjectConstantBuffer(const StaticObject& obj, Context& context)
{
	const Camera& camera = context.frame.camera;
	
	XMFLOAT4X4 mvpMat;
	XMStoreFloat4x4(&mvpMat, camera.GetViewProjMatrix());

	BufferHandler constBufferHandler = obj.frameData[context.frame.GetArrayIndex()].constantBufferHandler;

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(constBufferHandler);
	updateConstBufferArgs.data = &mvpMat;
	updateConstBufferArgs.byteSize = sizeof(mvpMat);
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);
}

void Renderer::UpdateDynamicObjectConstantBuffer(DynamicObject& obj, const entity_t& entity, Context& context)
{
	const Camera& camera = context.frame.camera;

	// Calculate transformation matrix
	XMMATRIX sseMvpMat = DynamicObjectModel::GenerateModelMat(entity) * camera.GetViewProjMatrix();

	XMFLOAT4X4 mvpMat;
	XMStoreFloat4x4(&mvpMat, sseMvpMat);

	// Calculate animation data
	auto[animMove, frontLerp, backLerp] = obj.model->GenerateAnimInterpolationData(entity);

	constexpr int updateDataSize = sizeof(ShDef::ConstBuff::AnimInterpTranstMap);

	std::array<std::byte, updateDataSize> updateData;

	std::byte* updateDataPtr = updateData.data();
	// It is possible to do it nicely via template parameter pack and unfold
	int cpySize = sizeof(XMFLOAT4X4);
	memcpy(updateDataPtr, &mvpMat, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &animMove, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &frontLerp, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &backLerp, cpySize);
	updateDataPtr += cpySize;

	assert(obj.constBuffer->constantBufferHandler != BufConst::INVALID_BUFFER_HANDLER && "Can't update dynamic const buffer, invalid offset");

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(obj.constBuffer->constantBufferHandler);
	updateConstBufferArgs.data = updateData.data();
	updateConstBufferArgs.byteSize = updateDataSize;
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);
}

BufferHandler Renderer::UpdateParticleConstantBuffer(Context& context)
{
	const Camera& camera = context.frame.camera;

	constexpr int updateDataSize = sizeof(ShDef::ConstBuff::CameraDataTransMat);

	const BufferHandler constantBufferHandler = m_uploadMemoryBuffer.Allocate(Utils::Align(updateDataSize, 
		Settings::CONST_BUFFER_ALIGNMENT));
	DO_IN_LOCK(context.frame.streamingObjectsHandlers, push_back(constantBufferHandler));

	assert(constantBufferHandler != BufConst::INVALID_BUFFER_HANDLER && "Can't update particle const buffer");

	XMFLOAT4X4 mvpMat;

	XMStoreFloat4x4(&mvpMat, camera.GetViewProjMatrix());

	auto[yaw, pitch, roll] = camera.GetBasis();

	std::array<std::byte, updateDataSize> updateData;

	std::byte* updateDataPtr = updateData.data();

	int cpySize = sizeof(XMFLOAT4X4);
	memcpy(updateDataPtr, &mvpMat, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &yaw, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &pitch, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &roll, cpySize);
	updateDataPtr += cpySize;

	cpySize = sizeof(XMFLOAT4);
	memcpy(updateDataPtr, &camera.position, cpySize);
	updateDataPtr += cpySize;

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(constantBufferHandler);
	updateConstBufferArgs.data = updateData.data();
	updateConstBufferArgs.byteSize = updateDataSize;
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);

	return constantBufferHandler;
}

Texture* Renderer::FindOrCreateTexture(std::string_view textureName, Context& context)
{
	std::scoped_lock<std::mutex> lock(m_textures.mutex);
	
	Texture* texture = nullptr;
	auto texIt = m_textures.obj.find(textureName.data());

	if (texIt != m_textures.obj.end())
	{
		texture = &texIt->second;
	}
	else
	{
		texture = _CreateTextureFromFile(textureName.data(), context);
	}

	return texture;
}

Texture* Renderer::FindTexture(std::string_view textureName)
{
	std::scoped_lock<std::mutex> lock(m_textures.mutex);

	auto texIt = m_textures.obj.find(textureName.data());

	return texIt == m_textures.obj.end() ? nullptr : &texIt->second;
}

void Renderer::ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
{
	// Copied from GL_ResampleTexture
	// Honestly, I don't know exactly what it does, I mean I understand that
	// it most likely upscales image, but how?
	int		i, j;
	const unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[1024], p2[1024];
	byte		*pix1, *pix2, *pix3, *pix4;

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;
	for (i = 0; i < outwidth; i++)
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracstep;
	}
	frac = 3 * (fracstep >> 2);
	for (i = 0; i < outwidth; i++)
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++, out += outwidth)
	{
		inrow = in + inwidth * (int)((i + 0.25)*inheight / outheight);
		inrow2 = in + inwidth * (int)((i + 0.75)*inheight / outheight);
		frac = fracstep >> 1;
		for (j = 0; j < outwidth; j++)
		{
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out + j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			((byte *)(out + j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			((byte *)(out + j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			((byte *)(out + j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
		}
	}
}

void Renderer::GetDrawTextureFullname(const char* name, char* dest, int destSize) const
{
	if (name[0] != '/' && name[0] != '\\')
	{
		Utils::Sprintf(dest, destSize, "pics/%s.pcx", name);
	}
	else
	{
		assert(destSize >= strlen(name) + 1);
		strcpy(dest, name + 1);
	}
}

void Renderer::UpdateTexture(Texture& tex, const std::byte* data, Context& context)
{
	Logs::Logf(Logs::Category::Textures, "Update Texture with name %s", tex.name.c_str());

	CommandList& commandList = context.commandList;

	// Count alignment and go for what we need
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.buffer.Get(), 0, 1);

	ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);
	Diagnostics::SetResourceNameWithAutoId(textureUploadBuffer.Get(), "TextureUploadBuffer_UpdateTexture");

	DO_IN_LOCK(context.frame.uploadResources, push_back(textureUploadBuffer));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = data;
	// Divide by 8 cause bpp is bits per pixel, not bytes
	textureData.RowPitch = tex.width * tex.bpp / 8;
	// Not SlicePitch but texture size in our case
	textureData.SlicePitch = textureData.RowPitch * tex.height;

	commandList.commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		tex.buffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COPY_DEST
	));

	UpdateSubresources(commandList.commandList.Get(), tex.buffer.Get(), textureUploadBuffer.Get(), 0, 0, 1, &textureData);
	commandList.commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		tex.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

void Renderer::GetDrawTextureSize(int* x, int* y, const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: Get draw texture size %s", name);

	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	const Texture* tex = FindTexture(texFullName.data());

	if (tex != nullptr)
	{
		*x = tex->width;
		*y = tex->height;
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

	unsigned char* rawPalette = reinterpret_cast<unsigned char *>(m_rawPalette.data());

	if (palette)
	{
		for (int i = 0; i < 256; i++)
		{
			rawPalette[i * 4 + 0] = palette[i * 3 + 0];
			rawPalette[i * 4 + 1] = palette[i * 3 + 1];
			rawPalette[i * 4 + 2] = palette[i * 3 + 2];
			rawPalette[i * 4 + 3] = 0xff;
		}
	}
	else
	{
		for (int i = 0; i < 256; i++)
		{
			rawPalette[i * 4 + 0] = m_8To24Table[i] & 0xff;
			rawPalette[i * 4 + 1] = (m_8To24Table[i] >> 8) & 0xff;
			rawPalette[i * 4 + 2] = (m_8To24Table[i] >> 16) & 0xff;
			rawPalette[i * 4 + 3] = 0xff;
		}
	}
}

void Renderer::EndLevelLoading()
{
	Logs::Log(Logs::Category::Generic, "API: End level loading");

	Frame& frame = m_dynamicModelRegContext->frame;

	m_dynamicModelRegContext->commandList.Close();
	
	Context createDeferredTextureContext = CreateContext(frame);
	CreateDeferredTextures(createDeferredTextureContext);
	
	CloseFrame(frame);

	ReleaseFrameResources(frame);

	ReleaseFrame(frame);

	if (std::shared_ptr<Semaphore> staticModelRegSemaphore = m_staticModelRegContext->frame.GetFinishSemaphore())
	{
		staticModelRegSemaphore->Wait();
	}


	m_staticModelRegContext = nullptr;
	m_dynamicModelRegContext = nullptr;

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
	Texture* rawTex = FindTexture(Texture::RAW_TEXTURE_NAME);

	if (rawTex == nullptr 
		|| rawTex->width != textureWidth
		|| rawTex->height != textureHeight)
	{
		constexpr int textureBitsPerPixel = 32;
		rawTex = CreateTextureFromDataDeferred(data,
			textureWidth, textureHeight, textureBitsPerPixel, Texture::RAW_TEXTURE_NAME, GetCurrentFrame());
	}
	else
	{
		drawCall.textureWidth = textureWidth;
		drawCall.textureHeight = textureHeight;

		const int textureSize = textureWidth * textureHeight;
		drawCall.data.resize(textureSize);
		memcpy(drawCall.data.data(), data, textureSize);
	}

	GetCurrentFrame().uiDrawCalls.push_back(std::move(drawCall));
}

void Renderer::Draw_Pic(int x, int y, const char* name, const BufferPiece& bufferPiece, Context& context)
{
	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	const Texture& texture = *FindOrCreateTexture(texFullName.data(), context);

	std::array<ShDef::Vert::PosTexCoord, 6> vertices;
	Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
		XMFLOAT2(texture.width, texture.height),
		XMFLOAT2(0.0f, 0.0f),
		XMFLOAT2(1.0f, 1.0f),
		vertices.data());

	FArg::DrawStreaming drawArgs;

	XMFLOAT4 pos = XMFLOAT4(x, y, 0.0f, 1.0f);

	drawArgs.vertices = reinterpret_cast<std::byte*>(vertices.data());
	drawArgs.verticesSizeInBytes = vertices.size() * sizeof(ShDef::Vert::PosTexCoord);
	drawArgs.verticesStride = sizeof(ShDef::Vert::PosTexCoord);
	drawArgs.texName = texFullName.data();
	drawArgs.pos = &pos;
	drawArgs.bufferPiece = &bufferPiece;
	drawArgs.context = &context;

	DrawStreaming(drawArgs);
}

void Renderer::Draw_Char(int x, int y, int num, const BufferPiece& bufferPiece, Context& context)
{
	num &= 0xFF;

	constexpr int charSize = 8;

	if ((num & 127) == 32)
		return;		// space

	if (y <= -charSize)
		return;		// totally off screen

	constexpr float texCoordScale = 0.0625f;

	const float uCoord = (num & 15) * texCoordScale;
	const float vCoord = (num >> 4) * texCoordScale;
	const float texSize = texCoordScale;

	std::array<ShDef::Vert::PosTexCoord, 6> vertices;
	Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
		XMFLOAT2(charSize, charSize),
		XMFLOAT2(uCoord, vCoord),
		XMFLOAT2(uCoord + texSize, vCoord + texSize),
		vertices.data());

	const int vertexStride = sizeof(ShDef::Vert::PosTexCoord);

	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(Texture::FONT_TEXTURE_NAME, texFullName.data(), texFullName.size());

	// Proper place for this is in Init(), but file loading system is not ready, when
	// init is called for renderer
	 if(FindTexture(texFullName.data()) == nullptr)
	 {
		 CreateTextureFromFile(texFullName.data(), context);
	 }

	 FArg::DrawStreaming drawArgs;

	 XMFLOAT4 pos = XMFLOAT4(x, y, 0.0f, 1.0f);

	 drawArgs.vertices = reinterpret_cast<std::byte*>(vertices.data());
	 drawArgs.verticesSizeInBytes = vertices.size() * vertexStride;
	 drawArgs.verticesStride = vertexStride;
	 drawArgs.texName = texFullName.data();
	 drawArgs.pos = &pos;
	 drawArgs.bufferPiece = &bufferPiece;
	 drawArgs.context = &context;
	
	 DrawStreaming(drawArgs);
}

void Renderer::Draw_RawPic(const DrawCall_StretchRaw& drawCall, const BufferPiece& bufferPiece, Context& context)
{
	// If there is no data, then texture is requested to be created for this frame. So no need to update
	if (drawCall.data.empty() == false)
	{
		const int textureSize = drawCall.textureWidth * drawCall.textureHeight;

		CommandList& commandList = context.commandList;

		std::vector<unsigned int> texture(textureSize, 0);
		for (int i = 0; i < textureSize; ++i)
		{
			texture[i] = m_rawPalette[std::to_integer<int>(drawCall.data[i])];
		}

		Texture* rawTex = FindTexture(Texture::RAW_TEXTURE_NAME);
		assert(rawTex != nullptr && "Draw_RawPic texture doesn't exist");

		UpdateTexture(*rawTex, reinterpret_cast<std::byte*>(texture.data()), context);
	}

	std::array<ShDef::Vert::PosTexCoord, 6> vertices;
	Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
		XMFLOAT2(drawCall.quadWidth, drawCall.quadHeight),
		XMFLOAT2(0.0f, 0.0f),
		XMFLOAT2(1.0f, 1.0f),
		vertices.data());

	const int vertexStride = sizeof(ShDef::Vert::PosTexCoord);

	FArg::DrawStreaming drawArgs;

	XMFLOAT4 pos = XMFLOAT4(drawCall.x, drawCall.y, 0.0f, 1.0f);

	drawArgs.vertices = reinterpret_cast<std::byte*>(vertices.data());
	drawArgs.verticesSizeInBytes = vertices.size() * vertexStride;
	drawArgs.verticesStride = vertexStride;
	drawArgs.texName = Texture::RAW_TEXTURE_NAME;
	drawArgs.pos = &pos;
	drawArgs.bufferPiece = &bufferPiece;
	drawArgs.context = &context;

	DrawStreaming(drawArgs);
}

void Renderer::AddDrawCall_Pic(int x, int y, const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: AddDrawCall_Pic %s", name);

	GetCurrentFrame().uiDrawCalls.emplace_back(DrawCall_Pic{ x, y, std::string(name) });
}

void Renderer::AddDrawCall_Char(int x, int y, int num)
{
	Logs::Log(Logs::Category::Generic, "API: AddDrawCall_Char");

	GetCurrentFrame().uiDrawCalls.emplace_back(DrawCall_Char{ x, y, num });
}

void Renderer::BeginFrame()
{
	Logs::Log(Logs::Category::Generic, "API: Begin frame");

	AcquireCurrentFrame();
	Frame& frame = GetCurrentFrame();

	frame.colorBufferAndView = &GetNextSwapChainBufferAndView();
	frame.scissorRect = m_scissorRect;

	frame.frameNumber = m_frameCounter;

	++m_frameCounter;
}

void Renderer::EndFrame()
{
	Logs::Log(Logs::Category::Generic, "API: EndFrame");

	// All heavy lifting is here

	Frame& frame = GetCurrentFrame();

	if (frame.texCreationRequests.empty() == false)
	{
		Context createDeferredTextureContext = CreateContext(frame);
		CreateDeferredTextures(createDeferredTextureContext);
	}

	// Proceed to next frame
	DetachCurrentFrame();

	// Create contexts
	// NOTE: creation order is the order in which command Lists will be submitted.
	Context beginFrameContext = CreateContext(frame);

	Context drawStaticObjectsContext = CreateContext(frame);
	
	Context drawDynamicObjectsContext = CreateContext(frame);

	Context drawParticlesContext = CreateContext(frame);

	Context drawUIContext = CreateContext(frame);
	
	Context endFrameContext = CreateContext(frame);

	// Set up dependencies
	
	endFrameContext.CreateDependencyFrom({
			&beginFrameContext,
			&drawStaticObjectsContext,
			&drawDynamicObjectsContext,
			&drawParticlesContext,
			&drawUIContext
		});


	JobQueue& jobQueue = m_jobSystem.GetJobQueue();

	// --- Begin frame job ---

	jobQueue.Enqueue(Job(
		[beginFrameContext, this] () mutable
	{
		BeginFrameJob(beginFrameContext);
	}));

	// --- Draw static objects job ---
	
	jobQueue.Enqueue(Job(
		[drawStaticObjectsContext, this]() mutable
	{
		DrawStaticGeometryJob(drawStaticObjectsContext);
	}));

	// --- Draw dynamic objects job ---

	jobQueue.Enqueue(Job(
		[drawDynamicObjectsContext, this]() mutable
	{
		DrawDynamicGeometryJob(drawDynamicObjectsContext);
	}));

	// --- Draw particles job ---

	jobQueue.Enqueue(Job(
		[drawParticlesContext, this]() mutable
	{
		DrawParticleJob(drawParticlesContext);
	}));
	
	// --- Draw UI job ---

	jobQueue.Enqueue(Job(
		[drawUIContext, this] () mutable 
	{
		DrawUIJob(drawUIContext);
	}));

	// --- End frame job ---

	jobQueue.Enqueue(Job(
		[endFrameContext, this]() mutable
	{
		EndFrameJob(endFrameContext);
	}));
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
	Frame& frame = m_dynamicModelRegContext != nullptr ? m_dynamicModelRegContext->frame : GetCurrentFrame();

	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	Texture* newTex = CreateTextureFromFileDeferred(texFullName.data(), frame);

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
	AcquireCurrentFrame();
	Frame& frame = GetCurrentFrame();

	Context context = CreateContext(frame);
	m_staticModelRegContext = std::make_unique<Context>(context);

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
	DetachCurrentFrame();

	m_jobSystem.GetJobQueue().Enqueue(Job(
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

	AcquireCurrentFrame();
	Frame& frame = GetCurrentFrame();

	m_dynamicModelRegContext = std::make_unique<Context>(CreateContext(frame));
	m_dynamicModelRegContext->commandList.Open();

	DetachCurrentFrame();
}

model_s* Renderer::RegisterModel(const char* name)
{
	Logs::Logf(Logs::Category::Generic, "API: Register model %s", name);

	std::string modelName = name;

	Frame& frame = m_dynamicModelRegContext->frame;
	
	model_t* mod = Mod_ForName(modelName.data(), qFalse, *m_dynamicModelRegContext);

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
				mod->skins[i] = FindOrCreateTexture(imageName, *m_dynamicModelRegContext);
			}

			m_dynamicObjectsModels[mod] = CreateDynamicGraphicObjectFromGLModel(mod, *m_dynamicModelRegContext);

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

	Frame& frame = GetCurrentFrame();

	frame.camera.Update(updateData);
	frame.camera.GenerateViewProjMat();
	
	frame.entitiesToDraw.resize(updateData.num_entities);
	memcpy(frame.entitiesToDraw.data(), updateData.entities, sizeof(entity_t) * updateData.num_entities);

	frame.particlesToDraw.resize(updateData.num_particles);
	memcpy(frame.particlesToDraw.data(), updateData.particles, sizeof(particle_t) * updateData.num_particles);
}
