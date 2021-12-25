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

void LightBaker::GenerateSourceData()
{
	clusterBakePoints = GenerateClustersBakePoints();

	currentBakeCluster = 0;
}

std::vector<std::vector<DirectX::XMFLOAT4>> LightBaker::GenerateClustersBakePoints() const
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

void LightBaker::BakeJob(GPUJobContext& context)
{
	//#TODO can I move WaitDependecy() to Dependency Guard?
	DependenciesRAIIGuard_t dependencyGuard(context);

	context.WaitDependency();

	while (true)
	{
		// Get next set of clusters to work on
		const int currentCluster = currentBakeCluster.fetch_add(1);

		if (currentCluster >= clusterBakePoints.size())
		{
			break;
		}

		const std::vector<XMFLOAT4>& bakePoints = clusterBakePoints[currentCluster];
		
		for (int bakePointIndex = 0; bakePointIndex < bakePoints.size(); ++bakePointIndex)
		{
			
			const XMFLOAT4& bakePoint = bakePoints[bakePointIndex];
			
			// One bake point is one probe  irradiance
			XMVECTOR sseProbeIrradiance = XMVectorZero();

			for (int i = 0; i < Settings::PROBE_SAMPLES_NUM; ++i)
			{
				sseProbeIrradiance = sseProbeIrradiance + XMLoadFloat4(&PathTraceFromProbe(bakePoint));
			}

			// Do Monte Carlo integration
			sseProbeIrradiance = sseProbeIrradiance / Settings::PROBE_SAMPLES_NUM;

		}
	}
}
//#DEBUG continue by placing this function in a proper space
XMFLOAT4 LightBaker::GatherDirectIrradianceAtInersectionPoint(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const
{
	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.origin) + XMLoadFloat4(&ray.direction) *
		nodeIntersectionResult.rayTriangleIntersection.t;

	XMFLOAT4 intersectionPoint;
	XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

	const SourceStaticObject& object = Renderer::Inst().sourceStaticObjects[nodeIntersectionResult.staticObjIndex];

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
	const std::vector<PointLight>& pointLights = Renderer::Inst().staticPointLights;
	const BSPTree& bsp = Renderer::Inst().bspTree;

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

XMFLOAT4 LightBaker::GatherIrradianceFromAreaLights(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const
{
	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.origin) + XMLoadFloat4(&ray.direction) *
		nodeIntersectionResult.rayTriangleIntersection.t;

	XMFLOAT4 intersectionPoint;
	XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

	const BSPTree& bsp = Renderer::Inst().bspTree;
	
	const BSPNode& intersectionNode = bsp.GetNodeWithPoint(intersectionPoint, bsp.nodes.front());
	if (intersectionNode.cluster == Const::INVALID_INDEX)
	{
		return XMFLOAT4{0.0f, 0.0f, 0.0f, 0.0f};
	}

	//#DEBUG I do DecompressClusterVisibility() do often, should I wrap up GetNodeWithPoint() and this
	// in a single method?
	const std::vector<bool> intersectionPVS = bsp.DecompressClusterVisibility(intersectionNode.cluster);
	const std::vector<SourceStaticObject>& staticObjects = Renderer::Inst().sourceStaticObjects;

	// Get all potentially visible objects
	std::vector<int> potentiallyVisibleObjects;
	for (const int leafIndex : bsp.leavesIndices)
	{
		const BSPNode& leaf = bsp.nodes[leafIndex];

		if (leaf.cluster != Const::INVALID_INDEX && intersectionPVS[leaf.cluster] == true)
		{
			potentiallyVisibleObjects.insert(potentiallyVisibleObjects.end(), leaf.objectsIndices.cbegin(), leaf.objectsIndices.cend());
		}
	}

	const std::vector<SurfaceLight>& staticSurfaceLights = Renderer::Inst().staticSurfaceLights;

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
	const SourceStaticObject& lightMesh = Renderer::Inst().sourceStaticObjects[light.surfaceIndex];

	const std::vector<float>& lightTrianglesPDF = light.trianglesPDF;

	const BSPTree& bsp = Renderer::Inst().bspTree;

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

		//#DEBUG this is just for my sanity. I will delete it later
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
XMFLOAT4 LightBaker::PathTraceFromProbe(const XMFLOAT4& probeCoord)
{
	XMVECTOR sseIrradiance = XMVectorZero();

	XMFLOAT4 intersectionPoint = probeCoord;
	XMFLOAT4 rayDir = GenerateUniformSphereSample();

	float samplesPDF = GetUniformSphereSamplePDF();

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

		auto [isIntersected, intersectionResult] = FindClosestRayIntersection(ray);

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
		XMFLOAT4 normal = BSPNodeRayIntersectionResult::GetNormal(intersectionResult);
		XMVECTOR sseNormal = XMLoadFloat4(&normal);

		XMVECTOR sseRayDir =
			XMVector4Transform(XMLoadFloat4(&GenerateCosineWeightedSample()), 
				XMLoadFloat4x4(&Utils::ConstructV1ToV2RotationMatrix(Utils::AXIS_Z, normal)));

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

std::tuple<bool, LightBaker::BSPNodeRayIntersectionResult> LightBaker::FindClosestRayIntersection(const Utils::Ray& ray) const
{
	const BSPTree& bsp = Renderer::Inst().GetBSPTree();

	// 1)
	const BSPNode& node = bsp.GetNodeWithPoint(ray.origin);

	BSPNodeRayIntersectionResult nodeIntersectionResult;
	float minT = FLT_MAX;

	// Try out this node first
	if (FindClosestIntersectionInNode(ray, node, nodeIntersectionResult, minT) == false)
	{
		//#DEBUG does the node I am in, include in this list as well?
		
		// Time to check PVS
		std::vector<bool> currentPVS =  bsp.DecompressClusterVisibility(node.cluster);

		for (const int leafIndex : bsp.leavesIndices)
		{
			const BSPNode& leaf = bsp.nodes[leafIndex];

			if (leaf.cluster != Const::INVALID_INDEX && currentPVS[leaf.cluster] == true)
			{
				FindClosestIntersectionInNode(ray, node, nodeIntersectionResult, minT);
			}
		}

	}

	const bool isIntersected = minT != FLT_MAX;

	return {isIntersected, nodeIntersectionResult};
}

bool LightBaker::FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, LightBaker::BSPNodeRayIntersectionResult& result, float& minT) const
{
	float nodeIntersectionT = FLT_MAX;

	if (Utils::IsRayIntersectsAABB(ray, node.aabb, &nodeIntersectionT) == false || 
		nodeIntersectionT > minT)
	{
		return false;
	}

	const std::vector<SourceStaticObject>& objects = Renderer::Inst().sourceStaticObjects;

	float minRayT = FLT_MAX;

	for (const int objectIndex : node.objectsIndices)
	{
		float rayT = FLT_MAX;

		const SourceStaticObject& object = objects[objectIndex];

		// No intersection at all
		if (Utils::IsRayIntersectsAABB(ray, object.aabb, &rayT) == false)
		{
			continue;
		}

		// Potential intersection with object further than what we have, early reject
		if (rayT > minRayT)
		{
			continue;
		}

		assert(object.indices.size() % 3 == 0 && "Invalid triangle indices");

		for (int triangleIndex = 0; triangleIndex < object.indices.size() / 3; ++triangleIndex)
		{
			const XMFLOAT4& v0 = object.vertices[object.indices[triangleIndex * 3 + 0]];
			const XMFLOAT4& v1 = object.vertices[object.indices[triangleIndex * 3 + 1]];
			const XMFLOAT4& v2 = object.vertices[object.indices[triangleIndex * 3 + 2]];

			Utils::RayTriangleIntersectionResult rayTriangleResult;

			if (Utils::IsRayIntersectsTriangle(ray, v0, v1, v2, rayTriangleResult) == false)
			{
				continue;
			}

			if (rayTriangleResult.t > minRayT)
			{
				continue;
			}

			// Reject backface triangles. Dot product should be negative to make sure we hit the front side of triangle
			XMVECTOR sseV0Normal = XMLoadFloat4(&object.normals[object.indices[triangleIndex * 3]]);
			if (XMVectorGetX(XMVector3Dot(sseV0Normal, XMLoadFloat4(&ray.direction))) >= 0.0f)
			{
				continue;
			}


			minRayT = rayTriangleResult.t;

			result.rayTriangleIntersection = rayTriangleResult;
			result.staticObjIndex = objectIndex;
			result.triangleIndex = triangleIndex;
		}

	}

	if (minRayT != FLT_MAX)
	{
		minT = minRayT;
		return true;
	}

	return false;
}

XMFLOAT4 LightBaker::BSPNodeRayIntersectionResult::GetNormal(const BSPNodeRayIntersectionResult& result)
{
	const SourceStaticObject& object = Renderer::Inst().sourceStaticObjects[result.staticObjIndex];

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
