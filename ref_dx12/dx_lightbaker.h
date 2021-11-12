#pragma once

#include <vector>
#include <array>
#include <atomic>

#include "dx_utils.h"
#include "dx_threadingutils.h"
#include "dx_bsp.h"

class LightBaker
{
	constexpr static int RANDOM_SAMPLES_SET_SIZE = 32;
	constexpr static int RANDOM_SAMPLES_SETS_NUM = 128;

public:
	DEFINE_SINGLETON(LightBaker);

	void GenerateSourceData();

	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateClustersBakePoints() const;

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	[[nodiscard]]
	std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_SAMPLES_SET_SIZE>> GenerateRandomSamples() const;

	void BakeJob(GPUJobContext& context);

//#DEBUG uncomment
//private:

	struct BSPNodeRayIntersectionResult
	{
		int staticObjIndex = Const::INVALID_INDEX;
		int triangleIndex = Const::INVALID_INDEX;

		Utils::RayTriangleIntersectionResult rayTriangleIntersection;
	};

	float GatherIrradianceAtInersectionPoint(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	float PathTrace(const Utils::Ray& ray) const;
	//#DEBUG this function passes minT in 'result' and 'minT' is that correct? Can i get rid of one of parameters?
	//#DEBUG this should not be in this class
	bool FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, BSPNodeRayIntersectionResult& result, float& minT) const;

	std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_SAMPLES_SET_SIZE>> samplesSets;

	std::vector<std::vector<XMFLOAT4>> clusterBakePoints;
	
	std::atomic<int> currentBakeCluster;

};