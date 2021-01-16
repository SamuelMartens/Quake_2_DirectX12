#include "dx_descriptorheap.h"

void _AllocDescriptorInternal(CD3DX12_CPU_DESCRIPTOR_HANDLE& handle, ID3D12Resource* resource, Descriptor_t* desc, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	// For some unknown reason compilation fails if I put this code inside AllocateDescriptor()

	switch (type)
	{
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
	{
		assert(resource != nullptr && "RTV allocation failure. Resource shall not be nullptr");

		D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_RENDER_TARGET_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateRenderTargetView(resource, rtvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
	{
		assert(resource != nullptr && "DSV allocation failure. Resource shall not be nullptr");

		D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_DEPTH_STENCIL_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateDepthStencilView(resource, dsvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
	{
		assert(resource != nullptr && "UAV allocation failure. Resource shall not be nullptr");

		//#TODO implement CBV
		// this works only with Shader resource view, but not CBV. Which requires different handle D3D12_CONSTANT_BUFFER_VIEW_DESC
		D3D12_SHADER_RESOURCE_VIEW_DESC* cbvSrvDesc = (desc == nullptr) ?
			nullptr : &std::get<D3D12_SHADER_RESOURCE_VIEW_DESC>(*desc);

		Infr::Inst().GetDevice()->CreateShaderResourceView(resource, cbvSrvDesc, handle);
		break;
	}
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
	{
		D3D12_SAMPLER_DESC* samplerDesc = &std::get<D3D12_SAMPLER_DESC>(*desc);

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
