#pragma once

#ifndef WIN32
#error "DirectX renderer can run only on Windows"
#endif // !WIN32


#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <memory>
#include <atomic>

#include "d3dx12.h"
#include "dx_texture.h"
#include "dx_common.h"
#include "dx_objects.h"
#include "dx_buffer.h"
#include "dx_shaderdefinitions.h"
#include "dx_glmodel.h"
#include "dx_camera.h"
#include "dx_material.h"
#include "dx_jobmultithreading.h"
#include "dx_frame.h"
#include "dx_descriptorheap.h"

extern "C"
{
	#include "../client/ref.h"
};

namespace FArg 
{
	struct UpdateUploadHeapBuff
	{
		ComPtr<ID3D12Resource> buffer;
		int offset = Const::INVALID_OFFSET;
		const void* data = nullptr;
		int byteSize = Const::INVALID_SIZE;
		int alignment = -1;
	};

	struct UpdateDefaultHeapBuff
	{
		ComPtr<ID3D12Resource> buffer;
		int offset = Const::INVALID_OFFSET;
		const void* data = nullptr;
		int byteSize = Const::INVALID_SIZE;
		int alignment = -1;
		Frame* frame = nullptr;
	};
};


//#TODO 
// 1) currently I do : Update - Draw, Update - Draw. It should be Update Update , Draw Draw (especially text)
// 2) Make your wrappers as exclusive owners of some resource, and operate with smart pointers instead to avoid mess
//    during resource management.(This requires rewrite some stuff like Textures or buffers)
// 3) For Movies and UI we don't need stream drawing, but just one quad object  and the width and height would be
//	  scaling of this quad along y or x axis
// 4) If decided to go with preallocated GPU buffers I should come up with some way to control buffers states
//	  and actively avoid redundant state transitions
// 5) Implement occlusion query. If this is not enough, resurrect BSP tree.
// 6) Device and factory should leave in separate class. That would fix a lot(will do it after job system is implemented)
class Renderer
{
private:
	Renderer();


	constexpr static int		 QFRAMES_NUM = 2;
	constexpr static int		 QSWAP_CHAIN_BUFFER_COUNT = 2;
	constexpr static bool		 QMSAA_ENABLED = false;
	constexpr static int		 QMSAA_SAMPLE_COUNT = 4;
	constexpr static int		 QTRANSPARENT_TABLE_VAL = 255;
	constexpr static int		 QCONST_BUFFER_ALIGNMENT = 256;
	constexpr static int		 QDYNAM_OBJECT_CONST_BUFFER_POOL_SIZE = 512;
	
	constexpr static int		 QCBV_SRV_DESCRIPTOR_HEAP_SIZE = 512;
	constexpr static int		 QRTV_DTV_DESCRIPTOR_HEAP_SIZE = QFRAMES_NUM;

	// 128 MB of upload memory
	constexpr static int		 QUPLOAD_MEMORY_BUFFER_SIZE = 128 * 1024 * 1024;
	constexpr static int		 QUPLOAD_MEMORY_BUFFER_HANDLERS_NUM = 16382;
	// 256 MB of default memory
	constexpr static int		 QDEFAULT_MEMORY_BUFFER_SIZE = 256 * 1024 * 1024;
	constexpr static int		 QDEFAULT_MEMORY_BUFFER_HANDLERS_NUM = 16382;

	constexpr static char		 QRAW_TEXTURE_NAME[] = "__DX_MOVIE_TEXTURE__";
	constexpr static char		 QFONT_TEXTURE_NAME[] = "conchars";

	constexpr static bool		 QDEBUG_LAYER_ENABLED = true;
	constexpr static bool		 QDEBUG_MESSAGE_FILTER_ENABLED = true;

public:

	constexpr static DXGI_FORMAT QBACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr static DXGI_FORMAT QDEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;


public:

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;
	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	~Renderer() = default;

	static Renderer& Inst();

	const refimport_t& GetRefImport() const { return m_refImport; };
	void SetRefImport(refimport_t RefImport) { m_refImport = RefImport; };

	// Buffers management
	void DeleteResources(ComPtr<ID3D12Resource> resourceToDelete);
	void DeleteDefaultMemoryBuffer(BufferHandler handler);
	void DeleteUploadMemoryBuffer(BufferHandler handler);
	
	void UpdateStreamingConstantBuffer(XMFLOAT4 position, XMFLOAT4 scale, BufferHandler handler, Frame& frame);
	void UpdateStaticObjectConstantBuffer(const StaticObject& obj, Frame& frame);
	void UpdateDynamicObjectConstantBuffer(DynamicObject& obj, const entity_t& entity, Frame& frame);
	BufferHandler UpdateParticleConstantBuffer(Frame& frame);

	Texture* FindOrCreateTexture(std::string_view textureName, Frame& frame);

	/*--- API functions begin --- */

	void Init(WNDPROC WindowProc, HINSTANCE hInstance);
	void BeginFrame();
	void EndFrame();
	void Draw_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data);
	void Draw_Pic(int x, int y, const char* name);
	void Draw_Char(int x, int y, int num);
	void GetDrawTextureSize(int* x, int* y, const char* name) const;
	void SetPalette(const unsigned char* palette);

	Texture* RegisterDrawPic(const char* name);
	void RegisterWorldModel(const char* model);
	void EndLevelLoading();
	model_s* RegisterModel(const char* name);
	void RenderFrame(const refdef_t& frameUpdateData);


	/*--- API functions end --- */

	/* Utils (for public use) */
	int GetMSAASampleCount() const;
	int GetMSAAQuality() const;

	/* Initialization and creation */
	void CreateCmdListAndCmdListAlloc(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandListAlloc);
	void CreateFences(ComPtr<ID3D12Fence>& fence);
	void CreateDepthStencilBuffer(ComPtr<ID3D12Resource>& buffer);
	int  GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const;



public:

	// Public because it is already wrapped up in class
	std::unique_ptr<DescriptorHeap>	rtvHeap = nullptr;
	std::unique_ptr<DescriptorHeap>	dsvHeap = nullptr;
	std::unique_ptr<DescriptorHeap> cbvSrvHeap = nullptr;

private:

	/* Initialize win32 specific stuff */
	void InitWin32(WNDPROC WindowProc, HINSTANCE hInstance);
	/* Initialize DirectX stuff */
	void InitDx();

	void EnableDebugLayer();
	void SetDebugMessageFilter();

	void InitUtils();

	void InitMemory();

	void InitScissorRect();

	void InitCamera();

	void InitFrames();

	void CreateSwapChainBuffersAndViews();

	void CreateDescriptorHeaps();

	void CreateSwapChain();

	void CheckMSAAQualitySupport();

	void CreateCommandQueue();

	void InitDescriptorSizes();

	void CreateCompiledMaterials();

	void CreateTextureSampler();

	ComPtr<ID3DBlob> LoadCompiledShader(const std::string& filename) const;
	ComPtr<ID3D12RootSignature> SerializeAndCreateRootSigFromRootDesc(const CD3DX12_ROOT_SIGNATURE_DESC& rootSigDesc) const;

	AssertBufferAndView& GetNextSwapChainBufferAndView();
	
	void PresentAndSwapBuffers(Frame& frame);

	/* Texture */
	Texture* CreateTextureFromFile(const char* name, Frame& frame);
	void CreateGpuTexture(const unsigned int* raw, int width, int height, int bpp, Frame& frame, Texture& outTex);
	Texture* CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, Frame& frame);
	void UpdateTexture(Texture& tex, const std::byte* data, Frame& frame);
	void ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);
	void GetDrawTextureFullname(const char* name, char* dest, int destSize) const;

	/* Buffer */
	ComPtr<ID3D12Resource> CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, Frame& frame);
	ComPtr<ID3D12Resource> CreateUploadHeapBuffer(UINT64 byteSize) const;
	void UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const;
	void UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args);

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Factory functionality */
	void CreatePictureObject(const char* pictureName, Frame& frame);
	DynamicObjectModel CreateDynamicGraphicObjectFromGLModel(const model_t* model, Frame& frame);
	void CreateGraphicalObjectFromGLSurface(const msurface_t& surf, Frame& frame);
	void DecomposeGLModelNode(const model_t& model, const mnode_t& node, Frame& frame);

	/* Rendering */
	void Draw(const StaticObject& object, Frame& frame);
	void DrawIndiced(const StaticObject& object, Frame& frame);
	void DrawIndiced(const DynamicObject& object, const entity_t& entity, Frame& frame);
	void DrawStreaming(const std::byte* vertices, int verticesSizeInBytes, int verticesStride, const char* texName, const XMFLOAT4& pos, Frame& frame);
	void AddParticleToDrawList(const particle_t& particle, BufferHandler vertexBufferHandler, int vertexBufferOffset);
	void DrawParticleDrawList(BufferHandler vertexBufferHandler, int vertexBufferSizeInBytes, BufferHandler constBufferHandler, Frame& frame);

	/* Utils */
	void GetDrawAreaSize(int* Width, int* Height);
	void Load8To24Table();
	void ImageBpp8To32(const std::byte* data, int width, int height, unsigned int* out) const;
	void FindImageScaledSizes(int width, int height, int& scaledWidth, int& scaledHeight) const;
	bool IsVisible(const StaticObject& obj) const;
	bool IsVisible(const entity_t& entity) const;
	DynamicObjectConstBuffer& FindDynamicObjConstBuffer();


	/* Materials */
	Material CompileMaterial(const MaterialSource& materialSourse) const;

	void SetMaterial(const std::string& name, Frame& frame);
	void ClearMaterial(Frame& frame);

	/* Frames */
	Frame& GetCurrentFrame();
	void SubmitFrame(Frame& frame);
	void WaitForFrame(Frame& frame) const;

	// Difference between OpenFrame/CloseFrame and BeginFrame/EndFrame is that first one is more generic,
	// means it supposed to be used for anything where you record command list
	// and then submit it. BeginFrame/EndFrame on the other hand is directly related to drawing where you
	// ,for example, have buffer to draw to
	void OpenFrame(Frame& frame) const;
	void CloseFrame(Frame& frame);

	int GenerateFenceValue();
	int GetFenceValue() const;

	HWND		m_hWindows = nullptr;

	refimport_t m_refImport;

	ComPtr<IDXGISwapChain> m_swapChain;

	AssertBufferAndView m_swapChainBuffersAndViews[QSWAP_CHAIN_BUFFER_COUNT];

	ComPtr<ID3D12CommandQueue>		  m_commandQueue;
	
	ComPtr<ID3D12DescriptorHeap>	  m_samplerHeap;

	// I need enforce alignment on this buffer, because I allocate constant buffers from it. 
	// I don't want to create separate buffer just for constant buffers, because that would increase complexity
	// without real need to do this. I still try to explicitly indicate places where I actually need alignment
	// in case I would decide to refactor this into separate buffer
	HandlerBuffer<QUPLOAD_MEMORY_BUFFER_SIZE, 
		QUPLOAD_MEMORY_BUFFER_HANDLERS_NUM, QCONST_BUFFER_ALIGNMENT> m_uploadMemoryBuffer;
	HandlerBuffer<QDEFAULT_MEMORY_BUFFER_SIZE, QDEFAULT_MEMORY_BUFFER_HANDLERS_NUM> m_defaultMemoryBuffer;
	
	tagRECT		   m_scissorRect;

	INT	m_currentBackBuffer = 0;

	/* Render target descriptor size */
	UINT								   m_rtvDescriptorSize = 0;
	/* Depth/Stencil descriptor size */
	UINT								   m_dsvDescriptorSize = 0;
	/* Constant buffer / shader resource descriptor size */
	UINT								   m_cbvSrbDescriptorSize = 0;
	/* Sampler descriptor size */
	UINT								   m_samplerDescriptorSize = 0;

	UINT m_MSQualityLevels = 0;

	std::unordered_map<std::string, Texture> m_textures;
	// If we want to delete resource we can't do this right away cause there is a high chance that this 
	// resource will still be in use, so we just put it here and delete it later 
	std::vector<ComPtr<ID3D12Resource>> m_resourcesToDelete;

	std::array<unsigned int, 256> m_8To24Table;
	std::array<unsigned int, 256> m_rawPalette;

	// Should I separate UI from game object? Damn, is this NWN speaks in me
	std::vector<StaticObject> m_staticObjects;
	std::unordered_map<model_t*, DynamicObjectModel> m_dynamicObjectsModels;
	// Expected to set a size for it during initialization. Don't change size afterward
	std::vector<DynamicObjectConstBuffer> m_dynamicObjectsConstBuffersPool;

	XMFLOAT4X4 m_uiProjectionMat;
	XMFLOAT4X4 m_uiViewMat;
	// DirectX and OpenGL have different directions for Y axis,
	// this matrix is required to fix this. Also apparently original Quake 2
	// had origin in a middle of a screen, while we have it in upper left corner,
	// so we need to center content to the screen center
	XMFLOAT4X4 m_yInverseAndCenterMatrix;

	Camera m_camera;

	std::vector<Material> m_materials;

	JobSystem m_jobSystem;

	std::array<Frame, QFRAMES_NUM> m_frames;
	int m_currentFrameIndex = Const::INVALID_INDEX;

	std::atomic<int>	m_fenceValue = 0;
	ComPtr<ID3D12Fence>	m_fence;

	int m_frameCounter = 0;

};