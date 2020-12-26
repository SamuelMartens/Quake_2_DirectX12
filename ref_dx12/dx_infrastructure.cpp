#include "dx_infrastructure.h"

void Infr::Init()
{
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

	// This is super weird. This function internally will throw com exception,
	// but result will be still fine.
	ThrowIfFailed(D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&device)
	));
}

ComPtr<ID3D12Device>& Infr::GetDevice()
{
	return device;
}

ComPtr<IDXGIFactory4>& Infr::GetFactory()
{
	return dxgiFactory;
}
