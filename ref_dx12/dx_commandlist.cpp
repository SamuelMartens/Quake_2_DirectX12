#include "dx_commandlist.h"

#include "dx_app.h"
#include "dx_infrastructure.h"
#include "dx_diagnostics.h"

void CommandList::Init()
{
	ComPtr<ID3D12Device>& device = Infr::Inst().GetDevice();

	// Create command allocator
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandListAlloc)));

	Diagnostics::SetResourceNameWithAutoId(commandListAlloc.Get(), "CmdAlloc");

	// Create command list 
	ThrowIfFailed(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(commandList.GetAddressOf())));

	Diagnostics::SetResourceNameWithAutoId(commandList.Get(), "CmdList");
}

void CommandList::Open()
{
	ThrowIfFailed(commandListAlloc->Reset());
	ThrowIfFailed(commandList->Reset(commandListAlloc.Get(), nullptr));

#ifdef VALIDATE_COMMAND_LIST
	isOpen = true;
#endif // VALIDATE_COMMAND_LIST

}

void CommandList::Close()
{
	ThrowIfFailed(commandList->Close());

#ifdef VALIDATE_COMMAND_LIST
	isOpen = false;
#endif // VALIDATE_COMMAND_LIST

}

bool CommandList::GetIsOpen() const
{
#ifdef VALIDATE_COMMAND_LIST
	return isOpen;
#endif
	return false;
}
