#pragma once

#include <string>
#include <array>
#include <vector>

struct Material
{
	// Sorted in priority order, in descending order
	enum class ID
	{
		Floor,
		RoughMetal,
		MetalWithPaint,
		Metal,
		Wood,
		Plastic,
		Default, // Must be lowest priority

		Count
	};

	static const std::array<std::vector<std::string>, static_cast<int>(ID::Count)> IDFilters;
	static const std::array<Material, static_cast<int>(ID::Count)> IDMaterials;

	static constexpr float DEFAULT_ROUGHNESS = 0.75f;
	static constexpr float DEFAULT_METALNESS = 0.85f;
	static constexpr float DEFAULT_REFLECTANCE = 1.0f;

	float roughness = DEFAULT_ROUGHNESS;
	float metalness = DEFAULT_METALNESS;
	float reflectance = DEFAULT_REFLECTANCE;

	static ID FindMaterialMatchFromTextureName(const std::string& textureName);
};

