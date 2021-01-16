#include "dx_pass.h"

#include <cassert>
#include <numeric>

#include "dx_threadingutils.h"
#include "dx_rendercallbacks.h"
#include "dx_utils.h"
#include "dx_settings.h"
#include "dx_app.h"
#include "dx_jobmultithreading.h"
#include "dx_resourcemanager.h"
#include "dx_memorymanager.h"

void Pass_UI::Init(PassParameters&& parameters)
{
	ASSERT_MAIN_THREAD;

	passParameters = std::move(parameters);

	// Pass memory exists have the same lifetime as pass itself. So unlike objects memory
	// I can allocate it only one time
	assert(passMemorySize == 0 && "Pass memory size should be null");
	passMemorySize = RootArg::GetSize(passParameters.passLocalRootArgs);

	if (passMemorySize > 0)
	{
		MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passMemorySize);
	}

	// UI uses streaming objects. So we need to preallocate piece of memory that will be
	// used for each run

	// Calculate amount of memory for const buffers per objects
	assert(perObjectConstBuffMemorySize == 0 && "Per object const memory should be null");
	perObjectConstBuffMemorySize =  RootArg::GetSize(passParameters.perObjectLocalRootArgsTemplate);


	// Calculate amount of memory for vertex buffers per objects
	assert(perVertexMemorySize == 0 && "Per Vertex Memory should be null");

	perVertexMemorySize = std::accumulate(passParameters.vertAttr.content.cbegin(), passParameters.vertAttr.content.cend(),
		0, [](int& sum, const Parsing::VertAttrField& field) 
	{
		return sum + Parsing::GetParseDataTypeSize(field.type);
	});

	assert(perObjectVertexMemorySize == 0 && "Per Object Vertex Memory should be null");
	// Every UI object is quad that consists of two triangles
	perObjectVertexMemorySize = perVertexMemorySize * 6;
}

void Pass_UI::Start(GPUJobContext& jobCtx)
{
	const std::vector<DrawCall_UI_t>& objects = jobCtx.frame.uiDrawCalls;
	auto& uploadMemory =
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	//Check if we need to free this memory, maybe same amount needs to allocated

	if (perObjectConstBuffMemorySize != 0)
	{
		objectConstBuffMemory = uploadMemory.Allocate(perObjectConstBuffMemorySize * objects.size());
	}

	vertexMemory = uploadMemory.Allocate(perObjectVertexMemorySize * objects.size());

	for (int i = 0; i < objects.size(); ++i)
	{
		// Special copy routine is required here.
		PassObj& passObj = drawObjects.emplace_back(PassObj{
			passParameters.perObjectLocalRootArgsTemplate,
			&objects[i] });

		// Init object root args

		const int objectOffset = i * perObjectConstBuffMemorySize;
		int rootArgOffset = 0;

		RenderCallbacks::RegisterLocalObjectContext regCtx = { jobCtx };

		for (RootArg::Arg_t& rootArg : passObj.rootArgs)
		{
			std::visit([this, i, objectOffset, &rootArgOffset, &passObj, &regCtx]
			(auto&& rootArg)
			{
				using T = std::decay_t<decltype(rootArg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					rootArg.gpuMem.handler = objectConstBuffMemory;
      				rootArg.gpuMem.offset = objectOffset + rootArgOffset;

					rootArgOffset += RootArg::GetConstBufftSize(rootArg);
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					rootArg.viewIndex = RootArg::AllocateDescTableView(rootArg);

					for (int i = 0; i < rootArg.content.size(); ++i)
					{
						RootArg::DescTableEntity_t& descTableEntitiy = rootArg.content[i];
						int currentViewIndex = rootArg.viewIndex + i;

						std::visit([this, objectOffset, &rootArgOffset, &passObj, &regCtx, &currentViewIndex]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
								//#TODO make view allocation
								descTableEntitiy.gpuMem.handler = objectConstBuffMemory;
								descTableEntitiy.gpuMem.offset = objectOffset + rootArgOffset;

								rootArgOffset += RootArg::GetConstBufftSize(descTableEntitiy);
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
							{

								std::visit([this, &descTableEntitiy, &currentViewIndex, &regCtx](auto&& drawCall)
								{
									RenderCallbacks::RegisterLocalObject(
										HASH(passParameters.name.c_str()),
										descTableEntitiy.hashedName,
										drawCall,
										currentViewIndex,
										regCtx
										);

								}, *passObj.originalDrawCall);
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
			auto& uploadMemory =
				MemoryManager::Inst().GetBuff<UploadBuffer_t>();

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
				updateVertexBufferArgs.buffer = uploadMemory.GetGpuBuffer();
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
				updateVertexBufferArgs.buffer = uploadMemory.GetGpuBuffer();
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
				updateVertexBufferArgs.buffer = uploadMemory.GetGpuBuffer();
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				ResourceManager::Inst().UpdateUploadHeapBuff(updateVertexBufferArgs);
			}

		}, objects[i]);
	}
}

void Pass_UI::RegisterPassResources(GPUJobContext& jobCtx)
{
	RenderCallbacks::RegisterLocalPassContext localPassContext = { jobCtx };

	int offset = 0;

	for (RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
	{
		std::visit([this, &offset, &localPassContext](auto&& arg) 
		{
			using T = std::decay_t<decltype(arg)>;

			if constexpr (std::is_same_v<T, RootArg::RootConstant>)
			{
				assert(false && "Root constant is not implemented");
			}

			if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
			{
				arg.gpuMem.handler = passConstBuffMemory;
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

					std::visit([this, &offset, &currentViewIndex, &localPassContext]
					(auto&& descTableEntitiy)
					{
						using T = std::decay_t<decltype(descTableEntitiy)>;

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
						{
							assert(false && "Desc table view is probably not implemented! Make sure it is");
							//#TODO make view allocation
							descTableEntitiy.gpuMem.handler = passConstBuffMemory;
							descTableEntitiy.gpuMem.offset = offset;

							offset += RootArg::GetConstBufftSize(descTableEntitiy);
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
						{
							RenderCallbacks::RegisterLocalPass(
								HASH(passParameters.name.c_str()),
								descTableEntitiy.hashedName,
								*this,
								currentViewIndex,
								localPassContext);
						}

					}, descTableEntitiy);
				}
			}

		}, arg);
	}
}

void Pass_UI::UpdatePassResources(GPUJobContext& jobCtx)
{
	RenderCallbacks::UpdateLocalPassContext localPassContext = { jobCtx };

	std::vector<std::byte> cpuMem(passMemorySize, static_cast<std::byte>(0));

	for (RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
	{
		std::visit([this, &localPassContext, &cpuMem](auto&& arg)
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
					RenderCallbacks::UpdateLocalPass(
						HASH(passParameters.name.c_str()),
						field.hashedName,
						*this,
						cpuMem[fieldOffset],
						localPassContext
					);

					// Proceed to next buffer
					fieldOffset += field.size;
				}
			}

			if constexpr (std::is_same_v<T, RootArg::DescTable>)
			{
				for (int i = 0; i < arg.content.size(); ++i)
				{
					RootArg::DescTableEntity_t& descTableEntity = arg.content[i];
					int currentViewIndex = arg.viewIndex + i;

					std::visit([this, &localPassContext, &cpuMem, &currentViewIndex](auto&& descTableEntity)
					{
						using T = std::decay_t<decltype(descTableEntity)>;

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
						{
							assert(false && "Desc table view is probably not implemented! Make sure it is");
						}

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
						{
					
							RenderCallbacks::UpdateLocalPass(
								HASH(passParameters.name.c_str()),
								descTableEntity.hashedName,
								*this,
								currentViewIndex,
								localPassContext
							);
						}

					}, descTableEntity);

				}
			}

		}, arg);
	}

	if (passMemorySize != 0)
	{
		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

		FArg::UpdateUploadHeapBuff updateConstBufferArgs;
		updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
		updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(passConstBuffMemory);
		updateConstBufferArgs.data = cpuMem.data();
		updateConstBufferArgs.byteSize = cpuMem.size();
		updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

		ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
	}
}

void Pass_UI::UpdateDrawObjects(GPUJobContext& jobCtx)
{
	RenderCallbacks::UpdateLocalObjectContext updateContext = { jobCtx };

	std::vector<std::byte> cpuMem(perObjectConstBuffMemorySize * drawObjects.size(), static_cast<std::byte>(0));

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		PassObj& obj = drawObjects[i];

		for (RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			std::visit([&updateContext, &obj, this, &cpuMem](auto&& rootArg) 
			{
				using T = std::decay_t<decltype(rootArg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				};

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					// Start of the memory of the current buffer
					int fieldOffset = rootArg.gpuMem.offset;

					for (RootArg::ConstBuffField& field : rootArg.content)
					{
						std::visit([this, &updateContext, &field, &cpuMem, &fieldOffset](auto&& obj)
						{
							RenderCallbacks::UpdateLocalObject(
								HASH(passParameters.name.c_str()),
								field.hashedName,
								obj,
								cpuMem[fieldOffset],
								updateContext);

						}, *obj.originalDrawCall);

						// Proceed to next buffer
						fieldOffset += field.size;
					}
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					for (int i = 0; i < rootArg.content.size(); ++i)
					{

						RootArg::DescTableEntity_t& descTableEntity = rootArg.content[i];
						int currentViewIndex = rootArg.viewIndex + i;

						std::visit([this, &updateContext, &cpuMem, &currentViewIndex](auto&& descTableEntity, auto&& obj)
						{
							using T = std::decay_t<decltype(descTableEntity)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
							{
								RenderCallbacks::UpdateLocalObject(
									HASH(passParameters.name.c_str()),
									descTableEntity.hashedName,
									obj,
									currentViewIndex,
									updateContext
								);
							}

						}, descTableEntity, *obj.originalDrawCall);

					}
				}

			}, rootArg);
		}
	}

	if (perObjectConstBuffMemorySize != 0)
	{
		auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

		FArg::UpdateUploadHeapBuff updateConstBufferArgs;
		updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
		updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(objectConstBuffMemory);
		updateConstBufferArgs.data = cpuMem.data();
		updateConstBufferArgs.byteSize = cpuMem.size();
		updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

		ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
	}
}

void Pass_UI::SetUpRenderState(GPUJobContext& jobCtx)
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

void Pass_UI::Draw(GPUJobContext& jobCtx)
{
	CommandList& commandList = jobCtx.commandList;
	Renderer& renderer = Renderer::Inst();
	const FrameGraph& frameGraph = jobCtx.frame.frameGraph;

	// Bind pass global argument
	frameGraph.BindPassGlobalRes(passParameters.passGlobalRootArgsIndices, commandList);

	// Bind pass local arguments
	for (const RootArg::Arg_t& arg :  passParameters.passLocalRootArgs)
	{
		RootArg::Bind(arg, commandList);
	}

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	vertexBufferView.StrideInBytes = perVertexMemorySize;
	vertexBufferView.SizeInBytes = perObjectVertexMemorySize;

	auto& uploadMemory =
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		const PassObj& obj = drawObjects[i];

		vertexBufferView.BufferLocation = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress() +
			uploadMemory.GetOffset(vertexMemory) + i * perObjectVertexMemorySize;

		commandList.commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		
		// Bind global args
		frameGraph.BindObjGlobalRes(passParameters.perObjGlobalRootArgsIndicesTemplate, i,
			commandList, Parsing::PassInputType::UI);


		// Bind local args
		for (const RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			RootArg::Bind(rootArg, jobCtx.commandList);
		}

		commandList.commandList->DrawInstanced(perObjectVertexMemorySize / perVertexMemorySize, 1, 0, 0);
	}
}

Pass_UI::~Pass_UI()
{
	ReleasePerFrameResources();

	auto& uploadMemory =
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	if (passConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		uploadMemory.Delete(passConstBuffMemory);
		passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}
}

void Pass_UI::Execute(GPUJobContext& context)
{
	if (context.frame.uiDrawCalls.empty() == true)
	{
		return;
	}
	
	Start(context);
	
	UpdatePassResources(context);
	UpdateDrawObjects(context);

	SetUpRenderState(context);
	Draw(context);
}

void Pass_UI::ReleasePerFrameResources()
{
	auto& uploadMemory =
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	if (objectConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		uploadMemory.Delete(objectConstBuffMemory);
		objectConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}
	
	if (vertexMemory != Const::INVALID_BUFFER_HANDLER )
	{
		uploadMemory.Delete(vertexMemory);
		vertexMemory = Const::INVALID_BUFFER_HANDLER;
	}
	
	drawObjects.clear();
}

