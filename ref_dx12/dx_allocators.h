#pragma once

#include <vector>
#include <list>
#include <mutex>

#include "dx_utils.h"

class FlagAllocator
{
public:
	FlagAllocator(const int flagsNum);

	int Allocate();
	void Delete(int index);

private:
	
	std::vector<bool> flags;
	std::mutex mutex;
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
		std::scoped_lock<std::mutex> lock(mutex);

		// Check before existing allocations
		{
			const int nextOffset = allocations.empty() ? SIZE : allocations.begin()->offset;

			if (nextOffset >= size)
			{
				allocations.push_front({ 0, size });
				ValidateAllocations();
				return 0;
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
					allocations.insert(nextAllocIt, { currAllocEnd, size });
					ValidateAllocations();
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
					ValidateAllocations();
					return lastAllocEnd;
				}
			}

			assert(false && "Failed to allocate part of buffer");
			return Const::INVALID_OFFSET;

		}
	};


	void Delete(int offset)
	{
		std::scoped_lock<std::mutex> lock(mutex);

		auto it = std::find_if(allocations.begin(), allocations.end(), [offset](const Allocation& alloc)
		{
			return offset == alloc.offset;
		});

		if (it == allocations.end())
		{
			assert(false && "Trying to delete memory that wasn't allocated.");
			return;
		}

		ValidateAllocations();
		allocations.erase(it);
	};

	void ClearAll()
	{
		std::scoped_lock<std::mutex> lock(mutex);

		allocations.clear();
	};

private:

	constexpr static bool isValidateAllocations = false;

	void ValidateAllocations() const
	{
		if constexpr (isValidateAllocations == true)
		{
			int prevOffset = -1;
			int prevSize = -1;
			for (auto it = allocations.cbegin(); it != allocations.cend(); ++it)
			{
				assert(it->offset > prevOffset);
				assert(it->offset != Const::INVALID_OFFSET);
				assert(it->size != Const::INVALID_SIZE);
				assert(it->offset + it->size < SIZE);

				if (prevOffset != -1)
				{
					assert(prevOffset + prevSize <= it->offset);
				}

				prevOffset = it->offset;
				prevSize = it->size;
			}
		}
	}

	std::list<Allocation> allocations;
	std::mutex mutex;
};
