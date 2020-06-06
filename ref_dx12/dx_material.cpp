#include "dx_material.h"


MaterialSource::MaterialSource()
{
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
}

