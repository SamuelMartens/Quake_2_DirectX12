#pragma once

#include <cstdlib>

// Source: https://www.foonathan.net/2016/09/assertions/
namespace DXAssert
{
	struct SourceLocation
	{
		const char* fileName = nullptr;
		long lineNumber = 0;
		const char* functionName = nullptr;
	};

	void AssertHandler(bool expr, const  SourceLocation& loc, const char* expression);

	inline void Assert(bool expr, const  SourceLocation& loc, const char* expression)
	{
		if (!expr)
		{
			AssertHandler(expr, loc, expression);
			// handle failed assertion
			std::abort();
		}
	}
}

#define _DX_ASSERT_SOURCE_LOCATION DXAssert::SourceLocation{__FILE__, __LINE__, __func__}

#define ENABLE_VALIDATION true

#if (ENABLE_VALIDATION)
#define DX_ASSERT(expr) DXAssert::Assert(expr, _DX_ASSERT_SOURCE_LOCATION, #expr)
#else
#define DX_ASSERT(expr) ((void)0)
#endif