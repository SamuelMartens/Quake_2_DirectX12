#pragma once

#include <wrl.h>
#include <d3d12.h>

#include "d3dx12.h"

using namespace Microsoft::WRL;

class Texture
{
public:

	ComPtr<ID3D12Resource> buffer;
	// Daamn, all the examples I saw so far use index in descriptor heap
	// and I don't understand why, cause we have to get this descriptor handle 
	// every time, why can't I descriptor handle directly?
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor;
};