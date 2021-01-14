#include "dx_memorymanager.h"

#include "dx_infrastructure.h"
#include "dx_resourcemanager.h"
#include "dx_diagnostics.h"

void MemoryManager::Init(GPUJobContext& context)
{
	GetBuff<DefaultBuffer_t>().Init(context);
	GetBuff<UploadBuffer_t>().Init(context);
}
