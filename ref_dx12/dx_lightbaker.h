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

struct PathSegment
{
	XMFLOAT4 v0 = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	XMFLOAT4 v1 = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

	XMFLOAT4 radiance = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

	int bounce = 0;
};

struct DiffuseProbe
{
	using DiffuseSH_t = SphericalHarmonic9_t<XMFLOAT4>;

	//#TODO this is incoming radiance right?
	DiffuseSH_t radianceSh;

	// Contains lines list, that describe rays used for tracing
	// these probes
	std::optional<std::vector<PathSegment>> pathTracingSegments;
};

struct ClusterProbeData
{
	int startIndex = Const::INVALID_INDEX;
};

enum class LightBakingMode
{
	AllClusters,
	CurrentPositionCluster
};


struct ProbePathTraceResult
{
	XMFLOAT4 radiance;

	std::optional<std::vector<PathSegment>> pathSegments;
};

enum class BakeFlags
{
	SaveRayPath,

	Count
};

struct BakingResult
{
	struct ClusterSize
	{
		int startIndex = Const::INVALID_INDEX;
	};

	std::optional<LightBakingMode> bakingMode;

	// If baking mode is CurrentPositionCluster this will store current
	// baking cluster
	std::optional<int> bakingCluster;
	std::optional<std::vector<ClusterSize>> clusterSizes;

	std::vector<DiffuseProbe> probeData;
};


class LightBaker
{
public:
	DEFINE_SINGLETON(LightBaker);


	void PreBake();
	void PostBake();

	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateClustersBakePoints();

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	void BakeJob();
	int GetTotalProbesNum() const;
	int GetBakedProbesNum() const;

	LightBakingMode GetBakingMode() const;
	bool GetBakeFlag(BakeFlags flag) const;

	BakingResult TransferBakingResult();

	void SetBakingMode(LightBakingMode genMode);
	void SetBakePosition(const XMFLOAT4& position);
	void SetBakeFlag(BakeFlags flag, bool value);

private:

	SphericalHarmonic9_t<float> GetSphericalHarmonic9Basis(const XMFLOAT4& direction) const;
	SphericalHarmonic9_t<XMFLOAT4> ProjectOntoSphericalHarmonic(const XMFLOAT4& direction, const XMFLOAT4& color) const;

	XMFLOAT4 GatherDirectIrradianceAtInersectionPoint(const Utils::Ray& ray, const Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLights(const Utils::Ray& ray,
		const  Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult) const;

	XMFLOAT4 GatherIrradianceFromAreaLight(const XMFLOAT4& intersectionPoint,
		const SurfaceLight& light) const;

	//#TODO passing direction by ref seems silly. Need to pass multiple value
	ProbePathTraceResult PathTraceFromProbe(const XMFLOAT4& probeCoord, XMFLOAT4& direction);

	std::vector<std::vector<XMFLOAT4>> clusterBakePoints;
	
	std::atomic<int> currentBakeCluster;

	std::atomic<int> probesBaked;
	std::vector<ClusterProbeData> clusterProbeData;

	std::optional<XMFLOAT4> bakePosition;

	LightBakingMode generationMode = LightBakingMode::CurrentPositionCluster;
	Utils::Flags<BakeFlags> bakeFlags;

	std::optional<int> bakeCluster;
	std::vector<DiffuseProbe> probes;
}; 