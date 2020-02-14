#pragma once

#include <windows.h>
#include <string>
#include <exception>
#include <sstream>
#include <comdef.h>

namespace DXUtils
{
	std::wstring StringToWString(const std::string& s);
	
#ifdef WIN32
	void VSCon_Printf(const char *msg, ...);
#endif

	class Exception : public std::exception
	{
	public:

		Exception() = default;
		Exception(HRESULT hr, const std::string& errorFuncName, const std::string& errorFileName,
			int errorLineNumber):
			functionName(errorFuncName),
			fileName(errorFileName),
			lineNumber(errorLineNumber)
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
}

// Helper utility converts D3D API failures into exceptions.
#ifndef ThrowIfFailed
#define ThrowIfFailed(func) \
{ \
	HRESULT hr__ = (func); \
	if (FAILED(hr__)) \
	{ \
		DXUtils::Exception except(hr__, #func, __FILE__, __LINE__); \
		DXUtils::VSCon_Printf("%s", except.what()); \
		throw except;\
	} \
}
#endif