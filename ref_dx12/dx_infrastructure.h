#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include "dx_common.h"
#include "dx_utils.h"

class Infr
{
public:
	
	DEFINE_SINGLETON(Infr);

	void Init();

	ComPtr<ID3D12Device>& GetDevice();
	ComPtr<IDXGIFactory4>& GetFactory();

private:

	ComPtr<ID3D12Device>   device;
	ComPtr<IDXGIFactory4>  dxgiFactory;
};