#include "dx_memorymanager.h"

#include "dx_infrastructure.h"

void MemoryManager::Init(Context& context)
{
	DefaultBuff_t& defaultBuff = GetBuff<Default>();

	//defaultBuff.allocBuffer.gpuBuffer = CreateDefaultHeapBuffer(nullptr, Settings::DEFAULT_MEMORY_BUFFER_SIZE, context);
}
