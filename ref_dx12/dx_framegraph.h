#pragma once

#include "dx_pass.h"

// FrameGraph is a functional part of the frame. 
// If a Frame is an object, FrameGraph is a Function that accept that object as argument
class FrameGraphSource
{
public:

	FrameGraphSource() = default;

	FrameGraphSource(const FrameGraphSource&) = default;
	FrameGraphSource& operator=(const FrameGraphSource&) = default;

	FrameGraphSource(FrameGraphSource&&) = default;
	FrameGraphSource& operator=(FrameGraphSource&&) = default;

	~FrameGraphSource() = default;

	std::vector<std::string> passes;
	std::vector<PassParametersSource> passesParametersSources;
};

class Frame;

class FrameGraph
{
public:

	FrameGraph() = default;

	FrameGraph(const FrameGraph&) = default;
	FrameGraph& operator=(const FrameGraph&) = default;

	FrameGraph(FrameGraph&&) = default;
	FrameGraph& operator=(FrameGraph&&) = default;

	~FrameGraph();

	void Execute(Frame& frame);

	void Init(GPUJobContext& context);

	//#DEBUG shares Context with BeginFrameJob()
	// should unite this functions in one job
	void BeginFrame(GPUJobContext& context);

	void EndFrame(GPUJobContext& context);
	

	// Frame also keeps data. Try to put only stuff directly related to passes here.
	// Everything else should go to Frame
	//#TODO in all names here rename perObject to Object, and perPass to Pass
	// leave how it is only where it is only for one object
	std::vector<Pass_t> passes;

	std::vector<RootArg::Arg_t> passesGlobalRes;
	
	std::array<std::vector<RootArg::Arg_t>,
	static_cast<int>(PassParametersSource::InputType::SIZE)> objGlobalResTemplate;

	std::array<std::vector<std::vector<RootArg::Arg_t>>,
	static_cast<int>(PassParametersSource::InputType::SIZE)> objGlobalRes;

	std::array<BufferHandler,
		static_cast<int>(PassParametersSource::InputType::SIZE)> objGlobalResMemory;

	int passGlobalMemorySize = 0;
	// Size for one UI object
	int perObjectGlobalMemoryUISize = 0;

	BufferHandler passGlobalMemory = Const::INVALID_BUFFER_HANDLER;

	//#DEBUG delete when proper runtime load is developed.
	// this is temp hack
	bool isInitalized = false;

private:
	void RegisterGlobalObjectsResUI(GPUJobContext& context);
	void UpdateGlobalObjectsResUI(GPUJobContext& context);
	
	void RegisterGlobaPasslRes(GPUJobContext& context);
	void UpdateGlobalPasslRes(GPUJobContext& context);

};