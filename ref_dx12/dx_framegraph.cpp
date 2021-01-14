#include "dx_framegraph.h"

#include "dx_app.h"
#include "dx_jobmultithreading.h"
#include "dx_memorymanager.h"
#include "dx_rendercallbacks.h"

FrameGraph::FrameGraph()
{
	for (BufferHandler& handler : objGlobalResMemory)
	{
		handler = Const::INVALID_BUFFER_HANDLER;
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
}

void FrameGraph::Execute(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	Renderer& renderer = Renderer::Inst();
	JobQueue& jobQueue = JobSystem::Inst().GetJobQueue();

	// Some preparations
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&frame.uiViewMat, tempMat);

	tempMat = XMMatrixOrthographicRH(frame.camera.width, frame.camera.height, 0.0f, 1.0f);
	XMStoreFloat4x4(&frame.uiProjectionMat, tempMat);

	// NOTE: creation order is the order in which command Lists will be submitted.
	// Set up dependencies 

	GPUJobContext initFrameGraphJobContext = renderer.CreateContext(frame);

	std::vector<GPUJobContext> framePassContexts;
	framePassContexts.emplace_back(renderer.CreateContext(frame));

	for (Pass_t& pass : passes)
	{
		framePassContexts.emplace_back(renderer.CreateContext(frame));

		framePassContexts.back().CreateDependencyFrom({&initFrameGraphJobContext});
	};

	GPUJobContext endFrameJobContext = renderer.CreateContext(frame);
	endFrameJobContext.CreateDependencyFrom(framePassContexts);

	jobQueue.Enqueue(Job([initFrameGraphJobContext, &renderer]() mutable
	{
		JOB_GUARD(initFrameGraphJobContext);
		//#DEBUG Wait is not called automatically
		initFrameGraphJobContext.frame.frameGraph.BeginFrame(initFrameGraphJobContext);

	}));

	jobQueue.Enqueue(Job([ctx = framePassContexts[0], &renderer]() mutable
	{
		renderer.BeginFrameJob(ctx);
	}));

	for (int i = 0; i < passes.size(); ++i)
	{
		std::visit([
			&jobQueue,
				// i + 1 because of begin frame job
				passJobContext = framePassContexts[i + 1]](auto&& pass)
			{
				jobQueue.Enqueue(Job(
					[passJobContext, &pass]() mutable
				{
					JOB_GUARD(passJobContext);

					//#DEBUG. Actually I need to figure out dependency stuff.
					// First of all it should be not CreateDependency but rather AddDependency
					// Secondly UI passes ore dependent on UI update. So they are defently should wait for it
					// Finally BeginFRameJob doesn't depend on this routine actually.
					// Is there a way I can handle this with resoure barrier?
					// In ideal world where I am a good programmer, FrameGraph actually meant to handle,
					// these sort of crap
					passJobContext.waitDependancy->Wait();
					//END

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
	RegisterGlobaPasslRes(context);
}

void FrameGraph::BindPassGlobalRes(const std::vector<int>& resIndices, CommandList& commandList) const
{
	for (const int index : resIndices)
	{
		RootArg::Bind(passesGlobalRes[index], commandList);
	}
}

void FrameGraph::BindObjGlobalRes(const std::vector<int>& resIndices, int objIndex, CommandList& commandList, PassParametersSource::InputType objType) const
{
	const std::vector<RootArg::Arg_t>& objRes = objGlobalRes[static_cast<int>(objType)][objIndex];

	for (const int index : resIndices)
	{
		RootArg::Bind(objRes[index], commandList);
	}
}

void FrameGraph::BeginFrame(GPUJobContext& context)
{
	if (isInitalized == false)
	{
		Init(context);

		isInitalized = true;
	}
	
	//#DEBUG should it be inside?
	if (context.frame.uiDrawCalls.empty() == false)
	{
		RegisterGlobalObjectsResUI(context);
		UpdateGlobalObjectsResUI(context);
	}

	UpdateGlobalPasslRes(context);
}

void FrameGraph::EndFrame(GPUJobContext& context)
{
	objGlobalRes[static_cast<int>(PassParametersSource::InputType::UI)].clear();

	auto& updateBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	BufferHandler& perObjectGlobalMemoryUI = 
		objGlobalResMemory[static_cast<int>(PassParametersSource::InputType::UI)];

	if (perObjectGlobalMemoryUI != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.Delete(perObjectGlobalMemoryUI);
		perObjectGlobalMemoryUI = Const::INVALID_BUFFER_HANDLER;
	}
}

void FrameGraph::RegisterGlobalObjectsResUI(GPUJobContext& context)
{
	std::vector<DrawCall_UI_t>& drawCalls = context.frame.uiDrawCalls;

	// Allocate memory
	BufferHandler& objectGlobalMemUI = objGlobalResMemory[static_cast<int>(PassParametersSource::InputType::UI)];

	assert(objectGlobalMemUI == Const::INVALID_BUFFER_HANDLER && "UI per object global mem should is not deallocated");

	if (perObjectGlobalMemoryUISize != 0)
	{
		objectGlobalMemUI = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(perObjectGlobalMemoryUISize * drawCalls.size());
	}

	// Get references on used data
	const std::vector<RootArg::Arg_t>& objResTemplate = objGlobalResTemplate[static_cast<int>(PassParametersSource::InputType::UI)];
	std::vector<std::vector<RootArg::Arg_t>>& objlResources = objGlobalRes[static_cast<int>(PassParametersSource::InputType::UI)];
	
	RenderCallbacks::RegisterGlobalObjectContext regContext = { context };

	for (int i = 0; i < context.frame.uiDrawCalls.size(); ++i)
	{
		DrawCall_UI_t& drawCall = context.frame.uiDrawCalls[i];

		std::vector<RootArg::Arg_t>& objRes = objlResources.emplace_back(objResTemplate);

		const int objectOffset = perObjectGlobalMemoryUISize * i;
		int argOffset = 0;

		for (RootArg::Arg_t& arg : objRes)
		{
			std::visit([&drawCall, &objectGlobalMemUI, objectOffset, &argOffset, &regContext](auto&& arg) 
			{
				using T = std::decay_t<decltype(arg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					arg.gpuMem.handler = objectGlobalMemUI;
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

						std::visit([objectOffset, &argOffset, &drawCall, &regContext, currentViewIndex, &objectGlobalMemUI]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
								//#TODO make view allocation
								descTableEntitiy.gpuMem.handler = objectGlobalMemUI;
								descTableEntitiy.gpuMem.offset = objectOffset + argOffset;

								argOffset += RootArg::GetConstBufftSize(descTableEntitiy);
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
							{
								RenderCallbacks::RegisterGlobalObject(
									descTableEntitiy.hashedName,
									&drawCall,
									&currentViewIndex,
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

void FrameGraph::UpdateGlobalObjectsResUI(GPUJobContext& context)
{
	
	//#DEBUG this should belong to frame (matrix I mean)
	// And delete it in UI pass
	int drawAreaWidth = 0;
	int drawAreaHeight = 0;
	Renderer::Inst().GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);

	XMMATRIX sseYInverseAndCenterMat = XMMatrixIdentity();
	sseYInverseAndCenterMat.r[1] = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	sseYInverseAndCenterMat = XMMatrixTranslation(-drawAreaWidth / 2, -drawAreaHeight / 2, 0.0f) * sseYInverseAndCenterMat;

	Frame& frame = context.frame;

	XMMATRIX sseViewMat = XMLoadFloat4x4(&frame.uiViewMat);
	XMMATRIX sseProjMat = XMLoadFloat4x4(&frame.uiProjectionMat);

	XMMATRIX viewProj = sseYInverseAndCenterMat * sseViewMat * sseProjMat;

	XMFLOAT4X4 yInverseAndCenterMat;
	XMStoreFloat4x4(&yInverseAndCenterMat, viewProj);

	RenderCallbacks::UpdateGlobalObjectContext updateContext = { yInverseAndCenterMat, context };

	std::vector<std::vector<RootArg::Arg_t>>& objGlobalResUI = objGlobalRes[static_cast<int>(PassParametersSource::InputType::UI)];

	std::vector<std::byte> cpuMem(perObjectGlobalMemoryUISize * objGlobalResUI.size(), static_cast<std::byte>(0));

	for (int i = 0; i < objGlobalResUI.size(); ++i)
	{
		std::vector<RootArg::Arg_t>& objRes = objGlobalResUI[i];
		DrawCall_UI_t& drawCall = context.frame.uiDrawCalls[i];

		for (RootArg::Arg_t& arg : objRes)
		{
			std::visit([&updateContext, &drawCall, &cpuMem](auto&& arg)
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
						std::visit([&updateContext, &field, &cpuMem, &fieldOffset](auto&& drawCall)
						{
							RenderCallbacks::UpdateGlobalObject(
								field.hashedName,
								&drawCall,
								&cpuMem[fieldOffset],
								updateContext);

						}, drawCall);

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

	if (perObjectGlobalMemoryUISize != 0)
	{
		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

		BufferHandler objGlobalMemoryUI = objGlobalResMemory[static_cast<int>(PassParametersSource::InputType::UI)];

		FArg::UpdateUploadHeapBuff updateConstBufferArgs;
		updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
		updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(objGlobalMemoryUI);
		updateConstBufferArgs.data = cpuMem.data();
		updateConstBufferArgs.byteSize = cpuMem.size();
		updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

		ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
	}
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
								&currentViewIndex,
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
						&cpuMem[fieldOffset],
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
								&currentViewIndex,
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
