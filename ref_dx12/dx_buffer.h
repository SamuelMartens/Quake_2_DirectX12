#pragma once

#include <list>
#include <vector>
#include <limits>
#include <d3d12.h>
#include <memory>
#include <cassert>
#include <algorithm>
#include <mutex>

#include "dx_common.h"
#include "dx_allocators.h"
#include "dx_utils.h"

using BufferHandler = uint32_t;

//#DEBUG can I move this to Const:: for convinience?
namespace BuffConst
{
	extern const BufferHandler INVALID_BUFFER_HANDLER;
};

struct BufferPiece
{
	BufferHandler handler = BuffConst::INVALID_BUFFER_HANDLER;
	int offset = Const::INVALID_OFFSET;
};

template<int SIZE>
struct AllocBuffer
{
	AllocBuffer() = default;

	AllocBuffer(const AllocBuffer&) = delete;
	AllocBuffer(AllocBuffer&&) = delete;

	AllocBuffer& operator=(const AllocBuffer&) = delete;
	AllocBuffer& operator=(AllocBuffer&&) = delete;

	// No need to explicitly free our gpu buffer. COM interface will release 
	// gpu memory
	~AllocBuffer() = default;

	BufferAllocator<SIZE> allocator;
	ComPtr<ID3D12Resource> gpuBuffer;
};

template<int BUFFER_SIZE, int HANDLERS_NUM, int ENFORCED_ALIGNMENT = 0>
class HandlerBuffer
{
public:

	HandlerBuffer()
		: m_handlers(HANDLERS_NUM, Const::INVALID_OFFSET)
	{};

	HandlerBuffer(const HandlerBuffer&) = delete;
	HandlerBuffer(HandlerBuffer&&) = delete;

	HandlerBuffer& operator=(const HandlerBuffer&) = delete;
	HandlerBuffer& operator=(HandlerBuffer&&) = delete;

	~HandlerBuffer() = default;

	BufferHandler Allocate(int size)
	{
		std::scoped_lock<std::mutex> lock(mutex);

		if constexpr (ENFORCED_ALIGNMENT != 0)
		{
			size = Utils::Align(size, ENFORCED_ALIGNMENT);
		}

		// Find free handler slot
		auto handlerIt = std::find_if(m_handlers.begin(), m_handlers.end(), [](BufferHandler h) 
		{
			return h == Const::INVALID_OFFSET;
		});

		assert(handlerIt != m_handlers.end() && "Can't find free handler during allocation");

		const BufferHandler handler = std::distance(m_handlers.begin(), handlerIt);

		m_handlers[handler] = allocBuffer.allocator.Allocate(size);

		return handler;
	}

	void Delete(BufferHandler handler)
	{
		std::scoped_lock<std::mutex> lock(mutex);

		assert(handler != BuffConst::INVALID_BUFFER_HANDLER && "Trying to delete invalid default buffer handler");

		assert(m_handlers[handler] != Const::INVALID_OFFSET);

		allocBuffer.allocator.Delete(m_handlers[handler]);

		m_handlers[handler] = Const::INVALID_OFFSET;
	}

	// IMPORTANT: handler is intentional layer of abstraction between offset and
	// the one who asked for memory allocation. Don't rely on it being the same long term,
	// keep handler around, ask for offset when you need it.
	int GetOffset(BufferHandler handler) const
	{
		std::scoped_lock<std::mutex> lock(mutex);

		assert(m_handlers[handler] != Const::INVALID_OFFSET);
		return m_handlers[handler];
	}

	AllocBuffer<BUFFER_SIZE> allocBuffer;

private:

	mutable std::mutex mutex;
	std::vector<int> m_handlers;
};



// This name sucks, but I can't come up with something better. This is just utility structure
// that helps keep together buffer and view, and also do debug check that structure is not
// acquired twice.
class AssertBufferAndView
{
public:

	void Lock();
	void Unlock();

	ComPtr<ID3D12Resource> buffer;
	int viewIndex = Const::INVALID_INDEX;

private:
	bool locked = false;
};