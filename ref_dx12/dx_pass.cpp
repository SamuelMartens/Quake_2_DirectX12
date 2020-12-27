#include "dx_pass.h"

#include <cassert>

#include "dx_threadingutils.h"
#include "dx_rendercallbacks.h"
#include "dx_utils.h"
#include "dx_settings.h"
#include "dx_app.h"
#include "dx_jobmultithreading.h"
#include "dx_resourcemanager.h"
#include "dx_memorymanager.h"

void Pass_UI::Init()
{
	ASSERT_MAIN_THREAD;

	// UI uses streaming objects. So we need to preallocate piece of memory that will be
	// used for each run

	// Calculate amount of memory for const buffers per objects
	assert(perObjectConstBuffMemorySize == 0 && "Per object const memory should be null");

	for (const RootArg::Arg_t& rootArg : passParameters.perObjectRootArgsTemplate)
	{
		std::visit([this](auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;

			if constexpr (std::is_same_v<T, RootArg::RootConstant>)
			{
				assert(false && "Root constants not implemented");
			}

			if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
			{
				perObjectConstBuffMemorySize += RootArg::GetConstBuffSize(rootArg);
			}

			if constexpr (std::is_same_v<T, RootArg::DescTable>)
			{
				for (const RootArg::DescTableEntity_t& descTableEntitiy : rootArg.content)
				{
					std::visit([this](auto&& descTableEntitiy)
					{
						using T = std::decay_t<decltype(descTableEntitiy)>;

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
						{
							std::for_each(descTableEntitiy.content.begin(), descTableEntitiy.content.end(),
								[this](const RootArg::ConstBuffField& f)
							{
								perObjectConstBuffMemorySize += f.size;
							});
						}

					}, descTableEntitiy);
				}
			}

		}, rootArg);
	}

	// Calculate amount of memory for vertex buffers per objects
	assert(perVertexMemorySize == 0 && "Per Vertex Memory should be null");

	for (const Parsing::VertAttrField& field : passParameters.vertAttr.content)
	{
		perVertexMemorySize += Parsing::GetParseDataTypeSize(field.type);
	}

	assert(perObjectVertexMemorySize == 0 && "Per Object Vertex Memory should be null");
	// Every UI object is quad that consists of two triangles
	perObjectVertexMemorySize = perVertexMemorySize * 6;


	// Init inverse matrix

	// Generate utils matrices
	int drawAreaWidth = 0;
	int drawAreaHeight = 0;

	Renderer::Inst().GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);

	XMMATRIX sseResultMatrix = XMMatrixIdentity();
	sseResultMatrix.r[1] = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	sseResultMatrix = XMMatrixTranslation(-drawAreaWidth / 2, -drawAreaHeight / 2, 0.0f) * sseResultMatrix;
	XMStoreFloat4x4(&yInverseAndCenterMatrix, sseResultMatrix);
}

void Pass_UI::Start(Context& jobCtx)
{
	const std::vector<DrawCall_UI_t>& objects = jobCtx.frame.uiDrawCalls;
	MemoryManager::UploadBuff_t& uploadMemory =
		MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	//Check if we need to free this memory, maybe same amount needs to allocated
	constBuffMemory = uploadMemory.Allocate(perObjectConstBuffMemorySize * objects.size());
	vertexMemory = uploadMemory.Allocate(perObjectVertexMemorySize * objects.size());

	for (int i = 0; i < objects.size(); ++i)
	{
		// Special copy routine is required here.
		StageObj& stageObj = drawObjects.emplace_back(StageObj{ 
			passParameters.perObjectRootArgsTemplate,
			&objects[i] });

		// Init object root args

		const int objectOffset = i * perObjectConstBuffMemorySize;
		int rootArgOffset = 0;

		RenderCallbacks::PerObjectRegisterContext regCtx = { jobCtx };

		for (RootArg::Arg_t& rootArg : stageObj.rootArgs)
		{
			std::visit([this, i, objectOffset, &rootArgOffset, &stageObj, &regCtx]
			(auto&& rootArg)
			{
				using T = std::decay_t<decltype(rootArg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					rootArg.gpuMem.handler = constBuffMemory;
      					rootArg.gpuMem.offset = objectOffset + rootArgOffset;

					rootArgOffset += RootArg::GetConstBuffSize(rootArg);
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					rootArg.viewIndex = RootArg::AllocateDescTableView(rootArg);

					for (int i = 0; i < rootArg.content.size(); ++i)
					{
						RootArg::DescTableEntity_t& descTableEntitiy = rootArg.content[i];
						const int currentViewIndex = rootArg.viewIndex + i;

						std::visit([this, objectOffset, &rootArgOffset, &stageObj, &regCtx, currentViewIndex]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
								//#DEBUG this needs view allocation
								descTableEntitiy.gpuMem.handler = constBuffMemory;
								descTableEntitiy.gpuMem.offset = objectOffset + rootArgOffset;

								rootArgOffset += RootArg::GetConstBuffSize(descTableEntitiy);
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
							{
								RenderCallbacks::PerObjectRegisterCallback(
									HASH(passParameters.name.c_str()),
									descTableEntitiy.hashedName,
									*stageObj.originalDrawCall,
									&currentViewIndex,
									regCtx
								);
							}

						}, descTableEntitiy);
					}
				}

			}, rootArg);
		}
	}

	// Init vertex data

	for (int i = 0; i < objects.size(); ++i)
	{
		std::visit([i, this](auto&& drawCall) 
		{
			using T = std::decay_t<decltype(drawCall)>;

			assert(perVertexMemorySize == sizeof(ShDef::Vert::PosTexCoord) && "Per vertex memory invalid size");
			Renderer& renderer = Renderer::Inst();
			MemoryManager::UploadBuff_t& uploadMemory =
				MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

			if constexpr (std::is_same_v<T, DrawCall_Pic>)
			{
				std::array<char, MAX_QPATH> texFullName;
			 	ResourceManager::Inst().GetDrawTextureFullname(drawCall.name.c_str(), texFullName.data(), texFullName.size());

				const Texture& texture = *ResourceManager::Inst().FindTexture(texFullName.data());

				std::array<ShDef::Vert::PosTexCoord, 6> vertices;
				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(texture.width, texture.height),
					XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(1.0f, 1.0f),
					vertices.data());

				const int uploadBuffOffset = uploadMemory.GetOffset(vertexMemory) + perObjectVertexMemorySize * i;

				FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
				updateVertexBufferArgs.buffer = uploadMemory.allocBuffer.gpuBuffer;
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				ResourceManager::Inst().UpdateUploadHeapBuff(updateVertexBufferArgs);
			}
			else if constexpr (std::is_same_v<T, DrawCall_Char>)
			{
				int num = drawCall.num & 0xFF;

				constexpr int charSize = 8;

				if ((num & 127) == 32)
					return;		// space

				if (drawCall.y <= -charSize)
					return;		// totally off screen

				constexpr float texCoordScale = 0.0625f;

				const float uCoord = (num & 15) * texCoordScale;
				const float vCoord = (num >> 4) * texCoordScale;
				const float texSize = texCoordScale;

				std::array<ShDef::Vert::PosTexCoord, 6> vertices;
				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(charSize, charSize),
					XMFLOAT2(uCoord, vCoord),
					XMFLOAT2(uCoord + texSize, vCoord + texSize),
					vertices.data());

				const int uploadBuffOffset = uploadMemory.GetOffset(vertexMemory) + perObjectVertexMemorySize * i;

				FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
				updateVertexBufferArgs.buffer = uploadMemory.allocBuffer.gpuBuffer;
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				ResourceManager::Inst().UpdateUploadHeapBuff(updateVertexBufferArgs);

			}
			else if constexpr (std::is_same_v<T, DrawCall_StretchRaw>)
			{
				std::array<ShDef::Vert::PosTexCoord, 6> vertices;
				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(drawCall.quadWidth, drawCall.quadHeight),
					XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(1.0f, 1.0f),
					vertices.data());

				const int uploadBuffOffset = uploadMemory.GetOffset(vertexMemory) + perObjectVertexMemorySize * i;

				FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
				updateVertexBufferArgs.buffer = uploadMemory.allocBuffer.gpuBuffer;
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				ResourceManager::Inst().UpdateUploadHeapBuff(updateVertexBufferArgs);
			}

		}, objects[i]);
	}
}

void Pass_UI::UpdateDrawObjects(Context& jobCtx)
{
	RenderCallbacks::PerObjectUpdateContext updateCtx = { XMFLOAT4X4(), jobCtx };

	Frame& frame = updateCtx.jobCtx.frame;

	XMMATRIX sseViewMat = XMLoadFloat4x4(&frame.uiViewMat);
	XMMATRIX sseProjMat = XMLoadFloat4x4(&frame.uiProjectionMat);
	XMMATRIX sseYInverseAndCenterMat = XMLoadFloat4x4(&yInverseAndCenterMatrix);

	XMMATRIX viewProj = sseYInverseAndCenterMat * sseViewMat * sseProjMat;

	XMStoreFloat4x4(&updateCtx.viewProjMat, viewProj);

	std::vector<std::byte> cpuMem(perObjectConstBuffMemorySize * drawObjects.size(), static_cast<std::byte>(0));

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		StageObj& obj = drawObjects[i];

		// Start of the memory for the current object
		int currentObjecOffset = i * perObjectConstBuffMemorySize;

		for (RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			std::visit([&updateCtx, &obj, this,  &currentObjecOffset, &cpuMem](auto&& rootArg) 
			{
				using T = std::decay_t<decltype(rootArg)>;

				Renderer& renderer = Renderer::Inst();

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				};

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					// Start of the memory of the current buffer
					int currentBufferOffset = currentObjecOffset;

					// If this is true, which it should be, some offset calculation here could be greatly simplified.
					//#DEBUG this is valid assumption, eh?
					assert(currentBufferOffset == rootArg.gpuMem.offset && "Update offset for const buffer is not equal, eh?");

					for (RootArg::ConstBuffField& field : rootArg.content)
					{
						std::visit([&rootArg, &updateCtx, this, &currentObjecOffset, &field, &cpuMem, &currentBufferOffset](auto&& obj)
						{
							RenderCallbacks::PerObjectUpdateCallback(
								HASH(passParameters.name.c_str()),
								field.hashedName,
								obj,
								&cpuMem[currentBufferOffset],
								updateCtx);

						}, *obj.originalDrawCall);

						// Proceed to next buffer
						currentBufferOffset += field.size;
					}

					currentObjecOffset += RootArg::GetConstBuffSize(rootArg);
					assert(currentBufferOffset < currentObjecOffset && "Update error. Buffers overwrite other buffer");
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					for (RootArg::DescTableEntity_t& descTableEntity : rootArg.content)
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

			}, rootArg);
		}

		assert(currentObjecOffset <= (i + 1) * perObjectConstBuffMemorySize && "Update error. Per object const buff offset overwrites next obj memory");
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

void Pass_UI::SetUpRenderState(Context& jobCtx)
{
	Frame& frame = jobCtx.frame;
	ComPtr<ID3D12GraphicsCommandList>& commandList = jobCtx.commandList.commandList;


	commandList->RSSetViewports(1, &passParameters.viewport);
	commandList->RSSetScissorRects(1, &frame.scissorRect);

	Renderer& renderer = Renderer::Inst();

	assert(passParameters.colorTargetNameHash == HASH("BACK_BUFFER") && "Custom render targets are not implemented");
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = renderer.rtvHeap->GetHandleCPU(frame.colorBufferAndView->viewIndex);

	assert(passParameters.depthTargetNameHash == HASH("BACK_BUFFER") && "Custom render targets are not implemented");
	D3D12_CPU_DESCRIPTOR_HANDLE depthTargetView = renderer.dsvHeap->GetHandleCPU(frame.depthBufferViewIndex);

	commandList->OMSetRenderTargets(1, &renderTargetView, true, &depthTargetView);

	ID3D12DescriptorHeap* descriptorHeaps[] = { renderer.cbvSrvHeap->GetHeapResource(),	renderer.samplerHeap->GetHeapResource() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);


	commandList->SetGraphicsRootSignature(passParameters.rootSingature.Get());
	commandList->SetPipelineState(passParameters.pipelineState.Get());
	commandList->IASetPrimitiveTopology(passParameters.primitiveTopology);
}

void Pass_UI::Draw(Context& jobCtx)
{
	ComPtr<ID3D12GraphicsCommandList>& commandList = jobCtx.commandList.commandList;
	Renderer& renderer = Renderer::Inst();

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	vertexBufferView.StrideInBytes = perVertexMemorySize;
	vertexBufferView.SizeInBytes = perObjectVertexMemorySize;

	MemoryManager::UploadBuff_t& uploadMemory =
		MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		const StageObj& obj = drawObjects[i];

		vertexBufferView.BufferLocation = uploadMemory.allocBuffer.gpuBuffer->GetGPUVirtualAddress() +
			uploadMemory.GetOffset(vertexMemory) + i * perObjectVertexMemorySize;

		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

		for (const RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			RootArg::Bind(rootArg, jobCtx.commandList);
		}

		commandList->DrawInstanced(perObjectVertexMemorySize / perVertexMemorySize, 1, 0, 0);
	}
}

void Pass_UI::Execute(Context& context)
{
	if (context.frame.uiDrawCalls.empty() == true)
	{
		return;
	}

	Start(context);
	
	UpdateDrawObjects(context);

	SetUpRenderState(context);
	Draw(context);
}

void Pass_UI::Finish()
{
	MemoryManager::UploadBuff_t& uploadMemory =
		MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	if (constBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		uploadMemory.Delete(constBuffMemory);
		constBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}
	
	if (vertexMemory != Const::INVALID_BUFFER_HANDLER )
	{
		uploadMemory.Delete(vertexMemory);
		vertexMemory = Const::INVALID_BUFFER_HANDLER;
	}
	
	drawObjects.clear();
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

	//#TODO get rid of begin frame hack
	// NOTE: creation order is the order in which command Lists will be submitted.
	// Set up dependencies 
	
	std::vector<Context> framePassContexts;
	//#DEBUG this is not even frame pass context
	Context& beginFrameContext = framePassContexts.emplace_back(renderer.CreateContext(frame));

	for (Pass_t& pass : passes)
	{
		framePassContexts.emplace_back(renderer.CreateContext(frame));
	};

	Context endFrameJobContext = renderer.CreateContext(frame);
	endFrameJobContext.CreateDependencyFrom(framePassContexts);

	jobQueue.Enqueue(Job([ctx = framePassContexts[0], &renderer]() mutable
	{
		renderer.BeginFrameJob(ctx);
	}));

	for (int i = 0; i < passes.size() ; ++i)
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

	jobQueue.Enqueue(Job([endFrameJobContext ,&renderer, this]() mutable 
	{
		renderer.EndFrameJob(endFrameJobContext);
	}));
}

void FrameGraph::BuildFrameGraph(PassMaterial&& passMaterial)
{
	passes.clear();

	for (int i = 0; i < passMaterial.passes.size(); ++i)
	{
		PassParameters& pass = passMaterial.passes[i];

		switch (pass.input)
		{
		case PassParametersSource::InputType::UI:
		{
			Pass_t& renderStage = passes.emplace_back(Pass_UI{});
			InitPass(std::move(pass), renderStage);
		}
			break;
		case PassParametersSource::InputType::Undefined:
		{
			assert(false && "Pass with undefined input is detected");
		}
			break;
		default:
			break;
		}
	}
}

void FrameGraph::InitPass(PassParameters&& passParameters, Pass_t& pass)
{
	std::visit([&passParameters](auto&& pass) 
	{
		pass.passParameters = std::move(passParameters);

		pass.Init();

	}, pass);
}
