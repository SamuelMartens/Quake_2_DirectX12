#include "dx_utils.h"

#include <codecvt>

std::wstring DXUtils::StringToWString(const std::string& s)
{
	//setup converter
	typedef std::codecvt_utf8<wchar_t> convert_type;
	static std::wstring_convert<convert_type, wchar_t> converter;

	//use converter (.to_bytes: wstr->str, .from_bytes: str->wstr)
	return converter.from_bytes(s);
}
