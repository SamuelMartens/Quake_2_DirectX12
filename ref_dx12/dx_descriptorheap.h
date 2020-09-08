#pragma once

#include <d3d12.h>
#include <vector>
#include <variant>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_allocators.h"



class DescriptorHeap
{
public:
	using Desc_t = std::variant<
		D3D12_RENDER_TARGET_VIEW_DESC,
		D3D12_DEPTH_STENCIL_VIEW_DESC,
		D3D12_SHADER_RESOURCE_VIEW_DESC>;

	DescriptorHeap(
		int descriptorsNum,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorsType,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags);

	int Allocate(ComPtr<ID3D12Resource> resource, DescriptorHeap::Desc_t* desc = nullptr);
	void Delete(int index);

	ID3D12DescriptorHeap* GetHeapResource();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetHandleCPU(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetHandleGPU(int index) const;

private:

	FlagAllocator alloc;
	ComPtr<ID3D12DescriptorHeap> heap;

	// Constant. Never change
	int DESCRIPTOR_SIZE = Const::INVALID_SIZE;
	const D3D12_DESCRIPTOR_HEAP_TYPE TYPE;

};