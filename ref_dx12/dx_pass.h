#pragma once

#include <functional>
#include <vector>
#include <variant>
#include <unordered_map>
#include <string_view>

#include "dx_buffer.h"
#include "dx_drawcalls.h"
#include "dx_common.h"
#include "dx_passparameters.h"
#include "dx_threadingutils.h"
#include "dx_objects.h"
#include "dx_glmodel.h"

// NOTE: if you want do add a new type of pass, here is your checklist
// Keep in mind that each pass type corresponds to certain input type
// 
// 1) Add new input type to pass grammar in Pass.grammar
// 2) Add pass creation code for that new input type inside dx_framegraphbuilder.cpp
// 3) Create new pass type in dx_pass.cpp
// 4) Add global resources for the new pass type inside FrameGraph class
// 5) Implement RegisterObject and UpdateObject functions for that pass resources
//		inside FrameGraph
// 7) Implement callbacks for resource updates (both local to pass and global) inside
//		dx_rendercallbacks.h
// 6) (OPTIONAL) Add object of appropriate type to dx_objects.h
// 
// Also be aware, that most of the passes don't handle resource management automatically,
// so you need to make a great care ensuring that non of resource is leaking. Don't 
// forget that beside pass local resources there is also global resources (inside FrameGraph)
// which need to be released too at some point.

// NOTE: passes are actually don't release their resources automatically,
// it is done manually via ReleaseResources functions. It is fine as long as 
// they live inside FrameGraph. Be careful putting them in any other place

//#TODO
// 1) Implement proper tex samplers handling. When I need more samplers
// 2) Implement root constants
// 3) Implement const buffers in descriptor table 
// 4) Do command list creation
class Pass_UI
{
	friend class PassUtils;
	friend class FrameGraphBuilder;

public:
	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		const DrawCall_UI_t* originalObj = nullptr;
	};

public:

	Pass_UI() = default;
	Pass_UI(const Pass_UI&) = default;
	Pass_UI& operator=(const Pass_UI&) = default;
	Pass_UI(Pass_UI&&) = default;
	Pass_UI& operator=(Pass_UI&&) = default;
	~Pass_UI();

	void Execute(GPUJobContext& context);
	void Init();

	void RegisterPassResources(GPUJobContext& context);
	
	void ReleasePerFrameResources(Frame& frame);
	void ReleasePersistentResources();

private:

	void UpdatePassResources(GPUJobContext& context);

	void RegisterObjects(GPUJobContext& context);
	void UpdateDrawObjects(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);
	void Draw(GPUJobContext& context);

private:

	PassParameters passParameters;

	int perObjectConstBuffMemorySize = Const::INVALID_SIZE;
	// One object vertex memory size
	int perObjectVertexMemorySize = Const::INVALID_SIZE;
	// One vertex size
	int perVertexMemorySize = Const::INVALID_SIZE;

	// Pass local args memory size
	int passMemorySize = Const::INVALID_SIZE;

	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	BufferHandler objectConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	BufferHandler vertexMemory = Const::INVALID_BUFFER_HANDLER;

	std::vector<PassObj> drawObjects;

};

class Pass_Static
{
	friend class PassUtils;
	friend class FrameGraphBuilder;

public:

	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		BufferHandler constantBuffMemory = Const::INVALID_BUFFER_HANDLER;
		const StaticObject* originalObj = nullptr;

		void ReleaseResources();
	};

public:

	Pass_Static() = default;
	Pass_Static(const Pass_Static&) = default;
	Pass_Static& operator=(const Pass_Static&) = default;
	Pass_Static(Pass_Static&&) = default;
	Pass_Static& operator=(Pass_Static&&) = default;
	~Pass_Static();

	void Execute(GPUJobContext& context);
	void Init();

	void RegisterPassResources(GPUJobContext& context);
	
	void ReleasePerFrameResources(Frame& frame);
	void ReleasePersistentResources();

	void RegisterObjects(const std::vector<StaticObject>& objects, GPUJobContext& context);

private:

	void UpdatePassResources(GPUJobContext& context);

	void UpdateDrawObjects(GPUJobContext& context);
	void Draw(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = Const::INVALID_SIZE;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;

	int perObjectConstBuffMemorySize = Const::INVALID_SIZE;

	std::vector<PassObj> drawObjects;

};

class Pass_Dynamic
{
	friend class PassUtils;
	friend class FrameGraphBuilder;

public:

	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		const entity_t* originalObj = nullptr;
	};

public:

	Pass_Dynamic() = default;
	Pass_Dynamic(const Pass_Dynamic&) = default;
	Pass_Dynamic& operator=(const Pass_Dynamic&) = default;
	Pass_Dynamic(Pass_Dynamic&&) = default;
	Pass_Dynamic& operator=(Pass_Dynamic&&) = default;
	~Pass_Dynamic();

	void Execute(GPUJobContext& context);
	void Init();

	void RegisterPassResources(GPUJobContext& context);

	void ReleasePerFrameResources(Frame& frame);
	void ReleasePersistentResources();

private:

	void RegisterEntities(GPUJobContext& context);
	void UpdateDrawEntities(GPUJobContext& context);
	
	void UpdatePassResources(GPUJobContext& context);

	void Draw(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = Const::INVALID_SIZE;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;

	int perObjectConstBuffMemorySize = Const::INVALID_SIZE;

	// Recreated every frame
	std::vector<PassObj> drawObjects;
	BufferHandler objectsConstBufferMemory = Const::INVALID_BUFFER_HANDLER;
};

class Pass_Particles
{
	friend class PassUtils;
	friend class FrameGraphBuilder;

public:

	Pass_Particles() = default;
	Pass_Particles(const Pass_Particles&) = default;
	Pass_Particles& operator=(const Pass_Particles&) = default;
	Pass_Particles(Pass_Particles&&) = default;
	Pass_Particles& operator=(Pass_Particles&&) = default;
	~Pass_Particles();

	void Execute(GPUJobContext& context);
	void Init();

	void RegisterPassResources(GPUJobContext& context);

	void ReleasePerFrameResources(Frame& frame);
	void ReleasePersistentResources();

private:

	void UpdatePassResources(GPUJobContext& context);

	void Draw(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = Const::INVALID_SIZE;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
};

class Pass_PostProcess
{
	friend class PassUtils;
	friend class FrameGraphBuilder;

public:

	Pass_PostProcess() = default;
	Pass_PostProcess(const Pass_PostProcess&) = default;
	Pass_PostProcess& operator=(const Pass_PostProcess&) = default;
	Pass_PostProcess(Pass_PostProcess&&) = default;
	Pass_PostProcess& operator=(Pass_PostProcess&&) = default;
	~Pass_PostProcess();

	void Execute(GPUJobContext& context);
	void Init();

	void RegisterPassResources(GPUJobContext& context);

	void ReleasePerFrameResources(Frame& frame);
	void ReleasePersistentResources();

private:

	void UpdatePassResources(GPUJobContext& context);

	void Dispatch(GPUJobContext& context);

	void SetComputeState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = Const::INVALID_SIZE;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;

};

class Pass_Debug
{
	friend class PassUtils;
	friend class FrameGraphBuilder;

public:
	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		const DebugObject_t* originalObj = nullptr;

		int vertexBufferSize = Const::INVALID_SIZE;
	};

	// Debug sphere init
	const static float LIGHT_PROBE_SPHERE_RADIUS;
	const static int LIGHT_PROBE_SPHERE_SUBDIVISION = 2;

	const static float POINT_LIGHT_SPHERE_RADIUS;
	const static int POINT_LIGHT_SPHERE_SUBDIVISION = 1;

	const static int LIGHT_BOUNDING_VOLUME_SUBDIVISION = 2;

public:

	Pass_Debug() = default;
	Pass_Debug(const Pass_Debug&) = default;
	Pass_Debug& operator=(const Pass_Debug&) = default;
	Pass_Debug(Pass_Debug&&) = default;
	Pass_Debug& operator=(Pass_Debug&&) = default;
	~Pass_Debug();

	void Execute(GPUJobContext& context);
	void Init();

	void RegisterPassResources(GPUJobContext& context);
	void UpdatePassResources(GPUJobContext& context);

	void ReleasePerFrameResources(Frame& frame);
	void ReleasePersistentResources();

private:

	void RegisterObjects(GPUJobContext& context);
	void UpdateDrawObjects(GPUJobContext& context);

	void Draw(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = Const::INVALID_SIZE;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;

	int perObjectConstBuffMemorySize = Const::INVALID_SIZE;

	// Size of one vertex
	int perVertexMemorySize = Const::INVALID_SIZE;

	int pathSegmenVertextMemorySize = Const::INVALID_SIZE;
	int lightSampleVertexMemorySize = Const::INVALID_SIZE;

	std::vector<XMFLOAT4> lightProbeDebugObjectVertices;
	std::vector<XMFLOAT4> pointLightDebugObjectVertices;

	// Recreated every frame
	std::vector<PassObj> drawObjects;
	BufferHandler objectsConstBufferMemory = Const::INVALID_BUFFER_HANDLER;
	BufferHandler objectsVertexBufferMemory = Const::INVALID_BUFFER_HANDLER;
};

using Pass_t = std::variant<Pass_UI, Pass_Static, Pass_Dynamic, Pass_Particles, Pass_PostProcess, Pass_Debug>;


struct PassTask
{
	using Callback_t = std::function<void(GPUJobContext&, const Pass_t*)>;

	Pass_t pass;

	std::vector<Callback_t> prePassCompileTimeCallbacks;
	std::vector<Callback_t> postPassCompileTimeCallbacks;
	
	std::vector<Callback_t> prePassRunTimeCallbacks;
	std::vector<Callback_t> postPassRunTimeCallbacks;

	void Execute(GPUJobContext& context);
};

class PassUtils
{
public:
	
	template<typename T>
	static int AllocateRenderTargetView(std::string_view renderTargetName, T& descriptorHeapAllocator)
	{
		DX_ASSERT(renderTargetName.empty() == false && "AllocateRenderTargetView failed. Invalid render target name");

		if (renderTargetName == PassParameters::COLOR_BACK_BUFFER_NAME || renderTargetName == PassParameters::DEPTH_BACK_BUFFER_NAME)
		{
			return Const::INVALID_INDEX;
		}
		
		Resource* tex = ResourceManager::Inst().FindResource(renderTargetName);

		DX_ASSERT(tex != nullptr && "AllocateRenderTargetView failed. No such texture");

		return descriptorHeapAllocator.Allocate(tex->gpuBuffer.Get());
	}

	static void AllocateColorDepthRenderTargetViews(PassParameters& passParams);

	template<typename T>
	static void ReleaseRenderTargetView(std::string_view renderTargetName, int& renderTargetIndex, T& descriptorHeapAllocator)
	{
		if (renderTargetName == PassParameters::COLOR_BACK_BUFFER_NAME || renderTargetName == PassParameters::DEPTH_BACK_BUFFER_NAME)
		{
			DX_ASSERT(renderTargetIndex == Const::INVALID_INDEX && "Render target view index should be clean if it is back buffer");
			return;
		}

		if (renderTargetIndex != Const::INVALID_INDEX)
		{
			descriptorHeapAllocator.Delete(renderTargetIndex);
			renderTargetIndex = Const::INVALID_INDEX;
		}
	}

	static void ReleaseColorDepthRenderTargetViews(PassParameters& passParams);

	template<typename T>
	static std::string_view GetPassName(const T& pass)
	{
		return pass.passParameters.name;
	}

	template<>
	static std::string_view GetPassName<Pass_t>(const Pass_t& pass)
	{
		return std::visit([](auto&& pass)
		{
			return GetPassName(pass);
		}, pass);
	}

	template<typename T>
	static Parsing::PassInputType GetPassInputType(const T& pass)
	{
		return *pass.passParameters.input;
	}

	template<>
	static Parsing::PassInputType GetPassInputType<Pass_t>(const Pass_t& pass)
	{
		return std::visit([](auto&& pass)
		{
			return GetPassInputType(pass);
		}, pass);
	}

	static void ClearColorCallback(XMFLOAT4 color, GPUJobContext& context, const Pass_t* pass);

	static void ClearDeptCallback(float value, GPUJobContext& context, const Pass_t* pass);

	static void InternalResourceProxiesToInterPassStateCallback(GPUJobContext& context, const Pass_t* pass);
	static void RenderTargetToRenderStateCallback(GPUJobContext& context, const Pass_t* pass);
	static void DepthTargetToRenderStateCallback(GPUJobContext& context, const Pass_t* pass);

	static void CopyResourceCallback(const std::string sourceName, const std::string destinationName, GPUJobContext& context, const Pass_t* pass);
	static void BackBufferToPresentStateCallback(GPUJobContext& context, const Pass_t* pass);

	static void CreateReabackResourceAndFillItUp(ResourceReadBackRequest request, GPUJobContext& context, const Pass_t* pass);

private:

	static const PassParameters& GetPassParameters(const Pass_t& pass);

};