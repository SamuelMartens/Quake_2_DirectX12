#include "dx_allocators.h"

#include <cassert>

FlagAllocator::FlagAllocator(const int flagsNum)
{
	flags.resize(flagsNum, false);
}

int FlagAllocator::Allocate()
{
	auto resIt = std::find(flags.begin(), flags.end(), false);

	assert(resIt != flags.end() && "Failed allocation attempt in flag allocator");

	*resIt = true;

	return std::distance(flags.begin(), resIt);
}

void FlagAllocator::Delete(int index)
{
	assert(flags[index] == true && "Attempt to delete free memory in flag allocator");

	flags[index] = false;
}


