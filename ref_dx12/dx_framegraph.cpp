#include "dx_framegraph.h"

#include "dx_app.h"
#include "dx_jobmultithreading.h"
#include "dx_memorymanager.h"
#include "dx_rendercallbacks.h"



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
	void _UpdateGlobalObjectRes(const std::vector<T>& objects, GPUJobContext& context, ResContextT resContext)
	{
		if (objects.empty() == true)
		{
			return;
		}

		Frame& frame = context.frame;

		RenderCallbacks::UpdateGlobalObjectContext updateContext = ConstructGlobalObjectContext(context);

		std::vector<std::vector<RootArg::Arg_t>>& objGlobalRes = resContext.objGlobalRes[static_cast<int>(INPUT_TYPE)];

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
						assert(false && "Root constant is not implemented");
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

					if constexpr (std::is_same_v<T, RootArg::DescTable>)
					{
						for (RootArg::DescTableEntity_t& descTableEntity : arg.content)
						{
							std::visit([](auto&& descTableEntity)
							{
								using T = std::decay_t<decltype(descTableEntity)>;

								if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
								{
									assert(false && "Desc table view is probably not implemented! Make sure it is");
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
	void _UpdateGlobalObjectResIndiced(const std::vector<T>& objects, const std::vector<int>& indices, GPUJobContext& context, ResContextT resContext)
	{
		if (objects.empty() == true || indices.empty() == true)
		{
			return;
		}

		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();
		Frame& frame = context.frame;

		RenderCallbacks::UpdateGlobalObjectContext updateContext = ConstructGlobalObjectContext(context);

		std::vector<std::vector<RootArg::Arg_t>>& objGlobalRes = resContext.objGlobalRes[static_cast<int>(INPUT_TYPE)];
		
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
						assert(false && "Root constant is not implemented");
					};

					if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
					{
						// Start of the memory of the current buffer
						int fieldOffset = arg.gpuMem.offset - objectOffset;;

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

					if constexpr (std::is_same_v<T, RootArg::DescTable>)
					{
						for (RootArg::DescTableEntity_t& descTableEntity : arg.content)
						{
							std::visit([](auto&& descTableEntity)
							{
								using T = std::decay_t<decltype(descTableEntity)>;

								if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
								{
									assert(false && "Desc table view is probably not implemented! Make sure it is");
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

	template< Parsing::PassInputType INPUT_TYPE, typename T, typename ResContextT>
	void _RegisterGlobalObjectsRes(const std::vector<T>& objects, GPUJobContext& context, ResContextT resContext)
	{
		if (objects.empty() == true)
		{
			return;
		}

		constexpr int INPUT_TYPE_INDEX = static_cast<int>(INPUT_TYPE);

		// Allocate memory
		BufferHandler& objectGlobalMem = resContext.objGlobalResMemory[INPUT_TYPE_INDEX];

		assert(objectGlobalMem == Const::INVALID_BUFFER_HANDLER && "_RegisterGlobalObjectsRes error. Per object global mem should is not deallocated");

		const int objMemorySize = resContext.perObjectGlobalMemorySize[INPUT_TYPE_INDEX];

		if (objMemorySize != 0)
		{
			objectGlobalMem = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(objMemorySize * objects.size());
		}

		// Get references on used data
		const std::vector<RootArg::Arg_t>& objResTemplate = resContext.objGlobalResTemplate[INPUT_TYPE_INDEX];
		std::vector<std::vector<RootArg::Arg_t>>& objlResources = resContext.objGlobalRes[INPUT_TYPE_INDEX];

		RenderCallbacks::RegisterGlobalObjectContext regContext = { context };

		// Actual registration
		for (int i = 0; i < objects.size(); ++i)
		{
			const auto& obj = objects[i];

			std::vector<RootArg::Arg_t>& objRes = objlResources.emplace_back(objResTemplate);

			const int objectOffset = objMemorySize * i;
			int argOffset = 0;

			for (RootArg::Arg_t& arg : objRes)
			{
				std::visit([&obj, &objectGlobalMem, objectOffset, &argOffset, &regContext](auto&& arg)
				{
					using T = std::decay_t<decltype(arg)>;

					if constexpr (std::is_same_v<T, RootArg::RootConstant>)
					{
						assert(false && "Root constant is not implemented");
					}

					if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
					{
						arg.gpuMem.handler = objectGlobalMem;
						arg.gpuMem.offset = objectOffset + argOffset;

						argOffset += RootArg::GetConstBufftSize(arg);
					}

					if constexpr (std::is_same_v<T, RootArg::DescTable>)
					{
						arg.viewIndex = RootArg::AllocateDescTableView(arg);

						for (int i = 0; i < arg.content.size(); ++i)
						{
							RootArg::DescTableEntity_t& descTableEntitiy = arg.content[i];
							const int currentViewIndex = arg.viewIndex + i;

							std::visit([objectOffset, &argOffset, &obj, &regContext, currentViewIndex, &objectGlobalMem]
							(auto&& descTableEntitiy)
							{
								using T = std::decay_t<decltype(descTableEntitiy)>;

								if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
								{
									assert(false && "Desc table view is probably not implemented! Make sure it is");
									//#TODO make view allocation
									descTableEntitiy.gpuMem.handler = objectGlobalMem;
									descTableEntitiy.gpuMem.offset = objectOffset + argOffset;

									argOffset += RootArg::GetConstBufftSize(descTableEntitiy);
								}

								if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
								{
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

	for (Pass_t& pass : passes)
	{
		std::visit([](auto&& pass)
		{
			pass.ReleasePerFrameResources();
			pass.ReleasePersistentResources();

		}, pass);
	}
}

void FrameGraph::Execute(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	Renderer& renderer = Renderer::Inst();
	JobQueue& jobQueue = JobSystem::Inst().GetJobQueue();

	// NOTE: creation order is the order in which command Lists will be submitted.
	// Set up dependencies 

	/*  Handle dependencies */
	GPUJobContext updateGlobalResJobContext = renderer.CreateContext(frame);
	GPUJobContext beginFrameJobContext = renderer.CreateContext(frame);

	std::vector<GPUJobContext> framePassContexts;
	for (Pass_t& pass : passes)
	{
		framePassContexts.emplace_back(renderer.CreateContext(frame));

		framePassContexts.back().CreateDependencyFrom({&updateGlobalResJobContext});
	};

	GPUJobContext endFrameJobContext = renderer.CreateContext(frame);

	std::vector<GPUJobContext*> endFrameDependency;

	endFrameDependency.reserve(1 + endFrameDependency.size());
	endFrameDependency.push_back(&beginFrameJobContext);

	std::transform(framePassContexts.begin(), framePassContexts.end(), std::back_inserter(endFrameDependency),
		[](GPUJobContext& context) 
	{
		return &context;
	});

	endFrameJobContext.CreateDependencyFrom(endFrameDependency);


	/* Enqueue jobs */
	// NOTE: context SHOULD be passed by value. Otherwise it will not exist when another thread will try to execute 
	// this job
	jobQueue.Enqueue(Job([updateGlobalResJobContext, &renderer]() mutable
	{
		JOB_GUARD(updateGlobalResJobContext);
		updateGlobalResJobContext.WaitDependency();

		updateGlobalResJobContext.frame.frameGraph->UpdateGlobalResources(updateGlobalResJobContext);

	}));

	jobQueue.Enqueue(Job([beginFrameJobContext, &renderer]() mutable
	{
		renderer.BeginFrameJob(beginFrameJobContext);
	}));

	for (int i = 0; i < passes.size(); ++i)
	{
		std::visit([
			&jobQueue,
				passJobContext = framePassContexts[i]](auto&& pass)
			{
				jobQueue.Enqueue(Job(
					[passJobContext, &pass]() mutable
				{
					JOB_GUARD(passJobContext);
					passJobContext.WaitDependency();

					pass.Execute(passJobContext);
				}));

			}, passes[i]);
	}

	jobQueue.Enqueue(Job([endFrameJobContext, &renderer, this]() mutable
	{
		renderer.EndFrameJob(endFrameJobContext);
	}));
}

void FrameGraph::Init(GPUJobContext& context)
{
	// Init utility data
	passGlobalMemorySize = RootArg::GetSize(passesGlobalRes);

	for (int i = 0; i < static_cast<int>(Parsing::PassInputType::SIZE); ++i)
	{
		perObjectGlobalMemorySize[i] = RootArg::GetSize(objGlobalResTemplate[i]);
	}

	// Pass global
	RegisterGlobaPasslRes(context);

	// Pass local
	for (Pass_t& pass : passes)
	{
		std::visit([&context](auto&& pass) 
		{
			using passT = std::decay_t<decltype(pass)>;

			pass.RegisterPassResources(context);

		}, pass);
	}

	// Register static objects
	RegisterObjects(Renderer::Inst().staticObjects, context);
}

void FrameGraph::BindPassGlobalRes(const std::vector<int>& resIndices, CommandList& commandList) const
{
	for (const int index : resIndices)
	{
		RootArg::Bind(passesGlobalRes[index], commandList);
	}
}

void FrameGraph::BindObjGlobalRes(const std::vector<int>& resIndices, int objIndex, CommandList& commandList, Parsing::PassInputType objType) const
{
	const std::vector<RootArg::Arg_t>& objRes = objGlobalRes[static_cast<int>(objType)][objIndex];

	for (const int index : resIndices)
	{
		RootArg::Bind(objRes[index], commandList);
	}
}


void FrameGraph::RegisterObjects(const std::vector<StaticObject>& objects, GPUJobContext& context)
{
	assert(objGlobalRes[static_cast<int>(Parsing::PassInputType::Static)].empty() &&
		"Object global res should be empty on registration");

	assert(objGlobalResMemory[static_cast<int>(Parsing::PassInputType::Static)] == Const::INVALID_BUFFER_HANDLER &&
		"Object global memory should be empty on registration");

	// Register global object resources
	_RegisterGlobalObjectsRes<Parsing::PassInputType::Static>(objects, context,
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });

	for (Pass_t& pass : passes)
	{
		std::visit([&objects ,&context](auto&& pass) 
		{
			using T = std::decay_t<decltype(pass)>;

			if constexpr (std::is_same_v<T, Pass_Static>)
			{
				pass.RegisterObjects(objects, context);
			}

		}, pass);
	}
}

void FrameGraph::UpdateGlobalResources(GPUJobContext& context)
{
	// This just doesn't belong here. I will do proper Initialization when
	// runtime load of frame graph will be implemented
	if (isInitalized == false)
	{
		Init(context);

		isInitalized = true;
	}
	

	RegisterGlobalObjectsResUI(context);
	UpdateGlobalObjectsResUI(context);

	UpdateGlobalObjectsResStatic(context);

	UpdateGlobalPasslRes(context);
}

void FrameGraph::ReleasePerFrameResources()
{
	for (Pass_t& pass : passes)
	{
		std::visit([](auto&& pass)
		{
			pass.ReleasePerFrameResources();
		}, pass);
	}

	objGlobalRes[static_cast<int>(Parsing::PassInputType::UI)].clear();

	auto& updateBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	BufferHandler& perObjectGlobalMemoryUI = 
		objGlobalResMemory[static_cast<int>(Parsing::PassInputType::UI)];

	if (perObjectGlobalMemoryUI != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.Delete(perObjectGlobalMemoryUI);
		perObjectGlobalMemoryUI = Const::INVALID_BUFFER_HANDLER;
	}
}

void FrameGraph::RegisterGlobalObjectsResUI(GPUJobContext& context)
{
	_RegisterGlobalObjectsRes<Parsing::PassInputType::UI>(context.frame.uiDrawCalls, context, 
		 ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });
}

void FrameGraph::UpdateGlobalObjectsResUI(GPUJobContext& context)
{
	_UpdateGlobalObjectRes<Parsing::PassInputType::UI>(context.frame.uiDrawCalls, context,
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });
}

void FrameGraph::UpdateGlobalObjectsResStatic(GPUJobContext& context)
{
	const std::vector<StaticObject>& staticObjects = Renderer::Inst().staticObjects;

	_UpdateGlobalObjectResIndiced<Parsing::PassInputType::Static>(staticObjects, context.frame.visibleStaticObjectsIndices, context,
		ResContext{ objGlobalResTemplate, objGlobalRes, objGlobalResMemory, perObjectGlobalMemorySize });
}

void FrameGraph::RegisterGlobaPasslRes(GPUJobContext& context)
{

	assert(passGlobalMemory == Const::INVALID_BUFFER_HANDLER && "Pass Global Memory shouldn't be preallocated");

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
				assert(false && "Root constant is not implemented");
			}

			if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
			{
				arg.gpuMem.handler = passGlobalMemory;
				arg.gpuMem.offset = offset;

				offset += RootArg::GetConstBufftSize(arg);
			}

			if constexpr (std::is_same_v<T, RootArg::DescTable>)
			{
				arg.viewIndex = RootArg::AllocateDescTableView(arg);

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
							assert(false && "Desc table view is probably not implemented! Make sure it is");
							//#TODO make view allocation
							descTableEntitiy.gpuMem.handler = passGlobalMemory;
							descTableEntitiy.gpuMem.offset = offset;

							offset += RootArg::GetConstBufftSize(descTableEntitiy);
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
						{
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
				assert(false && "Root constants are not implemented");
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
							assert(false && "Desc table view is probably not implemented! Make sure it is");
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
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