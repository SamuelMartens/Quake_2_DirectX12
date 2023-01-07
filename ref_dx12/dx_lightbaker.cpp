#include "dx_lightbaker.h"

#include <random>
#include <sstream>
#define _USE_MATH_DEFINES

#include "dx_app.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include "Lib/peglib.h"

const std::array<std::string,static_cast<int>(LightBakingMode::Count)> LightBakingMode_Str = 
{
	"AllCluster",
	"CurrentPositionCluster"
};

namespace
{
	inline float CalculateDistanceFalloff(float dist, float dist0, float distMax)
	{
		if (dist >= distMax)
		{
			return 0.0f;
		}

		// Treat dist0 as distance to surface. If we measure light closer than that distance
		// we are basically inside light source, so just return 1.0
		if (dist <= dist0)		
		{
			return 1.0f;
		}

		DX_ASSERT(dist > 0.0f && "Can't have negative distance");
		DX_ASSERT(distMax > 0 && "Max distance must be more than zero");

		// Real-Time Rendering (4th Edition), page 113
		const float windowedFunctionValue = std::powf(std::max(0.0f, 1.0f - std::powf(dist / distMax, 4)), 2);

		// Real-Time Rendering (4th Edition), page 111
		const float distanceFalloff = std::powf(dist0 / dist, 2);

		return windowedFunctionValue * distanceFalloff;
	}

	float GenerateNormalizedUniformDistributioSample()
	{
		static std::mutex mutex;
		std::scoped_lock<std::mutex> lock(mutex);
	
		static std::random_device randomDevice;
		static std::mt19937 randomGenerationEngine(randomDevice());

		std::uniform_real_distribution<> distribution(0.0f, 1.0f);

		return distribution(randomGenerationEngine);
	}

	XMFLOAT4 GenerateUniformSphereSample()
	{
		const float randNum1 = GenerateNormalizedUniformDistributioSample();
		const float randNum2 = GenerateNormalizedUniformDistributioSample();

		XMFLOAT4 sample;

		sample.w = 0.0f;
		sample.z = 1.0f - 2.0f * randNum1;
		sample.y = std::sin(2.0f * M_PI * randNum2) * std::sqrt(1.0f - sample.z * sample.z);
		sample.x = std::cos(2.0f * M_PI * randNum2) * std::sqrt(1.0f - sample.z * sample.z);

		return sample;
	}

	constexpr float GetUniformSphereSamplePDF()
	{
		return 1.0f / (4 * M_PI);
	}

	// Returns vector of sets of area lights. Per each sample:\
	// x, y - 2d value of sample applied to a certain triangle in mesh
	// z - value to figure out which triangle to sample in mesh, based on area of each triangle in mesh
	XMFLOAT4 GenerateAreaLightsSample()
	{
		XMFLOAT4 sample;

		sample.x = GenerateNormalizedUniformDistributioSample();
		sample.y = GenerateNormalizedUniformDistributioSample();
		sample.z = GenerateNormalizedUniformDistributioSample();

		return sample;
	}

	XMFLOAT4 GenerateConcentricDiskSample()
	{
		const float randNum1 = 2.0f * GenerateNormalizedUniformDistributioSample() - 1.0f;
		const float randNum2 = 2.0f * GenerateNormalizedUniformDistributioSample() - 1.0f;

		if (randNum1 == 0 && randNum2 == 0)
		{
			return XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		}
		
		float theta = 0.0f;
		float r = 0.0f;

		if (std::abs(randNum1) > std::abs(randNum2))
		{
			r = randNum1;
			theta = M_PI * randNum2 / (randNum1 * 4.0f);
		}
		else
		{
			r = randNum2;
			theta = M_PI / 2.0f - M_PI * randNum1 / (randNum2 * 4.0f);
		}

		XMFLOAT4 sample;
		sample.x = r * std::cos(theta);
		sample.y = r * std::sin(theta);
		sample.z = 0.0f;
		sample.w = 0.0f;

		return sample;
	}

	XMFLOAT4 GenerateCosineWeightedSample()
	{
		XMFLOAT4 sample = GenerateConcentricDiskSample();
		sample.z = std::sqrt(std::max(0.0f, 1.0f - sample.x * sample.x - sample.y * sample.y));

		return sample;
	}

	float GetCosineWeightedSamplePDF(float cosTheta)
	{
		// cosTheta == n * l
		return cosTheta / M_PI;
	}

	void AddPathSegment(std::vector<PathSegment>& segments, 
		const XMFLOAT4& v0, 
		const XMFLOAT4& v1,
		int bounce,
		const XMFLOAT4& radiance)
	{
		PathSegment segment;
		segment.v0 = v0;
		segment.v1 = v1;
		segment.bounce = bounce;
		segment.radiance = radiance;

		segments.push_back(segment);
	}

	void InitLightBakingResultParser(peg::parser& parser)
	{
		// Load grammar
		const std::string grammar = Utils::ReadFile(Utils::GenAbsolutePathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_LIGHT_BAKING_RESULT_FILENAME));

		parser.set_logger([](size_t line, size_t col, const std::string& msg)
		{
			Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

			DX_ASSERT(false && "Light baking result parsing error");
		});

		const bool loadGrammarResult = parser.load_grammar(grammar.c_str());
		DX_ASSERT(loadGrammarResult && "Can't load Light Baking grammar");

		parser["LightBakingData"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::LightBakingContext& parseCtx = *std::any_cast<Parsing::LightBakingContext*>(ctx);
			BakingData& bakingRes = parseCtx.bakingResult;

			bakingRes.bakingMode = std::any_cast<LightBakingMode>(sv[0]);
			bakingRes.probes = std::any_cast<std::vector<DiffuseProbe>>(sv[2]);
		};

		// --- Baking Mode
		parser["BakingModeSection"] = [](const peg::SemanticValues& sv) 
		{
			return LightBaker::StrToBakingMode(std::any_cast<std::string>(sv[0]));
		};

		// --- Cluster Data
		parser["BakingCluster"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::LightBakingContext& parseCtx = *std::any_cast<Parsing::LightBakingContext*>(ctx);
			BakingData& bakingRes = parseCtx.bakingResult;

			bakingRes.bakingCluster = std::any_cast<int>(sv[0]);
		};

		parser["ClusterFirstProbeIndices"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::LightBakingContext& parseCtx = *std::any_cast<Parsing::LightBakingContext*>(ctx);
			BakingData& bakingRes = parseCtx.bakingResult;

			const int sizesCount = std::any_cast<int>(sv[0]);
			
			DX_ASSERT(sv.size() - 1 == sizesCount && "Invalid sizes token number");

			std::vector<int> clusterFirstProbeIndices;
			clusterFirstProbeIndices.reserve(sizesCount);

			// i is init to 1, because sv[0] is sizes count
			for (int i = 1; i < sv.size(); ++i)
			{
				clusterFirstProbeIndices.push_back(std::any_cast<int>(sv[i]));
			}

			DX_ASSERT(clusterFirstProbeIndices.size() == sizesCount);

			bakingRes.clusterFirstProbeIndices = std::move(clusterFirstProbeIndices);
		};

		parser["ClusterGridSizes"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::LightBakingContext& parseCtx = *std::any_cast<Parsing::LightBakingContext*>(ctx);
			BakingData& bakingRes = parseCtx.bakingResult;

			const int sizesCount = std::any_cast<int>(sv[0]);

			DX_ASSERT(sv.size() - 1 == sizesCount && "Invalid sizes token number");

			std::vector<XMINT3> clusterGridSizes;
			clusterGridSizes.reserve(sizesCount);

			// i is init to 1, because sv[0] is sizes count
			for (int i = 1; i < sv.size(); ++i)
			{
				clusterGridSizes.push_back(std::any_cast<XMINT3>(sv[i]));
			}

			DX_ASSERT(clusterGridSizes.size() == sizesCount);

			bakingRes.clusterProbeGridSizes = std::move(clusterGridSizes);
		};

		// --- Probe Data
		parser["ProbeSection"] = [](const peg::SemanticValues& sv) 
		{
			const int probesCount = std::any_cast<int>(sv[0]);

			DX_ASSERT(sv.size() - 1 == probesCount && "Probe count doesn't match amount of probes");

			std::vector<DiffuseProbe> probes;
			probes.reserve(probesCount);

			// i is init to 1, because sv[0] is probe count
			for (int i = 1; i < sv.size(); ++i)
			{
				auto [probeIndex, probe] = std::any_cast<std::tuple<int, DiffuseProbe>>(sv[i]);

				DX_ASSERT(probeIndex == probes.size() && "Invalid probe Index");

				probes.push_back(std::move(probe));
			}

			DX_ASSERT(probesCount == probes.size());

			return probes;
		};

		parser["Probe"] = [](const peg::SemanticValues& sv)
		{
			const int probeIndex = std::any_cast<int>(sv[0]);

			DiffuseProbe probe;

			DX_ASSERT(probe.radianceSh.size() == sv.size() - 1 && "Invalid number of coefficients for probe data");

			// i is init to 1, because sv[0] is probe index
			for (int i = 1; i < sv.size(); ++i)
			{
				probe.radianceSh[i - 1] = std::any_cast<XMFLOAT4>(sv[i]);
			}

			return std::make_tuple(probeIndex, probe);
		};

		// --- Types
		parser["Float3"] = [](const peg::SemanticValues& sv)
		{
			return XMFLOAT4
			(
				std::any_cast<float>(sv[0]),
				std::any_cast<float>(sv[1]),
				std::any_cast<float>(sv[2]),
				0.0f
			);
		};

		parser["Float"] = [](const peg::SemanticValues& sv)
		{
			return stof(sv.token_to_string());
		};

		parser["Int3"] = [](const peg::SemanticValues& sv)
		{
			return XMINT3
			(
				std::any_cast<int>(sv[0]),
				std::any_cast<int>(sv[1]),
				std::any_cast<int>(sv[2])
			);
		};

		parser["Int"] = [](const peg::SemanticValues& sv)
		{
			return stoi(sv.token_to_string());
		};

		parser["Word"] = [](const peg::SemanticValues& sv)
		{
			return sv.token_to_string();
		};
	}
}

std::string LightBaker::BakingModeToStr(LightBakingMode mode)
{
	DX_ASSERT(static_cast<int>(mode) < LightBakingMode_Str.size() && "Invalid Light Baking mode");

	return LightBakingMode_Str[static_cast<int>(mode)];
}

LightBakingMode LightBaker::StrToBakingMode(const std::string& str)
{
	int bakingMode = Const::INVALID_INDEX;

	for (int i = 0; i < LightBakingMode_Str.size(); ++i)
	{
		if (str == LightBakingMode_Str[i])
		{
			bakingMode = i;
			break;
		}
	}

	DX_ASSERT(bakingMode != Const::INVALID_INDEX && "Can't convert string to baking mode");
	DX_ASSERT(bakingMode < static_cast<int> (LightBakingMode::Count) && "Invalid light baking mode");

	return static_cast<LightBakingMode>(bakingMode);
}

void LightBaker::Init()
{
	SetBakeFlag(BakeFlags::SamplePointLights, true);
	SetBakeFlag(BakeFlags::SampleAreaLights, true);

	SetBakingMode(LightBakingMode::CurrentPositionCluster);
}

void LightBaker::PreBake()
{
	ASSERT_MAIN_THREAD;

	DX_ASSERT(transferableData.clusterFirstProbeIndices.empty() == true && "Cluster probe data should be empty before bake");
	DX_ASSERT(transferableData.clusterProbeGridSizes.empty() == true && "Cluster probe grid data should be empty before bake");
	DX_ASSERT(clusterBakePoints.empty() == true && "Cluster bake points should be empty before bake");
	DX_ASSERT(probesBaked == 0 && "Amount of baked probes was not reset");
	DX_ASSERT(transferableData.probes.empty() == true && "Probes were baked, but not consumed");
	DX_ASSERT(transferableData.bakingMode.has_value() == true && "Baking mode is not set");
	DX_ASSERT(transferableData.bakingMode != LightBakingMode::AllClusters || bakeFlags[BakeFlags::SaveRayPath] == false &&
	"Can't save ray path if baking for all clusters");

	currentBakeCluster = 0;
	clusterBakePoints = GenerateClustersBakePoints();
	transferableData.clusterFirstProbeIndices.resize(clusterBakePoints.size());
	transferableData.clusterProbeGridSizes.resize(clusterBakePoints.size());

	int totalProbes = 0;

	for (int i = 0; i < clusterBakePoints.size(); ++i)
	{
		// Only fill data for non empty clusters
		if (clusterBakePoints[i].empty() == false)
		{
			// Fill up first probe data
			transferableData.clusterFirstProbeIndices[i] = totalProbes;
			totalProbes += clusterBakePoints[i].size();

			// Fill up cluster grid data
			transferableData.clusterProbeGridSizes[i] = GenerateClusterGridSize(i);
		}
	}

	transferableData.probes.resize(totalProbes);
}

void LightBaker::PostBake()
{
	ASSERT_MAIN_THREAD;
	
	DX_ASSERT(transferableData.probes.empty() == false && "Baking is finished, but no probes were generated");

	BakingData bakingResult = TransferBakingResult();

	if (GetBakeFlag(BakeFlags::SaveToFileAfterBake) == true)
	{
		SaveBakingResultsToFile(bakingResult);
	}

	Renderer::Inst().ConsumeDiffuseIndirectLightingBakingResult(std::move(bakingResult));

	probesBaked = 0;
	clusterBakePoints.clear();

	SetBakeFlag(BakeFlags::SaveToFileAfterBake, false);
}

std::vector<std::vector<XMFLOAT4>> LightBaker::GenerateClustersBakePoints()
{
	std::vector<std::vector<XMFLOAT4>> bakePoints;

	DX_ASSERT(transferableData.bakingMode.has_value() == true && "Baking mode is not set");

	switch (*transferableData.bakingMode)
	{
	case LightBakingMode::AllClusters:
	{
		transferableData.bakingCluster = std::nullopt;

		std::set<int> clustersSet = Renderer::Inst().GetBSPTree().GetClustersSet();

		if (clustersSet.empty() == true)
		{
			return {};
		}

		// Set is sorted. Here I am taking the last element which is MAX element,
		// and use it as a new size for bake point array
		bakePoints.resize(*clustersSet.rbegin() + 1);

		for (const int cluster : clustersSet)
		{
			bakePoints[cluster] = GenerateClusterBakePoints(cluster);
		}

		return bakePoints;
	}
		break;
	case LightBakingMode::CurrentPositionCluster:
	{
		DX_ASSERT(bakePosition.has_value() == true && "Bake position is not set");
		const BSPNode& cameraNode = Renderer::Inst().GetBSPTree().GetNodeWithPoint(*bakePosition);

		bakePosition.reset();

		DX_ASSERT(cameraNode.cluster != Const::INVALID_INDEX && "Camera node invalid index");

		transferableData.bakingCluster = cameraNode.cluster;

		bakePoints.resize(cameraNode.cluster + 1);
		bakePoints[cameraNode.cluster] = GenerateClusterBakePoints(cameraNode.cluster);
	}
		break;
	default:
		DX_ASSERT(false && "Invalid generation mode");
		break;
	}

	return bakePoints;
}

std::vector<XMFLOAT4> LightBaker::GenerateClusterBakePoints(int clusterIndex) const
{
	Utils::AABB clusterAABB = Renderer::Inst().GetBSPTree().GetClusterAABB(clusterIndex);
	
	constexpr XMFLOAT4 epsilonVec = XMFLOAT4(
		Settings::PATH_TRACING_EPSILON,
		Settings::PATH_TRACING_EPSILON,
		Settings::PATH_TRACING_EPSILON, 0.0f);

	XMVECTOR sseEpsilonVec = XMLoadFloat4(&epsilonVec);

	// Because of floating point math errors sometimes bake points would be slightly behind
	// actual meshes, so reduce AABB we use to generate bake points a little bit
	XMStoreFloat4(&clusterAABB.minVert, 
		XMLoadFloat4(&clusterAABB.minVert) + sseEpsilonVec);

	XMStoreFloat4(&clusterAABB.maxVert,
		XMLoadFloat4(&clusterAABB.maxVert) - sseEpsilonVec);

	const XMINT3 clusterGridSize = GenerateClusterGridSize(clusterIndex);

	std::vector<XMFLOAT4> bakePoints;
	bakePoints.reserve(clusterGridSize.x * clusterGridSize.y * clusterGridSize.z);

	for (int xIteration = 0; xIteration < clusterGridSize.x; ++xIteration)
	{
		for (int yIteration = 0; yIteration < clusterGridSize.y; ++yIteration)
		{
			for (int zIteration = 0; zIteration < clusterGridSize.z; ++zIteration)
			{
				bakePoints.push_back({
					std::min(clusterAABB.minVert.x + Settings::CLUSTER_PROBE_GRID_INTERVAL * xIteration, clusterAABB.maxVert.x),
					std::min(clusterAABB.minVert.y + Settings::CLUSTER_PROBE_GRID_INTERVAL * yIteration, clusterAABB.maxVert.y),
					std::min(clusterAABB.minVert.z + Settings::CLUSTER_PROBE_GRID_INTERVAL * zIteration, clusterAABB.maxVert.z),
					1.0f
					});
			}
		}
	}

	return bakePoints;
}

XMINT3 LightBaker::GenerateClusterGridSize(int clusterIndex) const
{
	Utils::AABB clusterAABB = Renderer::Inst().GetBSPTree().GetClusterAABB(clusterIndex);

	constexpr XMFLOAT4 epsilonVec = XMFLOAT4(
		Settings::PATH_TRACING_EPSILON,
		Settings::PATH_TRACING_EPSILON,
		Settings::PATH_TRACING_EPSILON, 0.0f);

	XMVECTOR sseEpsilonVec = XMLoadFloat4(&epsilonVec);

	// Because of floating point math errors sometimes bake points would be slightly behind
	// actual meshes, so reduce AABB we use to generate bake points a little bir
	XMStoreFloat4(&clusterAABB.minVert,
		XMLoadFloat4(&clusterAABB.minVert) + sseEpsilonVec);

	XMStoreFloat4(&clusterAABB.maxVert,
		XMLoadFloat4(&clusterAABB.maxVert) - sseEpsilonVec);


	// Amount of bake points along X axis
	const int xAxisNum = std::ceil((clusterAABB.maxVert.x - clusterAABB.minVert.x) / Settings::CLUSTER_PROBE_GRID_INTERVAL) + 1;
	// Amount of bake points along Y axis
	const int yAxisNum = std::ceil((clusterAABB.maxVert.y - clusterAABB.minVert.y) / Settings::CLUSTER_PROBE_GRID_INTERVAL) + 1;
	// Amount of bake points along X axis
	const int zAxisNum = std::ceil((clusterAABB.maxVert.z - clusterAABB.minVert.z) / Settings::CLUSTER_PROBE_GRID_INTERVAL) + 1;

	return { xAxisNum, yAxisNum, zAxisNum };
}

void LightBaker::BakeJob()
{
	while (true)
	{
		// Get next set of clusters to work on
		const int currentCluster = currentBakeCluster.fetch_add(1);

		if (currentCluster >= clusterBakePoints.size())
		{
			break;
		}

		const std::vector<XMFLOAT4>& bakePoints = clusterBakePoints[currentCluster];
		const int clusterProbeStartIndex = transferableData.clusterFirstProbeIndices[currentCluster];

		DX_ASSERT(clusterProbeStartIndex != Const::INVALID_INDEX && "Invalid cluster probe start index");

		for (int bakePointIndex = 0; bakePointIndex < bakePoints.size(); ++bakePointIndex)
		{
			const XMFLOAT4& bakePoint = bakePoints[bakePointIndex];
			
			DiffuseProbe& probe = transferableData.probes[clusterProbeStartIndex + bakePointIndex];

			SphericalHarmonic9_t<XMFLOAT4>& totalShProjection = probe.radianceSh;
			ZeroMemory(totalShProjection.data(), sizeof(XMFLOAT4) * totalShProjection.size());	
			
			if (bakeFlags[BakeFlags::SaveRayPath] == true)
			{
				probe.pathTracingSegments = std::vector<PathSegment>();
			}

			if (bakeFlags[BakeFlags::SaveLightSampling] == true)
			{
				probe.lightSamples = std::vector<PathLightSampleInfo_t>();
			}

			for (int i = 0; i < Settings::PROBE_SAMPLES_NUM; ++i)
			{
				XMFLOAT4 direction = { 0.0f, 0.0f, 0.0f, 1.0f };
				// Result of one sample
				const ProbePathTraceResult sampleRes = PathTraceFromProbe(bakePoint, direction);

				if (bakeFlags[BakeFlags::SaveRayPath] == true)
				{
					DX_ASSERT(sampleRes.pathSegments.has_value() == true && 
						"If SaveRayPath flag is on there should be segments");

					probe.pathTracingSegments->insert(probe.pathTracingSegments->end(),
						sampleRes.pathSegments->cbegin(), sampleRes.pathSegments->cend());
				}

				if (bakeFlags[BakeFlags::SaveLightSampling] == true)
				{
					probe.lightSamples->push_back(*sampleRes.lightSamples);
				}

				// Project single sample on SH
				SphericalHarmonic9_t<XMFLOAT4> sampleShProjection = ProjectOntoSphericalHarmonic(direction, sampleRes.radiance);

				for (int coeffIndex = 0; coeffIndex < totalShProjection.size(); ++coeffIndex)
				{
					// Accumulate from that value
					XMStoreFloat4(&totalShProjection[coeffIndex], 
						XMLoadFloat4(&totalShProjection[coeffIndex]) + XMLoadFloat4(&sampleShProjection[coeffIndex]));
				}
			}

			constexpr float monteCarloFactor = (1.0f / GetUniformSphereSamplePDF()) / Settings::PROBE_SAMPLES_NUM;

			for (XMFLOAT4& coeff : totalShProjection)
			{
				XMStoreFloat4(&coeff, XMLoadFloat4(&coeff) * monteCarloFactor);
			}

			probesBaked.fetch_add(1);
		}
	}

	if (GetTotalProbesNum() == GetBakedProbesNum())
	{
		bakeVersion += 1;
	}
}

void LightBaker::LoadBakingResultsFromFileJob()
{
	transferableData = LoadBakingResultsFromFile();
	
	bakeVersion += 1;
}

int LightBaker::GetTotalProbesNum() const
{
	return transferableData.probes.size();
}

int LightBaker::GetBakedProbesNum() const
{
	DX_ASSERT(probesBaked <= GetTotalProbesNum() && "Baked probes exceeded total probes");
	return probesBaked;
}

LightBakingMode LightBaker::GetBakingMode() const
{
	DX_ASSERT(transferableData.bakingMode.has_value() == true && "Baking mode is not set");

	return *transferableData.bakingMode;
}

bool LightBaker::GetBakeFlag(BakeFlags flag) const
{
	return bakeFlags[flag];
}

BakingData LightBaker::TransferBakingResult()
{
	// I want to be able to reset some data inside transferData after transfer
	// but if just return it, I can't do it. So that's why this local variable 
	// is introduced.
	BakingData resultToTransfer = std::move(transferableData);

	transferableData.bakingCluster.reset();

	return resultToTransfer;
}

void LightBaker::SetBakingMode(LightBakingMode genMode)
{
	transferableData.bakingMode = genMode;
}

void LightBaker::SetBakePosition(const XMFLOAT4& position)
{
	DX_ASSERT(bakePosition.has_value() == false && "Bake position is not cleared");
	bakePosition = position;
}

void LightBaker::SetBakeFlag(BakeFlags flag, bool value)
{
	bakeFlags.set(flag, value);
}

uint32_t LightBaker::GetBakeVersion() const
{
	return bakeVersion;
}

SphericalHarmonic9_t<float> LightBaker::GetSphericalHarmonic9Basis(const XMFLOAT4& direction) const
{
	// Source https://github.com/TheRealMJP/BakingLab.git
	SphericalHarmonic9_t<float> sphericalHarmonic;

	// Band 0
	sphericalHarmonic[0] = 0.282095f;

	// Band 1
	sphericalHarmonic[1] = -0.488603f * direction.y;
	sphericalHarmonic[2] = 0.488603f * direction.z;
	sphericalHarmonic[3] = -0.488603f * direction.x;

	// Band 2
	sphericalHarmonic[4] = 1.092548f * direction.x * direction.y;
	sphericalHarmonic[5] = -1.092548f * direction.y * direction.z;
	sphericalHarmonic[6] = 0.315392f * (3.0f * direction.z * direction.z - 1.0f);
	sphericalHarmonic[7] = -1.092548f * direction.x * direction.z;
	sphericalHarmonic[8] = 0.546274f * (direction.x * direction.x - direction.y * direction.y);

	return sphericalHarmonic;
}

SphericalHarmonic9_t<XMFLOAT4> LightBaker::ProjectOntoSphericalHarmonic(const XMFLOAT4& direction, const XMFLOAT4& color) const
{
	SphericalHarmonic9_t<XMFLOAT4> sphericalHarmonic;
	SphericalHarmonic9_t<float> basis = GetSphericalHarmonic9Basis(direction);

	XMVECTOR sseColor = XMLoadFloat4(&color);

	for (int i = 0; i < sphericalHarmonic.size(); ++i)
	{
		XMStoreFloat4(&sphericalHarmonic[i], sseColor * basis[i]);
	}

	return sphericalHarmonic;
}

XMFLOAT4 LightBaker::GatherDirectIradianceAtInersectionPoint(const Utils::Ray& ray, const Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult, LightSamplePoint* lightSampleDebugInfo) const
{
	const Renderer& renderer = Renderer::Inst();

	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.origin) + XMLoadFloat4(&ray.direction) * 
		(nodeIntersectionResult.rayTriangleIntersection.t - Settings::PATH_TRACING_EPSILON);

	XMFLOAT4 intersectionPoint;
	XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

	if (lightSampleDebugInfo != nullptr)
	{
		lightSampleDebugInfo->position = intersectionPoint;
	}

	const XMFLOAT4 intersectionNormal = Utils::BSPNodeRayIntersectionResult::GetNormal(nodeIntersectionResult);

	XMFLOAT4 pointLightsIradiance = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

	if (GetBakeFlag(BakeFlags::SamplePointLights) == true)
	{
		pointLightsIradiance = GatherDirectIrradianceFromPointLights(intersectionPoint, intersectionNormal, lightSampleDebugInfo);
	}

	XMFLOAT4 areaLightIrradiance = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

	if (GetBakeFlag(BakeFlags::SampleAreaLights) == true)
	{
		areaLightIrradiance = GatherDirectIradianceFromAreaLights(intersectionPoint, intersectionNormal, lightSampleDebugInfo);
	}

	XMFLOAT4 resultIradiance;
	XMStoreFloat4(&resultIradiance, XMLoadFloat4(&pointLightsIradiance)  + XMLoadFloat4(&areaLightIrradiance));

	return resultIradiance;
}

XMFLOAT4 LightBaker::GatherDirectIrradianceFromPointLights(const XMFLOAT4& intersectionPoint, const XMFLOAT4& intersectionSurfaceNormal, LightSamplePoint* lightSampleDebugInfo) const
{
	const std::vector<PointLight>& pointLights = Renderer::Inst().GetStaticPointLights();
	const BSPTree& bsp = Renderer::Inst().GetBSPTree();

	const XMVECTOR sseIntersectionPoint = XMLoadFloat4(&intersectionPoint);
	const XMVECTOR sseNormal = XMLoadFloat4(&intersectionSurfaceNormal);

	XMVECTOR sseResultIradiance = XMVectorZero();

	for (const PointLight& light : pointLights)
	{
		if (light.intensity == 0.0f)
		{
			continue;
		}

		XMVECTOR sseIntersectionPointToLight = XMLoadFloat4(&light.origin) - sseIntersectionPoint;

		const float distanceToLight = XMVectorGetX(XMVector3Length(sseIntersectionPointToLight));

		if (distanceToLight > Settings::POINT_LIGHTS_MAX_DISTANCE)
		{
			continue;
		}

		const float normalAndIntersectionDotProduct =
			XMVectorGetX(XMVector3Dot(XMVector3Normalize(sseIntersectionPointToLight), sseNormal));

		if (normalAndIntersectionDotProduct <= 0.0f)
		{
			continue;
		}

		// This is expensive check, as so it should be delayed as much as possible
		if (bsp.IsPointVisibleFromOtherPoint(intersectionPoint, light.origin) == false)
		{
			continue;
		}

		const float distanceFalloff = CalculateDistanceFalloff(distanceToLight, light.radius, Settings::POINT_LIGHTS_MAX_DISTANCE);

		if (distanceFalloff == 0.0f)
		{
			continue;
		}

		const XMVECTOR sseLightBaseRadiance = (XMLoadFloat4(&light.color) / M_PI) *
			light.intensity;

		XMVECTOR sseLightRadiance =
			distanceFalloff *
			sseLightBaseRadiance *
			normalAndIntersectionDotProduct;

#if (ENABLE_VALIDATION)
		{
			XMFLOAT4 baseLightRadiance;
			XMStoreFloat4(&baseLightRadiance, sseLightBaseRadiance);

			XMFLOAT4 sampledLightRadiance;
			XMStoreFloat4(&sampledLightRadiance, sseLightRadiance);

			// We can't receive more energy than light produces.
			DX_ASSERT(sampledLightRadiance.x >= 0.0f && sampledLightRadiance.x <= baseLightRadiance.x);
			DX_ASSERT(sampledLightRadiance.y >= 0.0f && sampledLightRadiance.y <= baseLightRadiance.y);
			DX_ASSERT(sampledLightRadiance.z >= 0.0f && sampledLightRadiance.z <= baseLightRadiance.z);
		}
#endif

		sseResultIradiance = sseResultIradiance + sseLightRadiance;

		if (lightSampleDebugInfo != nullptr)
		{
			LightSamplePoint::Sample sample;
			sample.lightType = DebugObject_LightSource::Type::Point;
			sample.position = light.origin;
			XMStoreFloat4(&sample.radiance, sseLightRadiance);

			lightSampleDebugInfo->samples.push_back(sample);
		}
	}

	XMFLOAT4 resultIrradiance;
	XMStoreFloat4(&resultIrradiance, sseResultIradiance);

	return resultIrradiance;
}

XMFLOAT4 LightBaker::GatherDirectIradianceFromAreaLights(const XMFLOAT4& intersectionPoint, const XMFLOAT4& intersectionSurfaceNormal, LightSamplePoint* lightSampleDebugInfo) const
{
	const Renderer& renderer = Renderer::Inst();

	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&intersectionPoint);

	const BSPTree& bsp = renderer.GetBSPTree();
	
	const BSPNode& intersectionNode = bsp.GetNodeWithPoint(intersectionPoint);

	if (intersectionNode.cluster == Const::INVALID_INDEX)
	{
		return XMFLOAT4{0.0f, 0.0f, 0.0f, 0.0f};
	}

	// Get all potentially visible objects
	std::vector<int> potentiallyVisibleObjects = bsp.GetPotentiallyVisibleObjects(intersectionPoint);
	
	const std::vector<AreaLight>& staticAreaLights = renderer.GetStaticAreaLights();

	// Get potentially visible light objects from all potentially visible objects
	std::vector<int> potentiallyVisibleLightsIndices;
	for (int i = 0; i < staticAreaLights.size(); ++i)
	{
		const AreaLight& areaLight = staticAreaLights[i];

		auto lightObjectIt = std::find(potentiallyVisibleObjects.cbegin(), 
			potentiallyVisibleObjects.cend(), areaLight.staticObjectIndex);

		if (lightObjectIt != potentiallyVisibleObjects.cend())
		{
			potentiallyVisibleLightsIndices.push_back(i);
		}
	}

	XMVECTOR sseResultIradiance = XMVectorZero();

	for (const int lightIndex : potentiallyVisibleLightsIndices)
	{
		XMFLOAT4 lightIrradiance = GatherDirectIradianceFromAreaLight(
			intersectionPoint,
			intersectionSurfaceNormal,
			staticAreaLights[lightIndex],
			lightSampleDebugInfo);

		sseResultIradiance = sseResultIradiance + XMLoadFloat4(&lightIrradiance);
	}

	XMFLOAT4 resultIradiance;
	XMStoreFloat4(&resultIradiance, sseResultIradiance);

	return resultIradiance;
}

XMFLOAT4 LightBaker::GatherDirectIradianceFromAreaLight(const XMFLOAT4& intersectionPoint, const XMFLOAT4& intersectionSurfaceNormal, const AreaLight& light, LightSamplePoint* lightSampleDebugInfo) const
{
	const SourceStaticObject& lightMesh = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];

	// Find light source texture
	const Resource* objTexture = ResourceManager::Inst().FindResource(lightMesh.textureKey);

	DX_ASSERT(objTexture != nullptr && "Can't find obj texture");
	DX_ASSERT(objTexture->cpuBuffer.has_value() == true && "No CPU buffer");

	const std::vector<float>& lightTrianglesPDF = light.trianglesPDF;

	const BSPTree& bsp = Renderer::Inst().GetBSPTree();

	const float lightRadiance = light.radiance;
	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&intersectionPoint);

	XMVECTOR sseRadianceSum = XMVectorZero();

	for (int i = 0; i < Settings::AREA_LIGHTS_SAMPLES_NUM; ++i)
	{
		const XMFLOAT4 sample = GenerateAreaLightsSample();

		const auto trinangleIndexIt = std::find_if(lightTrianglesPDF.cbegin(), lightTrianglesPDF.cend(), 
			[&sample](const float& trianglePDF)
		{
			return trianglePDF >= sample.z;
		});

		DX_ASSERT(trinangleIndexIt != lightTrianglesPDF.cend() && "Triangle sample not found");

		const int triangleIndex = std::distance(lightTrianglesPDF.cbegin(), trinangleIndexIt);

		// Convert random samples into barycentric coordinates of triangle
		const float u = 1.0f - std::sqrt(sample.x);
		const float v = sample.y * std::sqrt(sample.x);
		const float w = 1.0f - u - v;

		//This is just for my sanity. I will delete it later
		DX_ASSERT(u + v <= 1.0f && "Something funky with barycentric coordinates");

		const int V0Ind = lightMesh.indices[triangleIndex * 3 + 0];
		const int V1Ind = lightMesh.indices[triangleIndex * 3 + 1];
		const int V2Ind = lightMesh.indices[triangleIndex * 3 + 2];

		const XMVECTOR sseV0 = XMLoadFloat4(&lightMesh.verticesPos[V0Ind]);
		const XMVECTOR sseV1 = XMLoadFloat4(&lightMesh.verticesPos[V1Ind]);
		const XMVECTOR sseV2 = XMLoadFloat4(&lightMesh.verticesPos[V2Ind]);

		XMVECTOR sseLightSamplePoint = sseV0 * u + sseV1 * v + sseV2 * w;
		
		// Patch up light sample point, otherwise it might be a bit behind actual mesh
		const XMVECTOR sseIntersectionToLightDir = XMVector3Normalize(sseLightSamplePoint - sseIntersectionPoint);
		sseLightSamplePoint = sseIntersectionPoint + sseIntersectionToLightDir * 
			(XMVectorGetX(XMVector3Length(sseLightSamplePoint - sseIntersectionPoint)) - Settings::PATH_TRACING_EPSILON);


		const XMVECTOR sseIntersectionToSample = sseLightSamplePoint - sseIntersectionPoint;

		const float lightToRayAndLightNormalDot = XMVectorGetX(XMVector3Dot(
		 	sseIntersectionToSample, 
			XMLoadFloat4(&lightMesh.normals[V0Ind])));

		if (lightToRayAndLightNormalDot >= 0.0f)
		{
			// This point is behind chosen light. It will not contribute,
			// moving to the next sample
			continue;
		}

		const float distanceToSample = XMVectorGetX(XMVector3Length(sseIntersectionToSample));
		
		if (distanceToSample > Settings::AREA_LIGHTS_MAX_DISTANCE)
		{
			continue;
		}

		const float intersectionToSampleAndNormalDot = 
			XMVectorGetX(XMVector3Dot(XMVector3Normalize(sseIntersectionToSample),
			XMLoadFloat4(&intersectionSurfaceNormal)));

		if (intersectionToSampleAndNormalDot <= 0.0f)
		{
			// Light is behind intersection surface
			continue;
		}

		XMFLOAT4 lightSamplePoint;
		XMStoreFloat4(&lightSamplePoint, sseLightSamplePoint);

		if (bsp.IsPointVisibleFromOtherPoint(intersectionPoint, lightSamplePoint) == false)
		{
			continue;
		}
		
		const float distanceFalloff = CalculateDistanceFalloff(distanceToSample, 
			Settings::AREA_LIGHTS_MIN_DISTANCE,
			Settings::AREA_LIGHTS_MAX_DISTANCE);

		if (distanceFalloff == 0.0f)
		{
			continue;
		}

		// Find texcoord of sample point
		const XMVECTOR sseV0TexCoord = XMLoadFloat2(&lightMesh.textureCoords[V0Ind]);
		const XMVECTOR sseV1TexCoord = XMLoadFloat2(&lightMesh.textureCoords[V1Ind]);
		const XMVECTOR sseV2TexCoord = XMLoadFloat2(&lightMesh.textureCoords[V2Ind]);

		XMVECTOR sseInterpolatedTexCoords = sseV0TexCoord * u + sseV1TexCoord * v + sseV2TexCoord * w;
		XMFLOAT2 interpolatedTexCoords;
		XMStoreFloat2(&interpolatedTexCoords, sseInterpolatedTexCoords);
		interpolatedTexCoords = Utils::NomralizeWrapAroundTextrueCoordinates(interpolatedTexCoords);

		const XMFLOAT4 albedo = Utils::TextureBilinearSample(*objTexture->cpuBuffer, objTexture->desc.format, objTexture->desc.width, objTexture->desc.height, interpolatedTexCoords);
		
		const XMVECTOR sseSampleRadiance = (XMLoadFloat4(&albedo) / M_PI) *
			lightRadiance *
			distanceFalloff * intersectionToSampleAndNormalDot;

		sseRadianceSum = sseRadianceSum + sseSampleRadiance;

#if (ENABLE_VALIDATION)
		{
			XMFLOAT4 sampleRadiance;
			XMStoreFloat4(&sampleRadiance, sseSampleRadiance);

			// According to energy conservation law
			DX_ASSERT(sampleRadiance.x >= 0.0f && sampleRadiance.x <= light.radiance);
			DX_ASSERT(sampleRadiance.y >= 0.0f && sampleRadiance.y <= light.radiance);
			DX_ASSERT(sampleRadiance.z >= 0.0f && sampleRadiance.z <= light.radiance);
		}
#endif

		if (lightSampleDebugInfo != nullptr)
		{
			LightSamplePoint::Sample sample;
			sample.lightType = DebugObject_LightSource::Type::Area;
			sample.position = lightSamplePoint;
			XMStoreFloat4(&sample.radiance, sseSampleRadiance);

			lightSampleDebugInfo->samples.push_back(sample);
		}
	}

	// Now do Monte Carlo integration
	// Actual probability is p of each triangle multiplied by uniform PDF of each light area triangle sample,
	// but after few manipulation the result is exactly that
	const XMVECTOR sseIradiance = sseRadianceSum * light.area / Settings::AREA_LIGHTS_SAMPLES_NUM;

	XMFLOAT4 iradiance;
	XMStoreFloat4(&iradiance, sseIradiance);
	
	return iradiance;
}

// Will return indirect light that comes to probe via one sample
ProbePathTraceResult LightBaker::PathTraceFromProbe(const XMFLOAT4& probeCoord, XMFLOAT4& direction)
{
	ProbePathTraceResult result;

	if (bakeFlags[BakeFlags::SaveRayPath] == true)
	{
		result.pathSegments = std::vector<PathSegment>();
	}

	if (bakeFlags[BakeFlags::SaveLightSampling] == true)
	{
		result.lightSamples = PathLightSampleInfo_t();
	}

	const BSPTree& bspTree = Renderer::Inst().GetBSPTree();

	XMVECTOR sseRadiance = XMVectorZero();

	XMFLOAT4 intersectionPoint = probeCoord;
	XMFLOAT4 rayDir = GenerateUniformSphereSample();

	direction = rayDir;

	float samplesPDF = 1.0f;

	float nDotL = 1.0f;

	XMVECTOR sseThoroughput = { 1.0f, 1.0f, 1.0f, 0.0f };

	const XMVECTOR sseZ_AXIS = XMLoadFloat4(&Utils::AXIS_Z);

	int rayBounce = 0;

	while(true)
	{
		const bool isGuaranteedBounce = rayBounce < Settings::GUARANTEED_BOUNCES_NUM;

		if (isGuaranteedBounce == false)
		{
			break;

			// Apply Russian Roulette to terminate.
			const float sample = GenerateNormalizedUniformDistributioSample();
			
			if (sample < Settings::RUSSIAN_ROULETTE_ABSORBTION_PROBABILITY)
			{
				break;
			}

			sseThoroughput = sseThoroughput / (1.0f - Settings::RUSSIAN_ROULETTE_ABSORBTION_PROBABILITY);
		}

		// Find intersection
		Utils::Ray ray = { intersectionPoint, rayDir };
		auto [isIntersected, intersectionResult] = bspTree.FindClosestRayIntersection(ray);

		if (isIntersected == false)
		{
			if (bakeFlags[BakeFlags::SaveRayPath] == true)
			{
				constexpr float MISS_RAY_LEN = 25.0f;

				XMVECTOR sseSecondRayPoint = XMLoadFloat4(&ray.direction) * MISS_RAY_LEN +
					XMLoadFloat4(&ray.origin);

				XMFLOAT4 secondRayPoint;
				XMStoreFloat4(&secondRayPoint, sseSecondRayPoint);

				
				XMFLOAT4 radiance;
				XMStoreFloat4(&radiance, sseRadiance);

				AddPathSegment(*result.pathSegments,
					ray.origin,
					secondRayPoint,
					rayBounce,
					radiance);
			}

			break;
		}
		// Subtract epsilon, because floating point math error causes reconstructed intersection point to be slightly behind actual mesh
		XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.direction) * (intersectionResult.rayTriangleIntersection.t - Settings::PATH_TRACING_EPSILON) +
			XMLoadFloat4(&ray.origin);

		// Update intersection point
		XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

		if (bakeFlags[BakeFlags::SaveRayPath] == true)
		{
			XMFLOAT4 radiance;
			XMStoreFloat4(&radiance, sseRadiance);
			
			AddPathSegment(*result.pathSegments,
				ray.origin,
				intersectionPoint,
				rayBounce,
				radiance);
		}

		LightSamplePoint* lightGatherInfo = nullptr;

		if (bakeFlags[BakeFlags::SaveLightSampling] == true)
		{
			lightGatherInfo = &result.lightSamples->emplace_back(LightSamplePoint{});
		}

		XMFLOAT4 directIrradiance = GatherDirectIradianceAtInersectionPoint(ray, intersectionResult, lightGatherInfo);

		sseRadiance = sseRadiance + XMLoadFloat4(&directIrradiance) * sseThoroughput;

		// Generate new ray dir
		const XMFLOAT4 normal = Utils::BSPNodeRayIntersectionResult::GetNormal(intersectionResult);
		XMVECTOR sseNormal = XMLoadFloat4(&normal);

		const XMFLOAT4X4 rotationMat = Utils::ConstructV1ToV2RotationMatrix(Utils::AXIS_Z, normal);
		const XMFLOAT4 cosineWieghtedSample = GenerateCosineWeightedSample();

		XMVECTOR sseRayDir =
			XMVector4Transform(XMLoadFloat4(&cosineWieghtedSample), 
				XMLoadFloat4x4(&rotationMat));

		// Update ray dir
		XMStoreFloat4(&rayDir, sseRayDir);

		DX_ASSERT(Utils::IsAlmostEqual(XMVectorGetX(XMVector3Length(sseNormal)), 1.0f) && "Normal is not normalized");
		DX_ASSERT(Utils::IsAlmostEqual(XMVectorGetX(XMVector3Length(sseRayDir)), 1.0f) && "Ray Dir is not normalized");
		
		// Update nDotL
		nDotL = XMVectorGetX(XMVector3Dot(sseNormal, sseRayDir));

		// Fix up super small numbers
		if (std::abs(nDotL) < 0.000001f)
		{
			nDotL = 0.0f;
		}

		DX_ASSERT(nDotL >= 0.0f && "nDotL is negative, is it ok?");
		DX_ASSERT(Utils::IsAlmostEqual(nDotL, 
			XMVectorGetX(XMVector3Dot(XMLoadFloat4(&Utils::AXIS_Z), XMLoadFloat4(&cosineWieghtedSample)))) &&
		"Angle between unrotated sample and Z should be the same as angle between rotated sample and normal");

		// Update PDF 
		samplesPDF = GetCosineWeightedSamplePDF(nDotL); 
		// Avoid division by 0
		samplesPDF = std::max(samplesPDF, Utils::EPSILON);

		// Update Throughput
		
		// Find texture coordinates of the hit
		const XMFLOAT2 texCoords = Utils::BSPNodeRayIntersectionResult::GetTexCoord(intersectionResult);
		const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[intersectionResult.staticObjIndex];
		const Resource* objTexture = ResourceManager::Inst().FindResource(object.textureKey);

		DX_ASSERT(objTexture != nullptr && "Can't find obj texture");
		DX_ASSERT(objTexture->cpuBuffer.has_value() == true && "No CPU buffer");

		const XMFLOAT2 normalizedTextureCoordinates = Utils::NomralizeWrapAroundTextrueCoordinates(texCoords);
		const XMFLOAT4 albedo = Utils::TextureBilinearSample(*objTexture->cpuBuffer, objTexture->desc.format, objTexture->desc.width, objTexture->desc.height, normalizedTextureCoordinates);
		const XMVECTOR sseBrdf = XMLoadFloat4(&albedo) / M_PI;

		// I need to divide by PDF here because in all hemisphere I take only one sample of reflected light.
		// So in some sense I still do monte carlo but with only 1 sample
		sseThoroughput = sseThoroughput * sseBrdf * nDotL / samplesPDF;

		++rayBounce;
	}

	XMStoreFloat4(&result.radiance, sseRadiance);

	return result;
}


void LightBaker::SaveBakingResultsToFile(const BakingData& bakingResult) const
{
	std::stringstream bakingResultStream;

	// Baking mode
	DX_ASSERT(transferableData.bakingMode.has_value() == true && "Baking mode is not set");
	bakingResultStream << "BakingMode " << LightBaker::BakingModeToStr(*bakingResult.bakingMode);

	// Baking cluster
	if (bakingResult.bakingMode == LightBakingMode::CurrentPositionCluster)
	{
		DX_ASSERT(bakingResult.bakingCluster.has_value() &&
			"If baking mode is current cluster, there should be a value for baking cluster");

		bakingResultStream << "\n" << "BakingCluster " << *bakingResult.bakingCluster;
	}

	// First probe index
	if (bakingResult.bakingMode == LightBakingMode::AllClusters)
	{
		DX_ASSERT(bakingResult.clusterFirstProbeIndices.empty() == false &&
			"If baking mode is all clusters, cluster first probe indices are expected ");

		bakingResultStream << "\n" << "ClusterFirstProbeIndices " << bakingResult.clusterFirstProbeIndices.size();

		for (const int& firstProbeIndex : bakingResult.clusterFirstProbeIndices)
		{
			bakingResultStream << "\n" << firstProbeIndex;
		}
	}

	// Cluster Grid Sizes
	DX_ASSERT(bakingResult.clusterProbeGridSizes.empty() == false &&
		"Cluster probe grid can't be false");

	bakingResultStream << "\n" << "ClusterGridSizes " << bakingResult.clusterProbeGridSizes.size();

	for (const XMINT3& sizes : bakingResult.clusterProbeGridSizes)
	{
		bakingResultStream << "\n" << sizes.x << ", " << sizes.y << ", " << sizes.z;
	}


	// Probe Data
	bakingResultStream << std::fixed << std::setprecision(9);

	bakingResultStream << "\n" << "ProbeData " << bakingResult.probes.size();
	for (int i = 0; i < bakingResult.probes.size(); ++i)
	{
		const DiffuseProbe& probe = bakingResult.probes[i];


		bakingResultStream << "\n" << "Probe " << i;
		
		for (const XMFLOAT4& coef : probe.radianceSh)
		{
			bakingResultStream << "\n" << coef.x << ", " << coef.y << ", " << coef.z;
		}
	}

	bakingResultStream << std::defaultfloat;
	
	Utils::WriteFile(
		Utils::GenAbsolutePathToFile(Settings::DATA_DIR + "/" + Settings::LIGHT_BAKING_DATA_FILENAME),
		bakingResultStream.str());
}

BakingData LightBaker::LoadBakingResultsFromFile() const
{
	peg::parser parser;

	InitLightBakingResultParser(parser);

	const std::string dataFileContent = Utils::ReadFile(Utils::GenAbsolutePathToFile(Settings::DATA_DIR + "/" + Settings::LIGHT_BAKING_DATA_FILENAME));

	Parsing::LightBakingContext context;
	std::any ctx = &context;

	Logs::Log(Logs::Category::Parser, "Parse light baking result, start");

	parser.parse(dataFileContent.c_str(), ctx);

	Logs::Log(Logs::Category::Parser, "Parse light baking result, end");

	return context.bakingResult;
}
