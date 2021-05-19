#pragma once

#include <vector>

#include "dx_common.h"
#include "dx_objects.h"

struct PointLight
{
	XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
	float intensity = 0.0f;

	std::vector<int> clusters;
};

struct SurfaceLight
{
	int surfaceIndex = Const::INVALID_INDEX;
};