#pragma once

#include <vector>

#include "dx_common.h"
#include "dx_objects.h"

class Resource;

// In reality this is not Point light, but Spherical area light 
struct PointLight
{
	XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 1.0f };
	// NOTE: this doesn't indicate how far this light can reach, instead it shows physical size of this light.
	float radius = 0.0f;
	// Amount of power per unit solid angle
	float intensity = 0.0f;

	std::vector<int> clusters;
};

struct AreaLight
{
	// Index in array of static objects (currently belongs to Renderer)
	// Area Light == Static Object 
	// Be aware, that lighting characteristics are stored in texInfo
	// that is referenced by this StaticObject
	int staticObjectIndex = Const::INVALID_INDEX;

	float area = 0.0f;
	// Each value is upper bound of PDF for that triangle,
	// look previous triangle for lower bound value
	std::vector<float> trianglesPDF;

	XMFLOAT4 radiance = { 0.0f, 0.0f, 0.0f, 0.0f };

	static void InitIfValid(AreaLight& light);

	static XMFLOAT4 CalculateReflectivity(const Resource& texture, const std::byte* textureData);
	static XMFLOAT4 CalculateRadiance(const AreaLight& light);

	static float GetUniformSamplePDF(const AreaLight& light);
};