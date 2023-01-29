#include "dx_framegraph.h"

#include "dx_jobmultithreading.h"
#include "dx_rendercallbacks.h"
#include "dx_framegraphbuilder.h"

namespace
{
	template<
		typename objGlobalResTemplateT,
		typename objGlobalResT,
		typename objGlobalResMemoryT,
		typename perObjectGlobalMemorySizeT>
		struct ResContext
	{
		ResContext(const objGlobalResTemplateT& resTemplate,
			objGlobalResT& globalRes,
			objGlobalResMemoryT& globalResMemory,
			perObjectGlobalMemorySizeT& objectGlobalMemorySize) :
			objGlobalResTemplate(resTemplate),
			objGlobalRes(globalRes),
			objGlobalResMemory(globalResMemory),
			perObjectGlobalMemorySize(objectGlobalMemorySize)
		{};

		const objGlobalResTemplateT&  objGlobalResTemplate;
		objGlobalResT& objGlobalRes;
		objGlobalResMemoryT& objGlobalResMemory;
		perObjectGlobalMemorySizeT& perObjectGlobalMemorySize;
	};

	[[nodiscard]]
	RenderCallbacks::UpdateGlobalObjectContext ConstructGlobalObjectContext(GPUJobContext& context)
	{
		Frame& frame = context.frame;

		XMMATRIX sseViewProj = XMLoadFloat4x4(&frame.uiYInverseAndCenterMat) *
			XMLoadFloat4x4(&frame.uiViewMat) *
			XMLoadFloat4x4(&frame.uiProjectionMat);

		XMFLOAT4X4 viewProj;
		XMStoreFloat4x4(&viewProj, sseViewProj);

		return RenderCallbacks::UpdateGlobalObjectContext{ viewProj, context };
	}


	template<Parsing::PassInputType INPUT_TYPE, typename T, typename ResContextT>
	void _UpdateGlobalObjectsRes(const std::vector<T>& objects, GPUJobContext& context, ResContextT resContext)
	{
		if (objects.empty() == true)
		{
			return;
		}

		RenderCallbacks::UpdateGlobalObjectContext updateContext = ConstructGlobalObjectContext(context);

		std::vector<std::vector<RootArg::Arg_t>>& objGlobalRes =  std::get<static_cast<int>(INPUT_TYPE)>(resContext.objGlobalRes);

		const int perObjectGlobalMemorySize = resContext.perObjectGlobalMemorySize[static_cast<int>(INPUT_TYPE)];

		std::vector<std::byte> cpuMem(perObjectGlobalMemorySize * objects.size(), static_cast<std::byte>(0));

		for (int i = 0; i < objects.size(); ++i)
		{
			const T& object = objects[i];

			for (RootArg::Arg_t& arg : objGlobalRes[i])
			{
				std::visit([&updateContext, &object, &cpuMem](auto&& arg)
				{
					using T = std::decay_t<decltype(arg)>;

					if constexpr (std::is_same_v<T, RootArg::RootConstant>)
					{
						DX_ASSERT(false && "Root constant is not implemented");
					};

					if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
					{
						// Start of the memory of the current buffer
						int fieldOffset = arg.gpuMem.offset;

						for (RootArg::ConstBuffField& field : arg.content)
						{
							RenderCallbacks::UpdateGlobalObject(
								field.hashedName,
								object,
								cpuMem[fieldOffset],
								updateContext);


							// Proceed to next buffer
							fieldOffset += field.size;
						}
					}

					if constexpr (std::is_same_v<T, RootArg::StructuredBufferView>)
					{
						DX_ASSERT(arg.buffer != nullptr && "Structured buffer pointed for root arg resource should be initialized");

						RenderCallbacks::UpdateGlobalObject(
							arg.hashedName,
							object,
							*arg.buffer,
							updateContext);
					}

					if constexpr (std::is_same_v<T, RootArg::DescTable>)
					{
						for (RootArg::DescTableEntity_t& descTableEntity : arg.content)
						{
							std::visit([](auto&& descTableEntity)
							{
								using T = std::decay_t<decltype(descTableEntity)>;

								if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
								{
									DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
								}
							}, descTableEntity);
						}
					}

				}, arg);
			}
		}

		if (perObjectGlobalMemorySize != 0)
		{
			auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

			BufferHandler objGlobalResMemory = resContext.objGlobalResMemory[static_cast<int>(INPUT_TYPE)];

			FArg::UpdateUploadHeapBuff updateConstBufferArgs;
			updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
			updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(objGlobalResMemory);
			updateConstBufferArgs.data = cpuMem.data();
			updateConstBufferArgs.byteSize = cpuMem.size();
			updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

			ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
		}
	}

	template<Parsing::PassInputType INPUT_TYPE, typename T, typename ResContextT>
	void _UpdateGlobalObjectsResIndiced(const std::vector<T>& objects, const std::vector<int>& indices, GPUJobContext& context, ResContextT resContext)
	{
		if (objects.empty() == true || indices.empty() == true)
		{
			return;
		}

		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();
		Frame& frame = context.frame;

		RenderCallbacks::UpdateGlobalObjectContext updateContext = ConstructGlobalObjectContext(context);

		std::vector<std::vector<RootArg::Arg_t>>& objGlobalRes = std::get<static_cast<int>(INPUT_TYPE)>(resContext.objGlobalRes);;
		
		const int perObjectGlobalMemorySize = resContext.perObjectGlobalMemorySize[static_cast<int>(INPUT_TYPE)];
		
		BufferHandler objGlobalResMemory = resContext.objGlobalResMemory[static_cast<int>(INPUT_TYPE)];

		std::vector<std::byte> cpuMem(perObjectGlobalMemorySize, static_cast<std::byte>(0));

		for (int i = 0; i < indices.size(); ++i)
		{
			const int objectIndex = indices[i];
			const int objectOffset = perObjectGlobalMemorySize * objectIndex;

			const T& object = objects[objectIndex];

			for (RootArg::Arg_t& arg : objGlobalRes[objectIndex])
			{
				std::visit([&updateContext, &object, &cpuMem, objectOffset](auto&& arg)
				{
					using T = std::decay_t<decltype(arg)>;

					if constexpr (std::is_same_v<T, RootArg::RootConstant>)
					{
						DX_ASSERT(false && "Root constant is not implemented");
					};

					if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
					{
						// Start of the memory of the current buffer
						int fieldOffset = arg.gpuMem.offset - objectOffset;

						for (RootArg::ConstBuffField& field : arg.content)
						{
							RenderCallbacks::UpdateGlobalObject(
								field.hashedName,
								object,
								cpuMem[fieldOffset],
								updateContext);


							// Proceed to next buffer
							fieldOffset += field.size;
						}
					}

					if constexpr (std::is_same_v<T, RootArg::StructuredBufferView>)
					{
						DX_ASSERT(arg.buffer != nullptr && "Structured buffer pointed for root arg resource should be initialized");

						RenderCallbacks::UpdateGlobalObject(
							arg.hashedName,
							object,
							*arg.buffer,
							updateContext);
					}

					if constexpr (std::is_same_v<T, RootArg::DescTable>)
					{
						for (RootArg::DescTableEntity_t& descTableEntity : arg.content)
						{
							std::visit([](auto&& descTableEntity)
							{
								using T = std::decay_t<decltype(descTableEntity)>;

								if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
								{
									DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
								}
							}, descTableEntity);
						}
					}

				}, arg);
			}

			if (perObjectGlobalMemorySize != 0)
			{
				FArg::UpdateUploadHeapBuff updateConstBufferArgs;
				updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
				updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(objGlobalResMemory) + objectOffset;
				updateConstBufferArgs.data = cpuMem.data();
				updateConstBufferArgs.byteSize = cpuMem.size();
				updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

				ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
			}
		}
	}

	template<Parsing::PassInputType INPUT_TYPE, typename ResContextT>
	void _AllocateGlobalObjectConstMem(int objectsNum, ResContextT resContext)
	{
		constexpr int INPUT_TYPE_INDEX = static_cast<int>(INPUT_TYPE);

		// Allocate memory
		BufferHandler& objectGlobalMem = resContext.objGlobalResMemory[INPUT_TYPE_INDEX];

		DX_ASSERT(objectGlobalMem == Const::INVALID_BUFFER_HANDLER && "_RegisterGlobalObjectsRes error. Per object global mem should is not deallocated");

		const int objMemorySize = resContext.perObjectGlobalMemorySize[INPUT_TYPE_INDEX];

		if (objMemorySize > 0)
		{
			objectGlobalMem = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(objMemorySize * objectsNum);
		}
	}

	template<typename objT, typename AllocT>
	void _RegisterGlobalObjectRes(const objT& obj, std::vector<RootArg::Arg_t>& objRes, RenderCallbacks::RegisterGlobalObjectContext& regContext, AllocT& alloc)
	{
		for (RootArg::Arg_t& arg : objRes)
		{
			std::visit([&obj, &regContext, &alloc](auto&& arg)
			{
				using T = std::decay_t<decltype(arg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					DX_ASSERT(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
				}

				if constexpr (std::is_same_v<T, RootArg::UAView>)
				{
					DX_ASSERT(false && "Inline UAV are not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::StructuredBufferView>)
				{
					DX_ASSERT(arg.buffer == nullptr && "Structured buffer pointed for root arg resource should be empty");

					Utils::PointerAsRef<decltype(arg.buffer)> refToPointer{&arg.buffer};

					RenderCallbacks::RegisterGlobalObject(
						arg.hashedName,
						obj,
						refToPointer,
						regContext);
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					arg.viewIndex = RootArg::AllocateDescTableView(arg, alloc);

					for (int i = 0; i < arg.content.size(); ++i)
					{
						RootArg::DescTableEntity_t& descTableEntitiy = arg.content[i];
						const int currentViewIndex = arg.viewIndex + i;

						std::visit([&obj, &regContext, currentViewIndex]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture> ||
								std::is_same_v<T, RootArg::DescTableEntity_UAView> ||
								std::is_same_v<T, RootArg::DescTableEntity_StructuredBufferView>)
							{
								DX_ASSERT(descTableEntitiy.internalBindName.has_value() == false &&
									"PerObject resources is not suited to use internal bind");

								RenderCallbacks::RegisterGlobalObject(
									descTableEntitiy.hashedName,
									obj,
									currentViewIndex,
									regContext
								);
							}

						}, descTableEntitiy);
					}
				}

			}, arg);

		}
	}

	template< Parsing::PassInputType INPUT_TYPE, typename T, typename ResContextT, typename AllocT>
	void _RegisterGlobalObjectsRes(const std::vector<T>& objects, GPUJobContext& context, ResContextT resContext, AllocT& alloc)
	{
		DX_ASSERT(objects.empty() == false && "Register global object res received request with empty objects");
		
		constexpr int INPUT_TYPE_INDEX = static_cast<int>(INPUT_TYPE);

		// Get references on used data
		const std::vector<RootArg::Arg_t>& objResTemplate = std::get<INPUT_TYPE_INDEX>(resContext.objGlobalResTemplate);
		std::vector<std::vector<RootArg::Arg_t>>& objResources = std::get<INPUT_TYPE_INDEX>(resContext.objGlobalRes);;

		RenderCallbacks::RegisterGlobalObjectContext regContext = { context };

		// Actual registration
		for (int i = 0; i < objects.size(); ++i)
		{
			const auto& obj = objects[i];

			std::vector<RootArg::Arg_t>& objRes = objResources.emplace_back(objResTemplate);

			_RegisterGlobalObjectRes(obj, objRes, regContext, alloc);

		};

	}
}


FrameGraph::FrameGraph()
{
	for (BufferHandler& handler : objGlobalResMemory)
	{
		handler = Const::INVALID_BUFFER_HANDLER;
	}

	for(int& memSize : perObjectGlobalMemorySize)
	{
		memSize = Const::INVALID_SIZE;
	}
}

FrameGraph::~FrameGraph()
{
	auto& uploadBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	if (passGlobalMemory != Const::INVALID_BUFFER_HANDLER)
	{
		uploadBuff.Delete(passGlobalMemory);
	}

	for (BufferHandler handler : objGlobalResMemory)
	{
		if (handler != Const::INVALID_BUFFER_HANDLER)
		{
			uploadBuff.Delete(handler);
		}
	}

	if (internalTextureNames.use_count() == 1)
	{
		ResourceManager& resourceManager = ResourceManager::Inst();

		for (const std::string& name : *internalTextureNames)
		{
			resourceManager.DeleteResource(name.c_str());
		}
	}

	auto& rendererAllocator = *Renderer::Inst().cbvSrvHeapAllocator;

	// Delete global per pass resources
	for (RootArg::Arg_t& arg : passesGlobalRes)
	{
		RootArg::Release(arg, rendererAllocator);
	}

	// Deal with global per object resources
	for (std::vector<RootArg::Arg_t>& argList : std::get<static_cast<int>(Parsing::PassInputType::Static)>(objGlobalRes))
	{
		for (RootArg::Arg_t& arg : argList)
		{
			RootArg::Release(arg, rendererAllocator);
		}
	}
	std::get<static_cast<int>(Parsing::PassInputType::Static)>(objGlobalRes).clear();

	for (std::vector<RootArg::Arg_t>& argList : std::get<static_cast<int>(Parsing::PassInputType::Particles)>(objGlobalRes))
	{
		for (RootArg::Arg_t& arg : argList)
		{
			RootArg::Release(arg, rendererAllocator);
		}
	}
	std::get<static_cast<int>(Parsing::PassInputType::Particles)>(objGlobalRes).clear();

	for (RootArg::Arg_t& arg : std::get<static_cast<int>(Parsing::PassInputType::PostProcess)>(objGlobalRes))
	{
		RootArg::Release(arg, rendererAllocator);
	}
	std::get<static_cast<int>(Parsing::PassInputType::PostProcess)>(objGlobalRes).clear();

#if (ENABLE_VALIDATION)
	// Dirty way to make sure I cleaned up everything
	std::apply([](const auto&... resources) 
	{
		((DX_ASSERT(resources.empty() == true && "Not all resources were deleted on FrameGraph destruction")), ...);

	}, objGlobalRes);
#endif

	DX_ASSERT(std::get<static_cast<int>(Parsing::PassInputType::Dynamic)>(objGlobalRes).empty() == true &&
		"Dynamic per frame resources were not cleaned up");

	DX_ASSERT(std::get<static_cast<int>(Parsing::PassInputType::UI)>(objGlobalRes).empty() == true &&
		"UI per frame resources were not cleaned up");
}

void FrameGraph::Execute(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	Renderer& renderer = Renderer::Inst();
	JobQueue& jobQueue = JobSystem::Inst().GetJobQueue();
	const bool isRenderingState = renderer.GetState() == Renderer::State::Rendering;

	// NOTE: creation order is the order in which command Lists will be submitted.
	// Set up dependencies 

	/* Create Contexts */
	GPUJobContext updateGlobalResJobContext = renderer.CreateContext(frame, isRenderingState);
	std::vector<GPUJobContext> framePassContexts;
	// All passes depend from Global Resource Update
	for (PassTask& passTask : passTasks)
	{
		framePassContexts.emplace_back(renderer.CreateContext(frame, isRenderingState));
	}
	
	GPUJobContext endFrameJobContext = renderer.CreateContext(frame, false);
	// NOTE: should always be last job, before submitting frame, because of sloppy Render Target
	// state transition
	GPUJobContext drawDebugGuiJobContext = renderer.CreateContext(frame);

	/*  Handle dependencies */
	std::vector<GPUJobContext*> endFrameDependency;

	if (isRenderingState)
	{
		// All passes depend from Global Resource Update	
		for (GPUJobContext& jobContext : framePassContexts)
		{
			jobContext.CreateDependencyFrom({ &updateGlobalResJobContext });
		};
		
		AddTexturesProxiesToPassJobContexts(framePassContexts);
		endFrameDependency.reserve(endFrameDependency.size());

		// End Frame Job depends on Passes Jobs
		std::transform(framePassContexts.begin(), framePassContexts.end(), std::back_inserter(endFrameDependency),
			[](GPUJobContext& context)
		{
			return &context;
		});
	}

	// End Frame Job Depends on Draw Debug Gui Job
	endFrameDependency.push_back(&drawDebugGuiJobContext);
	endFrameJobContext.CreateDependencyFrom(endFrameDependency);

	/* Enqueue jobs */
	// NOTE: context SHOULD be passed by value. Otherwise it will not exist when another thread will try to execute 
	// this job
	if (isRenderingState)
	{
		jobQueue.Enqueue(Job([updateGlobalResJobContext, &renderer]() mutable
		{
			JOB_GUARD(updateGlobalResJobContext);
			updateGlobalResJobContext.WaitDependency();

			updateGlobalResJobContext.frame.frameGraph->UpdateGlobalResources(updateGlobalResJobContext);

		}));

		for (int i = 0; i < passTasks.size(); ++i)
		{
			jobQueue.Enqueue(Job(
				[passJobContext = framePassContexts[i], &passTask = passTasks[i]]() mutable
			{
				JOB_GUARD(passJobContext);

				std::string_view passName = PassUtils::GetPassName(passTask.pass);

				Diagnostics::BeginEvent(passJobContext.commandList->GetGPUList(), passName);
				Logs::Logf(Logs::Category::Job, "Pass job started: {}", passName);

				passJobContext.WaitDependency();

				passTask.Execute(passJobContext);

				Logs::Logf(Logs::Category::Job, "Pass job end: {}", passName);
				Diagnostics::EndEvent(passJobContext.commandList->GetGPUList());
			}));
		}
	}

	jobQueue.Enqueue(Job([drawDebugGuiJobContext, &renderer]() mutable
	{
		renderer.DrawDebugGuiJob(drawDebugGuiJobContext);
	}));

	jobQueue.Enqueue(Job([endFrameJobContext, &renderer]() mutable
	{
		renderer.EndFrameJob(endFrameJobContext);
	}));
}

void FrameGraph::Init(GPUJobContext& context)
{
	// Init utility data
	passGlobalMemorySize = RootArg::GetSize(passesGlobalRes);

	int index = 0;
	std::apply([&index, this](auto&... objResTemplate) 
	{
		((perObjectGlobalMemorySize[index++] = RootArg::GetSize(objResTemplate)), ...);
	}, objGlobalResTemplate);

	RegisterPassResources(context);

	// Register static objects
	RegisterStaticObjects(Renderer::Inst().GetStaticObjects(), context);
}

void FrameGraph::BindPassGlobalRes(const std::vector<int>& resIndices, CommandList& commandList) const
{
	for (const int index : resIndices)
	{
		RootArg::Bind(passesGlobalRes[index], commandList);
	}
}

void FrameGraph::BindComputePassGlobalRes(const std::vector<int>& resIndices, CommandList& commandList) const
{
	for (const int index : resIndices)
	{
		RootArg::BindCompute(passesGlobalRes[index], commandList);
	}
}

void FrameGraph::RegisterStaticObjects(const std::vector<StaticObject>& objects, GPUJobContext& context)
{
	if (objects.empty() == true)
	{
		return;
	}

	DX_ASSERT(std::get<static_cast<int>(Parsing::PassInputType::Static)>(objGlobalRes).empty() &&
		"Object global res should be empty on registration");

	DX_ASSERT(objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Static)] == Const::INVALID_BUFFER_HANDLER &&
		"Object global memory should be empty on registration");

	auto resContext = ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize };

	// Register global object resources
	_RegisterGlobalObjectsRes<Parsing::PassInputType::Static>(objects, context, resContext, *Renderer::Inst().cbvSrvHeapAllocator);

	// Allocate and attach memory
	_AllocateGlobalObjectConstMem<Parsing::PassInputType::Static>(objects.size(), resContext);
	
	int objectOffset = 0;
	BufferHandler objectGlobalMemory = objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Static)];
	const int objSize = perObjectGlobalMemorySize[static_cast<int>(Parsing::PassInputType::Static)];

	for (std::vector<RootArg::Arg_t>& args : std::get<static_cast<int>(Parsing::PassInputType::Static)>(objGlobalRes))
	{
		RootArg::AttachConstBufferToArgs(args, objectOffset, objectGlobalMemory);
		objectOffset += objSize;
	}


	for (PassTask& passTask : passTasks)
	{
		std::visit([&objects ,&context](auto&& pass) 
		{
			using T = std::decay_t<decltype(pass)>;

			if constexpr (std::is_same_v<T, Pass_Static>)
			{
				pass.RegisterObjects(objects, context);
			}

		}, passTask.pass);
	}
}

void FrameGraph::RegisterPassResources(GPUJobContext& context)
{
	// Pass global
	RegisterGlobaPasslRes(context);

	// Pass local
	for (PassTask& passTask : passTasks)
	{
		std::visit([&context](auto&& pass)
		{
			pass.Init();
			pass.RegisterPassResources(context);

		}, passTask.pass);
	}
}

void FrameGraph::RegisterGlobalObjectsResDynamicEntities(GPUJobContext& context)
{
	const std::vector<int>& visibleEntitiesIndices = context.frame.visibleEntitiesIndices;
	
	if (visibleEntitiesIndices.empty() == true)
	{
		return;
	}

	BufferHandler& entityMemory = objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Dynamic)];

	DX_ASSERT(entityMemory == Const::INVALID_BUFFER_HANDLER && "Entity memory should be cleaned up");

	std::vector<std::vector<RootArg::Arg_t>>& entityRes = std::get<static_cast<int>(Parsing::PassInputType::Dynamic)>(objGlobalRes);
	const std::vector<RootArg::Arg_t>& objResTemplate = std::get<static_cast<int>(Parsing::PassInputType::Dynamic)>(objGlobalResTemplate);

	DX_ASSERT(entityRes.empty() == true && "Entity Res should be cleaned up");

	RenderCallbacks::RegisterGlobalObjectContext regContext = { context };

	// Register 
	for (int visibleIndex : visibleEntitiesIndices)
	{
		std::vector<RootArg::Arg_t>& objRes = entityRes.emplace_back(objResTemplate);

		_RegisterGlobalObjectRes(context.frame.entities[visibleIndex], objRes, regContext, *context.frame.streamingCbvSrvAllocator);
	}

	// Allocate and attach memory
	_AllocateGlobalObjectConstMem<Parsing::PassInputType::Dynamic>(visibleEntitiesIndices.size(), 
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });

	int objectOffset = 0;
	BufferHandler objectGlobalMemory = objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Dynamic)];
	const int objSize = perObjectGlobalMemorySize[static_cast<int>(Parsing::PassInputType::Dynamic)];

	for (std::vector<RootArg::Arg_t>& args : entityRes)
	{
		RootArg::AttachConstBufferToArgs(args, objectOffset, objectGlobalMemory);
		objectOffset += objSize;
	}
}

void FrameGraph::UpdateGlobalResources(GPUJobContext& context)
{
	Diagnostics::BeginEvent(context.commandList->GetGPUList(), "UpdateGlobalResources");

	// This just doesn't belong here. I will do proper Initialization when
	// runtime load of frame graph will be implemented
	if (isInitalized == false)
	{
		Init(context);

		isInitalized = true;
	}
	
	RegisterParticles(context);

	RegisterGlobalObjectsResUI(context);
	UpdateGlobalObjectsResUI(context);

	UpdateGlobalObjectsResStatic(context);

	RegisterGlobalObjectsResDynamicEntities(context);
	UpdateGlobalObjectsResDynamic(context);

	RegisterGlobalObjectsResDebug(context);
	UpdateGlobalObjectsResDebug(context);

	UpdateGlobalPasslRes(context);

	Diagnostics::EndEvent(context.commandList->GetGPUList());
}

void FrameGraph::ReleasePerFrameResources(Frame& frame)
{
	for (PassTask& passTask : passTasks)
	{
		std::visit([&frame](auto&& pass)
		{
			pass.ReleasePerFrameResources(frame);
		}, passTask.pass);
	}
	
	for (std::vector<RootArg::Arg_t>& argList : std::get<static_cast<int>(Parsing::PassInputType::UI)>(objGlobalRes))
	{
		for (RootArg::Arg_t& arg : argList)
		{
			RootArg::Release(arg, *frame.streamingCbvSrvAllocator);
		}
	}
	std::get<static_cast<int>(Parsing::PassInputType::UI)>(objGlobalRes).clear();

	for (std::vector<RootArg::Arg_t>& argList : std::get<static_cast<int>(Parsing::PassInputType::Dynamic)>(objGlobalRes))
	{
		for (RootArg::Arg_t& arg : argList)
		{
			RootArg::Release(arg, *frame.streamingCbvSrvAllocator);
		}
	}
	std::get<static_cast<int>(Parsing::PassInputType::Dynamic)>(objGlobalRes).clear();

	for (std::vector<RootArg::Arg_t>& argList : std::get<static_cast<int>(Parsing::PassInputType::Debug)>(objGlobalRes))
	{
		for (RootArg::Arg_t& arg : argList)
		{
			RootArg::Release(arg, *frame.streamingCbvSrvAllocator);
		}
	}
	std::get<static_cast<int>(Parsing::PassInputType::Debug)>(objGlobalRes).clear();

	auto& updateBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	BufferHandler& perObjectGlobalMemoryUI = 
		objGlobalResMemory[static_cast<int>(Parsing::PassInputType::UI)];

	if (perObjectGlobalMemoryUI != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.Delete(perObjectGlobalMemoryUI);
		perObjectGlobalMemoryUI = Const::INVALID_BUFFER_HANDLER;
	}

	BufferHandler& perObjectGlobalMemoryDynamic =
		objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Dynamic)];

	if (perObjectGlobalMemoryDynamic != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.Delete(perObjectGlobalMemoryDynamic);
		perObjectGlobalMemoryDynamic = Const::INVALID_BUFFER_HANDLER;
	}

	BufferHandler& perObjectGlobalMemoryDebug =
		objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Debug)];

	if (perObjectGlobalMemoryDebug != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.Delete(perObjectGlobalMemoryDebug);
		perObjectGlobalMemoryDebug = Const::INVALID_BUFFER_HANDLER;
	}

	if (particlesVertexMemory != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.Delete(particlesVertexMemory);
		particlesVertexMemory = Const::INVALID_BUFFER_HANDLER;
	}
}

BufferHandler FrameGraph::GetParticlesVertexMemory() const
{
	return particlesVertexMemory;
}

std::vector<ResourceProxy> FrameGraph::GetTextureProxy() const
{
	return internalTextureProxy;
}

void FrameGraph::AddTexturesProxiesToPassJobContexts(std::vector<GPUJobContext>& jobContexts) const
{
	DX_ASSERT(jobContexts.empty() == false && "Can't attach texture proxies to job contexts, cause it is empty");

	// Attach internal texture proxies 
	for (GPUJobContext& jobContext : jobContexts)
	{
		jobContext.internalTextureProxies = GetTextureProxy();
	}

	// Deal with depth back buffer
	for (int i = 0; i < jobContexts.size(); ++i)
	{
		GPUJobContext& context = jobContexts[i];

		ResourceProxy& backBufferProxy = context.internalTextureProxies.emplace_back(ResourceProxy
			{
				*context.frame.depthStencilBuffer.Get()
			});

		backBufferProxy.hashedName = HASH(PassParameters::DEPTH_BACK_BUFFER_NAME);
	}

	// Deal with color back buffer
	{
		// Note, that first job context is special because we start not from default state, but from D3D12_RESOURCE_STATE_PRESENT
		GPUJobContext& firstContext = jobContexts.front();

		ResourceProxy& backBufferProxy = firstContext.internalTextureProxies.emplace_back(ResourceProxy
			{
				*firstContext.frame.colorBufferAndView->buffer.Get(),
				D3D12_RESOURCE_STATE_PRESENT
			});

		backBufferProxy.hashedName = HASH(PassParameters::COLOR_BACK_BUFFER_NAME);
	}

	for (int i = 1; i < jobContexts.size(); ++i)
	{
		GPUJobContext& context = jobContexts[i];

		ResourceProxy& backBufferProxy = context.internalTextureProxies.emplace_back(ResourceProxy
			{
				*context.frame.colorBufferAndView->buffer.Get()
			});

		backBufferProxy.hashedName = HASH(PassParameters::COLOR_BACK_BUFFER_NAME);
	}
}

bool FrameGraph::IsTextureProxiesCreationRequired() const
{
	return internalTextureNames != nullptr &&
		internalTextureNames->size() != internalTextureProxy.size();
}

void FrameGraph::CreateTextureProxies()
{
	DX_ASSERT(internalTextureNames != nullptr);
	DX_ASSERT(internalTextureProxy.empty() == true && "Internal texture proxy mush be clean at this stage");

	internalTextureProxy = FrameGraphBuilder::Inst().CreateFrameGraphTextureProxies(*internalTextureNames);
}

void FrameGraph::RegisterGlobalObjectsResUI(GPUJobContext& context)
{
	if (context.frame.uiDrawCalls.empty() == true)
	{
		return;
	}

	auto resContext = ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize };

	_RegisterGlobalObjectsRes<Parsing::PassInputType::UI>(context.frame.uiDrawCalls, context, resContext, *context.frame.streamingCbvSrvAllocator);

	// Allocate and attach memory
	_AllocateGlobalObjectConstMem<Parsing::PassInputType::UI>(context.frame.uiDrawCalls.size(), resContext);

	int objectOffset = 0;
	BufferHandler objectGlobalMemory = objGlobalResMemory[static_cast<int>(Parsing::PassInputType::UI)];
	const int objSize = perObjectGlobalMemorySize[static_cast<int>(Parsing::PassInputType::UI)];

	for (std::vector<RootArg::Arg_t>& args : std::get<static_cast<int>(Parsing::PassInputType::UI)>(objGlobalRes))
	{
		RootArg::AttachConstBufferToArgs(args, objectOffset, objectGlobalMemory);
		objectOffset += objSize;
	}
}

void FrameGraph::UpdateGlobalObjectsResUI(GPUJobContext& context)
{
	_UpdateGlobalObjectsRes<Parsing::PassInputType::UI>(context.frame.uiDrawCalls, context,
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });
}

void FrameGraph::UpdateGlobalObjectsResStatic(GPUJobContext& context)
{
	const std::vector<StaticObject>& staticObjects = Renderer::Inst().GetStaticObjects();

	_UpdateGlobalObjectsResIndiced<Parsing::PassInputType::Static>(staticObjects, context.frame.visibleStaticObjectsIndices, context,
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });
}

void FrameGraph::UpdateGlobalObjectsResDynamic(GPUJobContext& context)
{
	const std::vector<int>& visibleEntitiesIndices = context.frame.visibleEntitiesIndices;

	if (visibleEntitiesIndices.empty() == true)
	{
		return;
	}

	RenderCallbacks::UpdateGlobalObjectContext updateContext = ConstructGlobalObjectContext(context);

	std::vector<std::vector<RootArg::Arg_t>>& entityRes = std::get<static_cast<int>(Parsing::PassInputType::Dynamic)>(objGlobalRes);
		
	const int perObjectMemorySize = perObjectGlobalMemorySize[static_cast<int>(Parsing::PassInputType::Dynamic)];

	std::vector<std::byte> cpuMem(perObjectMemorySize * visibleEntitiesIndices.size(), static_cast<std::byte>(0));

	for (int i = 0; i < visibleEntitiesIndices.size(); ++i)
	{
		const entity_t& entity = context.frame.entities[visibleEntitiesIndices[i]];

		for (RootArg::Arg_t& arg : entityRes[i])
		{
			std::visit([&updateContext, &entity, &cpuMem](auto&& arg)
			{
				using T = std::decay_t<decltype(arg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					DX_ASSERT(false && "Root constant is not implemented");
				};

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					// Start of the memory of the current buffer
					int fieldOffset = arg.gpuMem.offset;

					for (RootArg::ConstBuffField& field : arg.content)
					{
						RenderCallbacks::UpdateGlobalObject(
							field.hashedName,
							entity,
							cpuMem[fieldOffset],
							updateContext);


						// Proceed to next buffer
						fieldOffset += field.size;
					}
				}

				if constexpr (std::is_same_v<T, RootArg::StructuredBufferView>)
				{
					DX_ASSERT(arg.buffer != nullptr && "Structured buffer pointed for root arg resource should be initialized");

					RenderCallbacks::UpdateGlobalObject(
						arg.hashedName,
						entity,
						*arg.buffer,
						updateContext);
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					for (RootArg::DescTableEntity_t& descTableEntity : arg.content)
					{
						std::visit([](auto&& descTableEntity)
						{
							using T = std::decay_t<decltype(descTableEntity)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
							}
						}, descTableEntity);
					}
				}

			}, arg);
		}
	}

	if (perObjectMemorySize != 0)
	{
		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

		FArg::UpdateUploadHeapBuff updateConstBufferArgs;
		updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
		updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Dynamic)]);
		updateConstBufferArgs.data = cpuMem.data();
		updateConstBufferArgs.byteSize = cpuMem.size();
		updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

		ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
	}
}

void FrameGraph::RegisterParticles(GPUJobContext& context)
{
	const std::vector<particle_t>& particlesToDraw = context.frame.particles;

	if (particlesToDraw.empty() == true)
	{
		return;
	}

	const int vertexBufferSize = SINGLE_PARTICLE_SIZE * particlesToDraw.size();

	const std::array<unsigned int, 256>& table8To24 = Renderer::Inst().GetTable8To24();

	std::vector<ShDef::Vert::PosCol> particleVertexData;
	particleVertexData.reserve(particlesToDraw.size());

	std::transform(particlesToDraw.cbegin(), particlesToDraw.cend(), std::back_inserter(particleVertexData),
		[&table8To24](const particle_t& particle) 
	{
		unsigned char color[4];
		*reinterpret_cast<int *>(color) = table8To24[particle.color];

		return  ShDef::Vert::PosCol{
		XMFLOAT4(particle.origin[0], particle.origin[1], particle.origin[2], 1.0f),
		XMFLOAT4(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, particle.alpha)
		};
	});

	auto& uploadMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	DX_ASSERT(particlesVertexMemory == Const::INVALID_BUFFER_HANDLER && "Particle vertex memory is not cleaned up");
	particlesVertexMemory = uploadMemory.Allocate(vertexBufferSize);

	// Deal with vertex buffer
	FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
	updateVertexBufferArgs.buffer = uploadMemory.GetGpuBuffer();
	updateVertexBufferArgs.offset = uploadMemory.GetOffset(particlesVertexMemory);
	updateVertexBufferArgs.data = particleVertexData.data();
	updateVertexBufferArgs.byteSize = vertexBufferSize;
	updateVertexBufferArgs.alignment = 0;
	ResourceManager::Inst().UpdateUploadHeapBuff(updateVertexBufferArgs);
}

void FrameGraph::RegisterGlobalObjectsResDebug(GPUJobContext& context)
{
	const std::vector<DebugObject_t>& debugObjects = context.frame.debugObjects;

	if (debugObjects.empty() == true)
	{
		return;
	}

	BufferHandler& debugObjMemory = objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Debug)];

	DX_ASSERT(debugObjMemory == Const::INVALID_BUFFER_HANDLER && "Debug objects memory should be cleaned up");

	std::vector<std::vector<RootArg::Arg_t>>& debugObjRes = std::get<static_cast<int>(Parsing::PassInputType::Debug)>(objGlobalRes);
	const std::vector<RootArg::Arg_t>& debugObjResTemplate = std::get<static_cast<int>(Parsing::PassInputType::Debug)>(objGlobalResTemplate);

	DX_ASSERT(debugObjRes.empty() == true && "Debug Res should be cleaned up");

	RenderCallbacks::RegisterGlobalObjectContext regContext = { context };

	// Register 
	for (const DebugObject_t& obj : debugObjects)
	{
		std::vector<RootArg::Arg_t>& objRes = debugObjRes.emplace_back(debugObjResTemplate);

		_RegisterGlobalObjectRes(obj, objRes, regContext, *context.frame.streamingCbvSrvAllocator);
	}

	// Allocate and attach memory
	_AllocateGlobalObjectConstMem<Parsing::PassInputType::Debug>(debugObjects.size(),
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });

	int objectOffset = 0;
	BufferHandler objectGlobalMemory = objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Debug)];
	const int objSize = perObjectGlobalMemorySize[static_cast<int>(Parsing::PassInputType::Debug)];

	for (std::vector<RootArg::Arg_t>& args : debugObjRes)
	{
		RootArg::AttachConstBufferToArgs(args, objectOffset, objectGlobalMemory);
		objectOffset += objSize;
	}
}

void FrameGraph::UpdateGlobalObjectsResDebug(GPUJobContext& context)
{
	const std::vector<DebugObject_t>& debugObjects = context.frame.debugObjects;

	if (debugObjects.empty() == true)
	{
		return;
	}

	_UpdateGlobalObjectsRes<Parsing::PassInputType::Debug>(debugObjects, context,
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });
}

void FrameGraph::RegisterGlobaPasslRes(GPUJobContext& context)
{

	DX_ASSERT(passGlobalMemory == Const::INVALID_BUFFER_HANDLER && "Pass Global Memory shouldn't be preallocated");

	if (passGlobalMemorySize != 0)
	{
		passGlobalMemory =
			MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passGlobalMemorySize);
	}

	RenderCallbacks::RegisterGlobalPassContext globalPassContext = { context };

	int offset = 0;

	for (RootArg::Arg_t& arg : passesGlobalRes)
	{
		std::visit([this, &offset, &globalPassContext](auto&& arg)
		{
			using T = std::decay_t<decltype(arg)>;

			if constexpr (std::is_same_v<T, RootArg::RootConstant>)
			{
				DX_ASSERT(false && "Root constant is not implemented");
			}

			if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
			{
				arg.gpuMem.handler = passGlobalMemory;
				arg.gpuMem.offset = offset;

				offset += RootArg::GetConstBufftSize(arg);
			}

			if constexpr (std::is_same_v<T, RootArg::StructuredBufferView>)
			{
				DX_ASSERT(arg.buffer == nullptr && "Structured buffer pointed for root arg resource should be empty");

				Utils::PointerAsRef<decltype(arg.buffer)> refToPointer{ &arg.buffer };

				RenderCallbacks::RegisterGlobalPass(
					arg.hashedName,
					refToPointer,
					globalPassContext
				);
			}

			if constexpr (std::is_same_v<T, RootArg::DescTable>)
			{
				arg.viewIndex = RootArg::AllocateDescTableView(arg, *Renderer::Inst().cbvSrvHeapAllocator);

				for (int i = 0; i < arg.content.size(); ++i)
				{
					RootArg::DescTableEntity_t& descTableEntitiy = arg.content[i];
					int currentViewIndex = arg.viewIndex + i;
					
					std::visit([this, &offset, &currentViewIndex, &globalPassContext]
					(auto&& descTableEntitiy)
					{
						using T = std::decay_t<decltype(descTableEntitiy)>;

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
						{
							DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
							//#TODO make view allocation
							descTableEntitiy.gpuMem.handler = passGlobalMemory;
							descTableEntitiy.gpuMem.offset = offset;

							offset += RootArg::GetConstBufftSize(descTableEntitiy);
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
						{
							if (descTableEntitiy.internalBindName.has_value())
							{
								// This is internal resource
								RenderCallbacks::RegisterInternalResource<D3D12_SHADER_RESOURCE_VIEW_DESC>(
									currentViewIndex,
									*descTableEntitiy.internalBindName
								);
							}
							else
							{
								RenderCallbacks::RegisterGlobalPass(
									descTableEntitiy.hashedName,
									currentViewIndex,
									globalPassContext
								);
							}
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_UAView>)
						{
							if (descTableEntitiy.internalBindName.has_value())
							{
								// This is internal resource
								RenderCallbacks::RegisterInternalResource<D3D12_UNORDERED_ACCESS_VIEW_DESC>(
									currentViewIndex,
									*descTableEntitiy.internalBindName
									);
							}
							else
							{
								RenderCallbacks::RegisterGlobalPass(
									descTableEntitiy.hashedName,
									currentViewIndex,
									globalPassContext
								);
							}
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_StructuredBufferView>)
						{
							DX_ASSERT(descTableEntitiy.internalBindName.has_value() == false && "Internal resource for Structured buffer is not implemented");

							RenderCallbacks::RegisterGlobalPass(
								descTableEntitiy.hashedName,
								currentViewIndex,
								globalPassContext
							);
						}

					}, descTableEntitiy);
				}
			}
		}, arg);
	}
}

void FrameGraph::UpdateGlobalPasslRes(GPUJobContext& context)
{
	RenderCallbacks::UpdateGlobalPassContext globalPassContext = { context };

	std::vector<std::byte> cpuMem(passGlobalMemorySize, static_cast<std::byte>(0));

	for (RootArg::Arg_t& arg : passesGlobalRes)
	{
		std::visit([&globalPassContext, &cpuMem](auto&& arg) 
		{
			using T = std::decay_t<decltype(arg)>;

			if constexpr (std::is_same_v<T, RootArg::RootConstant>)
			{
				DX_ASSERT(false && "Root constants are not implemented");
			}

			if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
			{
				int fieldOffset = arg.gpuMem.offset;

				for (RootArg::ConstBuffField& field : arg.content)
				{
					RenderCallbacks::UpdateGlobalPass(
						field.hashedName,
						cpuMem[fieldOffset],
						globalPassContext
					);

					// Proceed to next buffer
					fieldOffset += field.size;
				}
			}

			if constexpr (std::is_same_v<T, RootArg::StructuredBufferView>)
			{
				DX_ASSERT(arg.buffer != nullptr && "Structured buffer pointed for root arg resource should be initialized");

				RenderCallbacks::UpdateGlobalPass(
					arg.hashedName,
					*arg.buffer,
					globalPassContext);
			}

			if constexpr (std::is_same_v<T, RootArg::DescTable>)
			{
				for (int i = 0 ; i < arg.content.size(); ++i)
				{
					RootArg::DescTableEntity_t& descTableEntity = arg.content[i];
					int currentViewIndex = arg.viewIndex + i;

					std::visit([&globalPassContext, &cpuMem, &currentViewIndex](auto&& descTableEntity)
					{
						using T = std::decay_t<decltype(descTableEntity)>;

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
						{
							DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture> ||
							std::is_same_v<T, RootArg::DescTableEntity_UAView> ||
							std::is_same_v<T, RootArg::DescTableEntity_StructuredBufferView>)
						{
							RenderCallbacks::UpdateGlobalPass(
								descTableEntity.hashedName,
								currentViewIndex,
								globalPassContext
							);
						}

					}, descTableEntity);

				}
			}

		}, arg);
	}

	if (passGlobalMemorySize != 0)
	{
		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

		FArg::UpdateUploadHeapBuff updateConstBufferArgs;
		updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
		updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(passGlobalMemory);
		updateConstBufferArgs.data = cpuMem.data();
		updateConstBufferArgs.byteSize = cpuMem.size();
		updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

		ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
	}
}