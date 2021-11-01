#pragma once

#include <vector>
#include <array>

#include "dx_utils.h"

class LightBaker
{
	constexpr static int RANDOM_SAMPLES_SET_SIZE = 32;

public:
	DEFINE_SINGLETON(LightBaker);


	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateBakePoints() const;

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	[[nodiscard]]
	std::vector<std::array<XMFLOAT4, RANDOM_SAMPLES_SET_SIZE>> GenerateRandomSamples(int setsNum) const;

};