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

class FrameGraph
{
	friend class FrameGraphBuilder;

public:

	FrameGraph();

	FrameGraph(const FrameGraph&) = default;
	FrameGraph& operator=(const FrameGraph&) = default;

	FrameGraph(FrameGraph&&) = default;
	FrameGraph& operator=(FrameGraph&&) = default;

	~FrameGraph();

	/* Execution func */
	void Execute(class Frame& frame);
	void Init(GPUJobContext& context);

	/* Inner resource management  */
	void BindPassGlobalRes(const std::vector<int>& resIndices, CommandList& commandList) const;
	void BindObjGlobalRes(const std::vector<int>& resIndices, int objIndex, CommandList& commandList, Parsing::PassInputType objType) const;

	void UpdateGlobalResources(GPUJobContext& context);
	void ReleaseResources();


private:

	void RegisterGlobalObjectsResUI(GPUJobContext& context);
	void UpdateGlobalObjectsResUI(GPUJobContext& context);
	
	void RegisterGlobaPasslRes(GPUJobContext& context);
	void UpdateGlobalPasslRes(GPUJobContext& context);

	// Frame also keeps data. Try to put only stuff directly related to passes here.
	// Everything else should go to Frame
	std::vector<Pass_t> passes;

	std::vector<RootArg::Arg_t> passesGlobalRes;

	// Template of all global resources for object. Combined when global resources from different pases are mixed 
	std::array<std::vector<RootArg::Arg_t>,
		static_cast<int>(Parsing::PassInputType::SIZE)> objGlobalResTemplate;

	std::array<std::vector<std::vector<RootArg::Arg_t>>,
		static_cast<int>(Parsing::PassInputType::SIZE)> objGlobalRes;

	std::array<BufferHandler,
		static_cast<int>(Parsing::PassInputType::SIZE)> objGlobalResMemory;

	int passGlobalMemorySize = 0;
	// Size for one UI object
	int perObjectGlobalMemoryUISize = 0;

	BufferHandler passGlobalMemory = Const::INVALID_BUFFER_HANDLER;

	//#TODO delete when proper runtime load is developed.
	// this is temp hack
	bool isInitalized = false;

};