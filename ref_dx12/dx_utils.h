#pragma once

#include <windows.h>
#include <string>
#include <exception>
#include <sstream>
#include <comdef.h>
#include <cstddef>

namespace Utils
{
#ifdef WIN32
	void VSCon_Printf(const char *msg, ...);
#endif

	class Exception : public std::exception
	{
	public:

		Exception() = default;
		Exception(HRESULT hResult, const std::string& errorFuncName, const std::string& errorFileName,
			int errorLineNumber):
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

	// All those Load functions are just wrappers around Quake 2 GL implementation of loads
	// It's ok, cause they don't have any GL specific code.
	void LoadPCX(char* filename, std::byte** image, std::byte** palette,int* width, int* height);
	void LoadWal(char* filename, std::byte** image, int* width, int* height);
	void LoadTGA(char* filename, std::byte** image, int* width, int* height);
}

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