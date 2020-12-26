#include "dx_descriptorheap.h"

#include "dx_utils.h"
#include "dx_app.h"
#include "dx_infrastructure.h"

DescriptorHeap::DescriptorHeap(int descriptorsNum,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorsType,
	D3D12_DESCRIPTOR_HEAP_FLAGS flags) :
		alloc(descriptorsNum),
		TYPE(descriptorsType)
{
	DESCRIPTOR_SIZE = Renderer::Inst().GetDescriptorSize(TYPE);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc;

	heapDesc.NumDescriptors = descriptorsNum;
	heapDesc.Type = TYPE;
	heapDesc.Flags = flags;
	heapDesc.NodeMask = 0;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(heap.GetAddressOf())));
}

int DescriptorHeap::Allocate(ID3D12Resource* resource, DescriptorHeap::Desc_t* desc)
{
	const int allocatedIndex = alloc.Allocate();

	AllocateDescriptor(allocatedIndex, resource, desc);

	return allocatedIndex;
}

int DescriptorHeap::Allocate()
{
	return alloc.Allocate();
}

int DescriptorHeap::AllocateRange(std::vector<ID3D12Resource*>& resources, std::vector<Desc_t*> descs)
{
	assert(resources.empty() == false && "DescHeap AllocateRange error. Resources array can't be empty");
	assert(resources.size() == descs.size() && "DescHeap AllocateRange error. Resource and descriptor array sizes should be equal.");

	const int rangeSize = resources.size();

	const int allocatedIndexStart = alloc.AllocateRange(rangeSize);

	for (int i = 0; i < rangeSize; ++i)
	{
		AllocateDescriptor(allocatedIndexStart + i, resources[i], descs[i]);
	}

	return allocatedIndexStart;
}

int DescriptorHeap::AllocateRange(int size)
{
	assert(size != 0 && "Desc heap allocate range error. Can't allocate zero range");

	return alloc.AllocateRange(size);
}

void DescriptorHeap::Delete(int index)
{
	alloc.Delete(index);
}

void DescriptorHeap::DeleteRange(int index, int size)
{
	alloc.DeleteRange(index, size);
}

ID3D12DescriptorHeap* DescriptorHeap::GetHeapResource()
{
	return heap.Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetHandleCPU(int index) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), index, DESCRIPTOR_SIZE);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetHandleGPU(int index) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), index, DESCRIPTOR_SIZE);
}

void DescriptorHeap::AllocateDescriptor(int allocatedIndex, ID3D12Resource* resource, Desc_t* desc)
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE handle = GetHandleCPU(allocatedIndex);

	switch (TYPE)
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

		//#DEBUG this works only with Shader resource view, but not CBV. Which requires different handle D3D12_CONSTANT_BUFFER_VIEW_DESC
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