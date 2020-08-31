#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include "dx_common.h"

class Infr
{
public:
	Infr(const Infr&) = delete;
	Infr& operator=(const Infr&) = delete;
	
	Infr(Infr&&) = delete;
	Infr& operator=(Infr&&) = delete;

	~Infr() = delete;

	static Infr& Inst();

	void Init();

	ComPtr<ID3D12Device>& GetDevice();
	ComPtr<IDXGIFactory4>& GetFactory();

private:

	Infr() = default;

	ComPtr<ID3D12Device>   device;
	ComPtr<IDXGIFactory4>  dxgiFactory;
};