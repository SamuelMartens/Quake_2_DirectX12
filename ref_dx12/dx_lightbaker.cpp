#include "dx_lightbaker.h"
#include "dx_app.h"

#include <set>
#include <cassert>
#include <random>
#include <limits>
#include <numeric>
#include <mutex>

#define _USE_MATH_DEFINES
#include <cmath>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace
{
	inline float CalculateDistanceFalloff(float dist, float distMax)
	{
		constexpr float dist0 = 1.0f;
		constexpr float epsilon = std::numeric_limits<float>::epsilon();

		assert(distMax > 0 && "Max distance must be more than zero");

		// Real-Time Rendering (4th Edition), page 113
		const float windowedFunctionValue = std::powf(std::max(0.0f, 1.0f - std::powf(dist / distMax, 4)), 2);

		// Real-Time Rendering (4th Edition), page 111
		const float distanceFalloff = std::powf(dist0, 2) / (std::powf(dist, 2) + epsilon);

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
		return cosTheta / M_PI;
	}
}

void LightBaker::PreBake()
{
	ASSERT_MAIN_THREAD;

	assert(clusterProbeData.empty() == true && "Cluster probe data should be empty before bake");
	assert(clusterBakePoints.empty() == true && "Cluster bake points should be empty before bake");
	assert(probesBaked == 0 && "Amount of baked probes was not reset");
	assert(probes.empty() == true && "Probes were baked, but not consumed");
	
	currentBakeCluster = 0;
	clusterBakePoints = GenerateClustersBakePoints();
	clusterProbeData.resize(clusterBakePoints.size());

	int totalProbes = 0;

	for (int i = 0; i < clusterBakePoints.size(); ++i)
	{
		clusterProbeData[i].startIndex = totalProbes;

		totalProbes += clusterBakePoints[i].size();
	}

	probes.resize(totalProbes);
}

void LightBaker::PostBake()
{
	ASSERT_MAIN_THREAD;
	
	assert(probes.empty() == false && "Baking is finished, but no probes were generated");

	Renderer::Inst().ConsumeDiffuseIndirectLightingBakingResult(TransferBakingResult());

	probesBaked = 0;
	clusterProbeData.clear();
	clusterBakePoints.clear();
}

std::vector<std::vector<XMFLOAT4>> LightBaker::GenerateClustersBakePoints()
{
	std::vector<std::vector<XMFLOAT4>> bakePoints;

	switch (generationMode)
	{
	case LightBakingMode::AllClusters:
	{
		bakeCluster = std::nullopt;

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
		assert(bakePosition.has_value() == true && "Bake position is not set");
		const BSPNode& cameraNode = Renderer::Inst().GetBSPTree().GetNodeWithPoint(*bakePosition);

		bakePosition.reset();

		assert(cameraNode.cluster != Const::INVALID_INDEX && "Camera node invalid index");

		bakeCluster = cameraNode.cluster;

		bakePoints.resize(cameraNode.cluster + 1);
		bakePoints[cameraNode.cluster] = GenerateClusterBakePoints(cameraNode.cluster);
	}
		break;
	default:
		assert(false && "Invalid generation mode");
		break;
	}

	return bakePoints;
}

std::vector<XMFLOAT4> LightBaker::GenerateClusterBakePoints(int clusterIndex) const
{
	constexpr float bakePointsInterval = 50.0f;

	Utils::AABB clusterAABB = Renderer::Inst().GetBSPTree().GetClusterAABB(clusterIndex);
	
	// Amount of bake points along X axis
	const int xAxisNum = std::ceil((clusterAABB.maxVert.x - clusterAABB.minVert.x) / bakePointsInterval);
	// Amount of bake points along Y axis
	const int yAxisNum = std::ceil((clusterAABB.maxVert.y - clusterAABB.minVert.y) / bakePointsInterval);
	// Amount of bake points along X axis
	const int zAxisNum = std::ceil((clusterAABB.maxVert.z - clusterAABB.minVert.z) / bakePointsInterval);

	std::vector<XMFLOAT4> bakePoints;
	bakePoints.reserve(xAxisNum * yAxisNum * zAxisNum);

	for (int xIteration = 0; xIteration < xAxisNum; ++xIteration)
	{
		for (int yIteration = 0; yIteration < yAxisNum; ++yIteration)
		{
			for (int zIteration = 0; zIteration < zAxisNum; ++zIteration)
			{
				bakePoints.push_back({
					std::min(clusterAABB.minVert.x + bakePointsInterval * xIteration, clusterAABB.maxVert.x),
					std::min(clusterAABB.minVert.y + bakePointsInterval * yIteration, clusterAABB.maxVert.y),
					std::min(clusterAABB.minVert.z + bakePointsInterval * zIteration, clusterAABB.maxVert.z),
					1.0f
					});
			}
		}
	}

	return bakePoints;
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
		const int clusterProbeStartIndex = clusterProbeData[currentCluster].startIndex;

		assert(clusterProbeStartIndex != Const::INVALID_INDEX && "Invalid cluster probe start index");

		for (int bakePointIndex = 0; bakePointIndex < bakePoints.size(); ++bakePointIndex)
		{
			const XMFLOAT4& bakePoint = bakePoints[bakePointIndex];
			
			SphericalHarmonic9_t<XMFLOAT4>& totalShProjection = probes[clusterProbeStartIndex + bakePointIndex].radianceSh;
			ZeroMemory(totalShProjection.data(), sizeof(XMFLOAT4) * totalShProjection.size());	

			for (int i = 0; i < Settings::PROBE_SAMPLES_NUM; ++i)
			{
				XMFLOAT4 direction = { 0.0f, 0.0f, 0.0f, 1.0f };
				// Result of one sample
				const XMFLOAT4 sampleRadiance = PathTraceFromProbe(bakePoint, direction);
				// Project single sample on SH
				SphericalHarmonic9_t<XMFLOAT4> sampleShProjection = ProjectOntoSphericalHarmonic(direction, sampleRadiance);

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
}

int LightBaker::GetTotalProbesNum() const
{
	return probes.size();
}

int LightBaker::GetBakedProbesNum() const
{
	assert(probesBaked <= GetTotalProbesNum() && "Baked probes exceeded total probes");
	return probesBaked;
}

BakingResult LightBaker::TransferBakingResult()
{
	BakingResult result;

	result.probeData = std::move(probes);
	result.bakingMode = generationMode;
	result.bakingCluster = bakeCluster;

	if (generationMode == LightBakingMode::AllClusters)
	{
		std::vector<BakingResult::ClusterSize> clusterSizes;
		clusterSizes.reserve(clusterProbeData.size());

		for (int i = 0; i < clusterProbeData.size(); ++i)
		{
			clusterSizes.push_back(BakingResult::ClusterSize{
				clusterProbeData[i].startIndex, 
				static_cast<int>(clusterBakePoints[i].size())});
		}

		result.clusterSizes = clusterSizes;
	}

	// Now clear rest of baking state result
	generationMode = LightBakingMode::None;
	bakeCluster = std::nullopt;

	return result;
}

void LightBaker::SetBakingMode(LightBakingMode genMode)
{
	generationMode = genMode;
}

void LightBaker::SetBakePosition(const XMFLOAT4& position)
{
	assert(bakePosition.has_value() == false && "Bake position is not cleared");
	bakePosition = position;
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

XMFLOAT4 LightBaker::GatherDirectIrradianceAtInersectionPoint(const Utils::Ray& ray, const Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult) const
{
	const Renderer& renderer = Renderer::Inst();

	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.origin) + XMLoadFloat4(&ray.direction) *
		nodeIntersectionResult.rayTriangleIntersection.t;

	XMFLOAT4 intersectionPoint;
	XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

	const SourceStaticObject& object = renderer.GetSourceStaticObjects()[nodeIntersectionResult.staticObjIndex];

	const int v0Index = object.indices[nodeIntersectionResult.triangleIndex * 3 + 0];
	const int v1Index = object.indices[nodeIntersectionResult.triangleIndex * 3 + 1];
	const int v2Index = object.indices[nodeIntersectionResult.triangleIndex * 3 + 2];

	XMVECTOR sseV0Normal = XMLoadFloat4(&object.normals[v0Index]);
	XMVECTOR sseV1Normal = XMLoadFloat4(&object.normals[v1Index]);
	XMVECTOR sseV2Normal = XMLoadFloat4(&object.normals[v2Index]);

	XMVECTOR sseNormal = XMVector3Normalize(sseV0Normal * nodeIntersectionResult.rayTriangleIntersection.u +
		sseV1Normal * nodeIntersectionResult.rayTriangleIntersection.v +
		sseV2Normal * nodeIntersectionResult.rayTriangleIntersection.w);

	// Gather from point lights first 
	const std::vector<PointLight>& pointLights = renderer.GetStaticPointLights();
	const BSPTree& bsp = renderer.GetBSPTree();

	XMVECTOR sseResultIrradiance = XMVectorZero();

	for (const PointLight& light : pointLights)
	{
		if (light.intensity == 0.0f)
		{
			continue;
		}

		XMVECTOR sseIntersectionPointToLight = XMLoadFloat4(&light.origin) - sseIntersectionPoint;

		const float distanceToLight = XMVectorGetX(XMVector3Length(sseIntersectionPoint));

		if (distanceToLight > light.radius)
		{
			continue;
		}

		const float normalAndIntersectionDotProduct = XMVectorGetX(XMVector3Dot(sseIntersectionPointToLight, sseNormal));

		if (normalAndIntersectionDotProduct <= 0.0f)
		{
			continue;
		}

		// This is expensive check, as so it should be delayed as much as possible
		if (bsp.IsPointVisibleFromOtherPoint(intersectionPoint, light.origin) == false)
		{
			continue;
		}

		const float distanceFalloff = CalculateDistanceFalloff(distanceToLight, light.radius);

		sseResultIrradiance = sseResultIrradiance + 
			 distanceFalloff * XMLoadFloat4(&light.color) * light.intensity * normalAndIntersectionDotProduct;
	}

	const XMFLOAT4 areaLightIrradiance = GatherIrradianceFromAreaLights(ray, nodeIntersectionResult);

	XMFLOAT4 resultIrradiance;
	XMStoreFloat4(&resultIrradiance, sseResultIrradiance + XMLoadFloat4(&areaLightIrradiance));

	return resultIrradiance;
}

XMFLOAT4 LightBaker::GatherIrradianceFromAreaLights(const Utils::Ray& ray, const Utils::BSPNodeRayIntersectionResult& nodeIntersectionResult) const
{
	const Renderer& renderer = Renderer::Inst();

	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.origin) + XMLoadFloat4(&ray.direction) *
		nodeIntersectionResult.rayTriangleIntersection.t;

	XMFLOAT4 intersectionPoint;
	XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

	const BSPTree& bsp = renderer.GetBSPTree();
	
	const BSPNode& intersectionNode = bsp.GetNodeWithPoint(intersectionPoint);
	if (intersectionNode.cluster == Const::INVALID_INDEX)
	{
		return XMFLOAT4{0.0f, 0.0f, 0.0f, 0.0f};
	}

	// Get all potentially visible objects
	std::vector<int> potentiallyVisibleObjects = bsp.GetPotentiallyVisibleObjects(intersectionPoint);
	
	const std::vector<SurfaceLight>& staticSurfaceLights = renderer.GetStaticSurfaceLights();

	// Get potentially visible light objects from all potentially visible objects
	std::vector<int> potentiallyVisibleLightsIndices;
	for (int i = 0; i < staticSurfaceLights.size(); ++i)
	{
		const SurfaceLight& areaLight = staticSurfaceLights[i];

		auto lightObjectIt = std::find(potentiallyVisibleObjects.cbegin(), 
			potentiallyVisibleObjects.cend(), areaLight.surfaceIndex);

		if (lightObjectIt != potentiallyVisibleObjects.cend())
		{
			potentiallyVisibleLightsIndices.push_back(i);
		}
	}

	XMVECTOR sseResultIrradiance = XMVectorZero();

	for (const int lightIndex : potentiallyVisibleLightsIndices)
	{
		XMFLOAT4 lightIrradiance = GatherIrradianceFromAreaLight(
			intersectionPoint,
			staticSurfaceLights[lightIndex]);

		sseResultIrradiance = sseResultIrradiance + XMLoadFloat4(&lightIrradiance);
	}

	XMFLOAT4 resultIrradiance;
	XMStoreFloat4(&resultIrradiance, sseResultIrradiance);

	return resultIrradiance;
} 

XMFLOAT4 LightBaker::GatherIrradianceFromAreaLight(const XMFLOAT4& intersectionPoint, const SurfaceLight& light) const
{
	const SourceStaticObject& lightMesh = Renderer::Inst().GetSourceStaticObjects()[light.surfaceIndex];

	const std::vector<float>& lightTrianglesPDF = light.trianglesPDF;

	const BSPTree& bsp = Renderer::Inst().GetBSPTree();

	XMVECTOR sseSampleIrradiance = XMLoadFloat4(&light.irradiance);
	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&intersectionPoint);

	XMVECTOR sseIrradiance = XMVectorZero();

	for (int i = 0; i < Settings::AREA_LIGHTS_SAMPLES_NUM; ++i)
	{
		const XMFLOAT4 sample = GenerateAreaLightsSample();

		const auto trinangleIndexIt = std::find_if(lightTrianglesPDF.cbegin(), lightTrianglesPDF.cend(), 
			[&sample](const float& trianglePDF)
		{
			return trianglePDF >= sample.z;
		});

		assert(trinangleIndexIt != lightTrianglesPDF.cend() && "Triangle sample not found");

		const int triangleIndex = std::distance(lightTrianglesPDF.cbegin(), trinangleIndexIt);

		// Convert random samples into barycentric coordinates of triangle
		const float u = 1.0f - std::sqrt(sample.x);
		const float v = sample.y * std::sqrt(sample.x);
		const float w = 1.0f - u - v;

		//This is just for my sanity. I will delete it later
		assert(u + v <= 1.0f && "Something funky with barycentric coordinates");

		const int V0Ind = lightMesh.indices[triangleIndex * 3 + 0];
		const int V1Ind = lightMesh.indices[triangleIndex * 3 + 1];
		const int V2Ind = lightMesh.indices[triangleIndex * 3 + 2];

		XMVECTOR sseV0 = XMLoadFloat4(&lightMesh.vertices[V0Ind]);
		XMVECTOR sseV1 = XMLoadFloat4(&lightMesh.vertices[V1Ind]);
		XMVECTOR sseV2 = XMLoadFloat4(&lightMesh.vertices[V2Ind]);

		XMVECTOR sseLightSamplePoint = sseV0 * u + sseV1 * v + sseV2 * w;
		
		const float lightToRayAndLightNormalDot = XMVectorGetX(XMVector3Dot(
		 	sseLightSamplePoint - sseIntersectionPoint, 
			XMLoadFloat4(&lightMesh.normals[V0Ind])));

		if (lightToRayAndLightNormalDot <= 0.0f)
		{
			// This point is behind chosen light. It will not contribute,
			// moving to the next sample
			continue;
		}

		XMFLOAT4 lightSamplePoint;
		XMStoreFloat4(&lightSamplePoint, sseLightSamplePoint);

		if (bsp.IsPointVisibleFromOtherPoint(intersectionPoint, lightSamplePoint) == false)
		{
			continue;
		}
		
		const float distanceToSample = XMVectorGetX(XMVector3Length(sseLightSamplePoint - sseIntersectionPoint));

		sseIrradiance = sseIrradiance + 
			sseSampleIrradiance * CalculateDistanceFalloff(distanceToSample, Settings::AREA_LIGHTS_MAX_DISTANCE) *
			lightToRayAndLightNormalDot;
	}

	// Now do Monte Carlo integration
	sseIrradiance = sseIrradiance / SurfaceLight::GetUniformSamplePDF(light) / Settings::AREA_LIGHTS_SAMPLES_NUM;

	XMFLOAT4 irradiance;
	XMStoreFloat4(&irradiance, sseIrradiance);

	return irradiance;
}

// Will return indirect light that comes to probe via one sample
XMFLOAT4 LightBaker::PathTraceFromProbe(const XMFLOAT4& probeCoord, XMFLOAT4& direction)
{
	const BSPTree& bspTree = Renderer::Inst().GetBSPTree();

	XMVECTOR sseIrradiance = XMVectorZero();

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
			// Apply Russian Roulette to terminate.
			const float sample = GenerateNormalizedUniformDistributioSample();
			//#TODO MJP uses throughtput as well. But I don't quite understand his logic
			if (sample < Settings::RUSSIAN_ROULETTE_TERMINATION_PROBABILITY)
			{
				break;
			}

			sseThoroughput = sseThoroughput / (1.0f - Settings::RUSSIAN_ROULETTE_TERMINATION_PROBABILITY);
		}

		// Find intersection
		Utils::Ray ray = { intersectionPoint, rayDir };

		auto [isIntersected, intersectionResult] = bspTree.FindClosestRayIntersection(ray);

		if (isIntersected == false)
		{
			break;
		}

		XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.direction) * intersectionResult.rayTriangleIntersection.t +
			XMLoadFloat4(&ray.origin);

		// Update intersection point
		XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

		XMFLOAT4 directIrradiance = GatherDirectIrradianceAtInersectionPoint(ray, intersectionResult);

		sseIrradiance = sseIrradiance + XMLoadFloat4(&directIrradiance) * sseThoroughput;

		// Generate new ray dir
		XMFLOAT4 normal = Utils::BSPNodeRayIntersectionResult::GetNormal(intersectionResult);
		XMVECTOR sseNormal = XMLoadFloat4(&normal);

		const XMFLOAT4X4 rotationMat = Utils::ConstructV1ToV2RotationMatrix(Utils::AXIS_Z, normal);
		const XMFLOAT4 cosineWieghtedSample = GenerateCosineWeightedSample();

		XMVECTOR sseRayDir =
			XMVector4Transform(XMLoadFloat4(&cosineWieghtedSample), 
				XMLoadFloat4x4(&rotationMat));

		//#TODO the update order is messy, and very likely to be wrong.
		// we might update with next values for something, not current one
		// just be carefull

		// Update ray dir
		XMStoreFloat4(&rayDir, sseRayDir);

		assert(Utils::IsAlmostEqual(XMVectorGetX(XMVector3Length(sseNormal)), 1.0f) && "Normal is not normalized");
		assert(Utils::IsAlmostEqual(XMVectorGetX(XMVector3Length(sseRayDir)), 1.0f) && "Normal is not normalized");

		// Update nDotL
		nDotL = XMVectorGetX(XMVector3Dot(sseNormal, sseRayDir));

		assert(nDotL > 0.0f && "nDotL is negative, is it ok?");

		// Update PDF 
		samplesPDF = GetCosineWeightedSamplePDF(nDotL);

		// Update Throughput
		sseThoroughput = sseThoroughput * nDotL / samplesPDF;


		++rayBounce;
	}

	XMFLOAT4 irradiance;
	XMStoreFloat4(&irradiance, sseIrradiance);

	return irradiance;
}

XMFLOAT4 Utils::BSPNodeRayIntersectionResult::GetNormal(const Utils::BSPNodeRayIntersectionResult& result)
{
	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[result.staticObjIndex];

	const int v0Index = object.indices[result.triangleIndex * 3 + 0];
	const int v1Index = object.indices[result.triangleIndex * 3 + 1];
	const int v2Index = object.indices[result.triangleIndex * 3 + 2];

	XMVECTOR sseV0Normal = XMLoadFloat4(&object.normals[v0Index]);
	XMVECTOR sseV1Normal = XMLoadFloat4(&object.normals[v1Index]);
	XMVECTOR sseV2Normal = XMLoadFloat4(&object.normals[v2Index]);

	XMVECTOR sseNormal = XMVector3Normalize(sseV0Normal * result.rayTriangleIntersection.u +
		sseV1Normal * result.rayTriangleIntersection.v +
		sseV2Normal * result.rayTriangleIntersection.w);

	XMFLOAT4 normal;
	XMStoreFloat4(&normal, sseNormal);

	return normal;
}
