#pragma once

#include <vector>
#include <memory>
#include <array>
#include <set>

#include "dx_common.h"
#include "dx_utils.h"
#include "dx_glmodel.h"
#include "dx_camera.h"

struct BSPNode
{
	// Dividing plane
	Utils::Plane plane;

	std::array<int, 2> children = { Const::INVALID_INDEX, Const::INVALID_INDEX };

	Utils::AABB aabb;
	// If equal Const::INVALID_INDEX, it is empty
	int cluster = Const::INVALID_INDEX;

	std::vector<int> objectsIndices;

	bool IsLeaf() const;
};

class BSPTree
{
	//#DEBUG
	friend class LightBaker;
	//END

public:

	void Create(const mnode_t& root);
	void InitClusterVisibility(const dvis_t& vis, int visSize);

	std::vector<int> GetVisibleObjectsIndices(const Camera& camera) const;
	const BSPNode& GetNodeWithPoint(const XMFLOAT4& point) const;

	Utils::AABB GetClusterAABB(const int clusterIndex) const;
	std::set<int> GetClustersSet() const;

	[[nodiscard]]
	bool IsPointVisibleFromOtherPoint(const XMFLOAT4& p0, const XMFLOAT4& p1) const;

private:

	int AddNode(const mnode_t& sourceNode, int& meshesNum);

	const BSPNode& GetNodeWithPoint(const XMFLOAT4& point, const BSPNode& node) const;
	std::vector<bool> DecompressClusterVisibility(int cluster) const;

	std::vector<std::byte> clusterVisibility;

	std::vector<BSPNode> nodes;
	std::vector<int> leavesIndices;
};