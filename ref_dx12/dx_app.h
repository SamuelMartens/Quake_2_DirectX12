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
#include "dx_threadingutils.h"
#include "dx_frame.h"
#include "dx_descriptorheap.h"
#include "dx_commandlist.h"
#include "dx_pass.h"
#include "dx_utils.h"

extern "C"
{
	#include "../client/ref.h"
};

// Order is extremely important. Consider construction/destruction order
// when adding something
#define JOB_GUARD( context ) \
	DependenciesRAIIGuard_t dependenciesGuard(context); \
	CommandListRAIIGuard_t commandListGuard(context.commandList)

namespace FArg 
{

	struct DrawStreaming
	{
		const std::byte* vertices = nullptr;
		int verticesSizeInBytes = -1;
		int verticesStride = -1;
		const char* texName = nullptr;
		const XMFLOAT4* pos;
		const BufferPiece* bufferPiece;
		JobContext* context;
	};
};


//#TODO 
// 2) Make your wrappers as exclusive owners of some resource, and operate with smart pointers instead to avoid mess
//    during resource management.(This requires rewrite some stuff like Textures or buffers)
// 3) For Movies and UI we don't need stream drawing, but just one quad object  and the width and height would be
//	  scaling of this quad along y or x axis
// 4) If decided to go with preallocated GPU buffers I should come up with some way to control buffers states
//	  and actively avoid redundant state transitions
// 5) Implement occlusion query. If this is not enough, resurrect BSP tree.
class Renderer
{
	DEFINE_SINGLETON(Renderer);

	const refimport_t& GetRefImport() const { return m_refImport; };
	void SetRefImport(refimport_t RefImport) { m_refImport = RefImport; };

	/*--- Buffers management --- */
	void DeleteDefaultMemoryBuffer(BufferHandler handler);
	void DeleteUploadMemoryBuffer(BufferHandler handler);
	
	void UpdateStreamingConstantBuffer(XMFLOAT4 position, XMFLOAT4 scale, BufferPiece bufferPiece, JobContext& context);
	void UpdateStaticObjectConstantBuffer(const StaticObject& obj, JobContext& context);
	void UpdateDynamicObjectConstantBuffer(DynamicObject& obj, const entity_t& entity, JobContext& context);
	BufferHandler UpdateParticleConstantBuffer(JobContext& context);

	/*--- API functions begin --- */

	void Init(WNDPROC WindowProc, HINSTANCE hInstance);
	void AddDrawCall_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data);
	void AddDrawCall_Pic(int x, int y, const char* name);
	void AddDrawCall_Char(int x, int y, int num);


	void BeginFrame();
	//#TODO delete regualr EndFrame
	void EndFrame();
	void EndFrame_Material();

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
	const std::array<unsigned int, 256>& GetRawPalette() const;
	std::unique_ptr<DescriptorHeap>& GetDescTableHeap(const RootArg::DescTable& descTable);

	void Load8To24Table();
	void ImageBpp8To32(const std::byte* data, int width, int height, unsigned int* out) const;


	/* Initialization and creation */
	void CreateFences(ComPtr<ID3D12Fence>& fence);
	void CreateDepthStencilBuffer(ComPtr<ID3D12Resource>& buffer);
	int  GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const;

public:

	// Public because it is already wrapped up in class
	std::unique_ptr<DescriptorHeap>	rtvHeap = nullptr;
	std::unique_ptr<DescriptorHeap>	dsvHeap = nullptr;
	std::unique_ptr<DescriptorHeap> cbvSrvHeap = nullptr;
	std::unique_ptr<DescriptorHeap> samplerHeap = nullptr;

	//#TODO remove this when FrameGraph is implemented properly
	// DirectX and OpenGL have different directions for Y axis,
	// this matrix is required to fix this. Also apparently original Quake 2
	// had origin in a middle of a screen, while we have it in upper left corner,
	// so we need to center content to the screen center
	XMFLOAT4X4 m_yInverseAndCenterMatrix;

	//#TODO should belong to job System
	JobContext CreateContext(Frame& frame);

	/* Job  */
	void EndFrameJob(JobContext& context);
	void BeginFrameJob(JobContext& context);
	void DrawUIJob(JobContext& context);
	void DrawStaticGeometryJob(JobContext& context);
	void DrawDynamicGeometryJob(JobContext& context);
	void DrawParticleJob(JobContext& context);

private:

	/* Initialize win32 specific stuff */
	void InitWin32(WNDPROC WindowProc, HINSTANCE hInstance);
	/* Initialize DirectX stuff */
	void InitDx();

	void EnableDebugLayer();
	void SetDebugMessageFilter();

	void InitUtils();

	void InitScissorRect();

	void InitFrames();

	void InitCommandListsBuffer();

	void CreateSwapChainBuffersAndViews();

	void CreateDescriptorHeaps();

	void CreateSwapChain();

	void CheckMSAAQualitySupport();

	void CreateCommandQueue();

	void CreateCompiledMaterials();

	void CreateTextureSampler();

	ComPtr<ID3DBlob> LoadCompiledShader(const std::string& filename) const;
	ComPtr<ID3D12RootSignature> SerializeAndCreateRootSigFromRootDesc(const CD3DX12_ROOT_SIGNATURE_DESC& rootSigDesc) const;

	AssertBufferAndView& GetNextSwapChainBufferAndView();
	
	void PresentAndSwapBuffers(Frame& frame);

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Factory functionality */
	DynamicObjectModel CreateDynamicGraphicObjectFromGLModel(const model_t* model, JobContext& context);
	void CreateGraphicalObjectFromGLSurface(const msurface_t& surf, JobContext& frame);
	void DecomposeGLModelNode(const model_t& model, const mnode_t& node, JobContext& context);
	

	/* Rendering */
	void Draw(const StaticObject& object, JobContext& context);
	void DrawIndiced(const StaticObject& object, JobContext& context);
	void DrawIndiced(const DynamicObject& object, const entity_t& entity, JobContext& context);
	void DrawStreaming(const FArg::DrawStreaming& args);
	void AddParticleToDrawList(const particle_t& particle, BufferHandler vertexBufferHandler, int vertexBufferOffset);
	void DrawParticleDrawList(BufferHandler vertexBufferHandler, int vertexBufferSizeInBytes, BufferHandler constBufferHandler, JobContext& context);
	void Draw_Pic(int x, int y, const char* name, const BufferPiece& bufferPiece, JobContext& context);
	void Draw_Char(int x, int y, int num, const BufferPiece& bufferPiece, JobContext& context);
	void Draw_RawPic(const DrawCall_StretchRaw& drawCall, const BufferPiece& bufferPiece, JobContext& context);

	/* Utils */
	void FindImageScaledSizes(int width, int height, int& scaledWidth, int& scaledHeight) const;
	bool IsVisible(const entity_t& entity, const Camera& camera) const;
	DynamicObjectConstBuffer& FindDynamicObjConstBuffer();
	std::vector<int> BuildObjectsInFrustumList(const Camera& camera, const std::vector<Utils::AABB>& objCulling) const;

	/* Passes */
	void ExecuteDrawUIPass(JobContext& context, const PassParameters& pass);

	/* Materials */
	Material CompileMaterial(const MaterialSource& materialSourse) const;

	void SetMaterialAsync(const std::string& name, CommandList& commandList);
	void SetNonMaterialState(JobContext& context) const;

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
	
	CommandListBuffer<Settings::COMMAND_LISTS_NUM> m_commandListBuffer;

	tagRECT		   m_scissorRect;

	INT	m_currentBackBuffer = 0;

	UINT m_MSQualityLevels = 0;

	std::array<unsigned int, 256> m_8To24Table;
	std::array<unsigned int, 256> m_rawPalette;

	// Should I separate UI from game object? Damn, is this NWN speaks in me
	std::vector<StaticObject> m_staticObjects;
	std::vector<Utils::AABB> m_staticObjectsAABB;

	std::unordered_map<model_t*, DynamicObjectModel> m_dynamicObjectsModels;
	// Expected to set a size for it during initialization. Don't change size afterward
	LockVector_t<DynamicObjectConstBuffer> m_dynamicObjectsConstBuffersPool;

	std::vector<Material> m_materials;

	std::array<Frame, Settings::FRAMES_NUM> m_frames;
	int m_currentFrameIndex = Const::INVALID_INDEX;

	std::atomic<int>	m_fenceValue = 0;
	ComPtr<ID3D12Fence>	m_fence;

	int m_frameCounter = 0;

	/* Level registration data */
	std::unique_ptr<JobContext> m_staticModelRegContext;
	std::unique_ptr<JobContext> m_dynamicModelRegContext;
};