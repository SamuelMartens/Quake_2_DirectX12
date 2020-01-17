#include "dx_app.h"

#include <cassert>
#include <string>

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

void DXApp::InitWin32(WNDPROC WindowProc, HINSTANCE hInstance)
{
	if (m_hWindows)
	{
		ShutdownWin32();
	}

	//#DEBUG for some reason windows name is "Q", not "Quake 2"
	const std::wstring WindowsClassName = DXUtils::StringToWString("Quake 2");

	int Width = 0;
	int Height = 0;
	GetDrawAreaSize(&Width, &Height);

	WNDCLASS WindowClass;
	RECT	 ScreenRect;

	WindowClass.style			= 0;
	WindowClass.lpfnWndProc	= WindowProc;
	WindowClass.cbClsExtra		= 0;
	WindowClass.cbWndExtra		= 0;
	WindowClass.hInstance		= hInstance;
	WindowClass.hIcon			= 0;
	WindowClass.hCursor		= LoadCursor(NULL, IDC_ARROW);
	WindowClass.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_GRAYTEXT);
	WindowClass.lpszMenuName	= 0;
	WindowClass.lpszClassName  = static_cast<LPCWSTR>(WindowsClassName.c_str());

	assert(RegisterClass(&WindowClass) && "Failed to register win class.");

	ScreenRect.left = 0;
	ScreenRect.top = 0;
	ScreenRect.right = Width;
	ScreenRect.bottom = Height;

	// For now always run in windows mode (not fullscreen)
	const int ExStyleBist = 0;
	const int StyleBits = WINDOW_STYLE;

	AdjustWindowRect(&ScreenRect, StyleBits, FALSE);

	const int AdjustedWidth = ScreenRect.right - ScreenRect.left;
	const int AdjustedHeight = ScreenRect.bottom - ScreenRect.top;

	char xPosVarName[] = "vid_xpos";
	char xPosVarVal[] = "0";
	cvar_t* vid_xpos = GetRefImport().Cvar_Get(xPosVarName, xPosVarVal, 0);
	char yPosVarName[] = "vid_ypos";
	char yPosVarVal[] = "0";
	cvar_t* vid_ypos = GetRefImport().Cvar_Get(yPosVarName, yPosVarVal, 0);
	const int x = vid_xpos->value;
	const int y = vid_ypos->value;

	 m_hWindows = CreateWindowEx(
		ExStyleBist,
		WindowsClassName.c_str(),
		WindowsClassName.c_str(),
		StyleBits,
		x, y, AdjustedWidth, AdjustedHeight,
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

	GetRefImport().Vid_NewWindow(Width, Height);
}

void DXApp::InitDX()
{
#if defined(DEBUG) || defined(_DEBUG)
	// Enable D3D12 debug layer
	Microsoft::WRL::ComPtr<ID3D12Debug> DebugController;

	DXUtils::ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController)));
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

	SetViewport();
	SetScissor();
}

void DXApp::SetScissor()
{
	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);
	//#INFO scissor rectangle needs to be reset, every time command list is reset
	// Set scissor
	tagRECT ScissorRect = { 0, 0, DrawAreaWidth, DrawAreaHeight };
	m_pCommandList->RSSetScissorRects(1, &ScissorRect);
}

void DXApp::SetViewport()
{
	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);
	// Set viewport
	D3D12_VIEWPORT Viewport;
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;
	Viewport.Width = static_cast<float>(DrawAreaWidth);
	Viewport.Height = static_cast<float>(DrawAreaHeight);
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;

	m_pCommandList->RSSetViewports(1, &Viewport);
}

void DXApp::CreateDepthStencilBufferAndView()
{
	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);

	// Create the depth/stencil buffer and view
	D3D12_RESOURCE_DESC DepthStencilDesc;
	DepthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	DepthStencilDesc.Alignment = 0;
	DepthStencilDesc.Width = DrawAreaWidth;
	DepthStencilDesc.Height = DrawAreaHeight;
	DepthStencilDesc.DepthOrArraySize = 1;
	DepthStencilDesc.MipLevels = 1;
	DepthStencilDesc.Format = QDEPTH_STENCIL_FORMAT;
	DepthStencilDesc.SampleDesc.Count = 4;
	DepthStencilDesc.SampleDesc.Quality = m_MSQualityLevels - 1;
	DepthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	DepthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE OptimizedClearVal;
	OptimizedClearVal.Format = QDEPTH_STENCIL_FORMAT;
	OptimizedClearVal.DepthStencil.Depth = 1.0f;
	OptimizedClearVal.DepthStencil.Stencil = 0;
	DXUtils::ThrowIfFailed(m_pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&DepthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&OptimizedClearVal,
		IID_PPV_ARGS(m_pDepthStencilBuffer.GetAddressOf())));
}

void DXApp::CreateRenderTargetViews()
{
	// Create render target view
	CD3DX12_CPU_DESCRIPTOR_HANDLE RtvHeapHandle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < QSWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		// Get i-th buffer in a swap chain
		DXUtils::ThrowIfFailed(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pSwapChainBuffer[i])));

		// Create an RTV to it
		m_pDevice->CreateRenderTargetView(m_pSwapChainBuffer[i].Get(), nullptr, RtvHeapHandle);

		// Next entry in heap
		RtvHeapHandle.Offset(1, m_RtvDescriptorSize);
	}
}

void DXApp::CreateDescriptorsHeaps()
{
	// Create a descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc;
	RtvHeapDesc.NumDescriptors = QSWAP_CHAIN_BUFFER_COUNT;
	RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	RtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	RtvHeapDesc.NodeMask = 0;

	DXUtils::ThrowIfFailed(m_pDevice->CreateDescriptorHeap(
		&RtvHeapDesc,
		IID_PPV_ARGS(m_pRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc;
	DsvHeapDesc.NumDescriptors = 1;
	DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	DsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DsvHeapDesc.NodeMask = 0;

	DXUtils::ThrowIfFailed(m_pDevice->CreateDescriptorHeap(
		&DsvHeapDesc,
		IID_PPV_ARGS(m_pDsvHeap.GetAddressOf())));
}

void DXApp::CreateSwapChain()
{
	// Create swap chain
	m_pSwapChain.Reset();

	int DrawAreaWidth = 0;
	int DrawAreaHeight = 0;

	GetDrawAreaSize(&DrawAreaWidth, &DrawAreaHeight);

	DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
	SwapChainDesc.BufferDesc.Width = DrawAreaWidth;
	SwapChainDesc.BufferDesc.Height = DrawAreaHeight;
	SwapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	SwapChainDesc.BufferDesc.Format = QBACK_BUFFER_FORMAT;
	SwapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	SwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	SwapChainDesc.SampleDesc.Count = 4;
	SwapChainDesc.SampleDesc.Quality = m_MSQualityLevels - 1;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.BufferCount = QSWAP_CHAIN_BUFFER_COUNT;
	SwapChainDesc.OutputWindow = m_hWindows;
	SwapChainDesc.Windowed = true;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Node: Swap chain uses queue to perform flush.
	DXUtils::ThrowIfFailed(m_pDxgiFactory->CreateSwapChain(m_pCommandQueue.Get(),
		&SwapChainDesc,
		m_pSwapChain.GetAddressOf()));
}

void DXApp::CheckMSAAQualitySupport()
{
	// Check 4X MSAA Quality Support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS MSQualityLevels;
	MSQualityLevels.Format = QBACK_BUFFER_FORMAT;
	MSQualityLevels.SampleCount = 4;
	MSQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	MSQualityLevels.NumQualityLevels = 0;
	DXUtils::ThrowIfFailed(m_pDevice->CheckFeatureSupport(
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
	DXUtils::ThrowIfFailed(m_pDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&m_pCommandListAlloc)));

	// Create command list 
	DXUtils::ThrowIfFailed(m_pDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_pCommandListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(m_pCommandList.GetAddressOf())));

	m_pCommandList->Close();
}

void DXApp::CreateCommandQueue()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC QueueDesc = {};

	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	DXUtils::ThrowIfFailed(m_pDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&m_pCommandQueue)));
}

void DXApp::CreateFences()
{
	// Create fence
	DXUtils::ThrowIfFailed(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));
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
	DXUtils::ThrowIfFailed(D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_pDevice)
	));
}

void DXApp::CreateDxgiFactory()
{
	DXUtils::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_pDxgiFactory)));
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
