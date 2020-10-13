#pragma once

#include <string>
#include <variant>
#include <cstddef>
#include <vector>

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


struct DrawCall_StretchRaw
{
	int x = 0;
	int y = 0;

	int quadWidth = 0;
	int quadHeight = 0;

	int textureWidth = 0;
	int textureHeight = 0;

	std::vector<std::byte> data;
};

using DrawCall_UI_t = std::variant<DrawCall_Pic, DrawCall_Char, DrawCall_StretchRaw>;
