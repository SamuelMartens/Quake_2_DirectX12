#pragma once

#include <d3d12.h>
#include <vector>
#include <variant>
#include <optional>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_allocators.h"
#include "dx_infrastructure.h"

using ViewDescription_t = std::variant<
	D3D12_RENDER_TARGET_VIEW_DESC,
	D3D12_DEPTH_STENCIL_VIEW_DESC,
	std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>,
	std::optional<D3D12_CONSTANT_BUFFER_VIEW_DESC>,
	std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC>,
	D3D12_SAMPLER_DESC>;

void _AllocDescriptorInternal(int allocatedIndex, ID3D12Resource* resource, const ViewDescription_t* desc, D3D12_DESCRIPTOR_HEAP_TYPE type);

template<typename AllocatorType, int DESCRIPTORS_NUM, D3D12_DESCRIPTOR_HEAP_TYPE TYPE>
class GenericDescriptorHeapAllocator
{
public:

	GenericDescriptorHeapAllocator(int offset) :
		OFFSET(offset)
	{};

	[[nodiscard]]
	int Allocate() 
	{
		int result = Const::INVALID_INDEX;

		if constexpr (std::is_same_v<AllocatorType, FlagAllocator<DESCRIPTORS_NUM>>)
		{
			result = alloc.Allocate();

		}
		else if constexpr (std::is_same_v<AllocatorType, StreamingFlagAllocator<DESCRIPTORS_NUM>>)
		{
			result = alloc.Allocate(1);
		}
		else
		{
			// Dirty hack, to make static_assert(false) work in else branch
			static_assert(!sizeof(AllocatorType), "Usage of undefined heap allocator");
			return Const::INVALID_INDEX;
		}

		return result + OFFSET;
	};

	int Allocate(ID3D12Resource* resource, const ViewDescription_t* desc = nullptr) 
	{
		const int allocatedIndex = Allocate();

		AllocateDescriptor(allocatedIndex, resource, desc);
		 
		return allocatedIndex;
	};


	// Guarantees to allocate continuous range of descriptors
	// Return first index in the continuous range
	[[nodiscard]]
	int AllocateRange(int size) 
	{
		assert(size != 0 && "Desc heap allocate range error. Can't allocate zero range");

		int result = Const::INVALID_INDEX;

		if constexpr (std::is_same_v<AllocatorType, FlagAllocator<DESCRIPTORS_NUM>>)
		{
			result = alloc.AllocateRange(size);
		}
		else if constexpr (std::is_same_v<AllocatorType, StreamingFlagAllocator<DESCRIPTORS_NUM>>)
		{
			result = alloc.Allocate(size);
		}
		else
		{
			// Dirty hack, to make static_assert(false) work in else branch
			static_assert(!sizeof(AllocatorType), "Usage of undefined heap allocator");
			return Const::INVALID_INDEX;
		}

		return result + OFFSET;
	};

	int AllocateRange(std::vector<ID3D12Resource*>& resources, std::vector<const ViewDescription_t*> descs) 
	{
		assert(resources.empty() == false && "DescHeap AllocateRange error. Resources array can't be empty");
		assert(resources.size() == descs.size() && "DescHeap AllocateRange error. Resource and descriptor array sizes should be equal.");

		const int rangeSize = resources.size();

		const int allocatedIndexStart = AllocateRange(rangeSize);

		for (int i = 0; i < rangeSize; ++i)
		{
			AllocateDescriptor(allocatedIndexStart + i, resources[i], descs[i]);
		}

		return allocatedIndexStart;

	};

	void Delete(int index) 
	{
		if constexpr (std::is_same_v<AllocatorType, FlagAllocator<DESCRIPTORS_NUM>>)
		{
			alloc.Delete(index - OFFSET);
		}
		else if constexpr (std::is_same_v<AllocatorType, StreamingFlagAllocator<DESCRIPTORS_NUM>>)
		{
		}
		else
		{
			// Dirty hack, to make static_assert(false) work in else branch
			static_assert(!sizeof(AllocatorType), "Usage of undefined heap allocator");
		}
	};

	void DeleteRange(int index, int size) 
	{
		if constexpr (std::is_same_v<AllocatorType, FlagAllocator<DESCRIPTORS_NUM>>)
		{
			alloc.DeleteRange(index - OFFSET, size);
		}
		else if constexpr (std::is_same_v<AllocatorType, StreamingFlagAllocator<DESCRIPTORS_NUM>>)
		{
		}
		else
		{
			// Dirty hack, to make static_assert(false) work in else branch
			static_assert(!sizeof(AllocatorType), "Usage of undefined heap allocator");
		}
	};

	void Reset()
	{
		if constexpr (std::is_same_v<AllocatorType, FlagAllocator<DESCRIPTORS_NUM>>)
		{
			static_assert(false, "Not streaming allocator doesn't support this function");
		}
		else if constexpr (std::is_same_v<AllocatorType, StreamingFlagAllocator<DESCRIPTORS_NUM>>)
		{
			alloc.Reset();
		}
		else
		{
			// Dirty hack, to make static_assert(false) work in else branch
			static_assert(!sizeof(AllocatorType), "Usage of undefined heap allocator");
		}
	};

	void AllocateDescriptor(int allocatedIndex, ID3D12Resource* resource, const ViewDescription_t* desc) 
	{	
		_AllocDescriptorInternal(allocatedIndex, resource, desc, TYPE);
	};

private:

	AllocatorType alloc;

	const int OFFSET = Const::INVALID_OFFSET;
};


template<int DESCRIPTORS_NUM, D3D12_DESCRIPTOR_HEAP_TYPE TYPE>
using SequentialDescriptorHeapAllocator_t = GenericDescriptorHeapAllocator<FlagAllocator<DESCRIPTORS_NUM>, DESCRIPTORS_NUM, TYPE>;

template<int DESCRIPTORS_NUM, D3D12_DESCRIPTOR_HEAP_TYPE TYPE>
using StreamingDescriptorHeapAllocator_t = GenericDescriptorHeapAllocator<StreamingFlagAllocator<DESCRIPTORS_NUM>, DESCRIPTORS_NUM, TYPE>;

namespace DescriptorHeapUtils
{
	D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVTexture2DNullDescription();
	
}