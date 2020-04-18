#pragma once

#include <string>
#include <vector>
#include <array>

#include "dx_common.h"
#include "../game/q_shared.h"


//#TODO leaving it unfinished. I still have very bad understanding of current rendering
// to make proper decisions on what to leave and what to keep. When I will get something on a screen
// I will understand better how it works, and probably reimplement this
/*** 
	More C++ implementation of gl_model.h
	I skip class members that I don't think is required
***/

// mmodel_t
struct SubModel
{
	XMFLOAT3 mins = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 maxs = { 0.0f, 0.0f, 0.0f };

	XMFLOAT3 origin = { 0.0f, 0.0f, 0.0f };

	float radius = 0.0f;

	float headnode = 0.0f;

	int visleaf = -1;
	int firstface = -1;
	int numfaces = -1;
};

constexpr int VERTEXSIZE = 7;

// glpoly_s
struct Poly
{
	Poly()
	{
		memset(verts, 0, sizeof(verts));
	}

	Poly* next = nullptr;
	Poly* chain = nullptr;

	int numverts = -1;
	float verts[4][VERTEXSIZE]; // variable sized (xyz s1t1 s2t2)
};

struct ModelNode;

// None
struct ModelNodeBase
{
	int contents = 0; // will be a negative contents number
	int visframe = 0; // node needs to be traversed if current

	std::array<float, 6> minmaxs; // for bounding box

	ModelNode* parent = nullptr;
};

 // mnode_s
struct ModelNode : public ModelNodeBase
{
	cplane_t* plane = nullptr;
	
	std::array<ModelNode*, 2> children;

	unsigned short firstsurface = -1;
	unsigned short numsurfaces = -1;
};

 // mleaf_s
struct ModelLeaf : public ModelNodeBase
{
	int cluster = -1;
	int	area = -1;

	//msurface_t	**firstmarksurface;
	int	nummarksurfaces = -1;
};

// model_s
class Model
{
public:

	std::string name;

	int flags = 0;

	// Volume occupied by the model graphics

	XMFLOAT3 mins = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 maxs = { 0.0f, 0.0f, 0.0f };
	float radius = 0.0f;

	// Solid volume for clipping

	bool clipBox = false;
	XMFLOAT3 clipmins = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 clipmaxs = { 0.0f, 0.0f, 0.0f };

	// Brush model
	std::vector<SubModel> submodels;

	std::vector<cplane_t> planes;


};