#include "dx_material.h"

const std::array<std::vector<std::string>, static_cast<int>(Material::ID::Count)> Material::IDFilters =
{
	std::vector<std::string>{"floor"},						// Floor
	std::vector<std::string>{"ggrat", "broken"},			// RoughMetal
	std::vector<std::string>{"box"},						// MetalWithPaint
	std::vector<std::string>{"grate", "metal", "baselt"},	// Metal
	std::vector<std::string>{"color"},						// Wood
	std::vector<std::string>{"ceil"},						// Plastic
	std::vector<std::string>{}								// Default
};

const std::array<Material, static_cast<int>(Material::ID::Count)> Material::IDMaterials =
{
	Material{ 0.10f, 1.0f, 1.0f },  // Floor
	Material{ 0.99f, 0.93f, 1.0f },	// RoughMetal
	Material{ 0.25f, 0.85f, 1.0f },	// MetalWithPaint
	Material{ 0.50f, 1.0f, 1.0f },	// Metal
	Material{ 0.80f, 0.0f, 1.0f },	// Wood
	Material{ 0.20f, 0.0f, 1.0f },	// Plastic
	Material{}						// Default
};

Material::ID Material::FindMaterialMatchFromTextureName(const std::string& textureName)
{
	for (size_t materialIndex = 0; materialIndex < IDFilters.size(); ++materialIndex)
	{
		const std::vector<std::string>& materialFilters = IDFilters[materialIndex];

		for (size_t filterIndex = 0; filterIndex < materialFilters.size(); ++filterIndex)
		{
			if (textureName.find(materialFilters[filterIndex]) != std::string::npos)
			{
				return static_cast<ID>(materialIndex);
			}
		}
	}

	return ID::Default;
}
