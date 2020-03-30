#include "dx_constantbuffer.h"

#include <cassert>
#include <algorithm>


void BufferAllocator::Init(int size)
{
	SIZE = size;
}

int BufferAllocator::Allocate(int size)
{
	// This is pretty bad algorithm as it contains a lot of repetative code
	// feels like there is a chance to do it properly
	int currentOffset = 0;

	decltype(allocations)::iterator currentAllocation = allocations.begin();
	
	// Check before existing allocations
	const int nextAlloc = currentAllocation == allocations.end() ? SIZE : currentAllocation->offset;

	if (nextAlloc >= size)
	{
		Allocation newAlloc = { currentOffset, size };
		allocations.push_front(newAlloc);

		return newAlloc.offset;
	}

	// Check between existing allocations
	while (currentAllocation != allocations.end())
	{
		if (currentAllocation->offset - currentOffset >= size)
		{
			Allocation newAlloc;
			newAlloc.offset = currentOffset;
			newAlloc.size = size;

			allocations.insert(currentAllocation, newAlloc);

			return newAlloc.offset;
		}

		currentOffset = currentAllocation->size + currentAllocation->offset;
		++currentAllocation;
	} 

	// Check after existing allocations
	if (SIZE - currentOffset < size)
	{
		assert(false && "Failed memory allocation");
		return -1;
	}

	Allocation newAlloc;
	newAlloc.offset = currentOffset;
	newAlloc.size = size;

	allocations.push_back(newAlloc);

	return newAlloc.offset;
}

void BufferAllocator::Delete(int offset)
{
	auto it = std::find_if(allocations.begin(), allocations.end(), [offset](const Allocation& alloc) 
	{
		return offset == alloc.offset;
	});

	if(it == allocations.end())
	{
		assert(false && "Trying to delete memory that was allocated.");
		return;
	}

	allocations.erase(it);
}
