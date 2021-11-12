#include "dx_lightbaker.h"
#include "dx_app.h"

#include <set>
#include <cassert>
#include <random>

#define _USE_MATH_DEFINES
#include <cmath>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

void LightBaker::GenerateSourceData()
{
	samplesSets = GenerateRandomSamples();
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

std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_SAMPLES_SET_SIZE>> LightBaker::GenerateRandomSamples() const
{
	std::random_device randomDevice;
	std::mt19937 randomGenerationEngine(randomDevice());

	std::uniform_real_distribution<> distribution(0.0f, 1.0f);

	std::vector<std::array<XMFLOAT4, LightBaker::RANDOM_SAMPLES_SET_SIZE>> samplesSetArray(LightBaker::RANDOM_SAMPLES_SETS_NUM);

	for (int i = 0; i < RANDOM_SAMPLES_SETS_NUM; ++i)
	{
		std::array<XMFLOAT4, LightBaker::RANDOM_SAMPLES_SET_SIZE>& currentSamplesSet = samplesSetArray[i];

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

void LightBaker::BakeJob(GPUJobContext& context)
{
	//#TODO can I move WaitDependecy() to Dependency Guard?
	DependenciesRAIIGuard_t dependencyGuard(context);

	context.WaitDependency();

	// Data for picking random point
	std::random_device randomDevice;
	std::mt19937 randomGenerationEngine(randomDevice());

	std::uniform_int_distribution<> distribution(0, LightBaker::RANDOM_SAMPLES_SETS_NUM);

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
			const std::array<XMFLOAT4, LightBaker::RANDOM_SAMPLES_SET_SIZE>& samplesSet = samplesSets[distribution(randomGenerationEngine)];


		}
	}
}

float LightBaker::GatherIrradianceAtInersectionPoint(const Utils::Ray& ray, const BSPNodeRayIntersectionResult& nodeIntersectionResult) const
{
	XMVECTOR intersectionPoint = XMLoadFloat4(&ray.direction) * nodeIntersectionResult.rayTriangleIntersection.t;

	return 0.0f;
}

float LightBaker::PathTrace(const Utils::Ray& ray) const
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

	return 0.0f;
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
