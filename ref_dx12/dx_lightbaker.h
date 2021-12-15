#pragma once

#include <vector>
#include <array>
#include <atomic>

#include "dx_utils.h"
#include "dx_threadingutils.h"
#include "dx_bsp.h"
#include "dx_objects.h"

class LightBaker
{
public:

	constexpr static int RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE = 32;
	constexpr static int RANDOM_UNIFORM_SPHERE_SAMPLES_SETS_NUM = 128;

	constexpr static int AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE = 4;
	constexpr static int AREA_LIGHTS_RANDOM_SAMPLES_SETS_NUM = 18;

public:
	DEFINE_SINGLETON(LightBaker);

	void GenerateSourceData();

	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateClustersBakePoints() const;

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	[[nodiscard]]
	std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE>> GenerateRandomUniformSphereSamples() const;

	[[nodiscard]]
	std::vector<std::array<XMFLOAT4, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>> GenerateRandomAreaLightsSamples() const;

	void BakeJob(GPUJobContext& context);

//#DEBUG uncomment
//private:

	struct BSPNodeRayIntersectionResult
	{
		int staticObjIndex = Const::INVALID_INDEX;
		int triangleIndex = Const::INVALID_INDEX;

		Utils::RayTriangleIntersectionResult rayTriangleIntersection;
	};

	XMFLOAT4 GatherIrradianceAtInersectionPoint(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLights(const Utils::Ray& ray,
		const BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLight(const XMFLOAT4& intersectionPoint,
		const SurfaceLight& light,
		const std::array<XMFLOAT4, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>& samplesSet) const;

	XMFLOAT4 PathTrace(const Utils::Ray& ray) const;
	//#DEBUG this function passes minT in 'result' and 'minT' is that correct? Can i get rid of one of parameters?
	//#DEBUG this should not be in this class
	bool FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, BSPNodeRayIntersectionResult& result, float& minT) const;

	//#DEBUG this actually should be array too
	std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE>> uniformSphereSamplesSets;
	std::vector<std::array<XMFLOAT4, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>> areaLightsSamplesSets;

	std::vector<std::vector<XMFLOAT4>> clusterBakePoints;
	
	std::atomic<int> currentBakeCluster;

};