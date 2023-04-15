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

struct LightSamplePoint
{
	struct Sample
	{
		XMFLOAT4 position = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		XMFLOAT4 radiance = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

		Light::Type lightType = Light::Type::None;
	};

	XMFLOAT4 position = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

	std::vector<Sample> samples;
};

using PathLightSampleInfo_t = std::vector<LightSamplePoint>;

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

	// Stores incoming radiance
	DiffuseSH_t radianceSh;

	// Contains lines list, that describe rays used for tracing
	// these probes
	std::optional<std::vector<PathSegment>> pathTracingSegments;
	// Contains data about sampling light sources during path tracing.
	// Each vector is a list of intersection point for a single path. So for
	// one probe we have a list of path segments
	std::optional<std::vector<PathLightSampleInfo_t>> lightSamples;
};

// If changing this enum, don't forget to tweak string representation
enum class LightBakingMode
{
	AllClusters,
	CurrentPositionCluster,

	Count
};

extern const std::array<std::string,static_cast<int>(LightBakingMode::Count)>  LightBakingMode_Str;

struct ProbePathTraceResult
{
	XMFLOAT4 radiance;

	std::optional<std::vector<PathSegment>> pathSegments;
	std::optional<PathLightSampleInfo_t> lightSamples;
};

enum class BakeFlags
{
	SaveRayPath,
	SaveLightSampling,

	SamplePointLights,
	SampleAreaLights,

	SaveToFileAfterBake,

	Count
};

// Should match definition in SampleIndirect.pass
struct ClusterProbeGridInfo
{
	int32_t SizeX = Const::INVALID_SIZE;
	int32_t SizeY = Const::INVALID_SIZE;
	int32_t SizeZ = Const::INVALID_SIZE;
	int32_t StartIndex = Const::INVALID_INDEX;
};

struct BakingData
{
	// Technically, this is not optional, but if it is not initialized, then 
	// it should be explicitly not initialized.
	std::optional<LightBakingMode> bakingMode;

	// If baking mode is CurrentPositionCluster this will store current
	// baking cluster
	std::optional<int> bakingCluster;
	
	// Size of probe grid for each cluster
	std::vector<XMINT3> clusterProbeGridSizes;

	// Reflects index of the first probe for each cluster 
	std::vector<int> clusterFirstProbeIndices;

	std::vector<DiffuseProbe> probes;
};

namespace Parsing
{
	struct LightBakingContext
	{
		BakingData bakingResult;
	};
}


//#TODO
// 1) Implement albedo texture sampling on object
// 2) Implement albedo texture sampling on lights

class LightBaker
{
public:

	static std::string BakingModeToStr(LightBakingMode mode);
	static LightBakingMode StrToBakingMode(const std::string& str);

public:
	DEFINE_SINGLETON(LightBaker);

	void Init();

	void PreBake();
	void PostBake();

	[[nodiscard]]
	std::vector<std::vector<XMFLOAT4>> GenerateClustersBakePoints();

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateClusterBakePoints(int clusterIndex) const;

	XMINT3 GenerateClusterGridSize(int clusterIndex) const;

	void BakeJob();
	void LoadBakingResultsFromFileJob();

	int GetTotalProbesNum() const;
	int GetBakedProbesNum() const;

	LightBakingMode GetBakingMode() const;
	bool GetBakeFlag(BakeFlags flag) const;

	BakingData TransferBakingResult();

	void SetBakingMode(LightBakingMode genMode);
	void SetBakePosition(const XMFLOAT4& position);
	void SetBakeFlag(BakeFlags flag, bool value);

	uint32_t GetBakeVersion() const;

private:

	SphericalHarmonic9_t<float> GetSphericalHarmonic9Basis(const XMFLOAT4& direction) const;
	SphericalHarmonic9_t<XMFLOAT4> ProjectOntoSphericalHarmonic(const XMFLOAT4& direction, const XMFLOAT4& color) const;

	XMFLOAT4 GatherDirectIradianceAtInersectionPoint(
		const Utils::Ray& ray, 
		const Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult,
		LightSamplePoint* lightSampleDebugInfo) const;

	XMFLOAT4 GatherDirectIrradianceFromPointLights(
		const XMFLOAT4& intersectionPoint,
		const XMFLOAT4& intersectionSurfaceNormal,
		LightSamplePoint* lightSampleDebugInfo) const;

	XMFLOAT4 GatherDirectIradianceFromAreaLights(
		const XMFLOAT4& intersectionPoint,
		const XMFLOAT4& intersectionSurfaceNormal,
		LightSamplePoint* lightSampleDebugInfo) const;

	XMFLOAT4 GatherDirectIradianceFromAreaLight(
		const XMFLOAT4& intersectionPoint,
		const XMFLOAT4& intersectionSurfaceNormal,
		const AreaLight& light,
		LightSamplePoint* lightSampleDebugInfo) const;

	ProbePathTraceResult PathTraceFromProbe(const XMFLOAT4& probeCoord, XMFLOAT4& direction);

	void SaveBakingResultsToFile(const BakingData& bakingResult) const;
	BakingData LoadBakingResultsFromFile() const;

	std::vector<std::vector<XMFLOAT4>> clusterBakePoints;
	
	std::atomic<int> currentBakeCluster;
	std::atomic<int> probesBaked;

	std::optional<XMFLOAT4> bakePosition;

	Utils::Flags<BakeFlags> bakeFlags;
	
	// Contains data that will be sent to renderer after bake is over
	BakingData transferableData;

	std::atomic<uint32_t> bakeVersion = 0;
}; 