// assets.h - port of assets.lua (path -> sprite handle cache).
#pragma once
#include "engine.h"
namespace bt {
// Cached load. Unlike the Lua build (where presentation.lua, ui.lua and
// input_probe.lua each called sprite.load("/rd/plasma.png") directly and uploaded
// 4 copies of the same texture into 4 of the 16 sprite slots), every module goes
// through this cache.
eng::Sprite asset_sprite(const char* path);
}
