#include "dx_allocators.h"

#include <cassert>
#include <algorithm>

FlagAllocator::FlagAllocator(const int flagsNum)
{
	std::scoped_lock<std::mutex> lock(mutex);

	flags.resize(flagsNum, false);
}

int FlagAllocator::Allocate()
{
	std::scoped_lock<std::mutex> lock(mutex);

	auto resIt = std::find(flags.begin(), flags.end(), false);

	assert(resIt != flags.end() && "Failed allocation attempt in flag allocator");

	*resIt = true;

	return std::distance(flags.begin(), resIt);
}

int FlagAllocator::AllocateRange(int size)
{
	std::scoped_lock<std::mutex> lock(mutex);

	std::vector<bool> searchSequence(size, false);

	auto resIt = std::search(flags.begin(), flags.end(), searchSequence.begin(), searchSequence.end());

	assert(resIt != flags.end() && "Failed range allocation attempt in flag allocator");

	// Mark elements as done
	std::fill(resIt, resIt + size, true);

	return std::distance(flags.begin(), resIt);
}

void FlagAllocator::Delete(int index)
{
	std::scoped_lock<std::mutex> lock(mutex);

	assert(flags[index] == true && "Attempt to delete free memory in flag allocator");

	flags[index] = false;
}


