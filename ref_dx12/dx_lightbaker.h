#pragma once

#include <vector>
#include <array>
#include <atomic>
#include <tuple>

#include "dx_utils.h"
#include "dx_threadingutils.h"
#include "dx_bsp.h"
#include "dx_objects.h"
#include "dx_settings.h"

template<typename T>
using SphericalHarmonic9_t = std::array<T, 9>;

class LightBaker
{
	
public:
	DEFINE_SINGLETON(LightBaker);

	void GenerateSourceData();

	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateClustersBakePoints() const;

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	void BakeJob(GPUJobContext& context);

//#DEBUG uncomment
//private:

	SphericalHarmonic9_t<float> GetSphericalHarmonic9Basis(const XMFLOAT4& direction) const;
	SphericalHarmonic9_t<XMFLOAT4> ProjectOntoSphericalHarmonic(const XMFLOAT4& direction, const XMFLOAT4& color) const;

	struct BSPNodeRayIntersectionResult
	{
		int staticObjIndex = Const::INVALID_INDEX;
		int triangleIndex = Const::INVALID_INDEX;

		Utils::RayTriangleIntersectionResult rayTriangleIntersection;

		static XMFLOAT4 GetNormal(const BSPNodeRayIntersectionResult& result);
	};

	XMFLOAT4 GatherDirectIrradianceAtInersectionPoint(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLights(const Utils::Ray& ray,
		const BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLight(const XMFLOAT4& intersectionPoint,
		const SurfaceLight& light) const;

	//#TODO passing direction by ref seems silly. Need to pass multiple value
	XMFLOAT4 PathTraceFromProbe(const XMFLOAT4& probeCoord, XMFLOAT4& direction);

	// Is Intersected something, Intersection result
	[[nodiscard]]
	std::tuple<bool, BSPNodeRayIntersectionResult> FindClosestRayIntersection(const Utils::Ray& ray) const;
	//#DEBUG this function passes minT in 'result' and 'minT' is that correct? Can i get rid of one of parameters?
	//#DEBUG this should not be in this class
	bool FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, BSPNodeRayIntersectionResult& result, float& minT) const;

	std::vector<std::vector<XMFLOAT4>> clusterBakePoints;
	
	std::atomic<int> currentBakeCluster;

}; 