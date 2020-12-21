#pragma once

#include <functional>
#include <vector>
#include <variant>

#include "dx_buffer.h"
#include "dx_materialcompiler.h"
#include "dx_jobmultithreading.h"
#include "dx_drawcalls.h"
#include "dx_common.h"
#include "dx_frame.h"

//#TODO
// 1) Implement proper tex samplers handling. When I need more samplers
// 2) Implement root constants
// 3) Implement const buffers in descriptor table 
// 4) Implement custom render targets and resource creation
// 5) Implement include
// 6) Do proper logging for parsing and execution

//#INFO every frame should have stages collection. In this way I will not need to worry about multithreading handle inside stage itself
class RenderStage_UI
{
public:
	struct StageObj
	{
		std::vector<RootArg_t> rootArgs;
		const DrawCall_UI_t& originalDrawCall;
	};

public:

	void Execute(Context& context);
	void Init();

	//#TODO shouldn't be public
	Pass pass;

private:

	void Start(Context& jobCtx);
	void UpdateDrawObjects(Context& jobCtx);

	void SetUpRenderState(Context& jobCtx);
	void Draw(Context& jobCtx);
	void Finish();

private:

	unsigned int perObjectConstBuffMemorySize = 0;
	unsigned int perObjectVertexMemorySize = 0;
	unsigned int perVertexMemorySize = 0;

	BufferHandler constBuffMemory = BuffConst::INVALID_BUFFER_HANDLER;
	BufferHandler vertexMemory = BuffConst::INVALID_BUFFER_HANDLER;

	std::vector<StageObj> drawObjects;
};

using RenderStage_t = std::variant<RenderStage_UI>;

//#TODO this might go into separate file /or move thisto dx_frame? 
class FrameGraph
{
public:

	void Execute(Frame& frame);

	/* Initialization */
	void BuildFrameGraph(PassMaterial&& passMaterial);

	std::vector<RenderStage_t> stages;
private:

	void InitRenderStage(Pass&& pass, RenderStage_t& renderStage);

};