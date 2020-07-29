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
#include "dx_model.h"
#include "dx_diagnostics.h"

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


Renderer::Renderer()
{
	XMMATRIX tempMat = XMMatrixIdentity();

	XMStoreFloat4x4(&m_uiProjectionMat, tempMat);
	XMStoreFloat4x4(&m_uiViewMat, tempMat);
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
	InitDX();

	Load8To24Table();
}

void Renderer::BeginFrame()
{
	// Resetting viewport is mandatory
	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = static_cast<float>(m_camera.width);
	viewport.Height = static_cast<float>(m_camera.height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	m_commandList->RSSetViewports(1, &viewport);
	// Resetting scissor is mandatory 
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate buffer transition to write state
	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	// Clear back buffer and depth buffer
	m_commandList->ClearRenderTargetView(GetCurrentBackBufferView(), DirectX::Colors::Black, 0, nullptr);
	m_commandList->ClearDepthStencilView(
		GetDepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);


	// Specify buffer we are going to render to
	m_commandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvHeap.Get(), m_samplerHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// Set some matrices
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&m_uiViewMat, tempMat);

	tempMat = XMMatrixOrthographicRH(m_camera.width, m_camera.height, 0.0f, 1.0f);
	XMStoreFloat4x4(&m_uiProjectionMat, tempMat);
}

void Renderer::EndFrame()
{
	// Indicate current buffer state transition
	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	ExecuteCommandLists();
	FlushCommandQueue();

	// Safe to do here since Flush command queue is here. This used to be in the BeginFrame()
	// but it seems like some logic like loading new data on GPU happens after EndFrame, but
	// before BeginFrame. I can try to create separate command list for such things, or deffer
	// them and do it in the beginning of the frame.
	ThrowIfFailed(m_commandListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandListAlloc.Get(), nullptr));
	// Material should reset along with command list
	ClearMaterial();

	PresentAndSwapBuffers();
	

	// We flushed command queue, we know for sure that it is safe to release those resources
	m_uploadResources.clear();

	// Streaming drawing stuff
	for (BufferHandler handler : m_streamingObjectsHandlers)
	{
		m_uploadMemoryBuffer.Delete(handler);
	}

	m_streamingObjectsHandlers.clear();

	// Delete resource we wanted to delete
	m_resourcesToDelete.clear();
	// We are done with dynamic objects rendering. It's safe
	// to delete them
	m_frameDynamicObjects.clear();
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

void Renderer::InitDX()
{
	EnableDebugLayer();

	CreateDxgiFactory();
	CreateDevice();

	SetDebugMessageFilter();

	InitDescriptorSizes();

	CreateFences();

	CreateCommandQueue();
	CreateCmdAllocatorAndCmdList();

	CheckMSAAQualitySupport();

	CreateSwapChain();

	CreateDescriptorsHeaps();

	CreateRenderTargetViews();
	CreateDepthStencilBufferAndView();

	CreateCompiledMaterials();

	InitCamera();
	InitScissorRect();

	CreateTextureSampler();

	InitUtils();

	ExecuteCommandLists();
	FlushCommandQueue();

	ThrowIfFailed(m_commandListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandListAlloc.Get(), nullptr));
}

void Renderer::EnableDebugLayer()
{
	if (QDEBUG_LAYER_ENABLED == false)
	{
		return;
	}

	ComPtr<ID3D12Debug> debugController;

	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	debugController->EnableDebugLayer();
}

void Renderer::SetDebugMessageFilter()
{
	if (!QDEBUG_LAYER_ENABLED || !QDEBUG_MESSAGE_FILTER_ENABLED)
	{
		return;
	}

	ComPtr<ID3D12InfoQueue> infoQueue;
	ThrowIfFailed(m_device.As(&infoQueue));

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

	// Create default memory buffer
	m_defaultMemoryBuffer.allocBuffer.gpuBuffer = CreateDefaultHeapBuffer(nullptr ,QDEFAULT_MEMORY_BUFFER_SIZE);
	// Create upload memory buffer
	m_uploadMemoryBuffer.allocBuffer.gpuBuffer = CreateUploadHeapBuffer(QUPLOAD_MEMORY_BUFFER_SIZE);

	// Init raw palette with 0
	std::fill(m_rawPalette.begin(), m_rawPalette.end(), 0);

	// Init dynamic objects constant buffers pool
	m_dynamicObjectsConstBuffersPool.resize(QDYNAM_OBJECT_CONST_BUFFER_POOL_SIZE);
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

void Renderer::InitCamera()
{
	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);
	// Init viewport

	m_camera.width = DrawAreaWidth;
	m_camera.height = DrawAreaHeight;
}

void Renderer::CreateDepthStencilBufferAndView()
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
	depthStencilDesc.Format = QDEPTH_STENCIL_FORMAT;
	depthStencilDesc.SampleDesc.Count = GetMSAASampleCount();
	depthStencilDesc.SampleDesc.Quality = GetMSAAQuality();
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optimizedClearVal;
	optimizedClearVal.Format = QDEPTH_STENCIL_FORMAT;
	optimizedClearVal.DepthStencil.Depth = 1.0f;
	optimizedClearVal.DepthStencil.Stencil = 0;
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optimizedClearVal,
		IID_PPV_ARGS(m_depthStencilBuffer.GetAddressOf())));

	// Create depth stencil view
	m_device->CreateDepthStencilView(
		m_depthStencilBuffer.Get(),
		nullptr,
		GetDepthStencilView());

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_depthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		));
}

void Renderer::CreateRenderTargetViews()
{
	// Create render target view
	CD3DX12_CPU_DESCRIPTOR_HANDLE RtvHeapHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < QSWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		// Get i-th buffer in a swap chain
		ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_swapChainBuffer[i])));

		// Create an RTV to it
		m_device->CreateRenderTargetView(m_swapChainBuffer[i].Get(), nullptr, RtvHeapHandle);

		// Next entry in heap
		RtvHeapHandle.Offset(1, m_rtvDescriptorSize);
	}
}

void Renderer::CreateDescriptorsHeaps()
{
	// Create a render target descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = QSWAP_CHAIN_BUFFER_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_device->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(m_rtvHeap.GetAddressOf())));

	// Create depth target descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_device->CreateDescriptorHeap(
		&dsvHeapDesc,
		IID_PPV_ARGS(m_dsvHeap.GetAddressOf())));

	// Create ShaderResourceView and Constant Resource View heap
	D3D12_DESCRIPTOR_HEAP_DESC srvCbvHeapDesc;
	srvCbvHeapDesc.NumDescriptors = QCBV_SRV_DESCRIPTORS_NUM;
	srvCbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvCbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvCbvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_device->CreateDescriptorHeap(
		&srvCbvHeapDesc,
		IID_PPV_ARGS(m_cbvSrvHeap.GetAddressOf())));

	std::fill(m_cbvSrvRegistry.begin(), m_cbvSrvRegistry.end(), false);

	// Create sampler heap
	D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc;
	samplerHeapDesc.NumDescriptors = 1;
	samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	samplerHeapDesc.NodeMask = 0;
	
	ThrowIfFailed(m_device->CreateDescriptorHeap(
		&samplerHeapDesc,
		IID_PPV_ARGS(m_samplerHeap.GetAddressOf())));
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
	swapChainDesc.BufferDesc.Format = QBACK_BUFFER_FORMAT;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = GetMSAASampleCount();
	swapChainDesc.SampleDesc.Quality = GetMSAAQuality();
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = QSWAP_CHAIN_BUFFER_COUNT;
	swapChainDesc.OutputWindow = m_hWindows;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(m_dxgiFactory->CreateSwapChain(m_commandQueue.Get(),
		&swapChainDesc,
		m_swapChain.GetAddressOf()));
}

void Renderer::CheckMSAAQualitySupport()
{
	if (QMSAA_ENABLED == false)
		return;

	// Check 4X MSAA Quality Support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS MSQualityLevels;
	MSQualityLevels.Format = QBACK_BUFFER_FORMAT;
	MSQualityLevels.SampleCount = QMSAA_SAMPLE_COUNT;
	MSQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	MSQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(m_device->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&MSQualityLevels,
		sizeof(MSQualityLevels)
	));

	m_MSQualityLevels = MSQualityLevels.NumQualityLevels;
	assert(m_MSQualityLevels > 0 && "Unexpected MSAA quality levels");
}

void Renderer::CreateCmdAllocatorAndCmdList()
{
	// Create command allocator
	ThrowIfFailed(m_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&m_commandListAlloc)));

	// Create command list 
	ThrowIfFailed(m_device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(m_commandList.GetAddressOf())));
}

void Renderer::CreateCommandQueue()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
}

void Renderer::CreateFences()
{
	// Create fence
	ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
}

void Renderer::InitDescriptorSizes()
{
	// Get descriptors sizes
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_cbvSrbDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void Renderer::CreateDevice()
{
	// This is super weird. This function internally will throw com exception,
	// but result will be still fine.
	ThrowIfFailed(D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_device)
	));
}

void Renderer::CreateDxgiFactory()
{
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));
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

	ThrowIfFailed(m_device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&resultRootSig)
	));

	return resultRootSig;
}

void Renderer::ExecuteCommandLists()
{
	// Done with this command list
	ThrowIfFailed(m_commandList->Close());

	// Add command list to execution queue
	ID3D12CommandList* cmdLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);	
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

	ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&materialCompiled.pipelineState)));

	materialCompiled.primitiveTopology = materialSourse.primitiveTopology;

	return materialCompiled;
}

void Renderer::SetMaterial(const std::string& materialName)
{
	if (materialName == m_currentMaterialName)
	{
		return;
	}

	m_currentMaterialName = materialName;

	auto materialIt = std::find_if(m_materials.begin(), m_materials.end(), [materialName](const Material& mat)
	{
		return mat.name == materialName;
	});
	
	assert(materialIt != m_materials.end() && "Can't set requested material. It's not found");

	m_commandList->SetGraphicsRootSignature(materialIt->rootSingature.Get());
	m_commandList->SetPipelineState(materialIt->pipelineState.Get());
	m_commandList->IASetPrimitiveTopology(materialIt->primitiveTopology);
}

void Renderer::ClearMaterial()
{
	m_currentMaterialName.clear();
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

	m_device->CreateSampler(&samplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
}

int Renderer::GetMSAASampleCount() const
{
	return QMSAA_ENABLED ? QMSAA_SAMPLE_COUNT : 1;
}

int Renderer::GetMSAAQuality() const
{
	return QMSAA_ENABLED ? (m_MSQualityLevels - 1) : 0;
}

//************************************
// Method:    FlushCommandQueue
// FullName:  DXApp::FlushCommandQueue
// Access:    private 
// Returns:   void
// Details: Wait until we are done with previous frame
//			  before starting next one. This is very inefficient, and 
//			  should be never used. Still it is here for now, while
//			  I am prototyping
//************************************
void Renderer::FlushCommandQueue()
{
	// I actually rely on this stuff in some places, so I just need to be careful
	// when big refactoring time comes. 

	// Advance the fence value to mark a new point
	++m_currentFenceValue;

	// Add the instruction to command queue to set up a new value for fence. GPU will
	// only set this value when it will reach this point
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_currentFenceValue));

	if (m_fence->GetCompletedValue() >= m_currentFenceValue)
	{
		// GPU has reached the required value
		return;
	}

	// Wait unti GPU will reach our fence. Create dummy even which we will wait for
	HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

	ThrowIfFailed(m_fence->SetEventOnCompletion(m_currentFenceValue, eventHandle));

	//Waaait
	WaitForSingleObject(eventHandle, INFINITE);
	CloseHandle(eventHandle);
}

ID3D12Resource* Renderer::GetCurrentBackBuffer()
{
	return m_swapChainBuffer[m_currentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::GetCurrentBackBufferView()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE
	(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_currentBackBuffer,
		m_rtvDescriptorSize
	);
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::GetDepthStencilView()
{
	return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

//************************************
// Method:    PresentAndSwapBuffers
// FullName:  DXApp::SwapBuffers
// Access:    private 
// Returns:   void
// Details: Presents and swap current back buffer, and does some bookkeeping.
// All preparations should be done at this point including buffers transitions.
// This function will not do any oh these, so all on you. It do
//************************************
void Renderer::PresentAndSwapBuffers()
{
	ThrowIfFailed(m_swapChain->Present(0, 0));
	m_currentBackBuffer = (m_currentBackBuffer + 1) % QSWAP_CHAIN_BUFFER_COUNT;
}

Texture* Renderer::CreateTextureFromFile(const char* name)
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

	Texture* createdTex = CreateTextureFromData(reinterpret_cast<std::byte*>(image32), width, height, bpp, name);

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

void Renderer::CreateGpuTexture(const unsigned int* raw, int width, int height, int bpp, Texture& outTex)
{
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format =  DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	// Create destination texture
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&outTex.buffer)));
	
	// Count alignment and go for what we need
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(outTex.buffer.Get(), 0, 1);

	ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);

	m_uploadResources.push_back(textureUploadBuffer);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = raw;
	// Divide by 8 cause bpp is bits per pixel, not bytes
	textureData.RowPitch = width * bpp / 8;
	// Not SlicePitch but texture size in our case
	textureData.SlicePitch = textureData.RowPitch * height;

	UpdateSubresources(m_commandList.Get(), outTex.buffer.Get(), textureUploadBuffer.Get(), 0, 0, 1, &textureData);
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		outTex.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	
	const int descInd = AllocSrvSlot();
	outTex.texView = std::make_shared<TextureView>(descInd);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription = {};
	srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescription.Format = textureDesc.Format;
	srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescription.Texture2D.MostDetailedMip = 0;
	srvDescription.Texture2D.MipLevels = 1;
	srvDescription.Texture2D.ResourceMinLODClamp = 0.0f;
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
	descriptor.Offset(descInd, m_cbvSrbDescriptorSize);

	m_device->CreateShaderResourceView(outTex.buffer.Get(), &srvDescription, descriptor);
}

Texture* Renderer::CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name)
{
	Texture tex;
	CreateGpuTexture(reinterpret_cast<const unsigned int*>(data), width, height, bpp, tex);

	tex.width = width;
	tex.height = height;
	tex.bpp = bpp;

	tex.name = name;

	return &m_textures.insert_or_assign(tex.name, std::move(tex)).first->second;
}

ComPtr<ID3D12Resource> Renderer::CreateDefaultHeapBuffer(const void* data, UINT64 byteSize)
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

	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer)
	));

	if (data != nullptr)
	{
		// Create upload buffer
		ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(byteSize);
		m_uploadResources.push_back(uploadBuffer);

		// Describe upload resource data 
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data;
		subResourceData.RowPitch = byteSize;
		subResourceData.SlicePitch = subResourceData.RowPitch;

		UpdateSubresources(m_commandList.Get(), buffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	}

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
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

	ThrowIfFailed(m_device->CreateCommittedResource(
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
		args.byteSize != -1 &&
		args.data != nullptr &&
		args.offset != -1 && "Uninitialized arguments in update upload buff");

	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;

	BYTE* mappedMemory = nullptr;
	// This parameter indicates the range that CPU might read. If begin and end are equal, we promise
	// that CPU will never try to read from this memory
	D3D12_RANGE mappedRange = { 0, 0 };

	args.buffer->Map(0, &mappedRange, reinterpret_cast<void**>(&mappedMemory));

	memcpy(mappedMemory + args.offset, args.data, dataSize);

	args.buffer->Unmap(0, &mappedRange);
}

void Renderer::UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args)
{
	assert(args.buffer != nullptr &&
		args.alignment != -1 &&
		args.byteSize != -1 &&
		args.data != nullptr &&
		args.offset != -1 && "Uninitialized arguments in update default buff");

	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;


	// Create upload buffer
	ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(args.byteSize);
	m_uploadResources.push_back(uploadBuffer);

	FArg::UpdateUploadHeapBuff uploadHeapBuffArgs;
	uploadHeapBuffArgs.alignment = 0;
	uploadHeapBuffArgs.buffer = uploadBuffer;
	uploadHeapBuffArgs.byteSize = args.byteSize;
	uploadHeapBuffArgs.data = args.data;
	uploadHeapBuffArgs.offset = 0;
	UpdateUploadHeapBuff(uploadHeapBuffArgs);

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		args.buffer.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, 
		D3D12_RESOURCE_STATE_COPY_DEST
	));

	// Last argument is intentionally args.byteSize, cause that's how much data we pass to this function
	// we don't want to read out of range
	m_commandList->CopyBufferRegion(args.buffer.Get(), args.offset, uploadBuffer.Get(), 0, args.byteSize);

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		args.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ
	));

}

DynamicObjectConstBuffer& Renderer::FindDynamicObjConstBuffer()
{
	auto resIt = std::find_if(m_dynamicObjectsConstBuffersPool.begin(), m_dynamicObjectsConstBuffersPool.end(), 
		[](const DynamicObjectConstBuffer& buff) 
	{
		return buff.isInUse == false;
	});

	assert(resIt != m_dynamicObjectsConstBuffersPool.end() && "Can't find free dynamic object const buffer");


	if (resIt->constantBufferHandler == BufConst::INVALID_BUFFER_HANDLER)
	{
		// This buffer doesn't have any memory allocated. Do it now
		// Allocate constant buffer
		static const unsigned int DynamicObjectConstSize =
			Utils::Align(sizeof(ShDef::ConstBuff::AnimInterpTranstMap), QCONST_BUFFER_ALIGNMENT);

		resIt->constantBufferHandler = m_uploadMemoryBuffer.Allocate(DynamicObjectConstSize);
	}

	return *resIt;
}

void Renderer::FreeSrvSlot(int slotIndex)
{
	assert(m_cbvSrvRegistry[slotIndex] == true);

	m_cbvSrvRegistry[slotIndex] = false;
}

// Looks for free slot and marks slot as taken 
int Renderer::AllocSrvSlot()
{
	auto res = std::find(m_cbvSrvRegistry.begin(), m_cbvSrvRegistry.end(), false);

	assert(res != m_cbvSrvRegistry.end() && "Can't allocate shader resource view.");

	*res = true;

	return std::distance(m_cbvSrvRegistry.begin(), res);
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

void Renderer::CreatePictureObject(const char* pictureName)
{
	StaticObject& newObject = m_staticObjects.emplace_back(StaticObject());

	newObject.textureKey = pictureName;

	const Texture& texture = m_textures.find(newObject.textureKey)->second;

	// Create vertex buffer in RAM
	std::array<ShDef::Vert::PosTexCoord,6> vertices;
	Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f), 
		XMFLOAT2(texture.width, texture.height),
		XMFLOAT2(0.0f, 0.0f),
		XMFLOAT2(1.0f, 1.0f),
		vertices.data());


	newObject.vertexBuffer = CreateDefaultHeapBuffer(vertices.data(), sizeof(vertices));

	static const unsigned int PictureObjectConstSize = Utils::Align(sizeof(ShDef::ConstBuff::TransMat), QCONST_BUFFER_ALIGNMENT);

	newObject.constantBufferHandler = m_uploadMemoryBuffer.Allocate(PictureObjectConstSize);
}

DynamicObjectModel Renderer::CreateDynamicGraphicObjectFromGLModel(const model_t* model)
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
	UpdateDefaultHeapBuff(updateArgs);

	// Get indices in
	updateArgs.alignment = 0;
	updateArgs.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateArgs.byteSize = indexBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalizedIndexBuffer.data());
	updateArgs.offset = m_defaultMemoryBuffer.GetOffset(object.indices);

	UpdateDefaultHeapBuff(updateArgs);

	// Get tex coords in
	updateArgs.alignment = 0;
	updateArgs.buffer = m_defaultMemoryBuffer.allocBuffer.gpuBuffer;
	updateArgs.byteSize = texCoordsBufferSize;
	updateArgs.data = reinterpret_cast<const void*>(normalizedTexCoordsBuffer.data());
	updateArgs.offset = m_defaultMemoryBuffer.GetOffset(object.textureCoords);
	UpdateDefaultHeapBuff(updateArgs);

	// Get textures in
	object.textures.reserve(aliasHeader->num_skins);

	for (int i = 0; i < aliasHeader->num_skins; ++i)
	{
		object.textures.push_back(model->skins[i]->name);
	}

	return object;
}

void Renderer::CreateGraphicalObjectFromGLSurface(const msurface_t& surf)
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
	// Set the texture name
	obj.textureKey = surf.texinfo->image->name;

	obj.vertexBuffer = CreateDefaultHeapBuffer(vertices.data(), sizeof(ShDef::Vert::PosTexCoord) * vertices.size());

	static uint64_t allocSize = 0;
	uint64_t size = sizeof(ShDef::Vert::PosTexCoord) * vertices.size();

	assert(obj.vertexBuffer != nullptr && "Failed to create vertex buffer on GL surface transformation");

	// Fill up index buffer
	std::vector<uint32_t> indices;
	indices = Utils::GetIndicesListForTrianglelistFromPolygonPrimitive(vertices.size());

	obj.indexBuffer = CreateDefaultHeapBuffer(indices.data(), sizeof(uint32_t) * indices.size());

	const unsigned int PictureObjectConstSize = Utils::Align(sizeof(ShDef::ConstBuff::TransMat), QCONST_BUFFER_ALIGNMENT);

	obj.constantBufferHandler = m_uploadMemoryBuffer.Allocate(PictureObjectConstSize);

	std::vector<XMFLOAT4> verticesPos;
	verticesPos.reserve(vertices.size());

	for (const ShDef::Vert::PosTexCoord& vertex : vertices)
	{
		verticesPos.push_back(vertex.position);
	}

	obj.GenerateBoundingBox(verticesPos);
}

void Renderer::DecomposeGLModelNode(const model_t& model, const mnode_t& node)
{
	// Looks like if leaf return, leafs don't contain any geom
	if (node.contents != -1)
	{
		return;
	}

	// This is intermediate node, keep going for a leafs
	DecomposeGLModelNode(model, *node.children[0]);
	DecomposeGLModelNode(model, *node.children[1]);

	// Each surface inside node represents stand alone object with its own texture

	const unsigned int lastSurfInd = node.firstsurface + node.numsurfaces;
	const msurface_t* surf = &model.surfaces[node.firstsurface];

	for (unsigned int surfInd = node.firstsurface; 
			surfInd < lastSurfInd;
			++surfInd, ++surf)
	{
		assert(surf != nullptr && "Error during graphical objects generation");

		CreateGraphicalObjectFromGLSurface(*surf);
	}
}

void Renderer::Draw(const StaticObject& object)
{
	// Set vertex buffer
	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = object.vertexBuffer->GetGPUVirtualAddress();
	vertBuffView.StrideInBytes = sizeof( ShDef::Vert::PosTexCoord);
	vertBuffView.SizeInBytes = object.vertexBuffer->GetDesc().Width;

	m_commandList->IASetVertexBuffers(0, 1, &vertBuffView);
	
	// Binding root signature params

	// 1)
	const Texture& texture = m_textures.find(object.textureKey)->second;

	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
	texHandle.Offset(texture.texView->srvIndex, m_cbvSrbDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(0, texHandle);


	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texture.samplerInd, m_samplerDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(object.constantBufferHandler);

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	m_commandList->DrawInstanced(vertBuffView.SizeInBytes / vertBuffView.StrideInBytes, 1, 0, 0);
}

void Renderer::DrawIndiced(const StaticObject& object)
{
	assert(object.indexBuffer != nullptr && "Trying to draw indexed object without index buffer");

	// Set vertex buffer
	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = object.vertexBuffer->GetGPUVirtualAddress();
	vertBuffView.StrideInBytes = sizeof(ShDef::Vert::PosTexCoord);
	vertBuffView.SizeInBytes = object.vertexBuffer->GetDesc().Width;

	m_commandList->IASetVertexBuffers(0, 1, &vertBuffView);

	// Set index buffer
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	indexBufferView.BufferLocation = object.indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = object.indexBuffer->GetDesc().Width;

	m_commandList->IASetIndexBuffer(&indexBufferView);


	// Binding root signature params

	// 1)
	const Texture& texture = m_textures.find(object.textureKey)->second;

	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
	texHandle.Offset(texture.texView->srvIndex, m_cbvSrbDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(0, texHandle);

	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texture.samplerInd, m_samplerDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(object.constantBufferHandler);

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	m_commandList->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
}

void Renderer::DrawIndiced(const DynamicObject& object, const entity_t& entity)
{
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

	m_commandList->IASetVertexBuffers(0, _countof(vertexBufferViews), vertexBufferViews);


	// Set index buffer
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	indexBufferView.BufferLocation = defaultMemBuffVirtAddress +
		m_defaultMemoryBuffer.GetOffset(model.indices);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = model.headerData.indicesNum * sizeof(uint32_t);

	m_commandList->IASetIndexBuffer(&indexBufferView);

	// Pick texture
	assert(entity.skin == nullptr && "Custom skin. I am not prepared for this");

	auto texIt = m_textures.end();

	if (entity.skinnum >= MAX_MD2SKINS)
	{
		texIt = m_textures.find(model.textures[0]);
	}
	else
	{
		texIt = m_textures.find(model.textures[entity.skinnum]);

		if (texIt == m_textures.end())
		{
			texIt = m_textures.find(model.textures[0]);
		}
	}

	assert(texIt != m_textures.end() && "Not texture found for dynamic object rendering. Implement fall back");

	// Binding root signature params

	// 1)
	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
	texHandle.Offset(texIt->second.texView->srvIndex, m_cbvSrbDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(0, texHandle);

	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texIt->second.samplerInd, m_samplerDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constBuffer.constantBufferHandler);

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	m_commandList->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
}

void Renderer::DrawStreaming(const std::byte* vertices, int verticesSizeInBytes, int verticesStride, const char* texName, const XMFLOAT4& pos)
{
	// Allocate and update constant buffer
	BufferHandler constantBufferHandler = m_uploadMemoryBuffer.Allocate(Utils::Align(sizeof(ShDef::ConstBuff::TransMat), QCONST_BUFFER_ALIGNMENT));
	m_streamingObjectsHandlers.push_back(constantBufferHandler);

	UpdateStreamingConstantBuffer(pos, { 1.0f, 1.0f, 1.0f, 0.0f }, constantBufferHandler);

	// Deal with vertex buffer
	BufferHandler vertexBufferHandler = m_uploadMemoryBuffer.Allocate(Utils::Align(verticesSizeInBytes, 24));
	m_streamingObjectsHandlers.push_back(vertexBufferHandler);

	FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
	updateVertexBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateVertexBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(vertexBufferHandler);
	updateVertexBufferArgs.data = vertices;
	updateVertexBufferArgs.byteSize = verticesSizeInBytes;
	updateVertexBufferArgs.alignment = 0;
	UpdateUploadHeapBuff(updateVertexBufferArgs);

	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() + m_uploadMemoryBuffer.GetOffset(vertexBufferHandler);
	vertBuffView.StrideInBytes = verticesStride;
	vertBuffView.SizeInBytes = verticesSizeInBytes;
	
	m_commandList->IASetVertexBuffers(0, 1, &vertBuffView);

	// Binding root signature params

	// 1)
	assert(m_textures.find(texName) != m_textures.end() && "Can't draw, texture is not found.");

	const Texture& texture = m_textures.find(texName)->second;

	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
	texHandle.Offset(texture.texView->srvIndex, m_cbvSrbDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(0, texHandle);


	// 2)
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
	samplerHandle.Offset(texture.samplerInd, m_samplerDescriptorSize);

	m_commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);

	// 3)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constantBufferHandler);

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);


	m_commandList->DrawInstanced(verticesSizeInBytes / verticesStride, 1, 0, 0);
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

void Renderer::DrawParticleDrawList(BufferHandler vertexBufferHandler, int vertexBufferSizeInBytes, BufferHandler constBufferHandler)
{
	constexpr int vertexStrideInBytes = sizeof(ShDef::Vert::PosCol);

	D3D12_VERTEX_BUFFER_VIEW vertBufferView;
	vertBufferView.BufferLocation = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() + 
		m_uploadMemoryBuffer.GetOffset(vertexBufferHandler);
	vertBufferView.StrideInBytes = vertexStrideInBytes;
	vertBufferView.SizeInBytes = vertexBufferSizeInBytes;

	m_commandList->IASetVertexBuffers(0, 1, &vertBufferView);

	// Binding root signature params
	
	// 1)
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += m_uploadMemoryBuffer.GetOffset(constBufferHandler);

	m_commandList->SetGraphicsRootConstantBufferView(0, cbAddress);

	// Draw
	m_commandList->DrawInstanced(vertexBufferSizeInBytes / vertexStrideInBytes, 1, 0, 0);
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

		if (p != QTRANSPARENT_TABLE_VAL)
		{
			continue;
		}

		// Transparent pixel stays transparent, but with proper color to blend
		// no bleeding!		
		if (i > width && static_cast<int>(data[i - width]) != QTRANSPARENT_TABLE_VAL)
		{
			// if this is not first row and pixel above has value, pick it
			p = static_cast<int>(data[i - width]);
		}
		else if (i < size - width && static_cast<int>(data[i + width]) != QTRANSPARENT_TABLE_VAL)
		{
			// if this is not last row and pixel below has value, pick it
			p = static_cast<int>(data[i + width]);
		}
		else if (i > 0 && static_cast<int>(data[i - 1]) != QTRANSPARENT_TABLE_VAL)
		{
			// if pixel on left has value pick it
			p = static_cast<int>(data[i - 1]);
		}
		else if (i < size - 1 && static_cast<int>(data[i + 1]) != QTRANSPARENT_TABLE_VAL)
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

bool Renderer::IsVisible(const StaticObject& obj) const
{
	const static XMFLOAT4 divVector = XMFLOAT4(2.0f, 2.0f, 2.0f, 1.0f);
	const static FXMVECTOR sseDivVect = XMLoadFloat4(&divVector);

	FXMVECTOR sseBoundindBoxMin = XMLoadFloat4(&obj.bbMin);
	FXMVECTOR sseBoundindBoxMax = XMLoadFloat4(&obj.bbMax);
	FXMVECTOR sseBoundingBoxCenter = 
		XMVectorDivide(XMVectorAdd(sseBoundindBoxMax, sseBoundindBoxMin), sseDivVect);

	FXMVECTOR sseCameraPos = XMLoadFloat4(&m_camera.position);

	XMFLOAT4 lenVector;
	XMStoreFloat4(&lenVector, XMVector4Length(XMVectorSubtract(sseCameraPos, sseBoundingBoxCenter)));

	const float distance = lenVector.x;

	constexpr float visibilityDist = 400.0f;

	return distance < visibilityDist;
}

bool Renderer::IsVisible(const entity_t& entity) const
{
	FXMVECTOR sseEntityPos = XMLoadFloat4(&XMFLOAT4(entity.origin[0], entity.origin[1], entity.origin[2], 1.0f));
	FXMVECTOR sseCameraPos = XMLoadFloat4(&m_camera.position);
	
	XMFLOAT4 lenVector;
	XMStoreFloat4(&lenVector, XMVector4Length(XMVectorSubtract(sseCameraPos, sseEntityPos)));

	constexpr float visibilityDist = 400.0f;
	
	return lenVector.x < visibilityDist;
}

void Renderer::DeleteResources(ComPtr<ID3D12Resource> resourceToDelete)
{
	m_resourcesToDelete.push_back(resourceToDelete);
}

void Renderer::DeleteDefaultMemoryBuffer(BufferHandler handler)
{
	m_defaultMemoryBuffer.Delete(handler);
}

void Renderer::DeleteUploadMemoryBuffer(BufferHandler handler)
{
	m_uploadMemoryBuffer.Delete(handler);
}

void Renderer::UpdateStreamingConstantBuffer(XMFLOAT4 position, XMFLOAT4 scale, BufferHandler handler)
{
	assert(handler != BufConst::INVALID_BUFFER_HANDLER &&
		"Can't update constant buffer, invalid offset.");

	// Update transformation mat
	ShDef::ConstBuff::TransMat transMat;
	XMMATRIX modelMat = XMMatrixScaling(scale.x, scale.y, scale.z);
	
	modelMat = modelMat * XMMatrixTranslation(
		position.x,
		position.y,
		position.z
	);


	XMMATRIX sseViewMat = XMLoadFloat4x4(&m_uiViewMat);
	XMMATRIX sseProjMat = XMLoadFloat4x4(&m_uiProjectionMat);
	XMMATRIX sseYInverseAndCenterMat = XMLoadFloat4x4(&m_yInverseAndCenterMatrix);

	XMMATRIX sseMvpMat = modelMat * sseYInverseAndCenterMat * sseViewMat * sseProjMat;


	XMStoreFloat4x4(&transMat.transformationMat, sseMvpMat);

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(handler);
	updateConstBufferArgs.data = &transMat;
	updateConstBufferArgs.byteSize = sizeof(transMat);
	updateConstBufferArgs.alignment = QCONST_BUFFER_ALIGNMENT;
	// Update our constant buffer
	UpdateUploadHeapBuff(updateConstBufferArgs);
}

void Renderer::UpdateStaticObjectConstantBuffer(const StaticObject& obj)
{
	XMMATRIX sseMvpMat = obj.GenerateModelMat() *
		m_camera.GenerateViewMatrix() *
		m_camera.GenerateProjectionMatrix();

	XMFLOAT4X4 mvpMat;
	XMStoreFloat4x4(&mvpMat, sseMvpMat);


	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(obj.constantBufferHandler);
	updateConstBufferArgs.data = &mvpMat;
	updateConstBufferArgs.byteSize = sizeof(mvpMat);
	updateConstBufferArgs.alignment = QCONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);
}

void Renderer::UpdateDynamicObjectConstantBuffer(DynamicObject& obj, const entity_t& entity)
{
	// Calculate transformation matrix
	XMMATRIX sseMvpMat = DynamicObjectModel::GenerateModelMat(entity) *
		m_camera.GenerateViewMatrix() *
		m_camera.GenerateProjectionMatrix();

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
	updateConstBufferArgs.alignment = QCONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);
}

BufferHandler Renderer::UpdateParticleConstantBuffer()
{
	constexpr int updateDataSize = sizeof(ShDef::ConstBuff::CameraDataTransMat);

	const BufferHandler constantBufferHandler = m_uploadMemoryBuffer.Allocate(Utils::Align(updateDataSize, QCONST_BUFFER_ALIGNMENT));
	m_streamingObjectsHandlers.push_back(constantBufferHandler);

	assert(constantBufferHandler != BufConst::INVALID_BUFFER_HANDLER && "Can't update particle const buffer");

	XMFLOAT4X4 mvpMat;

	XMStoreFloat4x4(&mvpMat, m_camera.GenerateViewMatrix() * m_camera.GenerateProjectionMatrix());

	auto[yaw, pitch, roll] = m_camera.GetBasis();

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
	memcpy(updateDataPtr, &m_camera.position, cpySize);
	updateDataPtr += cpySize;

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = m_uploadMemoryBuffer.GetOffset(constantBufferHandler);
	updateConstBufferArgs.data = updateData.data();
	updateConstBufferArgs.byteSize = updateDataSize;
	updateConstBufferArgs.alignment = QCONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);

	return constantBufferHandler;
}

Texture* Renderer::FindOrCreateTexture(std::string_view textureName)
{
	Texture* texture = nullptr;
	auto texIt = m_textures.find(textureName.data());

	if (texIt != m_textures.end())
	{
		texture = &texIt->second;
	}
	else
	{
		texture = CreateTextureFromFile(textureName.data());
	}

	return texture;
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

void Renderer::UpdateTexture(Texture& tex, const std::byte* data)
{
	// Count alignment and go for what we need
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.buffer.Get(), 0, 1);

	ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);

	m_uploadResources.push_back(textureUploadBuffer);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = data;
	// Divide by 8 cause bpp is bits per pixel, not bytes
	textureData.RowPitch = tex.width * tex.bpp / 8;
	// Not SlicePitch but texture size in our case
	textureData.SlicePitch = textureData.RowPitch * tex.height;

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		tex.buffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COPY_DEST
	));

	UpdateSubresources(m_commandList.Get(), tex.buffer.Get(), textureUploadBuffer.Get(), 0, 0, 1, &textureData);
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		tex.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

void Renderer::Draw_Pic(int x, int y, const char* name)
{
	SetMaterial(MaterialSource::STATIC_MATERIAL_NAME);

	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	const Texture& texture = *FindOrCreateTexture(texFullName.data());

	std::array<ShDef::Vert::PosTexCoord, 6> vertices;
	Utils::MakeQuad(XMFLOAT2( 0.0f, 0.0f ),
		XMFLOAT2( texture.width, texture.height ),
		XMFLOAT2( 0.0f, 0.0f ),
		XMFLOAT2( 1.0f, 1.0f ),
		vertices.data());

	DrawStreaming(reinterpret_cast<std::byte*>(vertices.data()),
		vertices.size() * sizeof(ShDef::Vert::PosTexCoord),
		sizeof(ShDef::Vert::PosTexCoord),
		texFullName.data(),
		XMFLOAT4(x, y, 0.0f, 1.0f));
}

void Renderer::Draw_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data)
{
	const int texSize = textureWidth * textureHeight;
	
	SetMaterial(MaterialSource::STATIC_MATERIAL_NAME);

	std::vector<unsigned int> texture(texSize, 0);

	for (int i = 0; i < texSize; ++i)
	{
		texture[i] = m_rawPalette[std::to_integer<int>(data[i])];
	}
	
	auto rawTexIt = m_textures.find(QRAW_TEXTURE_NAME);
	if (rawTexIt == m_textures.end()
		|| rawTexIt->second.width != textureWidth
		|| rawTexIt->second.height != textureHeight)
	{
		constexpr int textureBitsPerPixel = 32;
		CreateTextureFromData(reinterpret_cast<std::byte*>(texture.data()), textureWidth, textureHeight, textureBitsPerPixel, QRAW_TEXTURE_NAME);

		rawTexIt = m_textures.find(QRAW_TEXTURE_NAME);
	}
	else
	{
		UpdateTexture(rawTexIt->second, reinterpret_cast<std::byte*>(texture.data()));
	}
	

	std::array<ShDef::Vert::PosTexCoord, 6> vertices;
	Utils::MakeQuad(XMFLOAT2( 0.0f, 0.0f ),
		XMFLOAT2( quadWidth, quadHeight ),
		XMFLOAT2( 0.0f, 0.0f ),
		XMFLOAT2( 1.0f, 1.0f ),
		vertices.data());

	const int vertexStride = sizeof(ShDef::Vert::PosTexCoord);

	DrawStreaming(reinterpret_cast<std::byte*>(vertices.data()), 
		vertices.size()* vertexStride,
		vertexStride,
		QRAW_TEXTURE_NAME,
		XMFLOAT4(x, y, 0.0f, 1.0f));
}

void Renderer::Draw_Char(int x, int y, int num)
{
	SetMaterial(MaterialSource::STATIC_MATERIAL_NAME);

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
	Utils::MakeQuad(XMFLOAT2( 0.0f, 0.0f ),
		XMFLOAT2(charSize, charSize),
		XMFLOAT2(uCoord, vCoord),
		XMFLOAT2(uCoord + texSize, vCoord + texSize),
		vertices.data());

	const int vertexStride = sizeof(ShDef::Vert::PosTexCoord);

	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(QFONT_TEXTURE_NAME, texFullName.data(), texFullName.size());

	// Proper place for this is in Init(), but file loading system is not ready, when
	// init is called for renderer
	if (m_textures.find(texFullName.data()) == m_textures.end())
	{
		CreateTextureFromFile(texFullName.data());
	}

	DrawStreaming(reinterpret_cast<std::byte*>(vertices.data()),
		vertices.size() * vertexStride,
		vertexStride,
		texFullName.data(),
		XMFLOAT4(x, y, 0.0f, 1.0f));
}

void Renderer::GetDrawTextureSize(int* x, int* y, const char* name) const
{
	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	const auto picIt = m_textures.find(texFullName.data());

	if (picIt != m_textures.cend())
	{
		*x = picIt->second.width;
		*y = picIt->second.height;
	}
	else
	{
		*x = -1;
		*y = -1;
	}
}

void Renderer::SetPalette(const unsigned char* palette)
{
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

// This seems to take 188Kb of memory. Which is not that bad, so I will leave it
// as it is for now. I believe there is other places to fix memory issues
#define	MAX_MOD_KNOWN	512
extern model_t	mod_known[MAX_MOD_KNOWN];
extern model_t* r_worldmodel;

void Renderer::RegisterWorldModel(const char* model)
{
	//#TODO I need to manage map model lifetime. So for example in this function
	// I would need to delete old map model and related to it objects before loading
	// a new one. (make sure to handle properly when some object is needed in the new
	// model, so you don't load it twice). Currently it doesn't make sense to do anything
	// with this, because I will have other models in game and I want to handle world model 
	// as part of this system. Maybe I should leave it as it is and just use old system?

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
	model_t* mapModel = Mod_ForName(fullName, qTrue);

	// Legacy from quake 2 model handling system
	r_worldmodel = mapModel;

	DecomposeGLModelNode(*mapModel, *mapModel->nodes);
}

void Renderer::RenderFrame(const refdef_t& frameUpdateData)
{
	m_camera.Update(frameUpdateData);

	// Static geometry 
	Diagnostics::BeginEvent(m_commandList.Get(), "Static materials");

	SetMaterial(MaterialSource::STATIC_MATERIAL_NAME);

	for (const StaticObject& obj : m_staticObjects)
	{
		if (IsVisible(obj) == false)
		{
			continue;
		}

		UpdateStaticObjectConstantBuffer(obj);

		if (obj.indexBuffer != nullptr)
		{
			DrawIndiced(obj);
		}
		else
		{
			Draw(obj);
		}
	}

	Diagnostics::EndEvent(m_commandList.Get());

	// Dynamic geometry
	Diagnostics::BeginEvent(m_commandList.Get(), "Dynamic materials");
	
	SetMaterial(MaterialSource::DYNAMIC_MATERIAL_NAME);

	for (int i = 0; i < frameUpdateData.num_entities; ++i)
	{
		const entity_t& entity = (frameUpdateData.entities[i]);

		if (entity.model == nullptr ||
			entity.flags  & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) ||
			IsVisible(entity) == false)
		{
			continue;
		}

		assert(m_dynamicObjectsModels.find(entity.model) != m_dynamicObjectsModels.end()
			&& "Cannot render dynamic graphical object. Such model is not found");


		DynamicObjectModel& model = m_dynamicObjectsModels[entity.model];
		DynamicObjectConstBuffer& constBuffer = FindDynamicObjConstBuffer();

		// Const buffer should be a separate component, because if we don't do this different enities
		// will use the same model, but with different transformation
		DynamicObject& object = m_frameDynamicObjects.emplace_back(DynamicObject(&model, &constBuffer ));

		UpdateDynamicObjectConstantBuffer(object, entity);
		DrawIndiced(object, entity);
	}

	Diagnostics::EndEvent(m_commandList.Get());

	// Particles
	Diagnostics::BeginEvent(m_commandList.Get(), "Particles");

	if (frameUpdateData.num_particles > 0)
	{
		SetMaterial(MaterialSource::PARTICLE_MATERIAL_NAME);

		// Particles share the same constant buffer, so we only need to update it once
		const BufferHandler particleConstantBufferHandler = UpdateParticleConstantBuffer();

		// Preallocate vertex buffer for particles
		constexpr int singleParticleSize = sizeof(ShDef::Vert::PosCol);
		const int vertexBufferSize = singleParticleSize * frameUpdateData.num_particles;
		const BufferHandler particleVertexBufferHandler = m_uploadMemoryBuffer.Allocate(vertexBufferSize);
		m_streamingObjectsHandlers.push_back(particleVertexBufferHandler);

		assert(particleVertexBufferHandler != BufConst::INVALID_BUFFER_HANDLER && "Failed to allocate particle vertex buffer");

		// Gather all particles, and do one draw call for everything at once
		const particle_t* particle = frameUpdateData.particles;
		for (int i = 0, currentVertexBufferOffset = 0; i < frameUpdateData.num_particles; ++i)
		{
			AddParticleToDrawList(*(particle + i), particleVertexBufferHandler, currentVertexBufferOffset);

			currentVertexBufferOffset += singleParticleSize;
		}

		DrawParticleDrawList(particleVertexBufferHandler, vertexBufferSize, particleConstantBufferHandler);
	}

	Diagnostics::EndEvent(m_commandList.Get());
}

Texture* Renderer::RegisterDrawPic(const char* name)
{
	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	Texture* newTex = FindOrCreateTexture(texFullName.data());
	
	return newTex;
}

model_s* Renderer::RegisterModel(const char* name)
{
	std::string modelName = name;

	model_t* mod = Mod_ForName(modelName.data(), qFalse);

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
				mod->skins[i] = FindOrCreateTexture(imageName);
			}

			m_dynamicObjectsModels[mod] = CreateDynamicGraphicObjectFromGLModel(mod);

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

void Renderer::EndLevelLoading()
{
	Mod_FreeAll();
}
