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

	int GetAreaLightSampleSetIndex()
	{
		static std::mutex mutex;
		std::scoped_lock<std::mutex> lock(mutex);

		static std::random_device randomDevice;
		static std::mt19937 randomGenerationEngine(randomDevice());

		std::uniform_int_distribution<> distribution(0, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SETS_NUM);

		return distribution(randomGenerationEngine);
	}

	int GetUniformSphereSampleSetIndex()
	{
		static std::mutex mutex;
		std::scoped_lock<std::mutex> lock(mutex);

		static std::random_device randomDevice;
		static std::mt19937 randomGenerationEngine(randomDevice());

		std::uniform_int_distribution<> distribution(0, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SETS_NUM);

		return distribution(randomGenerationEngine);
	}
}

void LightBaker::GenerateSourceData()
{
	uniformSphereSamplesSets = GenerateRandomUniformSphereSamples();
	areaLightsSamplesSets = GenerateRandomAreaLightsSamples();

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

std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE>> LightBaker::GenerateRandomUniformSphereSamples() const
{
	std::random_device randomDevice;
	std::mt19937 randomGenerationEngine(randomDevice());

	std::uniform_real_distribution<> distribution(0.0f, 1.0f);

	std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE>> samplesSetArray(LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SETS_NUM);

	for (int i = 0; i < RANDOM_UNIFORM_SPHERE_SAMPLES_SETS_NUM; ++i)
	{
		std::array<XMFLOAT4, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE>& currentSamplesSet = samplesSetArray[i];

		for (XMFLOAT4& sample : currentSamplesSet)
		{
			const float randNum1 = distribution(randomGenerationEngine);
			const float randNum2 = distribution(randomGenerationEngine);

			// Uniform sphere distribution
			sample.w = 0.0f;
			sample.z = 1.0f - 2.0f * randNum1;
			sample.y = std::sin(2.0f * M_PI * randNum2) * std::sqrt(1.0f - sample.z * sample.z);
			sample.x = std::cos(2.0f * M_PI * randNum2) * std::sqrt(1.0f - sample.z * sample.z);
		}
	}

	return samplesSetArray;
}

// Returns vector of sets of area lights. Per each sample:\
// x, y - 2d value of sample applied to a certain triangle in mesh
// z - value to figure out which triangle to sample in mesh, based on area of each triangle in mesh
std::vector<std::array<XMFLOAT4, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>> LightBaker::GenerateRandomAreaLightsSamples() const
{
	std::random_device randomDevice;
	std::mt19937 randomGenerationEngine(randomDevice());

	std::uniform_real_distribution<> distribution(0.0f, 1.0f);

	std::vector<std::array<XMFLOAT4, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>> samplesSetArray(LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SETS_NUM);

	for (int i = 0; i < AREA_LIGHTS_RANDOM_SAMPLES_SETS_NUM; ++i)
	{
		std::array<XMFLOAT4, LightBaker::AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>& currentSamplesSet = samplesSetArray[i];

		for (XMFLOAT4& sample : currentSamplesSet)
		{
			sample.x = distribution(randomGenerationEngine);
			sample.y = distribution(randomGenerationEngine);
			sample.z = distribution(randomGenerationEngine);
		}
	}

	return samplesSetArray;
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
			const std::array<XMFLOAT4, LightBaker::RANDOM_UNIFORM_SPHERE_SAMPLES_SET_SIZE>& samplesSet = uniformSphereSamplesSets[GetUniformSphereSampleSetIndex()];

			//#DEBUG finish here!
		}
	}
}

XMFLOAT4 LightBaker::GatherIrradianceAtInersectionPoint(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const
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

	XMVECTOR normal = XMVector3Normalize(sseV0Normal * nodeIntersectionResult.rayTriangleIntersection.u +
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

		const float normalAndIntersectionDotProduct = XMVectorGetX(XMVector3Dot(sseIntersectionPointToLight, normal));

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

	//#DEBUG don't forget to add radiance from area lights

	XMFLOAT4 resultIrradiance;
	XMStoreFloat4(&resultIrradiance, sseResultIrradiance);

	return resultIrradiance;
}

XMFLOAT4 LightBaker::GatherIrradianceFromAreaLights(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const
{
	XMVECTOR sseIntersectionPoint = XMLoadFloat4(&ray.origin) + XMLoadFloat4(&ray.direction) *
		nodeIntersectionResult.rayTriangleIntersection.t;

	XMFLOAT4 intersectionPoint;
	XMStoreFloat4(&intersectionPoint, sseIntersectionPoint);

	const BSPTree& bsp = Renderer::Inst().bspTree;
	//#DEBUG continue here.
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
			staticSurfaceLights[lightIndex],
			areaLightsSamplesSets[GetAreaLightSampleSetIndex()]);

		sseResultIrradiance = sseResultIrradiance + XMLoadFloat4(&lightIrradiance);
	}

	XMFLOAT4 resultIrradiance;
	XMStoreFloat4(&resultIrradiance, sseResultIrradiance);

	return resultIrradiance;
}

XMFLOAT4 LightBaker::GatherIrradianceFromAreaLight(const XMFLOAT4& intersectionPoint, const SurfaceLight& light, const std::array<XMFLOAT4, AREA_LIGHTS_RANDOM_SAMPLES_SET_SIZE>& samplesSet) const
{
	const SourceStaticObject& lightMesh = Renderer::Inst().sourceStaticObjects[light.surfaceIndex];

	const std::vector<float>& lightTrianglesPDF = light.trianglesPDF;

	const BSPTree& bsp = Renderer::Inst().bspTree;

	XMVECTOR sseIrradiance = XMVectorZero();

	for (int i = 0; i < samplesSet.size(); ++i)
	{
		const XMFLOAT4& sample = samplesSet[i];

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
		 	sseLightSamplePoint - XMLoadFloat4(&intersectionPoint), 
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
		//#DEBUG continue AREA lights here	
		//sseIrradiance = sseIrradiance + 
	}

	return XMFLOAT4();
}

XMFLOAT4 LightBaker::PathTrace(const Utils::Ray& ray) const
{
	// How should I approach finding closest intersection:
	// 1) Find in which node/cluster position lies
	// 2) Check geometry in this cluster/node
	// 3) If nothing found, look for closest geometry in PVC
	// 4) For every static object check AABB first
	// 5) If AABB fits, look for every triangle

	// !!! NODES AND CLUSTERS ALSO HAVE AABB, don't forget that!

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

	if (minT != FLT_MAX)
	{
		// We found intersection 

		// Intersection point normal
		//XMVECTOR normal;

		
	}

	return XMFLOAT4();
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