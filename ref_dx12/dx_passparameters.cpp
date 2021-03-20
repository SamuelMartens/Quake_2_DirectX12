#include "dx_passparameters.h"

#include <numeric>

#include "d3dx12.h"
#include "dx_settings.h"
#include "dx_app.h"
#include "dx_memorymanager.h"
#include "dx_framegraphbuilder.h"
#include "dx_pass.h"

const std::string PassParameters::BACK_BUFFER_NAME = "BACK_BUFFER";

namespace RootArg
{
	int AllocateDescTableView(const DescTable& descTable)
	{
		assert(descTable.content.empty() == false && "Allocation view error. Desc table content can't be empty.");

		// Figure what kind of desc hep to use from inspection of the first element
		return std::visit([size = descTable.content.size()](auto&& descTableEntity)
		{
			using T = std::decay_t<decltype(descTableEntity)>;

			if constexpr (
				std::is_same_v<T, DescTableEntity_ConstBufferView> ||
				std::is_same_v<T, DescTableEntity_Texture> ||
				std::is_same_v<T, DescTableEntity_UAView>)
			{
				return Renderer::Inst().cbvSrvHeap->AllocateRange(size);
			}
			else if constexpr (std::is_same_v<T, DescTableEntity_Sampler>)
			{
				// Samplers are hard coded to 0 for now. Always use 0
				// Samplers live in descriptor heap, which makes things more complicated, because
				// I loose one of indirections that I rely on.
				return 0;
				//return Renderer::Inst().samplerHeap->AllocateRange(size);
			}
			else
			{
				assert(false && "AllocateDescTableView error. Unknown type of desc table entity ");
			}

			return -1;

		}, descTable.content[0]);
	}

	void AttachConstBufferToArgs(std::vector<Arg_t>& rootArgs, int offset, BufferHandler gpuHandler)
	{
		for (RootArg::Arg_t& rootArg : rootArgs)
		{
			std::visit([gpuHandler, &offset]
			(auto&& rootArg)
			{
				using T = std::decay_t<decltype(rootArg)>;

				if constexpr (std::is_same_v<T, RootArg::RootConstant>)
				{
					assert(false && "Root constant is not implemented");
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
								assert(false && "Desc table view is probably not implemented! Make sure it is");
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

	int GetSize(const Arg_t& arg)
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
			else
			{
				assert(false && "RootArgGetSize, not implemented type");
				return 0;
			}

		}, arg);
	}

	int GetSize(const std::vector<Arg_t>& args)
	{
		return std::accumulate(args.cbegin(), args.cend(), 0, 
			[](int& sum, const Arg_t& arg)
		{
			return sum + GetSize(arg);
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

	void Bind(const Arg_t& rootArg, CommandList& commandList)
	{
		std::visit([&commandList](auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;
			Renderer& renderer = Renderer::Inst();

			auto& uploadMemory =
				MemoryManager::Inst().GetBuff<UploadBuffer_t>();

			assert(rootArg.bindIndex != Const::INVALID_INDEX && "Can't bind RootArg, invalid index");

			if constexpr (std::is_same_v<T, RootConstant>)
			{
				assert(false && "Root constants are not implemented");
			}
			if constexpr (std::is_same_v<T, ConstBuffView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(rootArg.gpuMem.offset);

				commandList.GetGPUList()->SetGraphicsRootConstantBufferView(rootArg.bindIndex, cbAddress);
			}
			else if constexpr (std::is_same_v<T, DescTable>)
			{
				assert(rootArg.content.empty() == false && "Trying to bind empty desc table");
				assert(rootArg.viewIndex != Const::INVALID_INDEX && "Invalid view index. Can't bind root arg");

				std::visit([&commandList, &renderer, &rootArg](auto&& descTableEntity)
				{
					using T = std::decay_t<decltype(descTableEntity)>;

					if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Sampler>)
					{
						commandList.GetGPUList()->SetGraphicsRootDescriptorTable(rootArg.bindIndex,
							renderer.samplerHeap->GetHandleGPU(rootArg.viewIndex));
					}
					else
					{
						commandList.GetGPUList()->SetGraphicsRootDescriptorTable(rootArg.bindIndex,
							renderer.cbvSrvHeap->GetHandleGPU(rootArg.viewIndex));
					}

				}, rootArg.content[0]);
			}

		}, rootArg);
	}
	
	void BindCompute(const Arg_t& rootArg, CommandList& commandList)
	{
		std::visit([&commandList](auto&& rootArg)
		{
			using T = std::decay_t<decltype(rootArg)>;
			Renderer& renderer = Renderer::Inst();

			auto& uploadMemory =
				MemoryManager::Inst().GetBuff<UploadBuffer_t>();

			assert(rootArg.bindIndex != Const::INVALID_INDEX && "Can't bind RootArg, invalid index");

			if constexpr (std::is_same_v<T, RootConstant>)
			{
				assert(false && "Root constants are not implemented");
			}
			if constexpr (std::is_same_v<T, ConstBuffView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + rootArg.gpuMem.offset;
				commandList.GetGPUList()->SetComputeRootConstantBufferView(rootArg.bindIndex, cbAddress);
			}
			if constexpr (std::is_same_v<T, UAView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.GetGpuBuffer()->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + rootArg.gpuMem.offset;
				commandList.GetGPUList()->SetComputeRootUnorderedAccessView(rootArg.bindIndex, cbAddress);
			}
			else if constexpr (std::is_same_v<T, DescTable>)
			{
				assert(rootArg.content.empty() == false && "Trying to bind empty desc table");
				assert(rootArg.viewIndex != Const::INVALID_INDEX && "Invalid view index. Can't bind root arg");

				std::visit([&commandList, &renderer, &rootArg](auto&& descTableEntity)
				{
					using T = std::decay_t<decltype(descTableEntity)>;

					if constexpr (std::is_same_v<T, RootArg::DescTableEntity_Sampler>)
					{
						commandList.GetGPUList()->SetComputeRootDescriptorTable(rootArg.bindIndex,
							renderer.samplerHeap->GetHandleGPU(rootArg.viewIndex));
					}
					else
					{
						commandList.GetGPUList()->SetComputeRootDescriptorTable(rootArg.bindIndex,
							renderer.cbvSrvHeap->GetHandleGPU(rootArg.viewIndex));
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
		assert(viewIndex == Const::INVALID_INDEX && "Trying to copy non empty root arg. Is this intended?");

		bindIndex = other.bindIndex;
		content = other.content;
		viewIndex = other.viewIndex;

		return *this;
	}

	DescTable& DescTable::operator=(DescTable&& other)
	{
		PREVENT_SELF_MOVE_ASSIGN;

		bindIndex = other.bindIndex;
		other.bindIndex = Const::INVALID_INDEX;

		content = std::move(other.content);

		viewIndex = other.viewIndex;
		other.viewIndex = Const::INVALID_INDEX;

		return *this;
	}

	DescTable::~DescTable()
	{
		if (content.empty() == true)
		{
			return;
		}

		std::visit([this](auto&& desctTableEntity)
		{
			using T = std::decay_t<decltype(desctTableEntity)>;

			//#TODO fix this when samplers are handled properly
			if constexpr (std::is_same_v<T, DescTableEntity_Sampler> == false)
			{
				if (viewIndex != Const::INVALID_INDEX)
				{
					Renderer::Inst().cbvSrvHeap->DeleteRange(viewIndex, content.size());
				}
			}

		}, content[0]);
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
		case DataType::Int:
			return sizeof(int32_t);
		default:
			assert(false && "Can't get parse data type size, invalid type");
			break;
		}

		return 0;
	}

	DXGI_FORMAT GetParseDataTypeDXGIFormat(DataType type)
	{
		switch (type)
		{
		case DataType::Float4x4:
			assert(false && "Parse data type, can't use Float4x4 for DXGI format");
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DataType::Float4:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DataType::Float2:
			return DXGI_FORMAT_R32G32_FLOAT;
		case DataType::Int:
			assert(false && "Parse data type, can't use Int for DXGI format");
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			assert(false && "Parse data type, unknown type");
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
			registerId == other.registerId;
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
}

PassParametersSource::PassParametersSource()
{
	ZeroMemory(&rasterPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	rasterPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	rasterPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
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
		assert(false && "Undefined shader type");
		break;
	}

	return "Undefined";
}

void PassParameters::AddGlobalPerObjectRootArgIndex(std::vector<int>& perObjGlobalRootArgsIndicesTemplate, std::vector<RootArg::Arg_t>& perObjGlobalResTemplate, RootArg::Arg_t&& arg)
{
	int resTemplateIndex = RootArg::FindArg(perObjGlobalResTemplate, arg);
	if (resTemplateIndex == Const::INVALID_INDEX)
	{
		// Res is not found create new
		perObjGlobalResTemplate.push_back(std::move(arg));

		// Add proper index
		perObjGlobalRootArgsIndicesTemplate.push_back(perObjGlobalResTemplate.size() - 1);
	}
	else
	{
		perObjGlobalRootArgsIndicesTemplate.push_back(resTemplateIndex);
	}
}
