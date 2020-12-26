#include "dx_material.h"

#include "dx_app.h"
#include "dx_memorymanager.h"

const std::string MaterialSource::STATIC_MATERIAL_NAME = "Static";
const std::string MaterialSource::DYNAMIC_MATERIAL_NAME = "Dynamic";
const std::string MaterialSource::PARTICLE_MATERIAL_NAME = "Particle";

std::vector<MaterialSource> MaterialSource::ConstructSourceMaterials()
{
	std::vector<MaterialSource> materialSources;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC defaultPsoDesc;
	ZeroMemory(&defaultPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	defaultPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	defaultPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	defaultPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	defaultPsoDesc.SampleMask = UINT_MAX;
	defaultPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	defaultPsoDesc.NumRenderTargets = 1;
	defaultPsoDesc.RTVFormats[0] = Settings::BACK_BUFFER_FORMAT;
	defaultPsoDesc.SampleDesc.Count = Renderer::Inst().GetMSAASampleCount();
	defaultPsoDesc.SampleDesc.Quality = Renderer::Inst().GetMSAAQuality();
	defaultPsoDesc.DSVFormat = Settings::DEPTH_STENCIL_FORMAT;


	// STATIC GEOM material -------------------
	MaterialSource& staticGeomMaterial = materialSources.emplace_back(MaterialSource());
	staticGeomMaterial.name = STATIC_MATERIAL_NAME;
	staticGeomMaterial.inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	staticGeomMaterial.psoDesc = defaultPsoDesc;
	staticGeomMaterial.psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	staticGeomMaterial.psoDesc.InputLayout =
	{
		staticGeomMaterial.inputLayout.data(),
		static_cast<UINT>(staticGeomMaterial.inputLayout.size())
	};

	staticGeomMaterial.shaders[MaterialSource::ShaderType::Vs] = "vs_PosTex.cso";
	staticGeomMaterial.shaders[MaterialSource::ShaderType::Ps] = "ps_PosTex.cso";
	

	// Root signature is an array of root parameters
	staticGeomMaterial.rootParameters.resize(3);
	{
		// First parameter texture descriptor table
		staticGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& textureCbvTable = staticGeomMaterial.descriptorRanges.back();
		textureCbvTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		staticGeomMaterial.rootParameters[0].InitAsDescriptorTable(textureCbvTable.size(), textureCbvTable.data());

		// Second parameter is samplers descriptor table
		staticGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& samplersTable = staticGeomMaterial.descriptorRanges.back();

		samplersTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

		staticGeomMaterial.rootParameters[1].InitAsDescriptorTable(samplersTable.size(), samplersTable.data());

		// Third parameter is constant buffer view
		staticGeomMaterial.rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	}

	staticGeomMaterial.rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	staticGeomMaterial.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DYNAMIC GEOM material --------------
	MaterialSource& dynamicGeomMaterial = materialSources.emplace_back(MaterialSource());
	dynamicGeomMaterial.name = DYNAMIC_MATERIAL_NAME;

	dynamicGeomMaterial.inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	dynamicGeomMaterial.psoDesc = defaultPsoDesc;
	dynamicGeomMaterial.psoDesc.InputLayout =
	{
		dynamicGeomMaterial.inputLayout.data(),
		static_cast<UINT>(dynamicGeomMaterial.inputLayout.size())
	};

	dynamicGeomMaterial.shaders[MaterialSource::ShaderType::Vs] = "vs_PosPosTex.cso";
	dynamicGeomMaterial.shaders[MaterialSource::ShaderType::Ps] = "ps_PosTex.cso";

	dynamicGeomMaterial.rootParameters.resize(3);
	{
		// First parameter texture descriptor table
		dynamicGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& textureCbvTable = dynamicGeomMaterial.descriptorRanges.back();
		textureCbvTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		dynamicGeomMaterial.rootParameters[0].InitAsDescriptorTable(textureCbvTable.size(), textureCbvTable.data());

		// Second parameter is samplers descriptor table
		dynamicGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& samplersTable = dynamicGeomMaterial.descriptorRanges.back();

		samplersTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

		dynamicGeomMaterial.rootParameters[1].InitAsDescriptorTable(samplersTable.size(), samplersTable.data());

		// Third parameter is constant buffer view
		dynamicGeomMaterial.rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	}

	dynamicGeomMaterial.rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	dynamicGeomMaterial.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// PARTICLE material -------------------
	MaterialSource& particleMaterial = materialSources.emplace_back(MaterialSource());
	particleMaterial.name = PARTICLE_MATERIAL_NAME;
	particleMaterial.inputLayout = 
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	particleMaterial.psoDesc = defaultPsoDesc;
	particleMaterial.psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	particleMaterial.psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	particleMaterial.psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	particleMaterial.psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	particleMaterial.psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;


	particleMaterial.psoDesc.InputLayout = 
	{
		particleMaterial.inputLayout.data(),
		static_cast<UINT>(particleMaterial.inputLayout.size())
	};

	particleMaterial.shaders[MaterialSource::ShaderType::Vs] = "vs_Particle.cso";
	particleMaterial.shaders[MaterialSource::ShaderType::Gs] = "gs_Particle.cso";
	particleMaterial.shaders[MaterialSource::ShaderType::Ps] = "ps_Particle.cso";

	// Root signature is an array of root parameters
	particleMaterial.rootParameters.resize(1);
	{
		// First parameter is constant buffer view
		particleMaterial.rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_GEOMETRY);
	}

	particleMaterial.rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	particleMaterial.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;

	return materialSources;
}

MaterialSource::MaterialSource()
{
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
}


PassSource::PassSource()
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
}

std::string_view PassSource::GetResourceName(const Resource_t& res)
{
	return std::visit([](auto&& resource)
	{
		return std::string_view(resource.name);
	},
	res);
}

std::string_view PassSource::GetResourceRawView(const Resource_t& res)
{
	return std::visit([](auto&& resource)
	{
		return std::string_view(resource.rawView);
	},
	res);
}

std::string PassSource::ShaderTypeToStr(ShaderType type)
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

unsigned int GetParseDataTypeSize(ParseDataType type)
{
	switch (type)
	{
	case ParseDataType::Float4x4:
		return sizeof(XMFLOAT4X4);
	case ParseDataType::Float4:
		return sizeof(XMFLOAT4);
	case ParseDataType::Float2:
		return sizeof(XMFLOAT2);
	case ParseDataType::Int:
		return sizeof(int32_t);
	default:
		assert(false && "Can't get parse data type size, invalid type");
		break;
	}

	return 0;
}

DXGI_FORMAT GetParseDataTypeDXGIFormat(ParseDataType type)
{
	switch (type)
	{
	case ParseDataType::Float4x4:
		assert(false && "Parse data type, can't use Float4x4 for DXGI format");
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case ParseDataType::Float4:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case ParseDataType::Float2:
		return DXGI_FORMAT_R32G32_FLOAT;
	case ParseDataType::Int:
		assert(false && "Parse data type, can't use Int for DXGI format");
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	default:
		assert(false && "Parse data type, unknown type");
		break;
	}

	return DXGI_FORMAT_R32G32B32A32_FLOAT;
}

int AllocateDescTableView(const RootArg_DescTable& descTable)
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

void BindRootArg(const RootArg_t& rootArg, CommandList& commandList)
{
	std::visit([&commandList](auto&& rootArg) 
	{
		using T = std::decay_t<decltype(rootArg)>;
		Renderer& renderer = Renderer::Inst();

		MemoryManager::UploadBuff_t& uploadMemory =
			MemoryManager::Inst().GetBuff<MemoryManager::Upload>();

		if constexpr (std::is_same_v<T, RootArg_RootConstant>) 
		{
			assert(false && "Root constants are not implemented");
		}
		if constexpr (std::is_same_v<T, RootArg_ConstBuffView>)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uploadMemory.allocBuffer.gpuBuffer->GetGPUVirtualAddress();

			cbAddress += uploadMemory.GetOffset(rootArg.gpuMem.handler) + rootArg.gpuMem.offset;
			commandList.commandList->SetGraphicsRootConstantBufferView(rootArg.index, cbAddress);
		}
		else if constexpr (std::is_same_v<T, RootArg_DescTable>)
		{
			assert(rootArg.content.empty() == false && "Trying to bind empty desc table");

			commandList.commandList->SetGraphicsRootDescriptorTable(rootArg.index, 
				renderer.GetDescTableHeap(rootArg)->GetHandleGPU(rootArg.viewIndex));
		}

	}, rootArg);
}

RootArg_DescTable::RootArg_DescTable(RootArg_DescTable&& other)
{
	*this = std::move(other);
}

RootArg_DescTable::RootArg_DescTable(const RootArg_DescTable& other)
{
	*this = other;
}

RootArg_DescTable& RootArg_DescTable::operator=(const RootArg_DescTable& other)
{
	assert(viewIndex == Const::INVALID_INDEX && "Trying to copy non empty root arg. Is this intended?");

	index = other.index;
	content = other.content;
	viewIndex = other.viewIndex;

	return *this;
}

RootArg_DescTable& RootArg_DescTable::operator=(RootArg_DescTable&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	index = other.index;
	other.index = Const::INVALID_INDEX;

	content = std::move(other.content);

	viewIndex = other.viewIndex;
	other.viewIndex = Const::INVALID_INDEX;

	return *this;
}

RootArg_DescTable::~RootArg_DescTable()
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