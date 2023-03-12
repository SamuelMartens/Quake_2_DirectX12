#include "dx_assert.h"

#include "dx_diagnostics.h"

void DXAssert::AssertHandler(bool expr, const SourceLocation& loc, const char* expression)
{
	Logs::Logf(Logs::Category::Validation, "ASSERTION FAIL: File {}, Func {}, Line {} , Expr {}",
		loc.fileName, loc.functionName, loc.lineNumber, expression);
}
