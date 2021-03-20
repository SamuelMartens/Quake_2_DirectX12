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

public:
	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		const DrawCall_UI_t* originalObj = nullptr;
	};

public:

	void Execute(GPUJobContext& context);
	void Init(PassParameters&& parameters);

	void RegisterPassResources(GPUJobContext& context);
	
	void ReleasePerFrameResources();
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

public:

	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		BufferHandler constantBuffMemory = Const::INVALID_BUFFER_HANDLER;
		const StaticObject* originalObj = nullptr;

		void ReleaseResources();
	};

public:

	void Execute(GPUJobContext& context);
	void Init(PassParameters&& parameters);

	void RegisterPassResources(GPUJobContext& context);
	
	void ReleasePerFrameResources();
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

public:

	struct PassObj
	{
		std::vector<RootArg::Arg_t> rootArgs;
		const entity_t* originalObj = nullptr;
	};

public:

	void Execute(GPUJobContext& context);
	void Init(PassParameters&& parameters);

	void RegisterPassResources(GPUJobContext& context);

	void ReleasePerFrameResources();
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

public:

	void Execute(GPUJobContext& context);
	void Init(PassParameters&& parameters);

	void RegisterPassResources(GPUJobContext& context);

	void ReleasePerFrameResources();
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

public:

	void Execute(GPUJobContext& context);
	void Init(PassParameters&& parameters);

	void RegisterPassResources(GPUJobContext& context);

	void ReleasePerFrameResources();
	void ReleasePersistentResources();

private:

	void UpdatePassResources(GPUJobContext& context);

	void Dispatch(GPUJobContext& context);

	void SetComputeState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = Const::INVALID_SIZE;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;

};

using Pass_t = std::variant<Pass_UI, Pass_Static, Pass_Dynamic, Pass_Particles, Pass_PostProcess>;


struct PassTask
{
	Pass_t pass;

	std::vector<std::function<void(GPUJobContext&)>> prePassCallbacks;
	std::vector<std::function<void(GPUJobContext&)>> postPassCallbacks;

	void Execute(GPUJobContext& context);
};

class PassUtils
{
public:
	//#DEBUG alloca should be moved to framebuilder
	template<typename T>
	static int AllocateRenderTargetView(std::string_view renderTargetName, T& descriptorHeap)
	{
		assert(renderTargetName.empty() == false && "AllocateRenderTargetView failed. Invalid render target name");

		if (renderTargetName == PassParameters::BACK_BUFFER_NAME)
		{
			return Const::INVALID_INDEX;
		}
		//#DEBUG you allocate this? Make sure it is destroyed
		Texture* tex = ResourceManager::Inst().FindTexture(renderTargetName);

		assert(tex != nullptr && "AllocateRenderTargetView failed. No such texture");

		return descriptorHeap.Allocate(tex->buffer.Get());
	}

	static void AllocateColorDepthRenderTargetViews(PassParameters& passParams);

	template<typename T>
	static void ReleaseRenderTargetView(std::string_view renderTargetName, int& renderTargetIndex, T& descriptorHeap)
	{
		if (renderTargetName == PassParameters::BACK_BUFFER_NAME)
		{
			assert(renderTargetIndex == Const::INVALID_INDEX && "Render target view index should be clean if it is back buffer");
			return;
		}

		if (renderTargetIndex != Const::INVALID_INDEX)
		{
			descriptorHeap.Delete(renderTargetIndex);
			renderTargetIndex = Const::INVALID_INDEX;
		}
	}

	static void ReleaseColorDepthRenderTargetViews(PassParameters& passParams);

	template<typename T>
	static const std::string_view GetPassRenderTargetName(const T& pass)
	{
		return pass.passParameters.colorTargetName;
	}

	template<>
	static const std::string_view GetPassRenderTargetName<Pass_t>(const Pass_t& pass)
	{
		return std::visit([](auto&& pass)
		{
			return GetPassRenderTargetName(pass);
		}, pass);
	}

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

	//#DEBUG callbacks like ClearRenderTarget and An render target to render state are spread 
	// across entire codebase. I need to bring this stuff together if possible
	//#DEBUG should I put these callbacks in a separate namespace?
	static void ClearColorBackBufferCallback(XMFLOAT4 color, GPUJobContext& context);
	static void ClearColorCallback(XMFLOAT4 color, int colorRenderTargetViewIndex, GPUJobContext& context);

	static void ClearDepthBackBufferCallback(float value, GPUJobContext& context);
	static void ClearDeptCallback(float value, int depthRenderTargetViewIndex, GPUJobContext& context);

	static void InternalTextureProxiesToInterPassStateCallback(GPUJobContext& context);
	static void RenderTargetToRenderStateCallback(const std::string renderTargetName, GPUJobContext& context);
	static void CopyTextureCallback(const std::string sourceName, const std::string destinationName, GPUJobContext& context);
};