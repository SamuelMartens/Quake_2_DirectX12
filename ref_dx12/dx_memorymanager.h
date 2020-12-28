#pragma once

#include <tuple>
#include <type_traits>

#include "dx_utils.h"
#include "dx_settings.h"
#include "dx_buffer.h"
#include "dx_jobmultithreading.h"

/*
	Controls preallocated GPU memory. It still possible to create separate 
	buffer via ResourceManager, but not recommended.
*/

class MemoryManager
{
public:
	using UploadBuff_t = HandlerBuffer<
		Settings::UPLOAD_MEMORY_BUFFER_SIZE,
		Settings::UPLOAD_MEMORY_BUFFER_HANDLERS_NUM,
		Settings::CONST_BUFFER_ALIGNMENT>;

	using DefaultBuff_t = HandlerBuffer<
		Settings::DEFAULT_MEMORY_BUFFER_SIZE,
		Settings::DEFAULT_MEMORY_BUFFER_HANDLERS_NUM>;

	enum Type
	{
		Upload,
		Default
	};

	DEFINE_SINGLETON(MemoryManager);

	void Init(GPUJobContext& context);

private:

	std::tuple<UploadBuff_t, DefaultBuff_t> buffers;

public:

	// The reason enum is used here but not buffer type, is because buffers are essentially the same template types,
	// so there is a chance of collision if we use types for getting buffer
	template<Type t>
	auto GetBuff() ->std::add_lvalue_reference_t<std::tuple_element_t<t, decltype(buffers)>>
	{
		return std::get<t>(buffers);
	}
};