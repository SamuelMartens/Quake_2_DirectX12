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
#include "dx_threadingutils.h"
#include "dx_frame.h"
#include "dx_descriptorheapallocator.h"
#include "dx_commandlist.h"
#include "dx_pass.h"
#include "dx_utils.h"
#include "dx_bsp.h"
#include "dx_light.h"

extern "C"
{
	#include "../client/ref.h"
};

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
		GPUJobContext* context;
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

	const refimport_t& GetRefImport() const { return refImport; };
	void SetRefImport(refimport_t RefImport) { refImport = RefImport; };

	/*--- Buffers management --- */
	void DeleteDefaultMemoryBuffer(BufferHandler handler);
	void DeleteUploadMemoryBuffer(BufferHandler handler);
	
	/*--- API functions begin --- */

	void Init(WNDPROC WindowProc, HINSTANCE hInstance);
	void AddDrawCall_RawPic(int x, int y, int quadWidth, int quadHeight, int textureWidth, int textureHeight, const std::byte* data);
	void AddDrawCall_Pic(int x, int y, const char* name);
	void AddDrawCall_Char(int x, int y, int num);


	void BeginFrame();
	void EndFrame();
	void PreRenderSetUpFrame(Frame& frame);
	void FlushAllFrames() const;

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
	const std::array<unsigned int, 256>& GetTable8To24() const;
	const std::vector<StaticObject>& GetStaticObjects() const;
	const std::unordered_map<model_t*, DynamicObjectModel>& GetDynamicModels() const;

	void Load8To24Table();
	void ImageBpp8To32(const std::byte* data, int width, int height, unsigned int* out) const;


	/* Initialization and creation */
	void CreateFences(ComPtr<ID3D12Fence>& fence);
	void CreateDepthStencilBuffer(ComPtr<ID3D12Resource>& buffer);
	int  GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const;

	void RebuildFrameGraph();

	ID3D12DescriptorHeap* GetRtvHeap();
	ID3D12DescriptorHeap* GetDsvHeap();
	ID3D12DescriptorHeap* GetCbvSrvHeap();
	ID3D12DescriptorHeap* GetSamplerHeap();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtvHandleCPU(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetRtvHandleGPU(int index) const;

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsvHandleCPU(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetDsvHandleGPU(int index) const;

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCbvSrvHandleCPU(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetCbvSrvHandleGPU(int index) const;

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetSamplerHandleCPU(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetSamplerHandleGPU(int index) const;

public:

	// Public because it is already wrapped up in class
	std::unique_ptr<SequentialDescriptorHeapAllocator_t<Settings::RTV_DTV_DESCRIPTOR_HEAP_SIZE,
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV>>	rtvHeapAllocator = nullptr;
	
	std::unique_ptr<SequentialDescriptorHeapAllocator_t<Settings::RTV_DTV_DESCRIPTOR_HEAP_SIZE,
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV>>	dsvHeapAllocator = nullptr;
	
	std::unique_ptr<SequentialDescriptorHeapAllocator_t<Settings::CBV_SRV_DESCRIPTOR_HEAP_SIZE,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>> cbvSrvHeapAllocator = nullptr;
	
	std::unique_ptr<SequentialDescriptorHeapAllocator_t<Settings::SAMPLER_DESCRIPTOR_HEAP_SIZE,
		D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER>> samplerHeapAllocator = nullptr;

	//#TODO remove this when FrameGraph is implemented properly
	// DirectX and OpenGL have different directions for Y axis,
	// this matrix is required to fix this. Also apparently original Quake 2
	// had origin in a middle of a screen, while we have it in upper left corner,
	// so we need to center content to the screen center
	XMFLOAT4X4 m_yInverseAndCenterMatrix;

	[[nodiscard]]
	GPUJobContext CreateContext(Frame& frame, bool acquireCommandList = true);

	/* Job  */
	void EndFrameJob(GPUJobContext& context);

	std::vector<int> BuildVisibleDynamicObjectsList(const Camera& camera, const std::vector<entity_t>& entities) const;

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

	void InitDescriptorSizes();

	void CreateDescriptorHeapsAllocators();

	void CreateSwapChain();

	void CheckMSAAQualitySupport();

	void CreateCommandQueue();

	void CreateTextureSampler();

	AssertBufferAndView& GetNextSwapChainBufferAndView();
	
	void PresentAndSwapBuffers(Frame& frame);

	/* Shutdown and clean up Win32 specific stuff */
	void ShutdownWin32();

	/* Geometry loading */
	[[nodiscard]]
	DynamicObjectModel CreateDynamicGraphicObjectFromGLModel(const model_t* model, GPUJobContext& context);
	void CreateGraphicalObjectFromGLSurface(const msurface_t& surf, GPUJobContext& frame);
	void DecomposeGLModelNode(const model_t& model, const mnode_t& node, GPUJobContext& context);

	/* Utils */
	void FindImageScaledSizes(int width, int height, int& scaledWidth, int& scaledHeight) const;
	bool IsVisible(const entity_t& entity, const Camera& camera) const;
	void RegisterObjectsAtFrameGraphs();

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

	/* Frame ownership */
	// Main thread will get some free frame, and execute everything that can't be done as a job.
	Frame& GetMainThreadFrame();
	void AcquireMainThreadFrame();
	void DetachMainThreadFrame();

	// As soon as main thread is done with some frame it will Detach it and pass along to job system.
	// When all jobs associated with this frame are done, the frame will be released ans later picked up
	// by main thread again
	void ReleaseFrame(Frame& frame);

	int GenerateFenceValue();
	int GetFenceValue() const;

	HWND		hWindows = nullptr;

	refimport_t refImport;

	ComPtr<IDXGISwapChain> swapChain;

	AssertBufferAndView swapChainBuffersAndViews[Settings::SWAP_CHAIN_BUFFER_COUNT];

	ComPtr<ID3D12CommandQueue>	commandQueue;
	//#TODO during building of frame graph I can ge exactly how much command lists I need
	CommandListBuffer<Settings::COMMAND_LISTS_NUM> commandListBuffer;

	tagRECT scissorRect;

	INT	currentBackBuffer = 0;

	UINT MSQualityLevels = 0;

	std::array<unsigned int, 256> Table8To24;
	std::array<unsigned int, 256> rawPalette;

	std::vector<StaticObject> staticObjects;
	std::unordered_map<model_t*, DynamicObjectModel> dynamicObjectsModels;

	std::vector<PointLight> staticPointLights;
	std::vector<SurfaceLight> staticSurfaceLights;

	BSPTree bspTree;

	std::array<Frame, Settings::FRAMES_NUM> frames;
	int currentFrameIndex = Const::INVALID_INDEX;

	std::atomic<int>	fenceValue = 0;
	ComPtr<ID3D12Fence>	fence;

	int frameCounter = 0;

	ComPtr<ID3D12DescriptorHeap> rtvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> dsvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> cbvSrvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> samplerHeap = nullptr;

	int rtvDescriptorSize = Const::INVALID_SIZE;
	int dsvDescriptorSize = Const::INVALID_SIZE;
	int cbvSrvDescriptorSize = Const::INVALID_SIZE;
	int samplerDescriptorSize = Const::INVALID_SIZE;

	/* Level registration data */
	std::unique_ptr<GPUJobContext> staticModelRegContext;
	std::unique_ptr<GPUJobContext> dynamicModelRegContext;
};