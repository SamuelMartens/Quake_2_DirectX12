#pragma once

#include <vector>
#include <array>
#include <atomic>
#include <tuple>
#include <optional>

#include "dx_utils.h"
#include "dx_threadingutils.h"
#include "dx_bsp.h"
#include "dx_objects.h"
#include "dx_settings.h"

template<typename T>
using SphericalHarmonic9_t = std::array<T, 9>;

struct DiffuseProbe
{
	//#TODO this is incoming radiance right?
	SphericalHarmonic9_t<XMFLOAT4> radianceSh;
};

struct ClusterProbeData
{
	int startIndex = Const::INVALID_INDEX;
};

class LightBaker
{
	
public:
	DEFINE_SINGLETON(LightBaker);

	enum class GenerationMode
	{
		AllClusters,
		CurrentPositionCluster
	};

	void PreBake();
	void PostBake();

	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateClustersBakePoints();

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	void BakeJob();
	int GetTotalProbes() const;
	int GetBakedProbes() const;

	void SetGenerationMode(GenerationMode genMode);
	void SetBakePosition(const XMFLOAT4& position);

private:

	SphericalHarmonic9_t<float> GetSphericalHarmonic9Basis(const XMFLOAT4& direction) const;
	SphericalHarmonic9_t<XMFLOAT4> ProjectOntoSphericalHarmonic(const XMFLOAT4& direction, const XMFLOAT4& color) const;

	

	XMFLOAT4 GatherDirectIrradianceAtInersectionPoint(const Utils::Ray& ray, const Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLights(const Utils::Ray& ray,
		const  Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLight(const XMFLOAT4& intersectionPoint,
		const SurfaceLight& light) const;

	//#TODO passing direction by ref seems silly. Need to pass multiple value
	XMFLOAT4 PathTraceFromProbe(const XMFLOAT4& probeCoord, XMFLOAT4& direction);

	std::vector<std::vector<XMFLOAT4>> clusterBakePoints;
	
	std::atomic<int> currentBakeCluster;

	std::atomic<int> probesBaked;
	std::vector<DiffuseProbe> probes;
	std::vector<ClusterProbeData> clusterProbeData;

	GenerationMode generationMode;
	std::optional<XMFLOAT4> bakePosition;
}; 