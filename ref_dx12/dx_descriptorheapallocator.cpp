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

		DX_ASSERT((resource != nullptr || desc != nullptr) 
			&& "RTV allocation failure. Resource or Description shall not be nullptr");

		const D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_RENDER_TARGET_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateRenderTargetView(resource, rtvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = Renderer::Inst().GetDsvHandleCPU(allocatedIndex);

		DX_ASSERT((resource != nullptr || desc != nullptr)
			&& "DSV allocation failure. Resource or Description shall not be nullptr");

		const D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_DEPTH_STENCIL_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateDepthStencilView(resource, dsvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = Renderer::Inst().GetCbvSrvHandleCPU(allocatedIndex);

		DX_ASSERT(desc != nullptr
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
				DX_ASSERT(false && "Not implemented");
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
		DX_ASSERT(false && "Invalid descriptor heap type");
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

D3D12_SHADER_RESOURCE_VIEW_DESC DescriptorHeapUtils::GetSRVBufferNullDescription()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	return srvDesc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC DescriptorHeapUtils::GenerateDefaultStructuredBufferViewDesc(Resource* buffer, int stride)
{
	DX_ASSERT(buffer->desc.dimension == D3D12_RESOURCE_DIMENSION_BUFFER && 
		"Structured buffer view can be created for structured buffer only");

	DX_ASSERT(stride > 0 && "Invalid stride for structured buffer view creation");

	DX_ASSERT(buffer->desc.width % stride == 0 &&
		"Structured buffer view creation error. Size and stride do not fit");

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = buffer->desc.width / stride;
	srvDesc.Buffer.StructureByteStride = stride;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	return srvDesc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC DescriptorHeapUtils::GenerateDefaultStructuredBufferUAV(Resource* buffer, int stride)
{
	DX_ASSERT(buffer->desc.dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		"Structured buffer UAV can be created for structured buffer only");

	DX_ASSERT(stride > 0 && "Invalid stride for structured buffer UAV creation");

	DX_ASSERT(buffer->desc.width % stride == 0 &&
		"Structured buffer UAV creation error. Size and stride do not fit");

	D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	desc.Buffer.FirstElement = 0;

	desc.Buffer.StructureByteStride = stride;
	desc.Buffer.NumElements = buffer->desc.width / desc.Buffer.StructureByteStride;
	desc.Buffer.CounterOffsetInBytes = 0;
	desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	return desc;
}
