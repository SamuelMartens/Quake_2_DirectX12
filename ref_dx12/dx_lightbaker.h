#pragma once

#include "dx_utils.h"

class LightBaker
{

public:
	DEFINE_SINGLETON(LightBaker);


	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateBakePoints() const;

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

private:

};