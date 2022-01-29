#pragma once

#include <vector>

#include "dx_common.h"
#include "dx_objects.h"

class Resource;

struct PointLight
{
	XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
	float intensity = 0.0f;

	std::vector<int> clusters;
};

//#TODO remove ambiguity created by this by naming here.
// This one is called surface, like in original Q2 rendering
// but in engine I call this things StaticObject
struct SurfaceLight
{
	// Surface index in array of static objects (currently belongs to Renderer)
	// surface == staticObject
	// Be aware, that lighting characteristics are stored in texInfo
	// that is referenced by this StaticObject
	int surfaceIndex = Const::INVALID_INDEX;

	float area = 0.0f;
	// Each value is upper bound of PDF for that triangle,
	// look previous triangle for lower bound value
	std::vector<float> trianglesPDF;

	XMFLOAT4 irradiance = { 0.0f, 0.0f, 0.0f, 0.0f };

	static void InitIfValid(SurfaceLight& light);

	static XMFLOAT4 CalculateReflectivity(const Resource& texture, const std::byte* textureData);
	static XMFLOAT4 CalculateIrradiance(const SurfaceLight& light);

	static float GetUniformSamplePDF(const SurfaceLight& light);
};