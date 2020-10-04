#include "dx_commandlist.h"

#include "dx_app.h"
#include "dx_infrastructure.h"

void CommandList::Init()
{
	ComPtr<ID3D12Device>& device = Infr::Inst().GetDevice();

	// Create command allocator
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandListAlloc)));

	// Create command list 
	ThrowIfFailed(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(commandList.GetAddressOf())));
}

void CommandList::Open()
{
	ThrowIfFailed(commandListAlloc->Reset());
	ThrowIfFailed(commandList->Reset(commandListAlloc.Get(), nullptr));
}

void CommandList::Close()
{
	ThrowIfFailed(commandList->Close());
}
