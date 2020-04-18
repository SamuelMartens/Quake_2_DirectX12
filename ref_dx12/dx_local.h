#pragma once

#include "dx_glmodel.h"

/*

Non-rendering part of the game, uses some structure that are defined
inside renderer as opaque structure. From my understanding they meant
to be kept in %renderer name%_local.h .This is why this file exists in
DX renderer.

*/

// Outside used mostly for checking is nullptr, and pointer comparison
//#TODO do I need to define something here if what I said before is the case?

struct image_s
{};
