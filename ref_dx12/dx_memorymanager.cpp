#include "dx_memorymanager.h"

#include "dx_infrastructure.h"
#include "dx_resourcemanager.h"
#include "dx_diagnostics.h"

void MemoryManager::Init(JobContext& context)
{
	DefaultBuff_t& defaultBuff = GetBuff<Default>();
	
	defaultBuff.allocBuffer.gpuBuffer = 
		ResourceManager::Inst().CreateDefaultHeapBuffer(nullptr, Settings::DEFAULT_MEMORY_BUFFER_SIZE, context);
	Diagnostics::SetResourceName(defaultBuff.allocBuffer.gpuBuffer.Get(), "DefaultMemoryHeap");

	
	UploadBuff_t& uploadBuff = GetBuff<Upload>();
	uploadBuff.allocBuffer.gpuBuffer =
		ResourceManager::Inst().CreateUploadHeapBuffer(Settings::UPLOAD_MEMORY_BUFFER_SIZE);
	Diagnostics::SetResourceName(uploadBuff.allocBuffer.gpuBuffer.Get(), "UploadMemoryHeap");
}
