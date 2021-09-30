#include "dx_bsp.h"

#include <algorithm>

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

	XMVECTOR sseAABBMax = XMLoadFloat4(&clusterAABB.bbMax);
	XMVECTOR sseAABBMin = XMLoadFloat4(&clusterAABB.bbMin);

	for (const BSPNode& node : nodes)
	{
		if (node.IsLeaf() == false || node.cluster != clusterIndex)
		{
			continue;
		}

		sseAABBMax = XMVectorMax(sseAABBMax, XMLoadFloat4(&node.aabb.bbMax));
		sseAABBMin = XMVectorMin(sseAABBMin, XMLoadFloat4(&node.aabb.bbMin));
	}

	assert(static_cast<bool>(XMVectorGetX(XMVectorEqual(sseAABBMax, sseAABBMin))) == false && "Cluster AABB is invalid");

	XMStoreFloat4(&clusterAABB.bbMax, sseAABBMax);
	XMStoreFloat4(&clusterAABB.bbMin, sseAABBMin);
	
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
