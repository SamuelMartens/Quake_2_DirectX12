#include "dx_framegraph.h"

#include "dx_app.h"
#include "dx_jobmultithreading.h"
#include "dx_memorymanager.h"
#include "dx_rendercallbacks.h"

FrameGraph::~FrameGraph()
{
	if (passGlobalMemory != Const::INVALID_BUFFER_HANDLER)
	{
		MemoryManager::Inst().GetBuff<MemoryManager::Upload>().allocBuffer.allocator.Delete(passGlobalMemory);
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
	std::vector<GPUJobContext> framePassContexts;
	GPUJobContext& beginFrameContext = framePassContexts.emplace_back(renderer.CreateContext(frame));

	for (Pass_t& pass : passes)
	{
		framePassContexts.emplace_back(renderer.CreateContext(frame));
	};

	GPUJobContext endFrameJobContext = renderer.CreateContext(frame);
	endFrameJobContext.CreateDependencyFrom(framePassContexts);

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

void FrameGraph::BeginFrame(GPUJobContext& context)
{
	if (isInitalized == false)
	{
		Init(context);

		isInitalized = true;
	}

	RegisterGlobalObjectsResUI(context);
	
	UpdateGlobalPasslRes(context);

	UpdateGlobalObjectsResUI(context);
}

void FrameGraph::EndFrame(GPUJobContext& context)
{
	objGlobalRes[static_cast<int>(PassParametersSource::InputType::UI)].clear();

	auto& updateBuff = MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	BufferHandler perObjectGlobalMemoryUI = 
		objGlobalResMemory[static_cast<int>(PassParametersSource::InputType::UI)];

	if (perObjectGlobalMemoryUI != Const::INVALID_BUFFER_HANDLER)
	{
		updateBuff.allocBuffer.allocator.Delete(perObjectGlobalMemoryUI);
	}
}

void FrameGraph::RegisterGlobalObjectsResUI(GPUJobContext& context)
{
	std::vector<DrawCall_UI_t>& drawCalls = context.frame.uiDrawCalls;

	if (drawCalls.empty() == true)
	{
		return;
	}

	// Allocate memory
	BufferHandler objectGlobalMemUI = objGlobalResMemory[static_cast<int>(PassParametersSource::InputType::UI)];

	assert(objectGlobalMemUI == Const::INVALID_BUFFER_HANDLER && "UI per object global mem should is not deallocated");

	objectGlobalMemUI = MemoryManager::Inst().GetBuff<MemoryManager::Upload>().allocBuffer.allocator.
		Allocate(perObjectGlobalMemoryUISize * drawCalls.size());

	// Get references on used data
	const std::vector<RootArg::Arg_t>& objResTemplate = objGlobalResTemplate[static_cast<int>(PassParametersSource::InputType::SIZE)];
	std::vector<std::vector<RootArg::Arg_t>>& objlResources = objGlobalRes[static_cast<int>(PassParametersSource::InputType::SIZE)];
	
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
	if (context.frame.uiDrawCalls.empty() == true)
	{
		return;
	}

	RenderCallbacks::UpdateGlobalObjectContext updateContext = { context };

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

	auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = uploadMemoryBuff.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(constBuffMemory);
	updateConstBufferArgs.data = cpuMem.data();
	updateConstBufferArgs.byteSize = cpuMem.size();
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
}

void FrameGraph::RegisterGlobaPasslRes(GPUJobContext& context)
{
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
					const int currentViewIndex = arg.viewIndex + i;

					std::visit([this, &offset, currentViewIndex, &globalPassContext]
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
				for (RootArg::DescTableEntity_t& descTableEntity : arg.content)
				{
					std::visit([&globalPassContext, &cpuMem](auto&& descTableEntity)
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

	auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = uploadMemoryBuff.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(passGlobalMemory);
	updateConstBufferArgs.data = cpuMem.data();
	updateConstBufferArgs.byteSize = cpuMem.size();
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
}
