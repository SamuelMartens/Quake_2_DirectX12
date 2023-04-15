#pragma once

#include <vector>
#include <variant>

#include "dx_common.h"
#include "dx_utils.h"

class Resource;

namespace Light
{
	// Should match shader definition in Debug.passh
	enum class Type
	{
		Area = 0,
		Point = 1,

		None
	};
};

struct GPULight
{
	XMFLOAT4X4 worldTransform;
	// RGB <- color, A <- intensity
	XMFLOAT4 colorAndIntensity = { 0.0f, 0.0f, 0.0f, 0.0f };
	// Point light X - radius
	// Area light X,Y sizes of OBB (in 2D)
	XMFLOAT2 extends = { 0.0f, 0.0f };
	int type = static_cast<int>(Light::Type::None);
};

// In reality this is not Point light, but Spherical area light 
struct PointLight
{
	XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 1.0f };
	float objectPhysicalRadius = 0.0f;
	// Amount of power per unit solid angle
	float intensity = 0.0f;

	std::vector<int> clusters;

	static Utils::Sphere GetBoundingSphere(const PointLight& light);
	static GPULight ToGPULight(const PointLight& light);
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

	float radiance = 0.0f;

	static void InitIfValid(AreaLight& light);
	static float CalculateRadiance(const AreaLight& light);

	static Utils::Sphere GetBoundingSphere(const AreaLight& light, const XMFLOAT2& extends, const XMFLOAT4& origin);
	static GPULight ToGPULight(const AreaLight& light);
};

struct GPULightBoundingVolume
{
	XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
};

struct LightBoundingVolume
{
	Light::Type type = Light::Type::None;
	Utils::Sphere shape;

	int sourceIndex = Const::INVALID_INDEX;
};