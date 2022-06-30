#include "dx_memorymanager.h"


void MemoryManager::Init(GPUJobContext& context)
{
	GetBuff<DefaultBuffer_t>().Init(context);
	GetBuff<UploadBuffer_t>().Init(context);
}
