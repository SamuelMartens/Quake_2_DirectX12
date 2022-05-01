#pragma once

#include <windows.h>
#include <string>
#include <exception>
#include <sstream>
#include <comdef.h>
#include <cstddef>
#include <vector>
#include <functional>
#include <DirectXMath.h>

#include "dx_shaderdefinitions.h"

#define DEFINE_SINGLETON(ClassName) \
	private: \
	ClassName() = default; \
	public: \
	ClassName(const ClassName&) = delete; \
	ClassName& operator=(const ClassName&) = delete; \
	ClassName(ClassName&&) = delete; \
	ClassName& operator=(ClassName&&) = delete; \
	~ClassName() = default;						\
												\
	static ClassName& Inst()	\
	{					\
		static ClassName* obj = nullptr;	\
		if (obj == nullptr) { obj = new ClassName(); } \
		return *obj;\
	}

		


using namespace DirectX;

struct BSPNode;

namespace Const
{
	constexpr int INVALID_OFFSET = -1;
	//#DEBUG write comment that it's used in shader
	constexpr int INVALID_INDEX = -1;
	constexpr int INVALID_SIZE = -1;
	constexpr int INVALID_HASHED_NAME = -1;
};


namespace Utils
{
	/* CONSTANTS */

	// I don't think this 3 constants deserve their own Math namespace for now.
	// However if I will have more math related stuff I should definitely make separate
	// namespace
	extern const XMFLOAT4 AXIS_X;
	extern const XMFLOAT4 AXIS_Y;
	extern const XMFLOAT4 AXIS_Z;

	constexpr float EPSILON = 0.00001f;

	/* TYPES */

	struct RayTriangleIntersectionResult
	{
		float t = FLT_MAX;
		// Barycentric coordinates of intersection
		float u = 0;
		float v = 0;
		float w = 0;
	};

	struct BSPNodeRayIntersectionResult
	{
		int staticObjIndex = Const::INVALID_INDEX;
		int triangleIndex = Const::INVALID_INDEX;

		Utils::RayTriangleIntersectionResult rayTriangleIntersection;

		static XMFLOAT4 GetNormal(const BSPNodeRayIntersectionResult& result);
	};

	struct AABB
	{
		// Bounding box, in WORLD space
		XMFLOAT4 minVert = { FLT_MAX, FLT_MAX, FLT_MAX, 1.0f };
		XMFLOAT4 maxVert = { -FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f };
	};

	struct Ray
	{
		XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
		XMFLOAT4 direction = { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	struct Plane
	{
		XMFLOAT4 normal = { 0.0f, 0.0f, 0.0f, 0.0f };
		float distance = 0.0f;
	};

	template<typename T>
	struct PointerAsRef
	{
		// T is usually a pointer. So 'pointer' is actually pointer to a pointer
		T* pointer = nullptr;
	};

	class Exception : public std::exception
	{
	public:

		Exception() = default;
		Exception(HRESULT hResult, const std::string& errorFuncName, const std::string& errorFileName,
			int errorLineNumber) :
			functionName(errorFuncName),
			fileName(errorFileName),
			lineNumber(errorLineNumber),
			errorCode(hResult)
		{};

		std::string ToString() const
		{
			_com_error err(errorCode);
			std::string msg = err.ErrorMessage();

			std::ostringstream stringStream;
			stringStream << "DX FAILURE: File " << fileName << " in function"
				<< functionName << " line " << lineNumber << " with Error: " << msg << std::endl;

			return stringStream.str();
		}

		const char* what() const override
		{
			static char buff[1024];
			strcpy(buff, ToString().c_str());

			return buff;
		}

		HRESULT errorCode = S_OK;
		std::string functionName;
		std::string fileName;
		int lineNumber;
	};

	template<typename T>
	using RAIIGuardFunc_t = void(T::*)();

	template<typename T, RAIIGuardFunc_t<T> onCreate, RAIIGuardFunc_t<T> onDestroy>
	class RAIIGuard
	{
	public:

		explicit RAIIGuard(T& obj) :
			objRef(obj)
		{
			static_assert(onCreate != nullptr || onDestroy != nullptr,
				"RAIIGuard should have at least one callback");

			if constexpr (onCreate != nullptr)
			{
				std::invoke(onCreate, objRef);
			}
		};

		RAIIGuard(const RAIIGuard&) = delete;
		RAIIGuard& operator=(const RAIIGuard&) = delete;

		RAIIGuard(RAIIGuard&&) = delete;
		RAIIGuard& operator=(RAIIGuard&&) = delete;

		~RAIIGuard()
		{
			if constexpr (onDestroy != nullptr)
			{
				std::invoke(onDestroy, objRef);
			}
		}

	private:

		T& objRef;
	};

	/* FUNCTIONS */

	void Sprintf(char* dest, int size, const char* fmt, ...);
	void VSCon_Printf(const char *msg, ...);
	std::string StrToLower(const std::string& str);

	std::wstring StringToWideString(const std::string& s);

	unsigned int Align(unsigned int size, unsigned int alignment);

	// All those Load functions are just wrappers around Quake 2 GL implementation of loads
	// It's ok, cause they don't have any GL specific code.
	void LoadPCX(char* filename, std::byte** image, std::byte** palette,int* width, int* height);
	void LoadWal(char* filename, std::byte** image, int* width, int* height);
	void LoadTGA(char* filename, std::byte** image, int* width, int* height);

	void MakeQuad(XMFLOAT2 posMin, XMFLOAT2 posMax, XMFLOAT2 texMin, XMFLOAT2 texMax, ShDef::Vert::PosTexCoord* outVert);

	std::vector<uint32_t> GetIndicesListForTrianglelistFromPolygonPrimitive(int numVertices);

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateNormals(const std::vector<XMFLOAT4>& vertices, const std::vector<uint32_t>& indices);

	inline bool VecEqual(const XMFLOAT2& v1, const XMFLOAT2& v2)
	{
		return std::abs(v1.x - v2.x) < EPSILON && std::abs(v1.y - v2.y) < EPSILON;
	}

	Plane ConstructPlane(const XMFLOAT4& p0, const XMFLOAT4& p1, const XMFLOAT4& p2);
	bool IsAABBBehindPlane(const Plane& plane, const AABB& aabb);

	XMFLOAT4X4 ConstructV1ToV2RotationMatrix(const XMFLOAT4& v1, const XMFLOAT4& v2);

	[[nodiscard]]
	Utils::AABB ConstructAABB(const std::vector<XMFLOAT4>& vertices);

	[[nodiscard]]
	bool IsRayIntersectsAABB(const Utils::Ray& ray, const Utils::AABB& aabb, float* t = nullptr);
	[[nodiscard]]
	bool IsRayIntersectsTriangle(const Utils::Ray& ray, const XMFLOAT4& v0, const XMFLOAT4& v1, const XMFLOAT4& v2, RayTriangleIntersectionResult& result);
	bool FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, Utils::BSPNodeRayIntersectionResult& result);

	bool IsVectorNotZero(const XMFLOAT4& vector);

	bool IsAlmostEqual(float a, float b);

	std::vector<XMFLOAT4> CreateSphere(float radius, int numSubdivisions = 1);
}

#define PREVENT_SELF_MOVE_ASSIGN if (this == &other) { return *this; }

// Helper utility converts D3D API failures into exceptions.
#ifndef ThrowIfFailed
#define ThrowIfFailed(func) \
{ \
	HRESULT hr__ = (func); \
	if (FAILED(hr__)) \
	{ \
		Utils::Exception except(hr__, #func, __FILE__, __LINE__); \
		Utils::VSCon_Printf("%s", except.what()); \
		throw except;\
	} \
}
#endif