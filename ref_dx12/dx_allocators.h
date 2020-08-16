#pragma once

#include <vector>

class FlagAllocator
{
public:
	FlagAllocator(const int flagsNum);

	int Allocate();
	void Delete(int index);

private:
	
	std::vector<bool> flags;
};