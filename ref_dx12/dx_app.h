#pragma once

#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>

#include "d3dx12.h"
#include "dx_texture.h"

using namespace Microsoft::WRL;

extern "C"
{
	#include "../client/ref.h"
};

class Renderer
{
private:
	Renderer() = default;

	constexpr static DXGI_FORMAT QBACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr static DXGI_FORMAT QDEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
	constexpr static int		 QSWAP_CHAIN_BUFFER_COUNT = 2;
	constexpr static bool		 QMSAA_ENABLED = false;
	constexpr static int		 QMSAA_SAMPLE_COUNT = 4;
	constexpr static int		 QTRANSPARENT_TABLE_VAL = 255;
	constexpr static int		 QCBV_SRV_DESCRIPTORS_NUM = 256;

public:

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;
	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	~Renderer() = default;

	static Renderer& Inst();

 	void Init(WNDPROC WindowProc, HINSTANCE hInstance);

	void BeginFrame(float CameraSeparation);
	void EndFrame();

	const refimport_t& GetRefImport() const { return m_RefImport; };
	void SetRefImport(refimport_t RefImport) { m_RefImport = RefImport; };

	void FreeSrvSlot(int slotIndex);
	int AllocSrvSlot();

	//#DEBUG remove later
	void DrawTextured(char* name);

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

	/* Texture */
	void CreateTexture(char* name);
	void CreateGpuTexture(const unsigned int* raw, int width, int height, int bpp, Texture& outTex);

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Utils */
	void GetDrawAreaSize(int* Width, int* Height);
	void LoadPalette();
	void ImageBpp8To32(const std::byte* data, int width, int height, unsigned int* out) const;
	void FindImageScaledSizes(int width, int height, int& scaledWidth, int& scaledHeight) const;
	void ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);
	void GetTextureFullname(const char* name, char* dest, int destSize) const;

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
	ComPtr<ID3D12DescriptorHeap>	  m_pCbvSrvHeap;

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

	std::unordered_map<std::string, Texture> m_textures;
	// When we upload something on GPU we need to make sure that its ComPtr is alive until 
	// we finish execution of command list that references this. So I will just put this stuff here
	// and clear this vector at the end of every frame.
	std::vector<ComPtr<ID3D12Resource>> m_uploadResources;

	std::array<unsigned int, 256> m_8To24Table;
	// Bookkeeping for which descriptors are taken and which aren't. This is very simple
	// true means slot is taken.
	std::array<bool, QCBV_SRV_DESCRIPTORS_NUM> m_cbvSrvRegistry;
};