#include "dx_lightbaker.h"
#include "dx_app.h"

#include <set>
#include <cassert>

#ifdef min
#undef min
#endif

std::vector<std::vector<DirectX::XMFLOAT4>> LightBaker::GenerateBakePoints() const
{
	std::set<int> clustersSet = Renderer::Inst().GetBSPTree().GetClustersSet();
	
	if (clustersSet.empty() == true)
	{
		return {};
	}

	std::vector<std::vector<DirectX::XMFLOAT4>> bakePoints(*clustersSet.rbegin());

	for (const int cluster : clustersSet)
	{
		bakePoints[cluster] = GenerateClusterBakePoints(cluster);
	}

	return bakePoints;
}

std::vector<XMFLOAT4> LightBaker::GenerateClusterBakePoints(int clusterIndex) const
{
	constexpr float bakePointsInterval = 50.0f;

	Utils::AABB clusterAABB = Renderer::Inst().GetBSPTree().GetClusterAABB(clusterIndex);
	
	// Amount of bake points along X axis
	const int xAxisNum = std::ceil((clusterAABB.bbMax.x - clusterAABB.bbMin.x) / bakePointsInterval);
	// Amount of bake points along Y axis
	const int yAxisNum = std::ceil((clusterAABB.bbMax.y - clusterAABB.bbMin.y) / bakePointsInterval);
	// Amount of bake points along X axis
	const int zAxisNum = std::ceil((clusterAABB.bbMax.z - clusterAABB.bbMin.z) / bakePointsInterval);

	std::vector<XMFLOAT4> bakePoints;
	bakePoints.reserve(xAxisNum * yAxisNum * zAxisNum);

	for (int xIteration = 0; xIteration < xAxisNum; ++xIteration)
	{
		for (int yIteration = 0; yIteration < yAxisNum; ++yIteration)
		{
			for (int zIteration = 0; zIteration < zAxisNum; ++zIteration)
			{
				bakePoints.push_back({
					std::min(clusterAABB.bbMin.x + bakePointsInterval * xIteration, clusterAABB.bbMax.x),
					std::min(clusterAABB.bbMin.y + bakePointsInterval * yIteration, clusterAABB.bbMax.y),
					std::min(clusterAABB.bbMin.z + bakePointsInterval * zIteration, clusterAABB.bbMax.z),
					1.0f
					});
			}
		}
	}

	return bakePoints;
}