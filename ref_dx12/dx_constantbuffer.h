#pragma once

#include <list>
#include <d3d12.h>

#include "dx_common.h"

class BufferAllocator
{
	struct Allocation
	{
		int offset = -1;
		int size = -1;
	};

public:

	BufferAllocator() = default;

	BufferAllocator(const BufferAllocator&) = delete;
	BufferAllocator(BufferAllocator&&) = delete;

	BufferAllocator& operator=(const BufferAllocator&) = delete;
	BufferAllocator& operator=(BufferAllocator&&) = delete;

	~BufferAllocator() = default;

	void Init(int size);

	int Allocate(int size);
	void Delete(int offset);

private:

	int SIZE = -1;

	std::list<Allocation> allocations;

};


struct ConstantBuffer
{
	BufferAllocator allocator;
	ComPtr<ID3D12Resource> gpuBuffer;
};
