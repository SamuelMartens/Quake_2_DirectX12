#pragma once

#include <vector>
#include <mutex>
#include <bitset>
#include <atomic>

#include "dx_utils.h"
#include "dx_assert.h"

template<int SIZE>
class FlagAllocator
{
public:
	FlagAllocator() = default;

	FlagAllocator(FlagAllocator&&) = delete;
	FlagAllocator(const FlagAllocator&) = delete;

	FlagAllocator& operator=(FlagAllocator&&) = delete;
	FlagAllocator& operator=(const FlagAllocator&) = delete;

	[[nodiscard]]
	int Allocate() 
	{
		std::scoped_lock<std::mutex> lock(mutex);

		int index = 0;

		while (index < flags.size() && flags[index] == true)
		{
			++index;
		}

		DX_ASSERT(index < flags.size() && "Failed allocation attempt in flag allocator");

		flags[index] = true;

		return index;

	};

	[[nodiscard]]
	int AllocateRange(int size) 
	{
		std::scoped_lock<std::mutex> lock(mutex);

		int setInRow = 0;

		// Find free bits span
		int index = 0;
		for (;index < flags.size(); ++index)
		{
			if (flags[index] == false)
			{
				++setInRow;

				if (setInRow == size)
				{
					break;
				}
			}
			else
			{
				setInRow = 0;
			}
		}

		DX_ASSERT(setInRow == size && "Failed range allocation attempt in flag allocator");

		// Mark bit as allocated
		for (; setInRow > 0; setInRow--, index--)
		{
			flags[index] = true;
			
		}

		return index + 1;
	};

	void Delete(int index) 
	{
		std::scoped_lock<std::mutex> lock(mutex);

		DX_ASSERT(flags[index] == true && "Attempt to delete free memory in flag allocator");

		flags[index] = false;

	};

	void DeleteRange(int index, int size)
	{
		std::scoped_lock<std::mutex> lock(mutex);

		for (int i = index; i < index + size; ++i)
		{
			DX_ASSERT(flags[i] == true && "DeleteRange trying to free some flag twice");

			flags[i] = false;
		}
	};

private:
	
	std::mutex mutex;
	std::bitset<SIZE> flags;
};

template<int SIZE>
class StreamingFlagAllocator
{
public:

	StreamingFlagAllocator() = default;

	StreamingFlagAllocator(StreamingFlagAllocator&&) = delete;
	StreamingFlagAllocator(const StreamingFlagAllocator&) = delete;

	StreamingFlagAllocator& operator=(StreamingFlagAllocator&&) = delete;
	StreamingFlagAllocator& operator=(const StreamingFlagAllocator&) = delete;


	[[nodiscard]]
	int Allocate(int size)
	{
		const int result = currentAllocationIndex.fetch_add(size);
		DX_ASSERT(currentAllocationIndex < SIZE && "Streaming allocator exceeded it's maximum size");

		return result;
	}

	void Reset()
	{
		currentAllocationIndex.store(0);
	}

private:

	std::atomic<int> currentAllocationIndex = 0;
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

	[[nodiscard]]
	int Allocate(int size)
	{
		std::scoped_lock<std::mutex> lock(mutex);

		DX_ASSERT(size > 0 && "Invalid allocation size request");

		// Check before existing allocations
		{
			const int nextOffset = allocations.empty() ? SIZE : allocations.begin()->offset;

			if (nextOffset >= size)
			{
				allocations.insert( allocations.begin(),{ 0, size });
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

			DX_ASSERT(false && "Failed to allocate part of buffer");
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
			DX_ASSERT(false && "Trying to delete memory that wasn't allocated.");
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

	constexpr static bool VALIDATE_ALLOCATIONS = false;

	void ValidateAllocations() const
	{
		if constexpr (VALIDATE_ALLOCATIONS == true)
		{
			constexpr int period = 60;
			static int count = 0;

			count++;

			if (count % period != 0)
			{
				return;
			}

			int prevOffset = -1;
			int prevSize = -1;
			for (auto it = allocations.cbegin(); it != allocations.cend(); ++it)
			{
				DX_ASSERT(it->offset > prevOffset);
				DX_ASSERT(it->offset != Const::INVALID_OFFSET);
				DX_ASSERT(it->size != Const::INVALID_SIZE);
				DX_ASSERT(it->offset + it->size < SIZE);

				if (prevOffset != -1)
				{
					DX_ASSERT(prevOffset + prevSize <= it->offset);
				}

				prevOffset = it->offset;
				prevSize = it->size;
			}
		}
	}
	
	std::vector<Allocation> allocations;
	std::mutex mutex;
};
