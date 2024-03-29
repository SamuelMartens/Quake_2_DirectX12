#pragma once

#include <windows.h>
#include <string>
#include <exception>
#include <sstream>
#include <comdef.h>
#include <cstddef>
#include <vector>
#include <functional>
#include <bitset>
#include <filesystem>
#include <optional>
#include <d3d12.h>
#include <DirectXMath.h>

#include "dx_shaderdefinitions.h"

#define DEFINE_SINGLETON(ClassName) \
	private: \
	ClassName() = default; \
	inline static ClassName* g_##ClassName; \
	public: \
	ClassName(const ClassName&) = delete; \
	ClassName& operator=(const ClassName&) = delete; \
	ClassName(ClassName&&) = delete; \
	ClassName& operator=(ClassName&&) = delete; \
	~ClassName() = default;						\
												\
	static ClassName& Inst()	\
	{					\
		if (g_##ClassName == nullptr) { g_##ClassName = new ClassName(); } \
		return *g_##ClassName;\
	}

		
// -=== How to create proper functions with DXMath ===-
// 1) The first three XMVECTOR  parameters should be of type FXMVECTOR
// 2) The fourth XMVECTOR should be of type GXMVECTOR
// 3) The fifth and six parameter should be of type HXMVECTOR
// 4) Any additional XMVECTOR parameters should be of type CXMVECTOR
// 
// 5) For first matrix parameter use FXMMATRIX
// 6) For 2+ use CXMMATRIX
// 
// 7) Use XM_CALLCONV before function name

using namespace DirectX;

struct BSPNode;

namespace Const
{
	//NOTE: values should match Constants.passh

	constexpr int INVALID_OFFSET = -1;
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

	constexpr float EPSILON = 0.0009f;

	/* TYPES */

	struct RayTriangleIntersectionResult
	{
		float t = FLT_MAX;
		// Barycentric coordinates of intersection
		float u = 0.0f;
		float v = 0.0f;
		float w = 0.0f;
	};

	struct BSPNodeRayIntersectionResult
	{
		int staticObjIndex = Const::INVALID_INDEX;
		int triangleIndex = Const::INVALID_INDEX;

		Utils::RayTriangleIntersectionResult rayTriangleIntersection;

		static XMFLOAT4 GetNormal(const BSPNodeRayIntersectionResult& result);
		static XMFLOAT2 GetTexCoord(const BSPNodeRayIntersectionResult& result);
	};

	struct AABB
	{
		// Bounding box, in WORLD space
		XMFLOAT4 minVert = { FLT_MAX, FLT_MAX, FLT_MAX, 1.0f };
		XMFLOAT4 maxVert = { -FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f };
	};

	struct Sphere
	{
		XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
		float radius = 0.0f;
	};

	struct Hemisphere
	{
		XMFLOAT4 origin = { 0.0f, 0.0f, 0.0f, 1.0f };
		XMFLOAT4 normal = { 0.0f, 0.0f, 0.0f, 0.0f };
		float radius = 0.0f;
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
		Exception(const std::string& errorFuncName, const std::string& errorFileName,
			int errorLineNumber, std::optional<HRESULT> hResult) :
			functionName(errorFuncName),
			fileName(errorFileName),
			lineNumber(errorLineNumber),
			errorCode(hResult)
		{};

		std::string ToString() const
		{
			std::string msg;

			if (errorCode.has_value() == true)
			{
				_com_error err(*errorCode);
				msg = err.ErrorMessage();
			}

			std::ostringstream stringStream;
			stringStream << "DX EXCEPTION: File " << fileName << " in function"
				<< functionName << " line " << lineNumber << " with Error: " << msg << std::endl;

			return stringStream.str();
		}

		const char* what() const override
		{
			static char buff[1024];
			strcpy(buff, ToString().c_str());

			return buff;
		}
		// Needed if we handle D3D API failure
		std::optional<HRESULT> errorCode;

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

	// From https://m-peko.github.io/craft-cpp/posts/different-ways-to-define-binary-flags/
	template<typename EnumT>
	class Flags
	{
		static_assert(std::is_enum_v<EnumT>, "Flags can only be specialized for enum types");

		using UnderlyingT = typename std::make_unsigned_t<typename std::underlying_type_t<EnumT>>;

	public:
		Flags& set(EnumT e, bool value = true)
		{
			bits_.set(underlying(e), value);
			return *this;
		}

		Flags& reset(EnumT e)
		{
			set(e, false);
			return *this;
		}

		Flags& reset()
		{
			bits_.reset();
			return *this;
		}

		[[nodiscard]] bool all() const
		{
			return bits_.all();
		}

		[[nodiscard]] bool any() const
		{
			return bits_.any();
		}

		[[nodiscard]] bool none() const
		{
			return bits_.none();
		}

		[[nodiscard]] constexpr std::size_t size() const
		{
			return bits_.size();
		}

		[[nodiscard]] std::size_t count() const
		{
			return bits_.count();
		}

		constexpr bool operator[](EnumT e) const
		{
			return bits_[underlying(e)];
		}

	private:

		static constexpr UnderlyingT underlying(EnumT e)
		{
			return static_cast<UnderlyingT>(e);
		}

	private:
		std::bitset<underlying(EnumT::Count)> bits_;
	};

	struct MouseInput
	{
		XMFLOAT2 position = { 0.0f, 0.0f };
		bool leftButtonClicked = false;
	};


	/* FUNCTIONS */
	void VSCon_Print(const std::string& msg);
	std::string StrToLower(const std::string& str);

	std::wstring StringToWideString(const std::string& s);

	unsigned int Align(unsigned int size, unsigned int alignment);

	// All those Load functions are just wrappers around Quake 2 GL implementation of loads
	// It's ok, cause they don't have any GL specific code.
	void LoadPCX(char* filename, std::byte** image, std::byte** palette,int* width, int* height);
	void LoadWal(char* filename, std::byte** image, int* width, int* height);
	void LoadTGA(char* filename, std::byte** image, int* width, int* height);

	void MakeQuad(XMFLOAT2 posMin, XMFLOAT2 posMax, XMFLOAT2 texMin, XMFLOAT2 texMax, ShDef::Vert::PosTexCoord* outVert);

	std::vector<int> GetIndicesListForTrianglelistFromPolygonPrimitive(int numVertices);

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateNormals(const std::vector<XMFLOAT4>& vertices, 
		const std::vector<int>& indices, std::vector<int>* degenerateTrianglesIndices = nullptr);

	inline bool VecEqual(const XMFLOAT2& v1, const XMFLOAT2& v2)
	{
		return std::abs(v1.x - v2.x) < EPSILON && std::abs(v1.y - v2.y) < EPSILON;
	}

	Plane ConstructPlane(const XMFLOAT4& p0, const XMFLOAT4& p1, const XMFLOAT4& p2);

	XMFLOAT4X4 ConstructV1ToV2RotationMatrix(const XMFLOAT4& v1, const XMFLOAT4& v2);

	[[nodiscard]]
	Utils::AABB ConstructAABB(const std::vector<XMFLOAT4>& vertices);

	[[nodiscard]]
	std::vector<XMFLOAT4> GenerateAABBVertices_LinePrimitveType(const Utils::AABB& aabb);

	[[nodiscard]]
	std::vector<XMFLOAT4> CreateSphere(float radius, int numSubdivisions = 1);

	bool IsAABBBehindPlane(const Plane& plane, const AABB& aabb);

	[[nodiscard]]
	bool IsRayIntersectsAABB(const Utils::Ray& ray, const Utils::AABB& aabb, float* t = nullptr);
	[[nodiscard]]
	bool IsRayIntersectsTriangle(const Utils::Ray& ray, const XMFLOAT4& v0, const XMFLOAT4& v1, const XMFLOAT4& v2, RayTriangleIntersectionResult& result);
	bool FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, Utils::BSPNodeRayIntersectionResult& result);

	float FindDistanceBetweenAABBs(const Utils::AABB& aabb1, const Utils::AABB& aabb2);

	int Find1DIndexFrom2D(XMINT2 sizeIn2D, XMINT2 coordsIn2D);

	[[nodiscard]]
	XMFLOAT2 NomralizeWrapAroundTextrueCoordinates(const XMFLOAT2& texCoords);
	[[nodiscard]]
	XMFLOAT4 TextureBilinearSample(const std::vector<std::byte>& texture, DXGI_FORMAT textureFormat, int width, int height, XMFLOAT2 texCoords);

	bool IsVectorNotZero(const XMFLOAT4& vector);

	bool IsAlmostEqual(float a, float b);

	std::filesystem::path GetAbsolutePathToRootDir();
	std::filesystem::path GenAbsolutePathToFile(const std::string& relativePath);

	std::string ReadFile(const std::filesystem::path& filePath);
	void WriteFile(const std::filesystem::path& filePath, const std::string& content);
}

#define PREVENT_SELF_MOVE_ASSIGN if (this == &other) { return *this; }

// Helper utility converts D3D API failures into exceptions.
#ifndef ThrowIfFailed
#define ThrowIfFailed(func) \
{ \
	HRESULT hr__ = (func); \
	if (FAILED(hr__)) \
	{ \
		Utils::Exception except(#func, __FILE__, __LINE__, hr__); \
		Utils::VSCon_Print(except.what()); \
		throw except;\
	} \
}
#endif

#ifndef ThrowIfFalse
#define ThrowIfFalse(val) \
{ \
	if ((val) == false) \
	{ \
		Utils::Exception except(__func__, __FILE__, __LINE__, std::nullopt); \
		Utils::VSCon_Print(except.what()); \
		throw except;\
	} \
}

#endif
