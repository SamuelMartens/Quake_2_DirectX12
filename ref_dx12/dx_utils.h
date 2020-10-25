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

using namespace DirectX;

namespace Const
{
	constexpr int INVALID_OFFSET = -1;
	constexpr int INVALID_INDEX = -1;
	constexpr int INVALID_SIZE = -1;
};


namespace Utils
{
	/* CONSTANTS */

	// I don't think this 3 constants deserve their own Math namespace for now.
	// However if I will have more math related stuff I should definitely make separate
	// namespace
	extern const XMFLOAT4 axisX;
	extern const XMFLOAT4 axisY;
	extern const XMFLOAT4 axisZ;

	/* CLASSES */

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

	std::wstring StringToWideString(const std::string& s);

	unsigned int Align(unsigned int size, unsigned int alignment);

	// All those Load functions are just wrappers around Quake 2 GL implementation of loads
	// It's ok, cause they don't have any GL specific code.
	void LoadPCX(char* filename, std::byte** image, std::byte** palette,int* width, int* height);
	void LoadWal(char* filename, std::byte** image, int* width, int* height);
	void LoadTGA(char* filename, std::byte** image, int* width, int* height);

	void MakeQuad(XMFLOAT2 posMin, XMFLOAT2 posMax, XMFLOAT2 texMin, XMFLOAT2 texMax, ShDef::Vert::PosTexCoord* outVert);

	std::vector<uint32_t> GetIndicesListForTrianglelistFromPolygonPrimitive(int numVertices);

	inline bool VecEqual(const XMFLOAT2& v1, const XMFLOAT2& v2)
	{
		constexpr float epsilon = 0.00001f;

		return std::abs(v1.x - v2.x) < epsilon && std::abs(v1.y - v2.y) < epsilon;
	}
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