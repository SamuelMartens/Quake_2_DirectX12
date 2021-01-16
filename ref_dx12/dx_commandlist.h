#pragma once

#include <d3d12.h>
#include <array>

#include "dx_common.h"
#include "dx_allocators.h"
#include "dx_utils.h"

class CommandList
{
public:

	CommandList() = default;

	CommandList(const CommandList&) = delete;
	CommandList& operator=(const CommandList&) = delete;

	CommandList(CommandList&&) = default;
	CommandList& operator=(CommandList&&) = default;

	void Init();

	void Open();
	void Close();

	// Rendering related
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12CommandAllocator> commandListAlloc;
};

template<int SIZE>
struct CommandListBuffer
{
	CommandListBuffer() = default;
	
	CommandListBuffer(const CommandListBuffer&) = delete;
	CommandListBuffer& operator=(const CommandListBuffer&) = delete;

	CommandListBuffer(CommandListBuffer&&) = delete;
	CommandListBuffer& operator=(CommandListBuffer&&) = delete;

	~CommandListBuffer() = default;

	std::array<CommandList, SIZE> commandLists;
	FlagAllocator<SIZE> allocator;
};

using CommandListRAIIGuard_t = Utils::RAIIGuard<CommandList, &CommandList::Open, &CommandList::Close>;