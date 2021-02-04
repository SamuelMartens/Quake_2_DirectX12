#pragma once

#include <functional>
#include <vector>
#include <variant>

#include "dx_buffer.h"
#include "dx_drawcalls.h"
#include "dx_common.h"
#include "dx_passparameters.h"
#include "dx_threadingutils.h"
#include "dx_objects.h"


// NOTE: passes are actually don't release their resources automatically,
// it is done manually via ReleaseResources functions. It is fine as long as 
// they live inside FrameGraph. Be careful putting them in any other place

//#TODO
// 1) Implement proper tex samplers handling. When I need more samplers
// 2) Implement root constants
// 3) Implement const buffers in descriptor table 
// 4) Implement custom render targets and resource creation
// 5) Implement include
// 6) Do proper logging for parsing and execution
// 7) Proper name generation for D3D objects


class Pass_UI
{
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
	void UpdatePassResources(GPUJobContext& context);
	void ReleasePerFrameResources();
	void ReleasePersistentResources();

private:

	void RegisterObjects(GPUJobContext& context);
	void UpdateDrawObjects(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);
	void Draw(GPUJobContext& context);

private:

	PassParameters passParameters;

	unsigned int perObjectConstBuffMemorySize = 0;
	// One object vertex memory size
	unsigned int perObjectVertexMemorySize = 0;
	// One vertex size
	unsigned int perVertexMemorySize = 0;

	// Pass local args memory size
	unsigned int passMemorySize = 0;

	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	BufferHandler objectConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	BufferHandler vertexMemory = Const::INVALID_BUFFER_HANDLER;

	std::vector<PassObj> drawObjects;

};

class Pass_Static
{
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
	void UpdatePassResources(GPUJobContext& context);
	void ReleasePerFrameResources();
	void ReleasePersistentResources();

	void RegisterObjects(const std::vector<StaticObject>& objects, GPUJobContext& context);

private:

	void UpdateDrawObjects(GPUJobContext& context);
	void Draw(GPUJobContext& context);

	void SetRenderState(GPUJobContext& context);

	PassParameters passParameters;

	int passMemorySize = 0;
	BufferHandler passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;

	unsigned int perObjectConstBuffMemorySize = 0;

	std::vector<PassObj> drawObjects;

};

using Pass_t = std::variant<Pass_UI, Pass_Static>;
