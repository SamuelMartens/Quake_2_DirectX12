#pragma once

#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>


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

public:

	DXApp(const DXApp&) = delete;
	DXApp& operator=(const DXApp&) = delete;
	DXApp(DXApp&&) = delete;
	DXApp& operator=(DXApp&&) = delete;

	~DXApp() = default;

	static DXApp& Inst();

 	void Init(WNDPROC WindowProc, HINSTANCE hInstance);

	const refimport_t& GetRefImport() const { return m_RefImport; };
	void SetRefImport(refimport_t RefImport) { m_RefImport = RefImport; };

private:

	/* Initialize win32 specific stuff */
	void InitWin32(WNDPROC WindowProc, HINSTANCE hInstance);
	/* Initialize DirectX stuff */
	void InitDX();

	void CreateDevice();
	void CreateDxgiFactory();

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Utils */
	void GetDrawAreaSize(int* Width, int* Height);

	HWND		m_hWindows = nullptr;

	refimport_t m_RefImport;

	Microsoft::WRL::ComPtr<ID3D12Device>   m_pDevice;
	Microsoft::WRL::ComPtr<IDXGIFactory4>  m_pDxgiFactory;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_pSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Fence>	   m_pFence;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pSwapChainBuffer[QSWAP_CHAIN_BUFFER_COUNT];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pDepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue>		  m_pCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>	  m_pCommandListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_pCommandList;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	  m_pRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	  m_pDsvHeap;

	/* Render target descriptor size */
	UINT								   m_RtvDescriptorSize = 0;
	/* Depth/Stencil descriptor size */
	UINT								   m_DsvDescriptorSize = 0;
	/* Constant buffer / shader resource descriptor size */
	UINT								   m_CbvSrbDescriptorSize = 0;

	UINT m_MSQualityLevels = 0;
};