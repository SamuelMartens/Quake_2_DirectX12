#include "dx_utils.h"

#include <codecvt>
#include <stdarg.h> 


std::wstring DXUtils::StringToWString(const std::string& s)
{
	//setup converter
	typedef std::codecvt_utf8<wchar_t> convert_type;
	static std::wstring_convert<convert_type, wchar_t> converter;

	//use converter (.to_bytes: wstr->str, .from_bytes: str->wstr)
	return converter.from_bytes(s);
}


#ifdef WIN32
/*
=================
VSCon_Printf

Print to Visual Studio developers console
=================
*/

void DXUtils::VSCon_Printf(const char *msg, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start(argptr, msg);
	vsprintf_s(text, msg, argptr);
	va_end(argptr);

	OutputDebugStringA(text);
}
#endif