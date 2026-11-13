// assets.h - path -> sprite handle cache (was assets.lua).
#pragma once
#include "engine.h"
namespace bt {
// Cached load; `alpha` selects ARGB1555 + translucent list. The first load of a path wins.
eng::Sprite asset_sprite(const char* path, bool alpha = false);
}
