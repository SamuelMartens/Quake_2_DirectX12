#pragma once

#include <list>
#include <vector>
#include <limits>
#include <d3d12.h>
#include <memory>
#include <algorithm>
#include <mutex>

#include "dx_common.h"
#include "dx_allocators.h"
#include "dx_utils.h"
#include "dx_resourcemanager.h"
#include "dx_diagnostics.h"
#include "dx_assert.h"

using BufferHandler = uint32_t;

class GPUJobContext;

namespace Const
{
	extern const BufferHandler INVALID_BUFFER_HANDLER;
};

struct BufferPiece
{
	BufferHandler handler = Const::INVALID_BUFFER_HANDLER;
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

	// No need to explicitly free GPU buffer. COM pointer will release 
	// gpu memory
	~AllocBuffer() = default;

	BufferAllocator<SIZE> allocator;
	ComPtr<ID3D12Resource> gpuBuffer;
};

enum class MemoryType
{
	Upload,
	Default
};

template<int BUFFER_SIZE, int HANDLERS_NUM, MemoryType TYPE, int ENFORCED_ALIGNMENT = 0>
class HandlerBuffer
{
public:

	HandlerBuffer()
		: handlers(HANDLERS_NUM, Const::INVALID_OFFSET)
	{};

	HandlerBuffer(const HandlerBuffer&) = delete;
	HandlerBuffer(HandlerBuffer&&) = delete;

	HandlerBuffer& operator=(const HandlerBuffer&) = delete;
	HandlerBuffer& operator=(HandlerBuffer&&) = delete;

	~HandlerBuffer() = default;

	void Init(GPUJobContext& context)
	{
		if constexpr (TYPE == MemoryType::Default)
		{
			allocBuffer.gpuBuffer = ResourceManager::Inst().CreateDefaultHeapBuffer(nullptr, BUFFER_SIZE, context);
			Diagnostics::SetResourceNameWithAutoId(GetGpuBuffer(), "DefaultMemoryHeap");
		}
		else if constexpr (TYPE == MemoryType::Upload)
		{
			allocBuffer.gpuBuffer = ResourceManager::Inst().CreateUploadHeapBuffer(BUFFER_SIZE);
			Diagnostics::SetResourceNameWithAutoId(GetGpuBuffer(), "UploadMemoryHeap");
		}
	}

	[[nodiscard]]
	BufferHandler Allocate(int size)
	{
		DX_ASSERT(size > 0 && "Invalid allocation size request");

		std::scoped_lock<std::mutex> lock(mutex);

		if constexpr (ENFORCED_ALIGNMENT != 0)
		{
			size = Utils::Align(size, ENFORCED_ALIGNMENT);
		}

		// Find free handler slot
		auto handlerIt = std::find_if(handlers.begin(), handlers.end(), [](BufferHandler h) 
		{
			return h == Const::INVALID_OFFSET;
		});

		DX_ASSERT(handlerIt != handlers.end() && "Can't find free handler during allocation");

		const BufferHandler handler = std::distance(handlers.begin(), handlerIt);

		handlers[handler] = allocBuffer.allocator.Allocate(size);

		return handler;
	}

	void Delete(BufferHandler handler)
	{
		std::scoped_lock<std::mutex> lock(mutex);

		DX_ASSERT(handler != Const::INVALID_BUFFER_HANDLER && "Trying to delete invalid default buffer handler");

		DX_ASSERT(handlers[handler] != Const::INVALID_OFFSET);

		allocBuffer.allocator.Delete(handlers[handler]);

		handlers[handler] = Const::INVALID_OFFSET;
	}

	// IMPORTANT: handler is intentional layer of abstraction between offset and
	// the one who asked for memory allocation. Don't rely on it being the same long term,
	// keep handler around, ask for offset when you need it.
	int GetOffset(BufferHandler handler) const
	{
		std::scoped_lock<std::mutex> lock(mutex);

		DX_ASSERT(handlers[handler] != Const::INVALID_OFFSET && "BufferHandler is invalid, can't get offset");
		return handlers[handler];
	}

	ID3D12Resource* GetGpuBuffer()
	{
		return allocBuffer.gpuBuffer.Get();
	}

private:

	AllocBuffer<BUFFER_SIZE> allocBuffer;

	mutable std::mutex mutex;
	std::vector<int> handlers;
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