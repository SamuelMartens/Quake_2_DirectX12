#pragma once

#include <d3d12.h>
#include <array>
#include <atomic>

#include "dx_common.h"
#include "dx_allocators.h"
#include "dx_utils.h"
#include "dx_assert.h"

#if (ENABLE_VALIDATION) 
#define VALIDATE_COMMAND_LIST
#endif // ENABLE_VALIDATION

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

	bool GetIsOpen() const;

	ID3D12GraphicsCommandList* GetGPUList();

private:

	// Rendering related
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12CommandAllocator> commandListAlloc;

#ifdef VALIDATE_COMMAND_LIST
	std::atomic<bool> isOpen = false;
#endif // VALIDATE_COMMAND_LIST



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

	void ValidateListsClosed(const std::vector<int>& lists) const
	{
#ifdef VALIDATE_COMMAND_LIST
		for(int i = 0; i < lists.size(); ++i)
		{
			assert(commandLists[lists[i]].GetIsOpen() == false && "Open command lists detected during validation");
		}
#endif // VALIDATE_COMMAND_LIST
	}

	std::array<CommandList, SIZE> commandLists;
	FlagAllocator<SIZE> allocator;
};

using CommandListRAIIGuard_t = Utils::RAIIGuard<CommandList, &CommandList::Open, &CommandList::Close>;