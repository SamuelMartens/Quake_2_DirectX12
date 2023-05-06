#include "dx_passparameters.h"

#include <numeric>

#include "Lib/crc32.h"
#include "dx_app.h"
#include "dx_framegraphbuilder.h"

namespace RootArg
{
	void AttachConstBufferToArgs(std::vector<Arg_t>& rootArgs, int offset, BufferHandler gpuHandler)
	{
		for (RootArg::Arg_t& rootArg : rootArgs)
		{
			AttachConstBufferToArg(rootArg, offset, gpuHandler);
		}
	}

	void AttachConstBufferToLocalArgs(std::vector<LocalArg>& rootLocalArgs, int offset, BufferHandler gpuHandler)
	{
		for (RootArg::LocalArg& rootLocalArg : rootLocalArgs)
		{
			AttachConstBufferToArg(rootLocalArg.arg, offset, gpuHandler);
		}
	}

	void AttachConstBufferToArg(Arg_t& rootArg, int& offset, BufferHandler gpuHandler)
	{
		std::visit([gpuHandler, &offset]
		(auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;

			if constexpr (std::is_same_v<T, RootArg::RootConstant>)
			{
				DX_ASSERT(false && "Root constant is not implemented");
			}

			if constexpr (std::is_same_v<T, RootArg::ConstBuffView>)
			{
				rootArg.gpuMem.handler = gpuHandler;
				rootArg.gpuMem.offset = offset;

				offset += RootArg::GetConstBufftSize(rootArg);
			}

			if constexpr (std::is_same_v<T, RootArg::DescTable>)
			{
				for (int i = 0; i < rootArg.content.size(); ++i)
				{
					RootArg::DescTableEntity_t& descTableEntitiy = rootArg.content[i];

					std::visit([gpuHandler, &offset]
					(auto&& descTableEntitiy)
					{
						using T = std::decay_t<decltype(descTableEntitiy)>;

						if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
						{
							DX_ASSERT(false && "Desc table view is probably not implemented! Make sure it is");
							//#TODO make view allocation
							descTableEntitiy.gpuMem.handler = gpuHandler;
							descTableEntitiy.gpuMem.offset = offset;

							offset += RootArg::GetConstBufftSize(descTableEntitiy);
						}

					}, descTableEntitiy);
				}
			}

		}, rootArg);
	}

	int GetDescTableSize(const DescTable& descTable)
	{
		int size = 0;

		for (const RootArg::DescTableEntity_t& descTableEntitiy : descTable.content)
		{
			std::visit([&size](auto&& descTableEntitiy)
			{
				using T = std::decay_t<decltype(descTableEntitiy)>;

				if constexpr (std::is_same_v<T, RootArg::DescTableEntity_ConstBufferView>)
				{
					std::for_each(descTableEntitiy.content.begin(), descTableEntitiy.content.end(),
						[&size](const RootArg::ConstBuffField& f)
					{
						size += f.size;
					});
				}

			}, descTableEntitiy);
		}

		return size;
	}

	int GetArgSize(const Arg_t& arg)
	{
		return std::visit([](auto&& arg)
		{
			using T = std::decay_t<decltype(arg)>;

			if constexpr (std::is_same_v<T, ConstBuffView>)
			{
				return GetConstBufftSize(arg);
			}
			else if constexpr (std::is_same_v<T, DescTable>)
			{
				return GetDescTableSize(arg);
			}
			else if constexpr (std::is_same_v<T, StructuredBufferView>)
			{
				DX_ASSERT(false && "Size for Structured buffers are not implemented");
				return 0;
			}
			else
			{
				DX_ASSERT(false && "RootArgGetSize, not implemented type");
				return 0;
			}

		}, arg);
	}

	int GetLocalArgsSize(const std::vector<LocalArg>& args)
	{
		return std::accumulate(args.cbegin(), args.cend(), 0, 
			[](int sum, const LocalArg& localArg)
		{
			return sum + GetArgSize(localArg.arg);
		});
	}

	int GetArgsSize(const std::vector<Arg_t>& args)
	{
		return std::accumulate(args.cbegin(), args.cend(), 0,
			[](int sum, const Arg_t& arg)
			{
				return sum + GetArgSize(arg);
			});
	}

	int FindArg(const std::vector<Arg_t>& args, const Arg_t& arg)
	{
		int res = Const::INVALID_INDEX;

		for (int i = 0; i < args.size(); ++i)
		{
			const Arg_t& currentArg = args[i];

			if (std::visit([](auto&& currentArg, auto&& arg)
			{
				using currentArgT = std::decay_t<decltype(currentArg)>;
				using argT = std::decay_t<decltype(arg)>;

				if constexpr (std::is_same_v<currentArgT, argT> == true)
				{
					if constexpr (std::is_same_v<argT, DescTable>)
					{
						return currentArg.content.size() == arg.content.size() && 
							std::equal(currentArg.content.cbegin(), currentArg.content.cend(), arg.content.cbegin(),
							[](const DescTableEntity_t& e1, const DescTableEntity_t& e2)
						{
							return std::visit([](auto&& e1, auto&& e2)
							{

								using e1T = std::decay_t<decltype(e1)>;
								using e2T = std::decay_t<decltype(e2)>;

								if constexpr (std::is_same_v<e1T, e2T>)
								{
									return e1.hashedName == e2.hashedName;
								}
								else
								{
									return false;
								}

							}, e1, e2);

						});
					}
					else
					{
						return arg.hashedName == currentArg.hashedName;
					}
				}
				else
				{
					return false;
				}

			}, currentArg, arg)) 

			{
				res = i;
				break;
			};
		}

		return res;
	}

	void Bind(const Arg_t& rootArg, const int bindIndex, CommandList& commandList)
	{
		Renderer& renderer = Renderer::Inst();

		std::visit([&commandList, &renderer, bindIndex](auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;

			auto& uploadMemory =
				MemoryManager::Inst().GetBuff<UploadBuffer_t>();

			DX_ASSERT(bindIndex != Const::INVALID_INDEX && "Can't bind RootArg, invalid index");

			if constexpr (std::is_same_v<T, RootConstant>)
			{
				DX_ASSERT(false && "Root constants are not implemented");
			}
			else if constexpr (std::is_same_v<T, ConstBuffView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(rootArg.gpuMem.offset);

				commandList.GetGPUList()->SetGraphicsRootConstantBufferView(bindIndex, cbAddress);
			}
			else if constexpr (std::is_same_v<T, StructuredBufferView>)
			{
				DX_ASSERT(rootArg.buffer != nullptr && "Invalid buffer. Can't bind root arg");

				commandList.GetGPUList()->SetGraphicsRootShaderResourceView(bindIndex,
					rootArg.buffer->gpuBuffer->GetGPUVirtualAddress());
			}
			else if constexpr (std::is_same_v<T, DescTable>)
			{
				DX_ASSERT(rootArg.content.empty() == false && "Trying to bind empty desc table");
				DX_ASSERT(rootArg.viewIndex != Const::INVALID_INDEX && "Invalid view index. Can't bind root arg");

				std::visit([&commandList, &renderer, &rootArg, bindIndex](auto&& descTableEntity)
				{
					using T = std::decay_t<decltype(descTableEntity)>;

					if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Sampler>)
					{
						commandList.GetGPUList()->SetGraphicsRootDescriptorTable(bindIndex,
							renderer.GetSamplerHandleGPU(rootArg.viewIndex));
					}
					else
					{
						commandList.GetGPUList()->SetGraphicsRootDescriptorTable(bindIndex,
							renderer.GetCbvSrvHandleGPU(rootArg.viewIndex));
					}

				}, rootArg.content[0]);
			}

		}, rootArg);

	}

	void BindCompute(const Arg_t& rootArg, const int bindIndex, CommandList& commandList)
	{
		Renderer& renderer = Renderer::Inst();

		std::visit([&commandList, &renderer, bindIndex](auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;

			auto& uploadMemory =
				MemoryManager::Inst().GetBuff<UploadBuffer_t>();

			DX_ASSERT(bindIndex != Const::INVALID_INDEX && "Can't bind RootArg, invalid index");

			if constexpr (std::is_same_v<T, RootConstant>)
			{
				DX_ASSERT(false && "Root constants are not implemented");
			}
			else if constexpr (std::is_same_v<T, ConstBuffView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + rootArg.gpuMem.offset;
				commandList.GetGPUList()->SetComputeRootConstantBufferView(bindIndex, cbAddress);
			}
			else if constexpr (std::is_same_v<T, UAView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + rootArg.gpuMem.offset;
				commandList.GetGPUList()->SetComputeRootUnorderedAccessView(bindIndex, cbAddress);
			}
			else if constexpr (std::is_same_v<T, StructuredBufferView>)
			{
				DX_ASSERT(rootArg.buffer != nullptr && "Invalid buffer. Can't bind root arg");

				commandList.GetGPUList()->SetComputeRootShaderResourceView(bindIndex,
					rootArg.buffer->gpuBuffer->GetGPUVirtualAddress());
			}
			else if constexpr (std::is_same_v<T, DescTable>)
			{
				DX_ASSERT(rootArg.content.empty() == false && "Trying to bind empty desc table");
				DX_ASSERT(rootArg.viewIndex != Const::INVALID_INDEX && "Invalid view index. Can't bind root arg");

				std::visit([&commandList, &renderer, &rootArg, bindIndex](auto&& descTableEntity)
				{
					using T = std::decay_t<decltype(descTableEntity)>;

					if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Sampler>)
					{
						commandList.GetGPUList()->SetComputeRootDescriptorTable(bindIndex,
							renderer.GetSamplerHandleGPU(rootArg.viewIndex));
					}
					else
					{
						commandList.GetGPUList()->SetComputeRootDescriptorTable(bindIndex,
							renderer.GetCbvSrvHandleGPU(rootArg.viewIndex));
					}

				}, rootArg.content[0]);
			}

		}, rootArg);

	}

	DescTable::DescTable(DescTable&& other)
	{
		*this = std::move(other);
	}

	DescTable::DescTable(const DescTable& other)
	{
		*this = other;
	}

	DescTable& DescTable::operator=(const DescTable& other)
	{
		DX_ASSERT(viewIndex == Const::INVALID_INDEX && "Trying to copy non empty root arg. Is this intended?");

		content = other.content;
		viewIndex = other.viewIndex;

		return *this;
	}

	DescTable& DescTable::operator=(DescTable&& other)
	{
		PREVENT_SELF_MOVE_ASSIGN;

		content = std::move(other.content);

		viewIndex = other.viewIndex;
		other.viewIndex = Const::INVALID_INDEX;

		return *this;
	}

	bool ConstBuffField::IsEqual(const ConstBuffField& other) const
	{
		return size == other.size &&
			hashedName == other.hashedName;
	}
}

namespace Parsing
{
	unsigned int GetParseDataTypeSize(DataType type)
	{
		switch (type)
		{
		case DataType::Float4x4:
			return sizeof(XMFLOAT4X4);
		case DataType::Float4:
			return sizeof(XMFLOAT4);
		case DataType::Float2:
			return sizeof(XMFLOAT2);
		case DataType::Float:
			return sizeof(float);
		case DataType::Int:
			return sizeof(int32_t);
		default:
			DX_ASSERT(false && "Can't get parse data type size, invalid type");
			break;
		}

		return 0;
	}

	DXGI_FORMAT GetParseDataTypeDXGIFormat(DataType type)
	{
		switch (type)
		{
		case DataType::Float4x4:
			DX_ASSERT(false && "Parse data type, can't use Float4x4 for DXGI format");
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DataType::Float4:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DataType::Float2:
			return DXGI_FORMAT_R32G32_FLOAT;
		case DataType::Float:
			return DXGI_FORMAT_R32G32_FLOAT;
		case DataType::Int:
			DX_ASSERT(false && "Parse data type, can't use Int for DXGI format");
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			DX_ASSERT(false && "Parse data type, unknown type");
			break;
		}

		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	}

	bool IsEqual(const Resource_t& res1, const Resource_t& res2)
	{
		return std::visit([](auto&& res1, auto&& res2) 
		{
			using T1 = std::decay_t<decltype(res1)>;
			using T2 = std::decay_t<decltype(res2)>;

			if constexpr (std::is_same_v<T1, T2> == false)
			{
				return false;
			}
			else
			{
				return res1.IsEqual(res2);
			}

		}, res1, res2);
	}

	bool Resource_Base::IsEqual(const Resource_Base& other) const
	{
		return name == other.name &&
			bindFrequency == other.bindFrequency &&
			registerId == other.registerId &&
			registerSpace == other.registerSpace;
	}


	bool Resource_ConstBuff::IsEqual(const Resource_ConstBuff& other) const
	{
		return Resource_Base::IsEqual(other) &&
			content.size() == other.content.size() &&
			std::equal(content.cbegin(), content.cend(), other.content.cbegin(),
				[](const RootArg::ConstBuffField& f1, const RootArg::ConstBuffField& r2) 
		{
			return f1.IsEqual(r2);
		});
		
	}

	bool Resource_StructuredBuffer::IsEqual(const Resource_StructuredBuffer& other) const
	{
		return Resource_Base::IsEqual(other) &&
			dataType == other.dataType;
	}

	std::string_view GetResourceName(const Parsing::Resource_t& res)
	{
		return std::visit([](auto&& resource)
		{
			return std::string_view(resource.name);
		},
			res);
	}

	std::string_view GetResourceRawView(const Parsing::Resource_t& res)
	{
		return std::visit([](auto&& resource)
		{
			return std::string_view(resource.rawView);
		},
			res);
	}

	Parsing::ResourceBindFrequency GetResourceBindFrequency(const Parsing::Resource_t& res)
	{
		return std::visit([](auto&& resource)
		{
			return *resource.bindFrequency;
		},
			res);
	}

	Parsing::ResourceScope GetResourceScope(const Resource_t& res)
	{
		return std::visit([](auto&& resource)
		{
			return *resource.scope;
		},
			res);
	}

	std::string_view GetMiscDefName(const MiscDef_t& def)
	{
		return std::visit([](auto&& definition)
		{
			return std::string_view(definition.name);
		},
			def);
	}

	std::string_view GetMiscDefRawView(const MiscDef_t& def)
	{
		return std::visit([](auto&& definition)
		{
			return std::string_view(definition.rawView);
		},
			def);
	}

	int GetVertAttrSize(const VertAttr& vertAttr)
	{
		return std::accumulate(vertAttr.content.cbegin(), vertAttr.content.cend(),
			0, [](int sum, const Parsing::VertAttrField& field)
		{
			return sum + Parsing::GetParseDataTypeSize(field.type);
		});
	}

	int GetStructBufferDataTypeSize(const StructBufferDataType_t& dataType)
	{
		return std::visit([](auto&& dataType) -> int
			{
				using T = std::decay_t<decltype(dataType)>;
				
				if constexpr (std::is_same_v<T, DataType>)
				{
					return static_cast<int>(GetParseDataTypeSize(dataType));
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					switch (HASH(dataType.c_str()))
					{
					case HASH("PerClusterLightIndexData"):
					{
						return sizeof(Light::ClusterLightData);
					}
					break;
					case HASH("LightCullingDataStruct"):
					{
						return sizeof(Light::ClusteredLighting_LightCullingData);
					}
					break;
					default:
						break;
					}

					// Write custom sizes here if there is one
					DX_ASSERT(false && "No type specified");
					return Const::INVALID_SIZE;
				}
				else
				{
					DX_ASSERT(false && "Unknown struct buffer data type");
				}
				
				return Const::INVALID_SIZE;
			}, dataType);
	}

}

PassParametersSource::PassParametersSource()
{
	ZeroMemory(&rasterPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	rasterPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	rasterPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	rasterPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	rasterPsoDesc.SampleMask = UINT_MAX;
	rasterPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	rasterPsoDesc.NumRenderTargets = 1;
	rasterPsoDesc.RTVFormats[0] = Settings::BACK_BUFFER_FORMAT;
	rasterPsoDesc.SampleDesc.Count = Renderer::Inst().GetMSAASampleCount();
	rasterPsoDesc.SampleDesc.Quality = Renderer::Inst().GetMSAAQuality();
	rasterPsoDesc.DSVFormat = Settings::DEPTH_STENCIL_FORMAT;

	ZeroMemory(&computePsoDesc, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));

	rootSignature = std::make_unique<Parsing::RootSignature>();
}

std::string PassParametersSource::ShaderTypeToStr(ShaderType type)
{
	switch (type)
	{
	case ShaderType::Vs:
		return "Vs";
	case ShaderType::Gs:
		return "Gs";
	case ShaderType::Ps:
		return "Ps";
	case ShaderType::Cs:
		return "Cs";
	default:
		DX_ASSERT(false && "Undefined shader type");
		break;
	}

	return "Undefined";
}

void PassParameters::AddGlobalPerObjectRootArgRef(std::vector<RootArg::GlobalArgRef>& perObjGlobalRootArgsRefsTemplate, std::vector<RootArg::Arg_t>& perObjGlobalResTemplate, int bindIndex, RootArg::Arg_t&& arg)
{
	const int resTemplateIndex = RootArg::FindArg(perObjGlobalResTemplate, arg);
	if (resTemplateIndex == Const::INVALID_INDEX)
	{
		// Add proper ref
		RootArg::GlobalArgRef ref;
		ref.bindIndex = bindIndex;
		ref.globalListIndex = perObjGlobalResTemplate.size();

		perObjGlobalRootArgsRefsTemplate.push_back(ref);

		// Res is not found create new
		perObjGlobalResTemplate.push_back(std::move(arg));
	}
	else
	{
		RootArg::GlobalArgRef ref;
		ref.bindIndex = bindIndex;
		ref.globalListIndex = resTemplateIndex;

		perObjGlobalRootArgsRefsTemplate.push_back(ref);
	}
}
