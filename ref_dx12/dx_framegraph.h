#pragma once

#include <tuple>
#include <vector>
#include <unordered_map>

#include "dx_pass.h"

// FrameGraph is a functional part of the frame. 
// If a Frame is an object, FrameGraph is a Function that accept that object as argument
class FrameGraphSource
{
public:

	struct Pass
	{
		std::string name;
	};

	struct FixedFunctionCopy
	{
		std::string source;
		std::string destination;
	};

	using Step_t = std::variant<
		Pass,
		FixedFunctionCopy
	>;

	struct FrameGraphResourceDecl
	{
		std::string name;
		D3D12_RESOURCE_DESC desc;
		std::optional<XMFLOAT4> clearValue;
	};


	FrameGraphSource() = default;

	FrameGraphSource(const FrameGraphSource&) = default;
	FrameGraphSource& operator=(const FrameGraphSource&) = default;

	FrameGraphSource(FrameGraphSource&&) = default;
	FrameGraphSource& operator=(FrameGraphSource&&) = default;

	~FrameGraphSource() = default;
	
	std::vector<Step_t> steps;

	std::vector<FrameGraphResourceDecl> resourceDeclarations;

	std::vector<PassParametersSource> passesParametersSources;
};

class FrameGraph
{
	friend class FrameGraphBuilder;

public:

	static const int SINGLE_PARTICLE_SIZE = sizeof(ShDef::Vert::PosCol);
	
	using PerObjectGlobalTemplate_t = std::tuple<
		// Static
		std::vector<RootArg::Arg_t>,
		// Dynamic
		std::vector<RootArg::Arg_t>,
		// Particles
		std::vector<RootArg::Arg_t>,
		// UI
		std::vector<RootArg::Arg_t>,
		// Post process
		std::vector<RootArg::Arg_t>,
		// Debug
		std::vector<RootArg::Arg_t>
	>;



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
	void BindComputePassGlobalRes(const std::vector<int>& resIndices, CommandList& commandList) const;

	template<Parsing::PassInputType INPUT_TYPE>
	void BindObjGlobalRes(const std::vector<int>& resIndices, int objIndex, CommandList& commandList) const
	{
		const std::vector<RootArg::Arg_t>& objRes = std::get<static_cast<int>(INPUT_TYPE)>(objGlobalRes)[objIndex];

		for (const int index : resIndices)
		{
			RootArg::Bind(objRes[index], commandList);
		}
	};


	/* Persistent objects registration */
	void RegisterObjects(const std::vector<StaticObject>& objects, GPUJobContext& context);
	
	void UpdateGlobalResources(GPUJobContext& context);
	void ReleasePerFrameResources(Frame& frame);

	/* Utils */
	BufferHandler GetParticlesVertexMemory() const;
	std::vector<ResourceProxy> GetTextureProxy() const;
	void AddTexturesProxiesToPassJobContexts(std::vector<GPUJobContext>& jobContexts) const;

private:

	// Per frame objects registration
	void RegisterGlobalObjectsResUI(GPUJobContext& context);
	void UpdateGlobalObjectsResUI(GPUJobContext& context);

	void UpdateGlobalObjectsResStatic(GPUJobContext& context);

	void RegisterGlobalObjectsResDynamicEntities(GPUJobContext& context);
	void UpdateGlobalObjectsResDynamic(GPUJobContext& context);

	void RegisterParticles(GPUJobContext& context);

	void RegisterGlobalObjectsResDebug(GPUJobContext& context);
	void UpdateGlobalObjectsResDebug(GPUJobContext& context);

	void RegisterGlobaPasslRes(GPUJobContext& context);
	void UpdateGlobalPasslRes(GPUJobContext& context);

	// Frame also keeps data. Try to put only stuff directly related to passes here.
	// Everything else should go to Frame
	std::vector<PassTask> passTasks;

	std::vector<RootArg::Arg_t> passesGlobalRes;

	// Template of all global resources for object. Combined when global resources from different pases are mixed 
	PerObjectGlobalTemplate_t objGlobalResTemplate;

	std::tuple<
	// Static
	std::vector<std::vector<RootArg::Arg_t>>, 
	// Dynamic
	std::vector<std::vector<RootArg::Arg_t>>,
	// Particles
	std::vector<std::vector<RootArg::Arg_t>>,
	// UI
	std::vector<std::vector<RootArg::Arg_t>>,
	// Post process
	std::vector<RootArg::Arg_t>,
	// Debug
	std::vector<std::vector<RootArg::Arg_t>>
	> objGlobalRes;


	std::array<BufferHandler,
		static_cast<int>(Parsing::PassInputType::SIZE)> objGlobalResMemory;

	// Size for one object
	std::array<int,
		static_cast<int>(Parsing::PassInputType::SIZE)> perObjectGlobalMemorySize;

	int passGlobalMemorySize = 0;

	BufferHandler passGlobalMemory = Const::INVALID_BUFFER_HANDLER;

	BufferHandler particlesVertexMemory = Const::INVALID_BUFFER_HANDLER;

	// This is shared pointer to imply that all framegraphs share this resource
	std::shared_ptr<std::vector<std::string>> internalTextureNames;

	// This data is copied whenever job needs texture proxy list.
	// Once created, don't change this
	std::vector<ResourceProxy> internalTextureProxy;

	//#TODO delete when proper runtime load is developed.
	// this is temp hack
	bool isInitalized = false;

};