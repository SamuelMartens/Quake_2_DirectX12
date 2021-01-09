#include "dx_passparameters.h"

#include "d3dx12.h"
#include "dx_settings.h"
#include "dx_app.h"
#include "dx_memorymanager.h"
#include "dx_framegraphbuilder.h"

namespace RootArg
{
	int AllocateDescTableView(const DescTable& descTable)
	{
		assert(descTable.content.empty() == false && "Allocation view error. Desc table content can't be empty.");

		// Figure what kind of desc hep to use from inspection of the first element
		return std::visit([size = descTable.content.size()](auto&& descTableEntity)
		{
			using T = std::decay_t<decltype(descTableEntity)>;

			if constexpr (std::is_same_v<T, DescTableEntity_ConstBufferView> ||
				std::is_same_v<T, DescTableEntity_Texture>)
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
						return std::equal(currentArg.content.cbegin(), currentArg.content.cend(), arg.content.cbegin(),
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

			MemoryManager::UploadBuff_t& uploadMemory =
				MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

			if constexpr (std::is_same_v<T, RootConstant>)
			{
				assert(false && "Root constants are not implemented");
			}
			if constexpr (std::is_same_v<T, ConstBuffView>)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.allocBuffer.gpuBuffer->GetGPUVirtualAddress();

				cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + rootArg.gpuMem.offset;
				commandList.commandList->SetGraphicsRootConstantBufferView(rootArg.bindIndex, cbAddress);
			}
			else if constexpr (std::is_same_v<T, DescTable>)
			{
				assert(rootArg.content.empty() == false && "Trying to bind empty desc table");

				commandList.commandList->SetGraphicsRootDescriptorTable(rootArg.bindIndex,
					renderer.GetDescTableHeap(rootArg)->GetHandleGPU(rootArg.viewIndex));
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
					Renderer::Inst().GetDescTableHeap(*this)->DeleteRange(viewIndex, content.size());
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

	bool Resource_ConstBuff::IsEqual(const Resource_ConstBuff& other) const
	{
		return name == other.name &&
			bindFrequency == other.bindFrequency &&
			registerId == other.registerId &&
			std::equal(content.cbegin(), content.cend(), other.content.cbegin(),
				[](const RootArg::ConstBuffField& f1, const RootArg::ConstBuffField& r2) 
		{
			return f1.IsEqual(r2);
		});
		
	}

	bool Resource_Texture::IsEqual(const Resource_Texture& other) const
	{
		return name == other.name &&
			bindFrequency == other.bindFrequency &&
			registerId == other.registerId;
	}

	bool Resource_Sampler::IsEqual(const Resource_Sampler& other) const
	{
		return name == other.name &&
			bindFrequency == other.bindFrequency &&
			registerId == other.registerId;
	}

}

PassParametersSource::PassParametersSource()
{
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = Settings::BACK_BUFFER_FORMAT;
	psoDesc.SampleDesc.Count = Renderer::Inst().GetMSAASampleCount();
	psoDesc.SampleDesc.Quality = Renderer::Inst().GetMSAAQuality();
	psoDesc.DSVFormat = Settings::DEPTH_STENCIL_FORMAT;

	rootSignature = std::make_unique<Parsing::RootSignature>();
}

std::string_view PassParametersSource::GetResourceName(const Parsing::Resource_t& res)
{
	return std::visit([](auto&& resource)
	{
		return std::string_view(resource.name);
	},
		res);
}

std::string_view PassParametersSource::GetResourceRawView(const Parsing::Resource_t& res)
{
	return std::visit([](auto&& resource)
	{
		return std::string_view(resource.rawView);
	},
		res);
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
	default:
		assert(false && "Undefined shader type");
		break;
	}

	return "Undefined";
}