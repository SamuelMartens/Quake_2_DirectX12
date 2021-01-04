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

void Pass_UI::Init(PassParameters&& parameters)
{
	ASSERT_MAIN_THREAD;

	passParameters = std::move(parameters);

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

void Pass_UI::Start(GPUJobContext& jobCtx)
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
		//#DEBUG this is first major slow down. I can beat it by reusing
		// pass objects rather than recreating them
		PassObj& passObj = drawObjects.emplace_back(PassObj{
			passParameters.perObjectRootArgsTemplate,
			&objects[i] });

		// Init object root args

		const int objectOffset = i * perObjectConstBuffMemorySize;
		int rootArgOffset = 0;

		RenderCallbacks::PerObjectRegisterContext regCtx = { jobCtx };

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
					rootArg.gpuMem.handler = constBuffMemory;
      					rootArg.gpuMem.offset = objectOffset + rootArgOffset;

					rootArgOffset += RootArg::GetConstBuffSize(rootArg);
				}

				if constexpr (std::is_same_v<T, RootArg::DescTable>)
				{
					//#DEBUG second major slow down that kills performance
					rootArg.viewIndex = RootArg::AllocateDescTableView(rootArg);

					for (int i = 0; i < rootArg.content.size(); ++i)
					{
						RootArg::DescTableEntity_t& descTableEntitiy = rootArg.content[i];
						const int currentViewIndex = rootArg.viewIndex + i;

						std::visit([this, objectOffset, &rootArgOffset, &passObj, &regCtx, currentViewIndex]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
								//#TODO make view allocation
								descTableEntitiy.gpuMem.handler = constBuffMemory;
								descTableEntitiy.gpuMem.offset = objectOffset + rootArgOffset;

								rootArgOffset += RootArg::GetConstBuffSize(descTableEntitiy);
							}

							if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Texture>)
							{
								RenderCallbacks::PerObjectRegisterCallback(
									HASH(passParameters.name.c_str()),
									descTableEntitiy.hashedName,
									*passObj.originalDrawCall,
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

void Pass_UI::UpdateDrawObjects(GPUJobContext& jobCtx)
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
		PassObj& obj = drawObjects[i];

		for (RootArg::Arg_t& rootArg : obj.rootArgs)
		{
			std::visit([&updateCtx, &obj, this, &cpuMem](auto&& rootArg) 
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
					int fieldOffset = rootArg.gpuMem.offset;

					for (RootArg::ConstBuffField& field : rootArg.content)
					{
						std::visit([&rootArg, &updateCtx, this, &field, &cpuMem, &fieldOffset](auto&& obj)
						{
							RenderCallbacks::PerObjectUpdateCallback(
								HASH(passParameters.name.c_str()),
								field.hashedName,
								obj,
								&cpuMem[fieldOffset],
								updateCtx);

						}, *obj.originalDrawCall);

						// Proceed to next buffer
						fieldOffset += field.size;
					}
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
	ComPtr<ID3D12GraphicsCommandList>& commandList = jobCtx.commandList.commandList;
	Renderer& renderer = Renderer::Inst();

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	vertexBufferView.StrideInBytes = perVertexMemorySize;
	vertexBufferView.SizeInBytes = perObjectVertexMemorySize;

	MemoryManager::UploadBuff_t& uploadMemory =
		MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		const PassObj& obj = drawObjects[i];

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

void Pass_UI::Execute(GPUJobContext& context)
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

