#pragma once

#include <vector>
#include <list>

#include "dx_utils.h"

class FlagAllocator
{
public:
	FlagAllocator(const int flagsNum);

	int Allocate();
	void Delete(int index);

private:
	
	std::vector<bool> flags;
};

struct Allocation
{
	int offset = Const::INVALID_OFFSET;
	int size = Const::INVALID_SIZE;
};

template<int SIZE>
class BufferAllocator
{
public:



	BufferAllocator() = default;

	BufferAllocator(const BufferAllocator&) = delete;
	BufferAllocator(BufferAllocator&&) = delete;

	BufferAllocator& operator=(const BufferAllocator&) = delete;
	BufferAllocator& operator=(BufferAllocator&&) = delete;

	~BufferAllocator() = default;

	int Allocate(int size)
	{
		// Check before existing allocations
		{
			const int nextOffset = allocations.empty() ? SIZE : allocations.begin()->offset;

			if (nextOffset >= size)
			{
				allocations.push_front({ 0, size });
			}
		}

		// Check between existing allocations
		{
			const int numIntervals = allocations.size() - 1;

			auto currAllocIt = allocations.begin();
			auto nextAllocIt = std::next(currAllocIt, 1);

			for (int i = 0; i < numIntervals; ++i, ++currAllocIt, ++nextAllocIt)
			{
				const int currAllocEnd = currAllocIt->offset + currAllocIt->size;
				if (nextAllocIt->offset - currAllocEnd >= size)
				{
					allocations.insert(currAllocIt, { currAllocEnd, size });
					return currAllocEnd;
				}
			}
		}

		// Check after existing allocations
		{
			// We checked empty case in the beginning of the function
			if (!allocations.empty())
			{
				const Allocation& lastAlloc = allocations.back();
				const int lastAllocEnd = lastAlloc.offset + lastAlloc.size;

				if (SIZE - lastAllocEnd >= size)
				{
					allocations.push_back({ lastAllocEnd, size });
					return lastAllocEnd;
				}
			}

			assert(false && "Failed to allocate part of buffer");
			return Const::INVALID_OFFSET;

		}
	};


	void Delete(int offset)
	{
		auto it = std::find_if(allocations.begin(), allocations.end(), [offset](const Allocation& alloc)
		{
			return offset == alloc.offset;
		});

		if (it == allocations.end())
		{
			assert(false && "Trying to delete memory that was allocated.");
			return;
		}

		allocations.erase(it);
	};

	void ClearAll()
	{
		allocations.clear();
	};

private:

	std::list<Allocation> allocations;
};
