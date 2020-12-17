#include "dx_renderstage.h"

#include <cassert>

#include "dx_threadingutils.h"
#include "dx_rendercallbacks.h"
#include "dx_utils.h"
#include "dx_settings.h"
#include "dx_app.h"

void RenderStage_UI::Init()
{
	ASSERT_MAIN_THREAD;

	// UI uses streaming objects. So we need to preallocate piece of memory that will be
	// used for each run

	// Calculate amount of memory for const buffers per objects
	assert(perObjectConstBuffMemorySize == 0 && "Per object const memory should be null");

	for (const RootArg_t& rootArg : pass.perObjectRootArgsTemplate)
	{
		std::visit([this](auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;

			if constexpr (std::is_same_v<T, RootArg_RootConstant>)
			{
				assert(false && "Root constants not implemented");
			}

			if constexpr(std::is_same_v<T, RootArg_ConstBuffView>)
			{
				perObjectConstBuffMemorySize += GetConstBuffSize(rootArg);
			}

			if constexpr(std::is_same_v<T, RootArg_DescTable>)
			{
				for (const DescTableEntity_t& descTableEntitiy : rootArg.content)
				{
					std::visit([this](auto&& descTableEntitiy) 
					{
						using T = std::decay_t<decltype(descTableEntitiy)>;

						if constexpr (std::is_same_v<T, DescTableEntity_ConstBufferView>)
						{
							std::for_each(descTableEntitiy.content.begin(), descTableEntitiy.content.end(),
								[this](const ConstBuffField& f)
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

	for (const VertAttrField& field : pass.vertAttr.content)
	{
		perVertexMemorySize += GetParseDataTypeSize(field.type);
	}

	assert(perObjectVertexMemorySize == 0 && "Per Object Vertex Memory should be null");
	// Every UI object is quad that consists of two triangles
	perObjectVertexMemorySize = perVertexMemorySize * 6;
}

void RenderStage_UI::RegisterUIDrawCalls(const std::vector<DrawCall_UI_t>& objects, Context& jobCtx)
{
	//#DEBUG FREE THIS MEMORY
	// REMEMBER MULTITHREADING
	//Check if we need to free this memory, maybe same amount needs to allocated
	constBuffMemory = Renderer::Inst().m_uploadMemoryBuffer.Allocate(perObjectConstBuffMemorySize * objects.size());
	vertexMemory = Renderer::Inst().m_uploadMemoryBuffer.Allocate(perObjectVertexMemorySize * objects.size());

	for (int i = 0; i < objects.size(); ++i)
	{
		StageObj& stageObj = drawObjects.emplace_back(StageObj{pass.perObjectRootArgsTemplate, objects[i]});

		// Init object root args

		const int objectOffset = i * perObjectConstBuffMemorySize;
		int rootArgOffset = 0;

		RenderCallbacks::PerObjectRegisterContext regCtx = { jobCtx };

		for (RootArg_t& rootArg : stageObj.rootArgs)
		{
			std::visit([this, i, objectOffset, &rootArgOffset, &stageObj, &regCtx]
			(auto&& rootArg)
			{
				using T = std::decay_t<decltype(rootArg)>;

				if constexpr (std::is_same_v<T, RootArg_RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				}

				if constexpr (std::is_same_v<T, RootArg_ConstBuffView>)
				{
					rootArg.gpuMem.handler = constBuffMemory;
					rootArg.gpuMem.offset = objectOffset + rootArgOffset;

					rootArgOffset += GetConstBuffSize(rootArg);
				}

				if constexpr (std::is_same_v<T, RootArg_DescTable>)
				{
					rootArg.viewIndex = AllocateDescTableView(rootArg);

					for (int i = 0; i < rootArg.content.size(); ++i)
					{
						DescTableEntity_t& descTableEntitiy = rootArg.content[i];
						const int currentViewIndex = rootArg.viewIndex + i;

						std::visit([this, objectOffset, &rootArgOffset, &stageObj, &regCtx, currentViewIndex]
						(auto&& descTableEntitiy)
						{
							using T = std::decay_t<decltype(descTableEntitiy)>;

							if constexpr (std::is_same_v<T, DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
								//#DEBUG this needs view allocation
								descTableEntitiy.gpuMem.handler = constBuffMemory;
								descTableEntitiy.gpuMem.offset = objectOffset + rootArgOffset;

								rootArgOffset += GetConstBuffSize(descTableEntitiy);
							}

							if constexpr (std::is_same_v<T, DescTableEntity_Texture>)
							{
								std::visit([this, &descTableEntitiy, &stageObj, &regCtx, currentViewIndex]
								(auto&& drawCall) 
								{
									RenderCallbacks::PerObjectRegisterCallback(
										HASH(pass.name.c_str()),
										descTableEntitiy.hashedName,
										drawCall,
										&currentViewIndex,
										regCtx
									);

								}, stageObj.originalDrawCall);

								
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

			if constexpr (std::is_same_v<T, DrawCall_Pic>)
			{
				std::array<char, MAX_QPATH> texFullName;
			 	renderer.GetDrawTextureFullname(drawCall.name.c_str(), texFullName.data(), texFullName.size());

				const Texture& texture = *renderer.FindTexture(texFullName.data());

				std::array<ShDef::Vert::PosTexCoord, 6> vertices;
				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(texture.width, texture.height),
					XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(1.0f, 1.0f),
					vertices.data());

				const int uploadBuffOffset = renderer.m_uploadMemoryBuffer.GetOffset(vertexMemory) + perObjectVertexMemorySize * i;

				FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
				updateVertexBufferArgs.buffer = renderer.m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				renderer.UpdateUploadHeapBuff(updateVertexBufferArgs);
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

				const int uploadBuffOffset = renderer.m_uploadMemoryBuffer.GetOffset(vertexMemory) + perObjectVertexMemorySize * i;

				FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
				updateVertexBufferArgs.buffer = renderer.m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				renderer.UpdateUploadHeapBuff(updateVertexBufferArgs);

			}
			else if constexpr (std::is_same_v<T, DrawCall_StretchRaw>)
			{
				std::array<ShDef::Vert::PosTexCoord, 6> vertices;
				Utils::MakeQuad(XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(drawCall.quadWidth, drawCall.quadHeight),
					XMFLOAT2(0.0f, 0.0f),
					XMFLOAT2(1.0f, 1.0f),
					vertices.data());

				const int uploadBuffOffset = renderer.m_uploadMemoryBuffer.GetOffset(vertexMemory) + perObjectVertexMemorySize * i;

				FArg::UpdateUploadHeapBuff updateVertexBufferArgs;
				updateVertexBufferArgs.buffer = renderer.m_uploadMemoryBuffer.allocBuffer.gpuBuffer;
				updateVertexBufferArgs.offset = uploadBuffOffset;
				updateVertexBufferArgs.data = vertices.data();
				updateVertexBufferArgs.byteSize = perObjectVertexMemorySize;
				updateVertexBufferArgs.alignment = 0;

				renderer.UpdateUploadHeapBuff(updateVertexBufferArgs);
			}

		}, objects[i]);
	}
}

void RenderStage_UI::UpdateDrawObjects(Context& jobCtx)
{
	RenderCallbacks::PerObjectUpdateContext updateCtx = { XMFLOAT4X4(), jobCtx };

	Frame& frame = updateCtx.jobCtx.frame;

	XMMATRIX sseViewMat = XMLoadFloat4x4(&frame.uiViewMat);
	XMMATRIX sseProjMat = XMLoadFloat4x4(&frame.uiProjectionMat);
	XMMATRIX sseYInverseAndCenterMat = XMLoadFloat4x4(&Renderer::Inst().m_yInverseAndCenterMatrix);

	XMMATRIX viewProj = sseYInverseAndCenterMat * sseViewMat * sseProjMat;

	XMStoreFloat4x4(&updateCtx.viewProjMat, viewProj);

	std::vector<std::byte> cpuMem(perObjectConstBuffMemorySize * drawObjects.size(), static_cast<std::byte>(0));

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		StageObj& obj = drawObjects[i];

		// Start of the memory for the current object
		int currentObjecOffset = i * perObjectConstBuffMemorySize;

		for (RootArg_t& rootArg : obj.rootArgs)
		{
			std::visit([&updateCtx, &obj, this,  &currentObjecOffset, &cpuMem](auto&& rootArg) 
			{
				using T = std::decay_t<decltype(rootArg)>;

				Renderer& renderer = Renderer::Inst();

				if constexpr (std::is_same_v<T, RootArg_RootConstant>)
				{
					assert(false && "Root constant is not implemented");
				};

				if constexpr (std::is_same_v<T, RootArg_ConstBuffView>)
				{
					// Start of the memory of the current buffer
					int currentBufferOffset = currentObjecOffset;

					// If this is true, which it should be, some offset calculation here could be greatly simplified.
					//#DEBUG this is valid assumption, eh?
					assert(currentBufferOffset == rootArg.gpuMem.offset && "Update offset for const buffer is not equal, eh?");

					for (ConstBuffField& field : rootArg.content)
					{
						std::visit([&rootArg, &updateCtx, this, &currentObjecOffset, &field, &cpuMem, &currentBufferOffset](auto&& obj)
						{
							RenderCallbacks::PerObjectUpdateCallback(
								HASH(pass.name.c_str()),
								field.hashedName,
								obj,
								&cpuMem[currentBufferOffset],
								updateCtx);

						}, obj.originalDrawCall);

						// Proceed to next buffer
						currentBufferOffset += field.size;
					}

					currentObjecOffset += GetConstBuffSize(rootArg);
					assert(currentBufferOffset < currentObjecOffset && "Update error. Buffers overwrite other buffer");
				}

				if constexpr (std::is_same_v<T, RootArg_DescTable>)
				{
					for (DescTableEntity_t& descTableEntity : rootArg.content)
					{
						std::visit([](auto&& descTableEntity) 
						{
							using T = std::decay_t<decltype(descTableEntity)>;

							if constexpr (std::is_same_v<T, DescTableEntity_ConstBufferView>)
							{
								assert(false && "Desc table view is probably not implemented! Make sure it is");
							}
						}, descTableEntity);
					}
				}

			}, rootArg);
		}

		assert(currentObjecOffset < (i + 1) * perObjectConstBuffMemorySize && "Update error. Per object const buff offset overwrites next obj memory");
	}

	auto& uploadMemoryBuff = Renderer::Inst().m_uploadMemoryBuffer;

	FArg::UpdateUploadHeapBuff updateConstBufferArgs;
	updateConstBufferArgs.buffer = uploadMemoryBuff.allocBuffer.gpuBuffer;
	updateConstBufferArgs.offset = uploadMemoryBuff.GetOffset(constBuffMemory);
	updateConstBufferArgs.data = cpuMem.data();
	updateConstBufferArgs.byteSize = cpuMem.size();
	updateConstBufferArgs.alignment = Settings::CONST_BUFFER_ALIGNMENT;

	Renderer::Inst().UpdateUploadHeapBuff(updateConstBufferArgs);
}

void RenderStage_UI::SetUpRenderState(Context& jobCtx)
{
	Frame& frame = jobCtx.frame;
	ComPtr<ID3D12GraphicsCommandList>& commandList = jobCtx.commandList.commandList;


	commandList->RSSetViewports(1, &pass.viewport);
	commandList->RSSetScissorRects(1, &frame.scissorRect);

	Renderer& renderer = Renderer::Inst();

	assert(pass.colorTargetNameHash == HASH("BACK_BUFFER") && "Custom render targets are not implemented");
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = renderer.rtvHeap->GetHandleCPU(frame.colorBufferAndView->viewIndex);

	assert(pass.depthTargetNameHash == HASH("BACK_BUFFER") && "Custom render targets are not implemented");
	D3D12_CPU_DESCRIPTOR_HANDLE depthTargetView = renderer.dsvHeap->GetHandleCPU(frame.depthBufferViewIndex);

	commandList->OMSetRenderTargets(1, &renderTargetView, true, &depthTargetView);

	ID3D12DescriptorHeap* descriptorHeaps[] = { renderer.cbvSrvHeap->GetHeapResource(),	renderer.samplerHeap->GetHeapResource() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);


	commandList->SetGraphicsRootSignature(pass.rootSingature.Get());
	commandList->SetPipelineState(pass.pipelineState.Get());
	commandList->IASetPrimitiveTopology(pass.primitiveTopology);
}

void RenderStage_UI::Draw(Context& jobCtx)
{
	ComPtr<ID3D12GraphicsCommandList>& commandList = jobCtx.commandList.commandList;
	Renderer& renderer = Renderer::Inst();

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	vertexBufferView.StrideInBytes = perVertexMemorySize;
	vertexBufferView.SizeInBytes = perObjectVertexMemorySize;

	for (int i = 0; i < drawObjects.size(); ++i)
	{
		const StageObj& obj = drawObjects[i];

		vertexBufferView.BufferLocation = renderer.m_uploadMemoryBuffer.allocBuffer.gpuBuffer->GetGPUVirtualAddress() +
			renderer.m_uploadMemoryBuffer.GetOffset(vertexMemory) + i * perObjectVertexMemorySize;

		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

		for (const RootArg_t& rootArg : obj.rootArgs)
		{
			BindRootArg(rootArg, jobCtx.commandList);
		}

		commandList->DrawInstanced(perObjectVertexMemorySize / perVertexMemorySize, 1, 0, 0);
	}
}

void RenderStage_UI::Execute(const std::vector<DrawCall_UI_t>& objects, Context& context)
{
	RegisterUIDrawCalls(objects, context);
	
	UpdateDrawObjects(context);

	SetUpRenderState(context);
	Draw(context);

	Finish();
}

void RenderStage_UI::Finish()
{
	Renderer::Inst().m_uploadMemoryBuffer.Delete(constBuffMemory);
	Renderer::Inst().m_uploadMemoryBuffer.Delete(vertexMemory);

	drawObjects.clear();
}
