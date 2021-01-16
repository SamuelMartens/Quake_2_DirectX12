#pragma once

#include <d3d12.h>
#include <vector>
#include <variant>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_allocators.h"
#include "dx_infrastructure.h"

using Descriptor_t = std::variant<
	D3D12_RENDER_TARGET_VIEW_DESC,
	D3D12_DEPTH_STENCIL_VIEW_DESC,
	D3D12_SHADER_RESOURCE_VIEW_DESC,
	D3D12_SAMPLER_DESC>;

void _AllocDescriptorInternal(CD3DX12_CPU_DESCRIPTOR_HANDLE& handle, ID3D12Resource* resource, Descriptor_t* desc, D3D12_DESCRIPTOR_HEAP_TYPE type);

template<int DESCRIPTORS_NUM, D3D12_DESCRIPTOR_HEAP_TYPE TYPE>
class DescriptorHeap
{
public:

	DescriptorHeap(
		int descSize,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags) :
		DESCRIPTOR_SIZE(descSize)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc;

		heapDesc.NumDescriptors = DESCRIPTORS_NUM;
		heapDesc.Type = TYPE;
		heapDesc.Flags = flags;
		heapDesc.NodeMask = 0;

		ThrowIfFailed(Infr::Inst().GetDevice()->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(heap.GetAddressOf())));
	};


	int Allocate() 
	{
		return alloc.Allocate();

	};

	int Allocate(ID3D12Resource* resource, Descriptor_t* desc = nullptr) 
	{
		const int allocatedIndex = alloc.Allocate();

		AllocateDescriptor(allocatedIndex, resource, desc);

		return allocatedIndex;
	};


	// Guarantees to allocate continuous range of descriptors
	// Return first index in the continuous range
	int AllocateRange(int size) 
	{
		assert(size != 0 && "Desc heap allocate range error. Can't allocate zero range");

		return alloc.AllocateRange(size);
	};

	int AllocateRange(std::vector<ID3D12Resource*>& resources, std::vector<Descriptor_t*> descs) 
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

	};

	void Delete(int index) 
	{
		alloc.Delete(index);
	};

	void DeleteRange(int index, int size) 
	{
		alloc.DeleteRange(index, size);
	};
	
	ID3D12DescriptorHeap* GetHeapResource() 
	{
		return heap.Get();
	};

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetHandleCPU(int index) const 
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), index, DESCRIPTOR_SIZE);
	};

	CD3DX12_GPU_DESCRIPTOR_HANDLE GetHandleGPU(int index) const 
	{
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), index, DESCRIPTOR_SIZE);
	};

	void AllocateDescriptor(int allocatedIndex, ID3D12Resource* resource, Descriptor_t* desc) 
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = GetHandleCPU(allocatedIndex);
		
		_AllocDescriptorInternal(handle, resource, desc, TYPE);
		
	};

private:

	

	FlagAllocator<DESCRIPTORS_NUM> alloc;
	ComPtr<ID3D12DescriptorHeap> heap;

	const int DESCRIPTOR_SIZE = Const::INVALID_SIZE;
};