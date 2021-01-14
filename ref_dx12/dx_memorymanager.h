#pragma once

#include <tuple>
#include <type_traits>

#include "dx_utils.h"
#include "dx_settings.h"
#include "dx_buffer.h"

/*
	Controls preallocated GPU memory. It still possible to create separate 
	buffer via ResourceManager, but not recommended.
*/

struct GPUJobContext;

using UploadBuffer_t = HandlerBuffer<
	Settings::UPLOAD_MEMORY_BUFFER_SIZE,
	Settings::UPLOAD_MEMORY_BUFFER_HANDLERS_NUM,
	MemoryType::Upload,
	Settings::CONST_BUFFER_ALIGNMENT>;

using DefaultBuffer_t = HandlerBuffer<
	Settings::DEFAULT_MEMORY_BUFFER_SIZE,
	Settings::DEFAULT_MEMORY_BUFFER_HANDLERS_NUM,
	MemoryType::Default>;

class MemoryManager
{
public:
	
	DEFINE_SINGLETON(MemoryManager);

	void Init(GPUJobContext& context);

private:

	std::tuple<UploadBuffer_t, DefaultBuffer_t> buffers;

public:


	template<typename T>
	T& GetBuff()
	{
		return std::get<T>(buffers);
	}
};