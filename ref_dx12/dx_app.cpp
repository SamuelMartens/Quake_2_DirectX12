#include "dx_app.h"

#include <cassert>
#include <string>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <d3dcompiler.h>
#include <DirectXColors.h>


#include "../win32/winquake.h"
#include "dx_utils.h"
#include "dx_shaderdefinitions.h"
#include "dx_glmodel.h"
#include "dx_camera.h"


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
	m_commandList->RSSetViewports(1, &m_viewport);
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

	m_commandList->SetGraphicsRootSignature(m_rootSingature.Get());

	// Set some matrices
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&m_uiViewMat, tempMat);

	tempMat = XMMatrixOrthographicLH(m_viewport.Width, m_viewport.Height, 0.0f, 1.0f);
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
	ThrowIfFailed(m_commandList->Reset(m_commandListAlloc.Get(), m_pipelineState.Get()));

	PresentAndSwapBuffers();
	

	// We flushed command queue, we know for sure that it is safe to release those resources
	m_uploadResources.clear();

	// Streaming drawing stuff
	m_streamingVertexBuffer.allocator.ClearAll();

	for(int offset : m_streamingConstOffsets)
	{
		m_constantBuffer.allocator.Delete(offset);
	}
	m_streamingConstOffsets.clear();

	// Deal with resource we wanted to delete
	m_resourcesToDelete.clear();
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

	assert(RegisterClass(&windowClass) && "Failed to register win class.");

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

	InitDescriptorSizes();

	CreateFences();

	CreateCommandQueue();
	CreateCmdAllocatorAndCmdList();

	CheckMSAAQualitySupport();

	CreateSwapChain();

	CreateDescriptorsHeaps();

	CreateRenderTargetViews();
	CreateDepthStencilBufferAndView();

	CreateRootSignature();
	CreateInputLayout();
	LoadShaders();

	CreatePipelineState();

	InitViewport();
	InitScissorRect();

	CreateTextureSampler();

	InitUtils();

	ExecuteCommandLists();
	FlushCommandQueue();

	ThrowIfFailed(m_commandListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandListAlloc.Get(), m_pipelineState.Get()));
}

void Renderer::EnableDebugLayer()
{
	if (QDEBUG_LAYER_ENABLED == false)
	{
		return;
	}

	Microsoft::WRL::ComPtr<ID3D12Debug> DebugController;

	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController)));
	DebugController->EnableDebugLayer();
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

	// Create constant buffer
	m_constantBuffer.allocator.Init(QCONST_BUFFER_SIZE);
	assert(Utils::Align(QCONST_BUFFER_SIZE, QCONST_BUFFER_ALIGNMENT) == QCONST_BUFFER_SIZE);
	m_constantBuffer.gpuBuffer = CreateUploadHeapBuffer(QCONST_BUFFER_SIZE);


	// Create streaming vertex buffer
	m_streamingVertexBuffer.allocator.Init(QSTREAMING_VERTEX_BUFFER_SIZE);
	m_streamingVertexBuffer.gpuBuffer = CreateUploadHeapBuffer(QSTREAMING_VERTEX_BUFFER_SIZE);

	// Init raw palette with 0
	std::fill(m_rawPalette.begin(), m_rawPalette.end(), 0);
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

void Renderer::InitViewport()
{
	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);
	// Init viewport
	m_viewport;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;
	m_viewport.Width = static_cast<float>(DrawAreaWidth);
	m_viewport.Height = static_cast<float>(DrawAreaHeight);
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
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

void Renderer::CreateRootSignature()
{
	// Root signature is an array of root parameters
	CD3DX12_ROOT_PARAMETER slotRootParameters[3];

	// First parameter texture descriptor table
	CD3DX12_DESCRIPTOR_RANGE textureCbvTable[1];
	textureCbvTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	slotRootParameters[0].InitAsDescriptorTable(_countof(textureCbvTable), textureCbvTable);

	// Second parameter is samplers descriptor table
	CD3DX12_DESCRIPTOR_RANGE samplersTable[1];
	samplersTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

	slotRootParameters[1].InitAsDescriptorTable(1, samplersTable);

	// Third parameter in constant buffer view
	slotRootParameters[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameters),
		slotRootParameters,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_rootSingature = SerializeAndCreateRootSigFromRootDesc(rootSigDesc);
}

void Renderer::CreatePipelineState()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { m_inputLayout.data(), static_cast<UINT>(m_inputLayout.size())};
	psoDesc.pRootSignature = m_rootSingature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(m_vsShader->GetBufferPointer()),
		m_vsShader->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(m_psShader->GetBufferPointer()),
		m_psShader->GetBufferSize()
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = QBACK_BUFFER_FORMAT;
	psoDesc.SampleDesc.Count = GetMSAASampleCount();
	psoDesc.SampleDesc.Quality = GetMSAAQuality();
	psoDesc.DSVFormat = QDEPTH_STENCIL_FORMAT;

	ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}

void Renderer::CreateInputLayout()
{
	m_inputLayout = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}


void Renderer::LoadShaders()
{
	const std::string psShader = "ps_PosTex.cso";
	const std::string vsShader = "vs_PosTex.cso";

	m_psShader = LoadCompiledShader(psShader);
	m_vsShader = LoadCompiledShader(vsShader);
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

	// Create upload buffer
	ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(byteSize);
	m_uploadResources.push_back(uploadBuffer);

	// Describe upload resource data 
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = data;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	UpdateSubresources(m_commandList.Get(), buffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

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
	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;

	BYTE* mappedMemory = nullptr;
	// This parameter indicates the range that CPU might read. If begin and end are equal, we promise
	// that CPU will never try to read from this memory
	D3D12_RANGE mappedRange = { 0, 0 };

	args.buffer->Map(0, &mappedRange, reinterpret_cast<void**>(&mappedMemory));

	memcpy(mappedMemory + args.offset, args.data, dataSize);

	args.buffer->Unmap(0, &mappedRange);
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

	assert(res != m_cbvSrvRegistry.end());

	*res = true;

	return std::distance(m_cbvSrvRegistry.begin(), res);
}

void Renderer::DeleteConstantBuffMemory(int offset)
{
	m_constantBuffer.allocator.Delete(offset);
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
	GraphicalObject& newObject = m_graphicalObjects.emplace_back(GraphicalObject());

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

	newObject.constantBufferOffset = m_constantBuffer.allocator.Allocate(PictureObjectConstSize);
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

	GraphicalObject& obj = m_graphicalObjects.emplace_back(GraphicalObject());
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

	// My allocator is insanely naive, and slow, I really should implement some proper allocator
	obj.constantBufferOffset = m_constantBuffer.allocator.Allocate(PictureObjectConstSize);

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

void Renderer::Draw(const GraphicalObject& object)
{
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
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
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += object.constantBufferOffset;

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	m_commandList->DrawInstanced(vertBuffView.SizeInBytes / vertBuffView.StrideInBytes, 1, 0, 0);
}

void Renderer::DrawIndiced(const GraphicalObject& object)
{
	assert(object.indexBuffer != nullptr && "Trying to draw indexed object without index buffer");

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += object.constantBufferOffset;

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);

	// Finally, draw
	m_commandList->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
}

void Renderer::DrawStreaming(const std::byte* vertices, int verticesSizeInBytes, int verticesStride, const char* texName, const XMFLOAT4& pos)
{
	// Allocate and update constant buffer
	int constantBufferOffset = m_constantBuffer.allocator.Allocate(Utils::Align(sizeof(ShDef::ConstBuff::TransMat), QCONST_BUFFER_ALIGNMENT));
	m_streamingConstOffsets.push_back(constantBufferOffset);

	UpdateStreamingConstantBuffer(pos, { 1.0f, 1.0f, 1.0f, 0.0f }, constantBufferOffset);

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Deal with vertex buffer
	int vertexBufferOffset = m_streamingVertexBuffer.allocator.Allocate(Utils::Align(verticesSizeInBytes, 24));

	FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
	updateVertexBufferArgs.buffer = m_streamingVertexBuffer.gpuBuffer;
	updateVertexBufferArgs.offset = vertexBufferOffset;
	updateVertexBufferArgs.data = vertices;
	updateVertexBufferArgs.byteSize = verticesSizeInBytes;
	updateVertexBufferArgs.alignment = 0;
	UpdateUploadHeapBuff(updateVertexBufferArgs);

	D3D12_VERTEX_BUFFER_VIEW vertBuffView;
	vertBuffView.BufferLocation = m_streamingVertexBuffer.gpuBuffer->GetGPUVirtualAddress() + vertexBufferOffset;
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
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer.gpuBuffer->GetGPUVirtualAddress();
	cbAddress += constantBufferOffset;

	m_commandList->SetGraphicsRootConstantBufferView(2, cbAddress);


	m_commandList->DrawInstanced(verticesSizeInBytes / verticesStride, 1, 0, 0);
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

bool Renderer::IsVisible(const GraphicalObject& obj) const
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

	constexpr float visibilityDist = 600.0f;

	return distance < visibilityDist;
}

void Renderer::DeleteResources(ComPtr<ID3D12Resource> resourceToDelete)
{
	m_resourcesToDelete.push_back(resourceToDelete);
}

void Renderer::UpdateStreamingConstantBuffer(XMFLOAT4 position, XMFLOAT4 scale, int offset)
{
	assert(offset != GraphicalObject::INVALID_OFFSET &&
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
	updateConstBufferArgs.buffer = m_constantBuffer.gpuBuffer;
	updateConstBufferArgs.offset = offset;
	updateConstBufferArgs.data = &transMat;
	updateConstBufferArgs.byteSize = sizeof(transMat);
	updateConstBufferArgs.alignment = QCONST_BUFFER_ALIGNMENT;
	// Update our constant buffer
	UpdateUploadHeapBuff(updateConstBufferArgs);
}

void Renderer::UpdateGraphicalObjectConstantBuffer(const GraphicalObject& obj)
{
	XMMATRIX sseMvpMat = obj.GenerateModelMat() *
		m_camera.GenerateViewMatrix() *
		m_camera.GenerateProjectionMatrix();

	XMFLOAT4X4 mvpMat;
	XMStoreFloat4x4(&mvpMat, sseMvpMat);


	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = m_constantBuffer.gpuBuffer;
	updateConstBufferArgs.offset = obj.constantBufferOffset;
	updateConstBufferArgs.data = &mvpMat;
	updateConstBufferArgs.byteSize = sizeof(mvpMat);
	updateConstBufferArgs.alignment = QCONST_BUFFER_ALIGNMENT;

	UpdateUploadHeapBuff(updateConstBufferArgs);
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

void Renderer::RegisterWorldModel(const char* model)
{
	//#TODO I am not sure that I want old models implementation yet.
	// I might write a wrapper of my own. 
	// IMPORTANT: don't forget to deletion of old world according to
	// Quake logic.

	char fullName[MAX_QPATH];
	Utils::Sprintf(fullName, sizeof(fullName), "maps/%s.bsp", model);

	//#DEBUG world model is taken from that list of preallocated models
	// I don't think I should use that. I should have some kind of container
	// for this "model_t" which are most likely are gonna be temporary objects
	model_t* worldModel = Mod_ForName(fullName, qTrue);

	DecomposeGLModelNode(*worldModel, *worldModel->nodes);
}

void Renderer::RenderFrame(const refdef_t& frameUpdateData)
{
	m_camera.Update(frameUpdateData);

	for (const GraphicalObject& obj : m_graphicalObjects)
	{
		if (!IsVisible(obj))
		{
			continue;
		}

		UpdateGraphicalObjectConstantBuffer(obj);

		if (obj.indexBuffer != nullptr)
		{
			DrawIndiced(obj);
		}
		else
		{
			Draw(obj);
		}
	}
}

Texture* Renderer::RegisterDrawPic(const char* name)
{
	std::array<char, MAX_QPATH> texFullName;
	GetDrawTextureFullname(name, texFullName.data(), texFullName.size());

	Texture* newTex = FindOrCreateTexture(texFullName.data());
	
	return newTex;
}
