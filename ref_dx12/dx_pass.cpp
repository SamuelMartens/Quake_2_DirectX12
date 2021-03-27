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


namespace
{
	template<typename T>
	void _RegisterPassResources(T& pass, PassParameters& passParameters, GPUJobContext& context)
	{
		RenderCallbacks::RegisterLocalPassContext localPassContext = { context };

		for (RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
		{
			std::visit([&pass, &localPassContext, &passParameters]
			(auto&& arg)
			{
				using T = std::decay_t<decltype(arg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
					
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					arg.viewIndex = RootArg::AllocateDescTableView(arg);

					for (int i = 0; i < arg.content.size(); ++i)
					{
						RootArg::DescTableEntity_t& descTableEntitiy = arg.content[i];
						int currentViewIndex = arg.viewIndex + i;

						std::visit([&pass, &currentViewIndex, &localPassContext, &passParameters]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
							{
								if (descTableEntitiy.internalBindName.has_value())
								{
									// This is internal resource
									RenderCallbacks::RegisterInternal<D3D12_SHADER_RESOURCE_VIEW_DESC>(
										currentViewIndex,
										*descTableEntitiy.internalBindName
									);
								}
								else
								{
									RenderCallbacks::RegisterLocalPass(
										HASH(passParameters.name.c_str()),
										descTableEntitiy.hashedName,
										pass,
										currentViewIndex,
										localPassContext);
								}
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_UAView>)
							{
								if (descTableEntitiy.internalBindName.has_value())
								{
									// This is internal resource
									RenderCallbacks::RegisterInternal<D3D12_UNORDERED_ACCESS_VIEW_DESC>(
										currentViewIndex,
										*descTableEntitiy.internalBindName
										);
								}
								else
								{
									RenderCallbacks::RegisterLocalPass(
										HASH(passParameters.name.c_str()),
										descTableEntitiy.hashedName,
										pass,
										currentViewIndex,
										localPassContext);
								}
							}

						}, descTableEntitiy);
					}
				}

			}, arg);
		}
	}

	template<typename T>
	void _UpdatePassResources(T& pass, PassParameters& passParameters, BufferHandler constBuffMemory, int constBuffMemorySize, GPUJobContext& context)
	{
		RenderCallbacks::UpdateLocalPassContext localPassContext = { context };

		std::vector<std::byte> cpuMem(constBuffMemorySize, static_cast<std::byte>(0));

		for (RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
		{
			std::visit([&pass, &localPassContext, &cpuMem, &passParameters](auto&& arg)
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
							pass,
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

						std::visit([&pass, &localPassContext, &cpuMem, &currentViewIndex, &passParameters](auto&& descTableEntity)
						{
							using T = std::decay_t<decltype(descTableEntity)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture> ||
								std::is_same_v<T, RootArg::DescTableEntity_UAView>)
							{

								RenderCallbacks::UpdateLocalPass(
									HASH(passParameters.name.c_str()),
									descTableEntity.hashedName,
									pass,
									currentViewIndex,
									localPassContext
								);
							}

						}, descTableEntity);

					}
				}

			}, arg);
		}

		if (constBuffMemorySize != 0)
		{
			auto& uploadMemoryBuff = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

			FArg::UpdateUploadHeapBuff updateConstBufferArgs;
			updateConstBufferArgs.buffer = uploadMemoryBuff.GetGpuBuffer();
			updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(constBuffMemory);
			updateConstBufferArgs.data = cpuMem.data();
			updateConstBufferArgs.byteSize = cpuMem.size();
			updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

			ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
		}
	}

	template<typename T>
	void _RegisterObjectArgs(T& obj, unsigned int passHashedName, RenderCallbacks::RegisterLocalObjectContext& regCtx)
	{
		for (RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			std::visit([&obj, &regCtx, passHashedName]
			(auto&& rootArg)
			{
				using T = std::decay_t<decltype(rootArg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
				{
				
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					rootArg.viewIndex = RootArg::AllocateDescTableView(rootArg);

					for (int i = 0; i < rootArg.content.size(); ++i)
					{
						RootArg::DescTableEntity_t& descTableEntitiy = rootArg.content[i];
						int currentViewIndex = rootArg.viewIndex + i;

						std::visit([&obj, &regCtx, &currentViewIndex, passHashedName]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Const buffer view is not implemented");
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture> ||
								std::is_same_v<T, RootArg::DescTableEntity_UAView>)
							{
								assert(descTableEntitiy.internalBindName.has_value() == false && 
									"PerObject resources is not suited to use internal bind");

								RenderCallbacks::RegisterLocalObject(
									passHashedName,
									descTableEntitiy.hashedName,
									*obj.originalObj,
									currentViewIndex,
									regCtx
								);
							}

						}, descTableEntitiy);
					}
				}

			}, rootArg);
		}
	}

	template<typename T>
	void _UpdateObjectArgs(T& obj, std::byte* cpuMem, unsigned int passHashedName, RenderCallbacks::UpdateLocalObjectContext& updateContext)
	{
		for (RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			std::visit([&updateContext, &obj, &cpuMem, passHashedName](auto&& rootArg)
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
						RenderCallbacks::UpdateLocalObject(
							passHashedName,
							field.hashedName,
							*obj.originalObj,
							cpuMem[fieldOffset],
							updateContext);

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

						std::visit([passHashedName, &updateContext, &cpuMem, &currentViewIndex, &obj](auto&& descTableEntity)
						{
							using T = std::decay_t<decltype(descTableEntity)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture> ||
								std::is_same_v<T, RootArg::DescTableEntity_UAView>)
							{
								RenderCallbacks::UpdateLocalObject(
									passHashedName,
									descTableEntity.hashedName,
									*obj.originalObj,
									currentViewIndex,
									updateContext
								);
							}

						}, descTableEntity);

					}
				}

			}, rootArg);
		}
	}

	void _SetRenderState(const PassParameters& params, GPUJobContext& context)
	{
		Frame& frame = context.frame;
		ID3D12GraphicsCommandList* commandList = context.commandList->GetGPUList();


		commandList->RSSetViewports(1, &params.viewport);
		commandList->RSSetScissorRects(1, &frame.scissorRect);

		Renderer& renderer = Renderer::Inst();

		const int colorRenderTargetViewIndex = params.colorTargetName == PassParameters::BACK_BUFFER_NAME ?
			frame.colorBufferAndView->viewIndex :
			params.colorTargetViewIndex;

		D3D12_CPU_DESCRIPTOR_HANDLE colorRenderTargetView = renderer.rtvHeap->GetHandleCPU(colorRenderTargetViewIndex);

		const int depthRenderTargetViewIndex = params.depthTargetName == PassParameters::BACK_BUFFER_NAME ?
			frame.depthBufferViewIndex :
			params.depthTargetViewIndex;

		D3D12_CPU_DESCRIPTOR_HANDLE depthRenderTargetView = renderer.dsvHeap->GetHandleCPU(depthRenderTargetViewIndex);

		commandList->OMSetRenderTargets(1, &colorRenderTargetView, true, &depthRenderTargetView);

		ID3D12DescriptorHeap* descriptorHeaps[] = { renderer.cbvSrvHeap->GetHeapResource(),	renderer.samplerHeap->GetHeapResource() };
		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		commandList->SetGraphicsRootSignature(params.rootSingature.Get());
		commandList->SetPipelineState(params.pipelineState.Get());
		commandList->IASetPrimitiveTopology(params.primitiveTopology);
	}
}



void Pass_UI::Init()
{
	// Pass memory exists have the same lifetime as pass itself. So unlike objects memory
	// I can allocate it only one time
	assert(passMemorySize == Const::INVALID_SIZE && "Pass_UI memory size should be uninitialized");
	passMemorySize = RootArg::GetSize(passParameters.passLocalRootArgs);

	if (passMemorySize > 0)
	{
		assert(passConstBuffMemory == Const::INVALID_BUFFER_HANDLER && "Pass_UI not cleaned up memory");
		passConstBuffMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passMemorySize);
	}

	// UI uses streaming objects. So we need to preallocate piece of memory that will be
	// used for each run

	// Calculate amount of memory for const buffers per objects
	assert(perObjectConstBuffMemorySize == Const::INVALID_SIZE && "Per object const memory should be uninitialized");
	perObjectConstBuffMemorySize =  RootArg::GetSize(passParameters.perObjectLocalRootArgsTemplate);


	// Calculate amount of memory for vertex buffers per objects
	assert(perVertexMemorySize == Const::INVALID_SIZE && "Per Vertex Memory should be uninitialized");

	perVertexMemorySize = std::accumulate(passParameters.vertAttr->content.cbegin(), passParameters.vertAttr->content.cend(),
		0, [](int& sum, const Parsing::VertAttrField& field) 
	{
		return sum + Parsing::GetParseDataTypeSize(field.type);
	});

	assert(perObjectVertexMemorySize == Const::INVALID_SIZE && "Per Object Vertex Memory should be uninitialized");
	// Every UI object is quad that consists of two triangles
	perObjectVertexMemorySize = perVertexMemorySize * 6;

	PassUtils::AllocateColorDepthRenderTargetViews(passParameters);
}

void Pass_UI::RegisterObjects(GPUJobContext& context)
{
	const std::vector<DrawCall_UI_t>& objects = context.frame.uiDrawCalls;
	auto& uploadMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	//Check if we need to free this memory, maybe same amount needs to allocated

	if (perObjectConstBuffMemorySize != 0)
	{
		assert(objectConstBuffMemory == Const::INVALID_BUFFER_HANDLER && "Pass_UI start not cleaned up memory");
		objectConstBuffMemory = uploadMemory.Allocate(perObjectConstBuffMemorySize * objects.size());
	}

	vertexMemory = uploadMemory.Allocate(perObjectVertexMemorySize * objects.size());
	RenderCallbacks::RegisterLocalObjectContext regContext = { context };
	const unsigned passHashedName = HASH(passParameters.name.c_str());

	for (int i = 0; i < objects.size(); ++i)
	{
		// Special copy routine is required here.
		PassObj& obj = drawObjects.emplace_back(PassObj{
			passParameters.perObjectLocalRootArgsTemplate,
			&objects[i] });

		// Init object root args

		const int objectOffset = i * perObjectConstBuffMemorySize;

		_RegisterObjectArgs(obj, passHashedName, regContext);
		RootArg::AttachConstBufferToArgs(obj.rootArgs, objectOffset, objectConstBuffMemory);
	}

	constexpr int VERTICES_PER_UI_OBJECT = 6;

	std::vector<ShDef::Vert::PosTexCoord> vertices;
	vertices.resize(objects.size() * VERTICES_PER_UI_OBJECT);

	// Init vertex data
	for (int i = 0; i < objects.size(); ++i)
	{
		std::visit([i, &vertices, VERTICES_PER_UI_OBJECT, this](auto&& drawCall) 
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

				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(texture.desc.width, texture.desc.height),
					XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(1.0f, 1.0f),
					&vertices[VERTICES_PER_UI_OBJECT * i]);
			}
			else if constexpr (std::is_same_v<T, DrawCall_Char>)
			{
				int num = drawCall.num & 0xFF;

				constexpr float texCoordScale = 0.0625f;

				const float uCoord = (num & 15) * texCoordScale;
				const float vCoord = (num >> 4) * texCoordScale;
				const float texSize = texCoordScale;

				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(Settings::CHAR_SIZE, Settings::CHAR_SIZE),
					XMFLOAT2(uCoord, vCoord),
					XMFLOAT2(uCoord + texSize, vCoord + texSize),
					&vertices[VERTICES_PER_UI_OBJECT * i]);
			}
			else if constexpr (std::is_same_v<T, DrawCall_StretchRaw>)
			{
				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(drawCall.quadWidth, drawCall.quadHeight),
					XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(1.0f, 1.0f),
					&vertices[VERTICES_PER_UI_OBJECT * i]);
			}

		}, objects[i]);
	}

	FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
	updateVertexBufferArgs.buffer = uploadMemory.GetGpuBuffer();
	updateVertexBufferArgs.offset =  uploadMemory.GetOffset(vertexMemory);
	updateVertexBufferArgs.data = vertices.data();
	updateVertexBufferArgs.byteSize = perObjectVertexMemorySize * objects.size();
	updateVertexBufferArgs.alignment = 0;

	ResourceManager::Inst().UpdateUploadHeapBuff(updateVertexBufferArgs);
}

void Pass_UI::RegisterPassResources(GPUJobContext& context)
{
	_RegisterPassResources(*this, passParameters, context);
	RootArg::AttachConstBufferToArgs(passParameters.passLocalRootArgs, 0, passConstBuffMemory);
}

void Pass_UI::UpdatePassResources(GPUJobContext& context)
{
	_UpdatePassResources(*this, passParameters, passConstBuffMemory, passMemorySize, context);
}

void Pass_UI::UpdateDrawObjects(GPUJobContext& jobContext)
{
	RenderCallbacks::UpdateLocalObjectContext updateContext = { jobContext };

	std::vector<std::byte> cpuMem(perObjectConstBuffMemorySize * drawObjects.size(), static_cast<std::byte>(0));

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		PassObj& obj = drawObjects[i];

		_UpdateObjectArgs(drawObjects[i], cpuMem.data(), HASH(passParameters.name.c_str()), updateContext);
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

void Pass_UI::SetRenderState(GPUJobContext& context)
{
	_SetRenderState(passParameters, context);
}

void Pass_UI::Draw(GPUJobContext& context)
{
	CommandList& commandList = *context.commandList;
	Renderer& renderer = Renderer::Inst();
	const FrameGraph& frameGraph = *context.frame.frameGraph;

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

		commandList.GetGPUList()->IASetVertexBuffers(0, 1, &vertexBufferView);
		
		// Bind global args
		frameGraph.BindObjGlobalRes<Parsing::PassInputType::UI>(passParameters.perObjGlobalRootArgsIndicesTemplate, i,
			commandList);


		// Bind local args
		for (const RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			RootArg::Bind(rootArg, *context.commandList);
		}

		commandList.GetGPUList()->DrawInstanced(perObjectVertexMemorySize / perVertexMemorySize, 1, 0, 0);
	}
}

void Pass_UI::Execute(GPUJobContext& context)
{
	if (context.frame.uiDrawCalls.empty() == true)
	{
		return;
	}
	
	RegisterObjects(context);
	
	UpdatePassResources(context);
	UpdateDrawObjects(context);

	SetRenderState(context);
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

void Pass_UI::ReleasePersistentResources()
{
	auto& uploadMemory =
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	if (passConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		uploadMemory.Delete(passConstBuffMemory);
		passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}

	PassUtils::ReleaseColorDepthRenderTargetViews(passParameters);
}

void Pass_Static::Execute(GPUJobContext& context)
{
	if (context.frame.visibleStaticObjectsIndices.empty() == true)
	{
		return;
	}

	UpdatePassResources(context);

	UpdateDrawObjects(context);

	SetRenderState(context);
	Draw(context);
}

void Pass_Static::Init()
{
	assert(passMemorySize == Const::INVALID_SIZE && "Pass_Static memory size should be unitialized");
	passMemorySize = RootArg::GetSize(passParameters.passLocalRootArgs);

	if (passMemorySize > 0)
	{
		assert(passConstBuffMemory == Const::INVALID_BUFFER_HANDLER && "Pass_Static not cleaned up memory");
		passConstBuffMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passMemorySize);
	}

	assert(perObjectConstBuffMemorySize == Const::INVALID_SIZE && "Pass_Static perObject memory size should be unitialized");
	perObjectConstBuffMemorySize = RootArg::GetSize(passParameters.perObjectLocalRootArgsTemplate);

	PassUtils::AllocateColorDepthRenderTargetViews(passParameters);
}

void Pass_Static::RegisterPassResources(GPUJobContext& context)
{
	_RegisterPassResources(*this, passParameters, context);
	RootArg::AttachConstBufferToArgs(passParameters.passLocalRootArgs, 0, passConstBuffMemory);
}

void Pass_Static::UpdatePassResources(GPUJobContext& context)
{
	_UpdatePassResources(*this, passParameters, passConstBuffMemory, passMemorySize, context);
}

void Pass_Static::ReleasePerFrameResources()
{

}

void Pass_Static::ReleasePersistentResources()
{
	auto& uploadMemory =
		MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	if (passConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		uploadMemory.Delete(passConstBuffMemory);
		passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}

	for (PassObj& obj : drawObjects)
	{
		obj.ReleaseResources();
	}

	PassUtils::ReleaseColorDepthRenderTargetViews(passParameters);
}

void Pass_Static::RegisterObjects(const std::vector<StaticObject>& objects, GPUJobContext& context)
{
	assert(drawObjects.empty() == true && "Static pass Object registration failed. Pass objects list should be empty");

	const unsigned passHashedName = HASH(passParameters.name.c_str());

	for (const StaticObject& object : objects)
	{
		PassObj& obj = drawObjects.emplace_back(PassObj{
		passParameters.perObjectLocalRootArgsTemplate,
		Const::INVALID_BUFFER_HANDLER,
		 &object });

		if (perObjectConstBuffMemorySize != 0)
		{
			obj.constantBuffMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(perObjectConstBuffMemorySize);
		}

		RenderCallbacks::RegisterLocalObjectContext regContext = { context };

		_RegisterObjectArgs(obj, passHashedName, regContext);
		RootArg::AttachConstBufferToArgs(obj.rootArgs, 0, obj.constantBuffMemory);
	}
}

void Pass_Static::UpdateDrawObjects(GPUJobContext& context)
{
	RenderCallbacks::UpdateLocalObjectContext updateContext = { context };

	auto& uploadMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	std::vector<std::byte> cpuMem(perObjectConstBuffMemorySize, static_cast<std::byte>(0));

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = uploadMemory.GetGpuBuffer();
	updateConstBufferArgs.offset = Const::INVALID_OFFSET;
	updateConstBufferArgs.data = cpuMem.data();
	updateConstBufferArgs.byteSize = cpuMem.size();
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	const unsigned passHashedName = HASH(passParameters.name.c_str());

	for (int i = 0; i < context.frame.visibleStaticObjectsIndices.size(); ++i)
	{
		PassObj& obj = drawObjects[context.frame.visibleStaticObjectsIndices[i]];

		_UpdateObjectArgs(obj, cpuMem.data(), passHashedName, updateContext);

		if (perObjectConstBuffMemorySize > 0)
		{
			updateConstBufferArgs.offset = uploadMemory.GetOffset(obj.constantBuffMemory);
			ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
		}
	}
}

void Pass_Static::SetRenderState(GPUJobContext& context)
{
	_SetRenderState(passParameters, context);
}

void Pass_Static::Draw(GPUJobContext& context)
{
	CommandList& commandList = *context.commandList;
	Renderer& renderer = Renderer::Inst();
	const FrameGraph& frameGraph = *context.frame.frameGraph;

	// Bind pass global argument
	frameGraph.BindPassGlobalRes(passParameters.passGlobalRootArgsIndices, commandList);

	// Bind pass local arguments
	for (const RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
	{
		RootArg::Bind(arg, commandList);
	}

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	vertexBufferView.StrideInBytes = sizeof(ShDef::Vert::PosTexCoord);

	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	auto& defaultMemory =
		MemoryManager::Inst().GetBuff<DefaultBuffer_t>();

	for (int i = 0; i < context.frame.visibleStaticObjectsIndices.size(); ++i)
	{
		const int objectIndex = context.frame.visibleStaticObjectsIndices[i];

		const PassObj& obj = drawObjects[objectIndex];

		// Bind global args
		frameGraph.BindObjGlobalRes<Parsing::PassInputType::Static>(passParameters.perObjGlobalRootArgsIndicesTemplate, objectIndex,
			commandList);


		// Bind local args
		for (const RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			RootArg::Bind(rootArg, *context.commandList);
		}

		// Vertices
		vertexBufferView.SizeInBytes = obj.originalObj->verticesSizeInBytes;
		vertexBufferView.BufferLocation = defaultMemory.GetGpuBuffer()->GetGPUVirtualAddress() +
			defaultMemory.GetOffset(obj.originalObj->vertices);

		commandList.GetGPUList()->IASetVertexBuffers(0, 1, &vertexBufferView);


		if (obj.originalObj->indices == Const::INVALID_BUFFER_HANDLER)
		{
			commandList.GetGPUList()->DrawInstanced(vertexBufferView.SizeInBytes / vertexBufferView.StrideInBytes, 1, 0, 0);
		}
		else
		{
			indexBufferView.BufferLocation = defaultMemory.GetGpuBuffer()->GetGPUVirtualAddress() +
				defaultMemory.GetOffset(obj.originalObj->indices);
			indexBufferView.SizeInBytes = obj.originalObj->indicesSizeInBytes;

			commandList.GetGPUList()->IASetIndexBuffer(&indexBufferView);
			commandList.GetGPUList()->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
		}

	}
}

void Pass_Static::PassObj::ReleaseResources()
{
	if (constantBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		MemoryManager::Inst().GetBuff<UploadBuffer_t>().Delete(constantBuffMemory);
		constantBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}
}

void Pass_Dynamic::Execute(GPUJobContext& context)
{
	if (context.frame.visibleEntitiesIndices.empty() == true)
	{
		return;
	}

	RegisterEntities(context);

	UpdatePassResources(context);
	UpdateDrawEntities(context);

	SetRenderState(context);
	Draw(context);
}

void Pass_Dynamic::Init()
{
	assert(passMemorySize == Const::INVALID_SIZE && "Pass_Dynamic memory size should be unitialized");
	passMemorySize = RootArg::GetSize(passParameters.passLocalRootArgs);

	if (passMemorySize > 0)
	{
		assert(passConstBuffMemory == Const::INVALID_BUFFER_HANDLER && "Pass_Static not cleaned up memory");
		passConstBuffMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passMemorySize);
	}

	assert(perObjectConstBuffMemorySize == Const::INVALID_SIZE && "Pass_Static perObject memory size should be unitialized");
	perObjectConstBuffMemorySize = RootArg::GetSize(passParameters.perObjectLocalRootArgsTemplate);

	PassUtils::AllocateColorDepthRenderTargetViews(passParameters);
}

void Pass_Dynamic::RegisterPassResources(GPUJobContext& context)
{
	_RegisterPassResources(*this, passParameters, context);
	RootArg::AttachConstBufferToArgs(passParameters.passLocalRootArgs, 0, passConstBuffMemory);
}

void Pass_Dynamic::UpdatePassResources(GPUJobContext& context)
{
	_UpdatePassResources(*this, passParameters, passConstBuffMemory, passMemorySize, context);
}

void Pass_Dynamic::ReleasePerFrameResources()
{
	if (objectsConstBufferMemory != Const::INVALID_BUFFER_HANDLER)
	{
		MemoryManager::Inst().GetBuff<UploadBuffer_t>().Delete(objectsConstBufferMemory);
		objectsConstBufferMemory = Const::INVALID_BUFFER_HANDLER;
	}

	drawObjects.clear();
}

void Pass_Dynamic::ReleasePersistentResources()
{
	if (passConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		MemoryManager::Inst().GetBuff<UploadBuffer_t>().Delete(passConstBuffMemory);
		passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}

	PassUtils::ReleaseColorDepthRenderTargetViews(passParameters);
}

void Pass_Dynamic::RegisterEntities(GPUJobContext& context)
{
	assert(drawObjects.empty() == true && "Dynamic pass entities registration failed. Pass objects list should be empty");

	const std::vector<int>& visibleEntitiesIndices = context.frame.visibleEntitiesIndices;

	assert(objectsConstBufferMemory == Const::INVALID_BUFFER_HANDLER && "Pass_Dynamic start not cleaned up memory");
	auto& uploadMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>();
	
	if (perObjectConstBuffMemorySize != 0)
	{
		objectsConstBufferMemory = uploadMemory.Allocate(perObjectConstBuffMemorySize * visibleEntitiesIndices.size());
	}

	const std::vector<RootArg::Arg_t>& perEntityArgTemplate = passParameters.perObjectLocalRootArgsTemplate;

	RenderCallbacks::RegisterLocalObjectContext regContext = { context };
	const unsigned passHashedName = HASH(passParameters.name.c_str());

	for (int i = 0; i < visibleEntitiesIndices.size(); ++i)
	{
		const entity_t& entitiy = context.frame.entities[visibleEntitiesIndices[i]];

		PassObj& drawEntity = drawObjects.emplace_back(PassObj{perEntityArgTemplate, 
			&entitiy});

		const int objectOffset = i * perObjectConstBuffMemorySize;

		_RegisterObjectArgs(drawEntity, passHashedName, regContext);
		RootArg::AttachConstBufferToArgs(drawEntity.rootArgs, objectOffset, objectsConstBufferMemory);
	}
}

void Pass_Dynamic::UpdateDrawEntities(GPUJobContext& context)
{
	RenderCallbacks::UpdateLocalObjectContext updateContext = { context };

	auto& uploadMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	const std::vector<int>& visibleEntitiesIndices = context.frame.visibleEntitiesIndices;

	std::vector<std::byte> cpuMem(perObjectConstBuffMemorySize * visibleEntitiesIndices.size(), static_cast<std::byte>(0));

	const unsigned passHashedName = HASH(passParameters.name.c_str());

	for (int i = 0; i < visibleEntitiesIndices.size(); ++i)
	{
		PassObj& entity = drawObjects[i];

		_UpdateObjectArgs(entity, cpuMem.data() + perObjectConstBuffMemorySize * i, passHashedName, updateContext);
	}

	if (perObjectConstBuffMemorySize != 0)
	{
		assert(objectsConstBufferMemory != Const::INVALID_BUFFER_HANDLER && "Pass_Dynamic memory is invalid");

		FArg::UpdateUploadHeapBuff updateConstBufferArgs;
		updateConstBufferArgs.buffer = uploadMemory.GetGpuBuffer();
		updateConstBufferArgs.offset = uploadMemory.GetOffset(objectsConstBufferMemory);
		updateConstBufferArgs.data = cpuMem.data();
		updateConstBufferArgs.byteSize = cpuMem.size();
		updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

		ResourceManager::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
	}
}

void Pass_Dynamic::Draw(GPUJobContext& context)
{
	CommandList& commandList = *context.commandList;
	Renderer& renderer = Renderer::Inst();
	const FrameGraph& frameGraph = *context.frame.frameGraph;

	// Bind pass global argument
	frameGraph.BindPassGlobalRes(passParameters.passGlobalRootArgsIndices, commandList);

	// Bind pass local arguments
	for (const RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
	{
		RootArg::Bind(arg, commandList);
	}

	// Position0, Position1, TexCoord 
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[3];

	constexpr int vertexSize = sizeof(XMFLOAT4);

	vertexBufferViews[0].StrideInBytes = vertexSize;
	vertexBufferViews[1].StrideInBytes = vertexSize;

	constexpr int texCoordStrideSize = sizeof(XMFLOAT2);

	vertexBufferViews[2].StrideInBytes = texCoordStrideSize;
	
	// Index buffer data
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	auto& defaultMemory = MemoryManager::Inst().GetBuff<DefaultBuffer_t>();
	const D3D12_GPU_VIRTUAL_ADDRESS defaultMemBuffVirtAddress = defaultMemory.GetGpuBuffer()->GetGPUVirtualAddress();

	const std::unordered_map<model_t*, DynamicObjectModel>& dynamicModels = Renderer::Inst().GetDynamicModels();

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		PassObj& drawEntitiy = drawObjects[i];
		
		// Bind global 
		frameGraph.BindObjGlobalRes<Parsing::PassInputType::Dynamic>(passParameters.perObjGlobalRootArgsIndicesTemplate, 
				i, commandList);

		// Bind local 
		for (const RootArg::Arg_t& rootArg : drawEntitiy.rootArgs)
		{
			RootArg::Bind(rootArg, *context.commandList);
		}

		// Set up vertex data
		const DynamicObjectModel& model = dynamicModels.at(drawEntitiy.originalObj->model);
		const int frameSize = vertexSize * model.headerData.animFrameVertsNum;
		const int vertexBufferStart = defaultMemory.GetOffset(model.vertices);

		// Position0
		vertexBufferViews[0].BufferLocation = defaultMemBuffVirtAddress +
			vertexBufferStart + frameSize * drawEntitiy.originalObj->oldframe;
		vertexBufferViews[0].SizeInBytes = frameSize;

		// Position1
		vertexBufferViews[1].BufferLocation = defaultMemBuffVirtAddress +
			vertexBufferStart + frameSize * drawEntitiy.originalObj->frame;
		vertexBufferViews[1].SizeInBytes = frameSize;

		// TexCoord
		vertexBufferViews[2].BufferLocation = defaultMemBuffVirtAddress +
			defaultMemory.GetOffset(model.textureCoords);
		vertexBufferViews[2].SizeInBytes = texCoordStrideSize * model.headerData.animFrameVertsNum;

		commandList.GetGPUList()->IASetVertexBuffers(0, _countof(vertexBufferViews), vertexBufferViews);
   
		// Set index buffer
		indexBufferView.BufferLocation = defaultMemBuffVirtAddress +
			defaultMemory.GetOffset(model.indices);
		indexBufferView.SizeInBytes = model.headerData.indicesNum * sizeof(uint32_t);

		commandList.GetGPUList()->IASetIndexBuffer(&indexBufferView);

		commandList.GetGPUList()->DrawIndexedInstanced(indexBufferView.SizeInBytes / sizeof(uint32_t), 1, 0, 0, 0);
	}
}

void Pass_Dynamic::SetRenderState(GPUJobContext& context)
{
	_SetRenderState(passParameters, context);
}

void Pass_Particles::Execute(GPUJobContext& context)
{
	if (context.frame.particles.empty() == true)
	{
		return;
	}

	UpdatePassResources(context);

	SetRenderState(context);
	Draw(context);
}

void Pass_Particles::Init()
{
	assert(passMemorySize == Const::INVALID_SIZE && "Pass_Particles memory size should be uninitialized");
	passMemorySize = RootArg::GetSize(passParameters.passLocalRootArgs);

	if (passMemorySize > 0)
	{
		assert(passConstBuffMemory == Const::INVALID_BUFFER_HANDLER && "Pass_Particles not cleaned up memory");
		passConstBuffMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passMemorySize);
	}

	assert(passParameters.perObjectLocalRootArgsTemplate.empty() == true && "Particle pass is not suited to have local per object resources");
	assert(passParameters.perObjGlobalRootArgsIndicesTemplate.empty() == true && "Particle pass is not suited to have global per object resources");

	PassUtils::AllocateColorDepthRenderTargetViews(passParameters);
}

void Pass_Particles::RegisterPassResources(GPUJobContext& context)
{
	_RegisterPassResources(*this, passParameters, context);
	RootArg::AttachConstBufferToArgs(passParameters.passLocalRootArgs, 0, passConstBuffMemory);
}

void Pass_Particles::ReleasePerFrameResources()
{

}

void Pass_Particles::ReleasePersistentResources()
{
	if (passConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		MemoryManager::Inst().GetBuff<UploadBuffer_t>().Delete(passConstBuffMemory);
		passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}

	PassUtils::ReleaseColorDepthRenderTargetViews(passParameters);
}

void Pass_Particles::UpdatePassResources(GPUJobContext& context)
{
	_UpdatePassResources(*this, passParameters, passConstBuffMemory, passMemorySize, context);
}

void Pass_Particles::Draw(GPUJobContext& context)
{
	CommandList& commandList = *context.commandList;
	const FrameGraph& frameGraph = *context.frame.frameGraph;

	// Bind pass global argument
	frameGraph.BindPassGlobalRes(passParameters.passGlobalRootArgsIndices, commandList);

	// Bind pass local arguments
	for (const RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
	{
		RootArg::Bind(arg, commandList);
	}

	auto& uploadMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>();

	D3D12_VERTEX_BUFFER_VIEW vertBufferView;
	vertBufferView.BufferLocation = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress() +
		uploadMemory.GetOffset(context.frame.frameGraph->GetParticlesVertexMemory());
	vertBufferView.StrideInBytes = FrameGraph::SINGLE_PARTICLE_SIZE;
	vertBufferView.SizeInBytes = vertBufferView.StrideInBytes * context.frame.particles.size();

	commandList.GetGPUList()->IASetVertexBuffers(0, 1, &vertBufferView);

	commandList.GetGPUList()->DrawInstanced(vertBufferView.SizeInBytes / vertBufferView.StrideInBytes, 1, 0, 0);
}

void Pass_Particles::SetRenderState(GPUJobContext& context)
{
	_SetRenderState(passParameters, context);
}

void Pass_PostProcess::Execute(GPUJobContext& context)
{
	UpdatePassResources(context);

	SetComputeState(context);
	Dispatch(context);
}

void Pass_PostProcess::Init()
{
	assert(passMemorySize == Const::INVALID_SIZE && "Pass_PostProcess memory size should be uninitialized");
	passMemorySize = RootArg::GetSize(passParameters.passLocalRootArgs);

	if (passMemorySize > 0)
	{
		assert(passConstBuffMemory == Const::INVALID_BUFFER_HANDLER && "Pass_PostProcess not cleaned up memory");
		passConstBuffMemory = MemoryManager::Inst().GetBuff<UploadBuffer_t>().Allocate(passMemorySize);
	}

	assert(passParameters.perObjectLocalRootArgsTemplate.empty() == true && "PostProcess pass is not suited to have local per object resources");
	assert(passParameters.perObjGlobalRootArgsIndicesTemplate.empty() == true && "PostProcess pass is not suited to have global per object resources");

}

void Pass_PostProcess::RegisterPassResources(GPUJobContext& context)
{
	_RegisterPassResources(*this, passParameters, context);
	RootArg::AttachConstBufferToArgs(passParameters.passLocalRootArgs, 0, passConstBuffMemory);
}

void Pass_PostProcess::ReleasePerFrameResources()
{

}

void Pass_PostProcess::ReleasePersistentResources()
{
	if (passConstBuffMemory != Const::INVALID_BUFFER_HANDLER)
	{
		MemoryManager::Inst().GetBuff<UploadBuffer_t>().Delete(passConstBuffMemory);
		passConstBuffMemory = Const::INVALID_BUFFER_HANDLER;
	}
}

void Pass_PostProcess::UpdatePassResources(GPUJobContext& context)
{
	_UpdatePassResources(*this, passParameters, passConstBuffMemory, passMemorySize, context);
}

void Pass_PostProcess::Dispatch(GPUJobContext& context)
{
	CommandList& commandList = *context.commandList;
	const FrameGraph& frameGraph = *context.frame.frameGraph;

	// Bind pass global argument
	frameGraph.BindComputePassGlobalRes(passParameters.passGlobalRootArgsIndices, commandList);

	// Bind pass local arguments
	for (const RootArg::Arg_t& arg : passParameters.passLocalRootArgs)
	{
		RootArg::BindCompute(arg, commandList);
	}

	assert(passParameters.threadGroups.has_value() == true && "PostProcess pass has not threadGroups specified");

	commandList.GetGPUList()->Dispatch(
		passParameters.threadGroups.value()[0],
		passParameters.threadGroups.value()[1],
		passParameters.threadGroups.value()[2]);
}

void Pass_PostProcess::SetComputeState(GPUJobContext& context)
{
	ID3D12GraphicsCommandList* commandList = context.commandList->GetGPUList();

	Renderer& renderer = Renderer::Inst();

	ID3D12DescriptorHeap* descriptorHeaps[] = { renderer.cbvSrvHeap->GetHeapResource(),	renderer.samplerHeap->GetHeapResource() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandList->SetComputeRootSignature(passParameters.rootSingature.Get());
	commandList->SetPipelineState(passParameters.pipelineState.Get());
}

void PassTask::Execute(GPUJobContext& context)
{
	for (Callback_t& callback : prePassCallbacks)
	{
		callback(context, &pass);
	}

	std::visit([&context](auto&& pass) 
	{
		pass.Execute(context);
	}, pass);

	for (Callback_t& callback : postPassCallbacks)
	{
		callback(context, &pass);
	}
}

void PassUtils::AllocateColorDepthRenderTargetViews(PassParameters& passParams)
{
	passParams.colorTargetViewIndex =
		PassUtils::AllocateRenderTargetView(passParams.colorTargetName, *Renderer::Inst().rtvHeap);

	passParams.depthTargetViewIndex =
		PassUtils::AllocateRenderTargetView(passParams.depthTargetName, *Renderer::Inst().dsvHeap);
}

void PassUtils::ReleaseColorDepthRenderTargetViews(PassParameters& passParams)
{
	PassUtils::ReleaseRenderTargetView(passParams.colorTargetName, passParams.colorTargetViewIndex, *Renderer::Inst().rtvHeap);
	PassUtils::ReleaseRenderTargetView(passParams.depthTargetName, passParams.depthTargetViewIndex, *Renderer::Inst().dsvHeap);
}

void PassUtils::ClearColorBackBufferCallback(XMFLOAT4 color, GPUJobContext& context, const Pass_t* pass)
{
	D3D12_CPU_DESCRIPTOR_HANDLE colorRenderTargetView = Renderer::Inst().rtvHeap->GetHandleCPU(context.frame.colorBufferAndView->viewIndex);
	context.commandList->GetGPUList()->ClearRenderTargetView(colorRenderTargetView, &color.x, 0, nullptr);
}

void PassUtils::ClearColorCallback(XMFLOAT4 color, GPUJobContext& context, const Pass_t* pass)
{
	assert(pass != nullptr && "Pass value is nullptr");

	const PassParameters& params = GetPassParameters(*pass);

	assert(params.colorTargetViewIndex != Const::INVALID_INDEX && "ClearColorCallback invalid index");

	D3D12_CPU_DESCRIPTOR_HANDLE colorRenderTargetView = Renderer::Inst().rtvHeap->GetHandleCPU(params.colorTargetViewIndex);
	context.commandList->GetGPUList()->ClearRenderTargetView(colorRenderTargetView, &color.x, 0, nullptr);
}

void PassUtils::ClearDepthBackBufferCallback(float value, GPUJobContext& context, const Pass_t* pass)
{
	D3D12_CPU_DESCRIPTOR_HANDLE depthRenderTargetView = Renderer::Inst().dsvHeap->GetHandleCPU(context.frame.depthBufferViewIndex);
	context.commandList->GetGPUList()->ClearDepthStencilView(
		depthRenderTargetView,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		value,
		0,
		0,
		nullptr);

}

void PassUtils::ClearDeptCallback(float value, GPUJobContext& context, const Pass_t* pass)
{
	assert(pass != nullptr && "Pass value is nullptr");

	const PassParameters& params = GetPassParameters(*pass);

	assert(params.depthTargetViewIndex != Const::INVALID_INDEX && "ClearDeptCallback invalid index");

	D3D12_CPU_DESCRIPTOR_HANDLE depthRenderTargetView = Renderer::Inst().dsvHeap->GetHandleCPU(params.depthTargetViewIndex);
	context.commandList->GetGPUList()->ClearDepthStencilView(
		depthRenderTargetView,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		value,
		0,
		0,
		nullptr);
}

void PassUtils::InternalTextureProxiesToInterPassStateCallback(GPUJobContext& context, const Pass_t* pass)
{
	// In case I will have performance problems, it is extremely easy to optimize this.
	// Just batch barriers.
	ID3D12GraphicsCommandList* commandList = context.commandList->GetGPUList();
	
	for (ResourceProxy& textureProxy : context.internalTextureProxies) 
	{
		textureProxy.TransitionTo(Texture::DEFAULT_STATE, commandList);
	}
}

void PassUtils::RenderTargetToRenderStateCallback(GPUJobContext& context, const Pass_t* pass)
{
	assert(pass != nullptr && "Pass value is nullptr");

	const PassParameters& params = GetPassParameters(*pass);

	ResourceProxy::FindAndTranslateTo(
		params.colorTargetName,
		context.internalTextureProxies,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		context.commandList->GetGPUList()
	);
}

void PassUtils::CopyTextureCallback(const std::string sourceName, const std::string destinationName, GPUJobContext& context, const Pass_t* pass)
{
	std::vector<ResourceProxy>& proxies = context.internalTextureProxies;

	unsigned int sourceHashedName = HASH(sourceName.c_str());

	auto sourceProxyIt = std::find_if(proxies.begin(), proxies.end(), [sourceHashedName]
	(const ResourceProxy& proxy)
	{
		return proxy.hashedName == sourceHashedName;
	});

	assert(sourceProxyIt != proxies.end() && "CopyTextureCallback failed. Can't find source proxy");
	sourceProxyIt->TransitionTo(D3D12_RESOURCE_STATE_COPY_SOURCE, context.commandList->GetGPUList());


	unsigned int destinationHashedName = HASH(destinationName.c_str());

	auto destinationProxyIt = std::find_if(proxies.begin(), proxies.end(), [destinationHashedName]
	(const ResourceProxy& proxy)
	{
		return proxy.hashedName == destinationHashedName;
	});

	assert(destinationProxyIt != proxies.end() && "CopyTextureCallback failed. Can't find source proxy");
	destinationProxyIt->TransitionTo(D3D12_RESOURCE_STATE_COPY_DEST, context.commandList->GetGPUList());

	context.commandList->GetGPUList()->CopyResource(&destinationProxyIt->resource, &sourceProxyIt->resource);

}

void PassUtils::BackBufferToPresentStateCallback(GPUJobContext& context, const Pass_t* pass)
{
	ResourceProxy::FindAndTranslateTo(
		PassParameters::BACK_BUFFER_NAME,
		context.internalTextureProxies,
		D3D12_RESOURCE_STATE_PRESENT,
		context.commandList->GetGPUList()
	);
}

const PassParameters& PassUtils::GetPassParameters(const Pass_t& pass)
{
	return std::visit([](auto&& pass) -> const PassParameters&
	{
		return pass.passParameters;
	}, pass);
}
