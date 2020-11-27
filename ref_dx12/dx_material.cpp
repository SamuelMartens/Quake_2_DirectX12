#include "dx_material.h"

#include "dx_app.h"

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
	//#DEBUG make pso default state intitalization here
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
}
