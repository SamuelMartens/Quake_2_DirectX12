#include "dx_descriptorheapallocator.h"

#include "dx_app.h"

void _AllocDescriptorInternal(int allocatedIndex, ID3D12Resource* resource, const ViewDescription_t* desc, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	// For some unknown reason compilation fails if I put this code inside AllocateDescriptor()

	switch (type)
	{
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = Renderer::Inst().GetRtvHandleCPU(allocatedIndex);

		assert((resource != nullptr || desc != nullptr) 
			&& "RTV allocation failure. Resource or Description shall not be nullptr");

		const D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_RENDER_TARGET_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateRenderTargetView(resource, rtvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = Renderer::Inst().GetDsvHandleCPU(allocatedIndex);

		assert((resource != nullptr || desc != nullptr)
			&& "DSV allocation failure. Resource or Description shall not be nullptr");

		const D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_DEPTH_STENCIL_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateDepthStencilView(resource, dsvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = Renderer::Inst().GetCbvSrvHandleCPU(allocatedIndex);

		assert(desc != nullptr
			&& "CBV_SRV_UAV allocation failure. Description shall not be nullptr");

		std::visit([&resource, &handle](auto&& description) 
		{
			using T = std::decay_t<decltype(description)>;

			if constexpr (std::is_same_v<T, std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>>)
			{
				const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = description.has_value() ?
					&description.value() : nullptr;

				Infr::Inst().GetDevice()->CreateShaderResourceView(resource, srvDesc, handle);
			}
			
			if constexpr (std::is_same_v<T, std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC>>)
			{
				const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = description.has_value() ?
					&description.value() : nullptr;

				Infr::Inst().GetDevice()->CreateUnorderedAccessView(resource, nullptr, uavDesc, handle);
			}
			
			if constexpr (std::is_same_v<T, std::optional<D3D12_CONSTANT_BUFFER_VIEW_DESC>>)
			{
				assert(false && "Not implemented");
			}

		}, *desc);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = Renderer::Inst().GetSamplerHandleCPU(allocatedIndex);

		const D3D12_SAMPLER_DESC* samplerDesc = &std::get<D3D12_SAMPLER_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateSampler(samplerDesc, handle);

		break;
	}
	default:
	{
		assert(false && "Invalid descriptor heap type");
		break;
	}
	}
}

D3D12_SHADER_RESOURCE_VIEW_DESC DescriptorHeapUtils::GetSRVTexture2DNullDescription()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	return srvDesc;
}
