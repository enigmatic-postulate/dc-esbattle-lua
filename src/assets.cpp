// assets.cpp - port of assets.lua
#include "assets.h"
#include <string.h>
#include <stdio.h>

namespace bt {

namespace {
struct Entry { const char* path; eng::Sprite spr; };
Entry g_cache[16];   // engine supports at most 16 sprites
int   g_count = 0;
}

eng::Sprite asset_sprite(const char* path) {
    for (int i = 0; i < g_count; ++i)
        if (strcmp(g_cache[i].path, path) == 0) return g_cache[i].spr;

    eng::Sprite s = eng::sprite_load(path);
    if (!s) {
        char msg[128];
        snprintf(msg, sizeof msg, "sprite.load FAILED: %s", path);
        eng::dbg_print(msg);
        return 0;                       // not cached, like the Lua version
    }
    if (g_count < 16) g_cache[g_count++] = Entry{ path, s };  // path must be a string literal
    return s;
}

} // namespace bt
