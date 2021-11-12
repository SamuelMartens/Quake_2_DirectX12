#include "dx_bsp.h"

#include <algorithm>

#include "dx_app.h"

namespace
{
	inline bool AABBIntersectsCameraFrustum(const Utils::AABB& aabb, const std::array<Utils::Plane, 6>& cameraFrustum)
	{
		return std::all_of(cameraFrustum.cbegin(), cameraFrustum.cend(), [&aabb](const Utils::Plane& plane)
		{
			return Utils::IsAABBBehindPlane(plane, aabb) == false;
		});
	}
}


void BSPTree::Create(const mnode_t& root)
{
	int meshesNum = 0;
	AddNode(root, meshesNum);
}

void BSPTree::InitClusterVisibility(const dvis_t& vis, int visSize)
{
	clusterVisibility.reserve(visSize);
	memcpy(clusterVisibility.data(), &vis, visSize);
}

std::vector<int> BSPTree::GetVisibleObjectsIndices(const Camera& camera) const
{
	std::vector<int> visibleObjects;

	if (nodes.empty() == true)
	{
		return visibleObjects;
	}

	const BSPNode& cameraNode = GetNodeWithPoint(camera.position, nodes.front());

	assert(cameraNode.cluster != Const::INVALID_INDEX && "Camera is located in invalid BSP node.");

	std::vector<bool> currentPVS = DecompressClusterVisibility(cameraNode.cluster);

	std::array<Utils::Plane, 6> cameraFrustum = camera.GetFrustumPlanes();

	for (const int leafIndex : leavesIndices)
	{
		const BSPNode& leaf = nodes[leafIndex];

		if (leaf.cluster != Const::INVALID_INDEX && currentPVS[leaf.cluster] == true && AABBIntersectsCameraFrustum(leaf.aabb, cameraFrustum))
		{
			visibleObjects.insert(visibleObjects.end(), leaf.objectsIndices.cbegin(), leaf.objectsIndices.cend());
		}
	}

	return visibleObjects;
}

int BSPTree::AddNode(const mnode_t& sourceNode, int& meshesNum)
{
	// WARNING: this function should process surfaces in the same order as
	// DecomposeGLModelNode

	BSPNode& node = nodes.emplace_back(BSPNode{});
	const int addedNodeIndex = nodes.size() - 1;

	node.aabb = { 
		XMFLOAT4(sourceNode.minmaxs[0], sourceNode.minmaxs[1], sourceNode.minmaxs[2], 1.0f),
		XMFLOAT4(sourceNode.minmaxs[3], sourceNode.minmaxs[4], sourceNode.minmaxs[5], 1.0f), 
	};

	if (Node_IsLeaf(&sourceNode))
	{
		const mleaf_t& leaf = reinterpret_cast<const mleaf_t&>(sourceNode);
		const msurface_t* const* surf = leaf.firstmarksurface;

		node.cluster = leaf.cluster;
		node.objectsIndices.reserve(leaf.nummarksurfaces);

		for (int i = 0; i < leaf.nummarksurfaces; ++i, ++surf)
		{
			if (Surf_IsEmpty(*surf) == false)
			{
				node.objectsIndices.push_back(meshesNum);
				++meshesNum;
			}
		}

		node.objectsIndices.shrink_to_fit();

		leavesIndices.push_back(static_cast<int>(nodes.size()) - 1);
	}
	else
	{
		const int nodesSize = static_cast<int>(nodes.size());

		node.plane.distance = sourceNode.plane->dist;
		node.plane.normal = XMFLOAT4(sourceNode.plane->normal[0], sourceNode.plane->normal[1], sourceNode.plane->normal[2], 0.0f);

		// "node" is the reference that potentially is not valid after AddNode call.
		// because "nodes" might be reallocated, which will invalidate this reference
		nodes[addedNodeIndex].children[0] = AddNode(*sourceNode.children[0], meshesNum);
		nodes[addedNodeIndex].children[1] = AddNode(*sourceNode.children[1], meshesNum);
	}

	return addedNodeIndex;
}

Utils::AABB BSPTree::GetClusterAABB(const int clusterIndex) const
{
	Utils::AABB clusterAABB;

	XMVECTOR sseAABBMax = XMLoadFloat4(&clusterAABB.maxVert);
	XMVECTOR sseAABBMin = XMLoadFloat4(&clusterAABB.minVert);

	for (const BSPNode& node : nodes)
	{
		if (node.IsLeaf() == false || node.cluster != clusterIndex)
		{
			continue;
		}

		sseAABBMax = XMVectorMax(sseAABBMax, XMLoadFloat4(&node.aabb.maxVert));
		sseAABBMin = XMVectorMin(sseAABBMin, XMLoadFloat4(&node.aabb.minVert));
	}

	assert(static_cast<bool>(XMVectorGetX(XMVectorEqual(sseAABBMax, sseAABBMin))) == false && "Cluster AABB is invalid");

	XMStoreFloat4(&clusterAABB.maxVert, sseAABBMax);
	XMStoreFloat4(&clusterAABB.minVert, sseAABBMin);
	
	return clusterAABB;
}

std::set<int> BSPTree::GetClustersSet() const
{
	std::set<int> clusters;

	for (const BSPNode& node : nodes)
	{
		if (node.cluster == Const::INVALID_INDEX)
		{
			continue;
		}

		clusters.emplace(node.cluster);
	}

	return clusters;
}

bool BSPTree::IsPointVisibleFromOtherPoint(const XMFLOAT4& p0, const XMFLOAT4& p1) const
{
	const BSPNode& p0Node = GetNodeWithPoint(p0, nodes.front());
	const BSPNode& p1Node = GetNodeWithPoint(p1, nodes.front());

	assert((p0Node.cluster != Const::INVALID_INDEX && p1Node.cluster != Const::INVALID_INDEX) &&
		"Checking visibility for points, that have invalid cluster, is this intentional?");

	std::vector<bool> p0PVS = DecompressClusterVisibility(p0Node.cluster);

	// p1 is out of p0 PVS
	if (p0PVS[p1Node.cluster] == false)
	{
		return false;
	}

	XMVECTOR sseP0 = XMLoadFloat4(&p0);
	XMVECTOR sseP1 = XMLoadFloat4(&p1);

	// Get T that is from p0 to p1 only intersections with t in between matters
	const float maxT = XMVectorGetX(XMVector3Length(sseP1 - sseP0));

	Utils::Ray ray;
	
	ray.origin = p0;
	XMStoreFloat4(&ray.direction, XMVector3Normalize(sseP1 - sseP0));

	const std::vector<SourceStaticObject>& objects = Renderer::Inst().sourceStaticObjects;

	for (const int leafIndex : leavesIndices)
	{
		const BSPNode& leaf = nodes[leafIndex];

		float tAABB = FLT_MAX;

		if (Utils::IsRayIntersectsAABB(ray, leaf.aabb, &tAABB) == false)
		{
			continue;
		}

		// Intersection point is too far
		if (tAABB > maxT)
		{
			continue;
		}

		for (const int objectIndex : leaf.objectsIndices)
		{
			const SourceStaticObject& object = objects[objectIndex];

			if (Utils::IsRayIntersectsAABB(ray, object.aabb, &tAABB) == false)
			{
				continue;
			}

			if (tAABB > maxT)
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

				if (rayTriangleResult.t > maxT)
				{
					continue;
				}

				// Any triangle in between means points are not visible
				return false;

			}
		}
	}

	return true;
}

const BSPNode& BSPTree::GetNodeWithPoint(const XMFLOAT4& point, const BSPNode& node) const
{
	// Only leaves have valid cluster values
	if (node.IsLeaf())
	{
		return node;
	}

	const float pointToPlane = XMVectorGetX(XMVector4Dot(XMLoadFloat4(&point), XMLoadFloat4(&node.plane.normal))) -
		node.plane.distance;

	const int childrenNodeInt = pointToPlane > 0 ? node.children[0] : node.children[1];

	assert(childrenNodeInt != Const::INVALID_INDEX && "GetPointInNode failed. Invalid children node index");

	return GetNodeWithPoint(point, nodes[childrenNodeInt]);
}

const BSPNode& BSPTree::GetNodeWithPoint(const XMFLOAT4& point) const
{
	return GetNodeWithPoint(point, nodes.front());
}

std::vector<bool> BSPTree::DecompressClusterVisibility(int cluster) const
{
	// Original format
	const dvis_t* dVis = reinterpret_cast<const dvis_t*>(clusterVisibility.data());

	const byte* clusterPVS = reinterpret_cast<const byte*>(clusterVisibility.data()) +
		dVis->bitofs[cluster][0];

	std::vector<bool> result(dVis->numclusters, false);

	for (int currentCluster = 0; currentCluster < dVis->numclusters; ++clusterPVS)
	{
		if (*clusterPVS == 0)
		{
			++clusterPVS;
			currentCluster += 8 * (*clusterPVS);
		}
		else
		{
			for (uint8_t bit = 1; bit != 0; bit *= 2, ++currentCluster)
			{
				if (*clusterPVS & bit)
				{
					result[currentCluster] = true;
				}
			}
		}
	}

	return result;
}

bool BSPNode::IsLeaf() const noexcept
{
	return children[0] == Const::INVALID_INDEX && children[1] == Const::INVALID_INDEX;
}
