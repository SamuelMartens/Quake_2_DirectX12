#pragma once

#include <d3d12.h>
#include <vector>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_allocators.h"


class DescriptorHeap
{
public:
	DescriptorHeap(
		int descriptorsNum,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorsType,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		ComPtr<ID3D12Device> dev);

	int Allocate(ComPtr<ID3D12Resource> resource);
	void Delete(int index);

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetHandle(int index);

private:

	FlagAllocator alloc;
	ComPtr<ID3D12DescriptorHeap> heap;
	ComPtr<ID3D12Device> device;

	// Constant. Never change
	int DESCRIPTOR_SIZE = -1;
	const D3D12_DESCRIPTOR_HEAP_TYPE TYPE;

};