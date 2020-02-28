#pragma once

#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <string>
#include <vector>

#include "d3dx12.h"

using namespace Microsoft::WRL;

extern "C"
{
	#include "../client/ref.h"
};

class DXApp
{
private:
	DXApp() = default;

	constexpr static DXGI_FORMAT QBACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr static DXGI_FORMAT QDEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
	constexpr static int		 QSWAP_CHAIN_BUFFER_COUNT = 2;
	constexpr static bool		 QMSAA_ENABLED = false;
	constexpr static int		 QMSAA_SAMPLE_COUNT = 4;

public:

	DXApp(const DXApp&) = delete;
	DXApp& operator=(const DXApp&) = delete;
	DXApp(DXApp&&) = delete;
	DXApp& operator=(DXApp&&) = delete;

	~DXApp() = default;

	static DXApp& Inst();

 	void Init(WNDPROC WindowProc, HINSTANCE hInstance);

	void BeginFrame(float CameraSeparation);
	void EndFrame();

	const refimport_t& GetRefImport() const { return m_RefImport; };
	void SetRefImport(refimport_t RefImport) { m_RefImport = RefImport; };

private:

	/* Initialize win32 specific stuff */
	void InitWin32(WNDPROC WindowProc, HINSTANCE hInstance);
	/* Initialize DirectX stuff */
	void InitDX();

	void InitScissorRect();

	void InitViewport();

	void CreateDepthStencilBufferAndView();

	void CreateRenderTargetViews();

	void CreateDescriptorsHeaps();

	void CreateSwapChain();

	void CheckMSAAQualitySupport();

	void CreateCmdAllocatorAndCmdList();

	void CreateCommandQueue();

	void CreateFences();
	void InitDescriptorSizes();
	void CreateDevice();
	void CreateDxgiFactory();

	void CreateRootSignature();
	void CreatePipelineState();
	void CreateInputLayout();
	void LoadShaders();

	int GetMSAASampleCount() const;
	int GetMSAAQuality() const;

	ComPtr<ID3DBlob> LoadCompiledShader(const std::string& filename) const;
	ComPtr<ID3D12RootSignature> SerializeAndCreateRootSigFromRootDesc(const CD3DX12_ROOT_SIGNATURE_DESC& rootSigDesc) const;

	void ExecuteCommandLists();
	void FlushCommandQueue();
	
	ID3D12Resource* GetCurrentBackBuffer();
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView();
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView();
	
	void PresentAndSwapBuffers();

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Utils */
	void GetDrawAreaSize(int* Width, int* Height);

	HWND		m_hWindows = nullptr;

	refimport_t m_RefImport;

	ComPtr<ID3D12Device>   m_pDevice;
	ComPtr<IDXGIFactory4>  m_pDxgiFactory;

	ComPtr<IDXGISwapChain> m_pSwapChain;
	ComPtr<ID3D12Fence>	   m_pFence;
	ComPtr<ID3D12Resource> m_pSwapChainBuffer[QSWAP_CHAIN_BUFFER_COUNT];
	ComPtr<ID3D12Resource> m_pDepthStencilBuffer;

	ComPtr<ID3D12CommandQueue>		  m_pCommandQueue;
	ComPtr<ID3D12CommandAllocator>	  m_pCommandListAlloc;
	ComPtr<ID3D12GraphicsCommandList> m_pCommandList;

	ComPtr<ID3D12DescriptorHeap>	  m_pRtvHeap;
	ComPtr<ID3D12DescriptorHeap>	  m_pDsvHeap;

	std::vector<D3D12_INPUT_ELEMENT_DESC> m_inputLayout;
	ComPtr<ID3D12PipelineState>		  m_pPipelineState;
	ComPtr<ID3D12RootSignature>		  m_pRootSingature;

	ComPtr<ID3DBlob> m_pPsShader;
	ComPtr<ID3DBlob> m_pVsShader;

	D3D12_VIEWPORT m_viewport;
	tagRECT		   m_scissorRect;

	INT	m_currentBackBuffer = 0;

	/* Render target descriptor size */
	UINT								   m_RtvDescriptorSize = 0;
	/* Depth/Stencil descriptor size */
	UINT								   m_DsvDescriptorSize = 0;
	/* Constant buffer / shader resource descriptor size */
	UINT								   m_CbvSrbDescriptorSize = 0;

	UINT m_MSQualityLevels = 0;

	UINT64 m_currentFenceValue = 0;
};