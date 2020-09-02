#pragma once

#include <string>
#include <variant>

struct DrawCall_Pic
{
	int x = -1;
	int y = -1;
	
	std::string name;
};


struct DrawCall_Char
{
	int x = -1;
	int y = -1;

	int num = -1;
};


using DrawCall_UI_t = std::variant<DrawCall_Pic, DrawCall_Char>;
