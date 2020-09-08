#pragma once

#include <d3d12.h>

#include "dx_common.h"

struct CommandList
{
	void Init();

	void Open();
	void Close();

	// Rendering related
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12CommandAllocator> commandListAlloc;
};