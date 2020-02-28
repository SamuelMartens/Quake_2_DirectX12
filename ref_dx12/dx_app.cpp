#include "dx_app.h"

#include <cassert>
#include <string>
#include <fstream>
#include <d3dcompiler.h>
#include <DirectXColors.h>

#include "d3dx12.h"
#include "../win32/winquake.h"
#include "dx_utils.h"

DXApp& DXApp::Inst()
{
	static DXApp* app = nullptr;

	if (app == nullptr)
	{
		app = new DXApp();
	}

	return *app;
}

void DXApp::Init(WNDPROC WindowProc, HINSTANCE hInstance)
{
	InitWin32(WindowProc, hInstance);
	InitDX();
}

void DXApp::BeginFrame(float CameraSeparation)
{
	ThrowIfFailed(m_pCommandListAlloc->Reset());
	ThrowIfFailed(m_pCommandList->Reset(m_pCommandListAlloc.Get(), m_pPipelineState.Get()));

	// Resetting viewport is mandatory
	m_pCommandList->RSSetViewports(1, &m_viewport);
	// Resetting scissor is mandatory 
	m_pCommandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate buffer transition to write state
	m_pCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	// Clear back buffer and depth buffer
	//#DEBUG change clear color to black as soon as you will verify that this works
	m_pCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), DirectX::Colors::Black, 0, nullptr);
	m_pCommandList->ClearDepthStencilView(
		GetDepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);


	// Specify buffer we are going to render to
	m_pCommandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());
}

void DXApp::EndFrame()
{
	// Indicate current buffer state transition
	m_pCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	ExecuteCommandLists();

	PresentAndSwapBuffers();

	FlushCommandQueue();
}

void DXApp::InitWin32(WNDPROC WindowProc, HINSTANCE hInstance)
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

void DXApp::InitDX()
{
	//#TODO all this stuff should be reworked to avoid redundant
	// members of the class
#if defined(DEBUG) || defined(_DEBUG)
	// Enable D3D12 debug layer
	Microsoft::WRL::ComPtr<ID3D12Debug> DebugController;

	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController)));
	DebugController->EnableDebugLayer();
#endif

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

	// If we recorded some commands during initialization, execute them here
	ExecuteCommandLists();
	FlushCommandQueue();
}

void DXApp::InitScissorRect()
{
	int drawAreaWidth = 0;
	int drawAreaHeight = 0;

	GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);
	//#INFO scissor rectangle needs to be reset, every time command list is reset
	// Set scissor
	m_scissorRect = { 0, 0, drawAreaWidth, drawAreaHeight };
}

void DXApp::InitViewport()
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

void DXApp::CreateDepthStencilBufferAndView()
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
	ThrowIfFailed(m_pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optimizedClearVal,
		IID_PPV_ARGS(m_pDepthStencilBuffer.GetAddressOf())));

	// Create depth stencil view
	m_pDevice->CreateDepthStencilView(
		m_pDepthStencilBuffer.Get(),
		nullptr,
		GetDepthStencilView());

	m_pCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_pDepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		));
}

void DXApp::CreateRenderTargetViews()
{
	// Create render target view
	CD3DX12_CPU_DESCRIPTOR_HANDLE RtvHeapHandle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < QSWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		// Get i-th buffer in a swap chain
		ThrowIfFailed(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pSwapChainBuffer[i])));

		// Create an RTV to it
		m_pDevice->CreateRenderTargetView(m_pSwapChainBuffer[i].Get(), nullptr, RtvHeapHandle);

		// Next entry in heap
		RtvHeapHandle.Offset(1, m_RtvDescriptorSize);
	}
}

void DXApp::CreateDescriptorsHeaps()
{
	// Create a descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = QSWAP_CHAIN_BUFFER_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_pDevice->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(m_pRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_pDevice->CreateDescriptorHeap(
		&dsvHeapDesc,
		IID_PPV_ARGS(m_pDsvHeap.GetAddressOf())));
}

void DXApp::CreateSwapChain()
{
	// Create swap chain
	m_pSwapChain.Reset();

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

	// Node: Swap chain uses queue to perform flush.
	ThrowIfFailed(m_pDxgiFactory->CreateSwapChain(m_pCommandQueue.Get(),
		&swapChainDesc,
		m_pSwapChain.GetAddressOf()));
}

void DXApp::CheckMSAAQualitySupport()
{
	if (QMSAA_ENABLED == false)
		return;

	// Check 4X MSAA Quality Support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS MSQualityLevels;
	MSQualityLevels.Format = QBACK_BUFFER_FORMAT;
	MSQualityLevels.SampleCount = QMSAA_SAMPLE_COUNT;
	MSQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	MSQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(m_pDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&MSQualityLevels,
		sizeof(MSQualityLevels)
	));

	m_MSQualityLevels = MSQualityLevels.NumQualityLevels;
	assert(m_MSQualityLevels > 0 && "Unexpected MSAA quality levels");
}

void DXApp::CreateCmdAllocatorAndCmdList()
{
	// Create command allocator
	ThrowIfFailed(m_pDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&m_pCommandListAlloc)));

	// Create command list 
	ThrowIfFailed(m_pDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_pCommandListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(m_pCommandList.GetAddressOf())));
}

void DXApp::CreateCommandQueue()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)));
}

void DXApp::CreateFences()
{
	// Create fence
	ThrowIfFailed(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));
}

void DXApp::InitDescriptorSizes()
{
	// Get descriptor sizes
	m_RtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrbDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DXApp::CreateDevice()
{
	// This is super weird. This function internally will throw com exception,
	// but result will be still fine.
	ThrowIfFailed(D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_pDevice)
	));
}

void DXApp::CreateDxgiFactory()
{
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_pDxgiFactory)));
}

ComPtr<ID3D12RootSignature> DXApp::SerializeAndCreateRootSigFromRootDesc(const CD3DX12_ROOT_SIGNATURE_DESC& rootSigDesc) const
{
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	
	ThrowIfFailed(D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()));

	ComPtr<ID3D12RootSignature> resultRootSig;

	ThrowIfFailed(m_pDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&resultRootSig)
	));

	return resultRootSig;
}

void DXApp::ExecuteCommandLists()
{
	// Done with this command list
	ThrowIfFailed(m_pCommandList->Close());

	// Add command list to execution queue
	ID3D12CommandList* cmdLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
}

void DXApp::CreateRootSignature()
{
	// Root signature is an array of root parameters
	CD3DX12_ROOT_PARAMETER slotRootParameters[2];

	// First parameter texture/CBV descriptor table
	CD3DX12_DESCRIPTOR_RANGE textureCbvRanges[2];
	textureCbvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	textureCbvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	slotRootParameters[0].InitAsDescriptorTable(2, textureCbvRanges);

	// Second parameter is samplers descriptor table
	CD3DX12_DESCRIPTOR_RANGE samplersRanges[1];
	samplersRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

	slotRootParameters[1].InitAsDescriptorTable(1, samplersRanges);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameters),
		slotRootParameters,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_pRootSingature = SerializeAndCreateRootSigFromRootDesc(rootSigDesc);
}

void DXApp::CreatePipelineState()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { m_inputLayout.data(), static_cast<UINT>(m_inputLayout.size())};
	psoDesc.pRootSignature = m_pRootSingature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(m_pVsShader->GetBufferPointer()),
		m_pVsShader->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(m_pPsShader->GetBufferPointer()),
		m_pPsShader->GetBufferSize()
	};
	//#DEBUG here counter clockwise might hit me in a face. Cause 
	// GL uses counter clockwise. Let's see what I will get first, and 
	// then fix this problem
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

	ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPipelineState)));
}

void DXApp::CreateInputLayout()
{
	m_inputLayout = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}


void DXApp::LoadShaders()
{
	//#DEBUG
	char result[MAX_PATH];
	std::string path1(result, GetModuleFileName(nullptr, result, MAX_PATH));
	//END

	//#DEBUG stupid problem. I can't find where is my app runs from. When I will find it
	// I should fix output for shader compiler to where it is needed
	//const std::string psShader = "D:\\Projects\\quake2-vs2015\\debug\\ps_PosTex.cso";
	//const std::string vsShader = "D:\\Projects\\quake2-vs2015\\debug\\vs_PosTex.cso";
	const std::string psShader = "ps_PosTex.cso";
	const std::string vsShader = "vs_PosTex.cso";

	m_pPsShader = LoadCompiledShader(psShader);
	m_pVsShader = LoadCompiledShader(vsShader);
}

int DXApp::GetMSAASampleCount() const
{
	return QMSAA_ENABLED ? QMSAA_SAMPLE_COUNT : 1;
}

int DXApp::GetMSAAQuality() const
{
	return QMSAA_ENABLED ? (m_MSQualityLevels - 1) : 0;
}

//************************************
// Method:    FlushCommandQueue
// FullName:  DXApp::FlushCommandQueue
// Access:    private 
// Returns:   void
// Qualifier: Wait until we are done with previous frame
//			  before starting next one. This is very inefficient, and 
//			  should be never used. Still it is here for now, while
//			  I am prototyping
//************************************
void DXApp::FlushCommandQueue()
{
	// Advance the fence value to mark a new point
	++m_currentFenceValue;

	// Add the instruction to command queue to set up a new value for fence. GPU will
	// only set this value when it will reach this point
	ThrowIfFailed(m_pCommandQueue->Signal(m_pFence.Get(), m_currentFenceValue));

	if (m_pFence->GetCompletedValue() >= m_currentFenceValue)
	{
		// GPU has reached the required value
		return;
	}

	// Wait unti GPU will reach our fence. Create dummy even which we will wait for
	HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

	ThrowIfFailed(m_pFence->SetEventOnCompletion(m_currentFenceValue, eventHandle));

	//Waaait
	WaitForSingleObject(eventHandle, INFINITE);
	CloseHandle(eventHandle);
}

ID3D12Resource* DXApp::GetCurrentBackBuffer()
{
	return m_pSwapChainBuffer[m_currentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DXApp::GetCurrentBackBufferView()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE
	(
		m_pRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_currentBackBuffer,
		m_RtvDescriptorSize
	);
}

D3D12_CPU_DESCRIPTOR_HANDLE DXApp::GetDepthStencilView()
{
	return m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

//************************************
// Method:    PresentAndSwapBuffers
// FullName:  DXApp::SwapBuffers
// Access:    private 
// Returns:   void
// Qualifier: Presents and swap current back buffer, and does some bookkeeping.
// All preparations should be done at this point including buffers transitions.
// This function will not do any oh these, so all on you. It do
//************************************
void DXApp::PresentAndSwapBuffers()
{
	ThrowIfFailed(m_pSwapChain->Present(0, 0));
	m_currentBackBuffer = (m_currentBackBuffer + 1) % QSWAP_CHAIN_BUFFER_COUNT;
}

ComPtr<ID3DBlob> DXApp::LoadCompiledShader(const std::string& filename) const
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

void DXApp::ShutdownWin32()
{
	DestroyWindow(m_hWindows);
	m_hWindows = NULL;
}

void DXApp::GetDrawAreaSize(int* Width, int* Height)
{

	char modeVarName[] = "gl_mode";
	char modeVarVal[] = "3";
	cvar_t* mode = GetRefImport().Cvar_Get(modeVarName, modeVarVal, CVAR_ARCHIVE);

	assert(mode);
	GetRefImport().Vid_GetModeInfo(Width, Height, static_cast<int>(mode->value));
}
