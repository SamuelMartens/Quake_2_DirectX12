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
#include "dx_threadingutils.h"
#include "dx_frame.h"
#include "dx_descriptorheap.h"
#include "dx_commandlist.h"

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
		Context* context = nullptr;
	};

	struct DrawStreaming
	{
		const std::byte* vertices = nullptr;
		int verticesSizeInBytes = -1;
		int verticesStride = -1;
		const char* texName = nullptr;
		const XMFLOAT4* pos;
		const BufferPiece* bufferPiece;
		Context* context;
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

public:

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;
	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	~Renderer() = default;

	static Renderer& Inst();

	const refimport_t& GetRefImport() const { return m_refImport; };
	void SetRefImport(refimport_t RefImport) { m_refImport = RefImport; };

	/*--- Buffers management --- */
	void RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete);
	void DeleteRequestedResources();
	void DeleteDefaultMemoryBuffer(BufferHandler handler);
	void DeleteUploadMemoryBuffer(BufferHandler handler);
	
	void UpdateStreamingConstantBuffer(XMFLOAT4 position, XMFLOAT4 scale, BufferPiece bufferPiece, Context& context);
	void UpdateStaticObjectConstantBuffer(const StaticObject& obj, Context& context);
	void UpdateDynamicObjectConstantBuffer(DynamicObject& obj, const entity_t& entity, Context& context);
	BufferHandler UpdateParticleConstantBuffer(Context& context);

	Texture* FindOrCreateTexture(std::string_view textureName, Context& context);
	Texture* FindTexture(std::string_view textureName);

	/*--- API functions begin --- */

	void Init(WNDPROC WindowProc, HINSTANCE hInstance);
	void AddDrawCall_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data);
	void AddDrawCall_Pic(int x, int y, const char* name);
	void AddDrawCall_Char(int x, int y, int num);


	void BeginFrame();
	void EndFrame();

	void GetDrawTextureSize(int* x, int* y, const char* name);
	void SetPalette(const unsigned char* palette);

	Texture* RegisterDrawPic(const char* name);
	void RegisterWorldModel(const char* model);
	void BeginLevelLoading(const char* map);
	void EndLevelLoading();
	model_s* RegisterModel(const char* name);
	void UpdateFrame(const refdef_t& updateData);


	/*--- API functions end --- */

	/* Utils (for public use) */
	int GetMSAASampleCount() const;
	int GetMSAAQuality() const;
	void GetDrawAreaSize(int* Width, int* Height);

	/* Initialization and creation */
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

	void InitMemory(Context& context);

	void InitScissorRect();

	void InitFrames();

	void InitCommandListsBuffer();

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
	Texture* CreateTextureFromFileDeferred(const char* name, Frame& frame);
	Texture* CreateTextureFromFile(const char* name, Context& context);
	Texture* CreateTextureFromDataDeferred(const std::byte* data, int width, int height, int bpp, const char* name, Frame& frame);
	Texture* CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, Context& context);
	Texture* _CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, Context& context);
	Texture* _CreateTextureFromFile(const char* name, Context& context);
	void _CreateGpuTexture(const unsigned int* raw, int width, int height, int bpp, Context& context, Texture& outTex);
	void CreateDeferredTextures(Context& context);
	void UpdateTexture(Texture& tex, const std::byte* data, Context& context);
	void ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);
	void GetDrawTextureFullname(const char* name, char* dest, int destSize) const;

	/* Buffer */
	ComPtr<ID3D12Resource> CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, Context& context);
	ComPtr<ID3D12Resource> CreateUploadHeapBuffer(UINT64 byteSize) const;
	void UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const;
	void UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args);

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Factory functionality */
	DynamicObjectModel CreateDynamicGraphicObjectFromGLModel(const model_t* model, Context& context);
	void CreateGraphicalObjectFromGLSurface(const msurface_t& surf, Context& frame);
	void DecomposeGLModelNode(const model_t& model, const mnode_t& node, Context& context);
	Context CreateContext(Frame& frame);

	/* Rendering */
	void Draw(const StaticObject& object, Context& context);
	void DrawIndiced(const StaticObject& object, Context& context);
	void DrawIndiced(const DynamicObject& object, const entity_t& entity, Context& context);
	void DrawStreaming(const FArg::DrawStreaming& args);
	void AddParticleToDrawList(const particle_t& particle, BufferHandler vertexBufferHandler, int vertexBufferOffset);
	void DrawParticleDrawList(BufferHandler vertexBufferHandler, int vertexBufferSizeInBytes, BufferHandler constBufferHandler, Context& context);
	void Draw_Pic(int x, int y, const char* name, const BufferPiece& bufferPiece, Context& context);
	void Draw_Char(int x, int y, int num, const BufferPiece& bufferPiece, Context& context);
	void Draw_RawPic(const DrawCall_StretchRaw& drawCall, const BufferPiece& bufferPiece, Context& context);

	/* Utils */
	void Load8To24Table();
	void ImageBpp8To32(const std::byte* data, int width, int height, unsigned int* out) const;
	void FindImageScaledSizes(int width, int height, int& scaledWidth, int& scaledHeight) const;
	bool IsVisible(const entity_t& entity, const Camera& camera) const;
	DynamicObjectConstBuffer& FindDynamicObjConstBuffer();
	std::vector<int> BuildObjectsInFrustumList(const Camera& camera, const std::vector<Utils::AABB>& objCulling) const;

	/* Job  */
	void EndFrameJob(Context& context);
	void BeginFrameJob(Context& context);
	void DrawUIJob(Context& context);
	void DrawStaticGeometryJob(Context& context);
	void DrawDynamicGeometryJob(Context& context);
	void DrawParticleJob(Context& context);

	/* Passes */
	void ExecuteDrawUIPass(Context& context, const Pass& pass);

	/* Materials */
	Material CompileMaterial(const MaterialSource& materialSourse) const;

	void SetMaterialAsync(const std::string& name, CommandList& commandList);
	void SetNonMaterialState(Context& context) const;

	/* Frames */
	void SubmitFrame(Frame& frame);
	void WaitForFrame(Frame& frame) const;
	void WaitForPrevFrame(Frame& frame) const;

	// Difference between OpenFrame/CloseFrame and BeginFrame/EndFrame is that first one is more generic,
	// means it supposed to be used for anything where you record command list
	// and then submit it. BeginFrame/EndFrame on the other hand is directly related to drawing where you
	// ,for example, have buffer to draw to
	void OpenFrame(Frame& frame) const;
	void CloseFrame(Frame& frame);
	void ReleaseFrameResources(Frame& frame);

	// Frame ownership
	Frame& GetCurrentFrame();
	void AcquireCurrentFrame();
	void DetachCurrentFrame();
	void ReleaseFrame(Frame& frame);

	int GenerateFenceValue();
	int GetFenceValue() const;

	HWND		m_hWindows = nullptr;

	refimport_t m_refImport;

	ComPtr<IDXGISwapChain> m_swapChain;

	AssertBufferAndView m_swapChainBuffersAndViews[Settings::SWAP_CHAIN_BUFFER_COUNT];

	ComPtr<ID3D12CommandQueue>		  m_commandQueue;
	
	ComPtr<ID3D12DescriptorHeap>	  m_samplerHeap;

	// I need enforce alignment on this buffer, because I allocate constant buffers from it. 
	// I don't want to create separate buffer just for constant buffers, because that would increase complexity
	// without real need to do this. I still try to explicitly indicate places where I actually need alignment
	// in case I would decide to refactor this into separate buffer
	HandlerBuffer<Settings::UPLOAD_MEMORY_BUFFER_SIZE, 
		Settings::UPLOAD_MEMORY_BUFFER_HANDLERS_NUM, 
		Settings::CONST_BUFFER_ALIGNMENT> m_uploadMemoryBuffer;
	HandlerBuffer<Settings::DEFAULT_MEMORY_BUFFER_SIZE, Settings::DEFAULT_MEMORY_BUFFER_HANDLERS_NUM> m_defaultMemoryBuffer;
	
	CommandListBuffer<Settings::COMMAND_LISTS_NUM> m_commandListBuffer;

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

	LockUnorderedMap_t<std::string, Texture> m_textures;
	// If we want to delete resource we can't do this right away cause there is a high chance that this 
	// resource will still be in use, so we just put it here and delete it later 
	LockVector_t<ComPtr<ID3D12Resource>> m_resourcesToDelete;

	std::array<unsigned int, 256> m_8To24Table;
	std::array<unsigned int, 256> m_rawPalette;

	// Should I separate UI from game object? Damn, is this NWN speaks in me
	std::vector<StaticObject> m_staticObjects;
	std::vector<Utils::AABB> m_staticObjectsAABB;

	std::unordered_map<model_t*, DynamicObjectModel> m_dynamicObjectsModels;
	// Expected to set a size for it during initialization. Don't change size afterward
	LockVector_t<DynamicObjectConstBuffer> m_dynamicObjectsConstBuffersPool;

	// DirectX and OpenGL have different directions for Y axis,
	// this matrix is required to fix this. Also apparently original Quake 2
	// had origin in a middle of a screen, while we have it in upper left corner,
	// so we need to center content to the screen center
	XMFLOAT4X4 m_yInverseAndCenterMatrix;

	std::vector<Material> m_materials;

	JobSystem m_jobSystem;

	std::array<Frame, Settings::FRAMES_NUM> m_frames;
	int m_currentFrameIndex = Const::INVALID_INDEX;

	std::atomic<int>	m_fenceValue = 0;
	ComPtr<ID3D12Fence>	m_fence;

	int m_frameCounter = 0;

	/* Level registration data */
	std::unique_ptr<Context> m_staticModelRegContext;
	std::unique_ptr<Context> m_dynamicModelRegContext;
};