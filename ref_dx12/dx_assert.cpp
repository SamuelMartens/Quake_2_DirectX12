#include "dx_assert.h"

#include "dx_diagnostics.h"

void DXAssert::AssertHandler(bool expr, const SourceLocation& loc, const char* expression)
{
	Logs::Logf(Logs::Category::Generic, "ASSERTION FAIL: File %s, Func %s, Line %d , Expr %s",
		loc.fileName, loc.functionName, loc.lineNumber, expression);
}
