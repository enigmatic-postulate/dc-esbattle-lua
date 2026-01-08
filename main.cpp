#include <kos.h>
#include <png/png.h>
#include <dirent.h>
#include <dc/video.h>
#include <string>
#include <vector>
extern "C" {
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
}

struct Sprite {
    kos_img_t img;
    pvr_ptr_t tex;
    pvr_poly_hdr_t hdr_nearest;   // ADD
    pvr_poly_hdr_t hdr_bilinear;  // ADD
};


static Sprite g_sprites[16];
static int g_sprite_count = 0;

static char g_dbg_line[128] = "dbg: (empty)";
static volatile int g_dbg_dirty = 1;

// --- Sprite batching by filter (ADD) ---
enum SpriteFilterMode : int { SF_NEAREST = 0, SF_BILINEAR = 1 };

struct SpriteCmd {
    float cx, cy, w, h, a;
};

// 16 sprites max, two filter buckets
static std::vector<SpriteCmd> g_q_nearest[16];
static std::vector<SpriteCmd> g_q_bilinear[16];

static inline void queue_sprite_cmd(int idx, const SpriteCmd& cmd, SpriteFilterMode f) {
    if (idx < 0 || idx >= 16) return;
    if (f == SF_BILINEAR) g_q_bilinear[idx].push_back(cmd);
    else                 g_q_nearest[idx].push_back(cmd);
}

//static inline void emit_sprite_rot_with_hdr(Sprite* s, const SpriteCmd& c, const pvr_poly_hdr_t& hdr) {
//    if (!s || !s->tex) return;
//
//    float hw = c.w * 0.5f, hh = c.h * 0.5f;
//    float cs = fcos(c.a);
//    float sn = fsin(c.a);
//
//    float lx[4] = { -hw,  hw, -hw,  hw };
//    float ly[4] = { -hh, -hh,  hh,  hh };
//
//    float x[4], y[4];
//    for (int i = 0; i < 4; ++i) {
//        x[i] = c.cx + (lx[i] * cs - ly[i] * sn);
//        y[i] = c.cy + (lx[i] * sn + ly[i] * cs);
//    }
//
//    // Submit header once per sprite group
//    pvr_prim(&hdr, sizeof(hdr));
//
//    pvr_vertex_t v;
//    v.argb = 0xFFFFFFFF; v.oargb = 0; v.z = 1.0f;
//
//    // NOTE: your UVs assume full texture 0..1
//    v.flags = PVR_CMD_VERTEX;     v.x = x[0]; v.y = y[0]; v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
//    v.flags = PVR_CMD_VERTEX;     v.x = x[1]; v.y = y[1]; v.u = 1.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
//    v.flags = PVR_CMD_VERTEX;     v.x = x[2]; v.y = y[2]; v.u = 0.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
//    v.flags = PVR_CMD_VERTEX_EOL; v.x = x[3]; v.y = y[3]; v.u = 1.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
//}

static void flush_sprite_batches() {
    // 1) NEAREST pass
    for (int i = 0; i < g_sprite_count; ++i) {
        if (g_q_nearest[i].empty()) continue;
        Sprite* s = &g_sprites[i];
        // submit header once, then all quads
        pvr_prim(&s->hdr_nearest, sizeof(s->hdr_nearest));
        for (const auto& c : g_q_nearest[i]) {
            // emit vertices only (no header inside)
            float hw = c.w * 0.5f, hh = c.h * 0.5f;
            float cs = fcos(c.a), sn = fsin(c.a);
            float lx[4] = { -hw,  hw, -hw,  hw };
            float ly[4] = { -hh, -hh,  hh,  hh };
            float x[4], y[4];
            for (int k = 0; k < 4; ++k) {
                x[k] = c.cx + (lx[k] * cs - ly[k] * sn);
                y[k] = c.cy + (lx[k] * sn + ly[k] * cs);
            }

            pvr_vertex_t v;
            v.argb = 0xFFFFFFFF; v.oargb = 0; v.z = 1.0f;

            v.flags = PVR_CMD_VERTEX;     v.x = x[0]; v.y = y[0]; v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX;     v.x = x[1]; v.y = y[1]; v.u = 1.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX;     v.x = x[2]; v.y = y[2]; v.u = 0.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX_EOL; v.x = x[3]; v.y = y[3]; v.u = 1.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
        }
        g_q_nearest[i].clear();
    }

    // 2) BILINEAR pass
    for (int i = 0; i < g_sprite_count; ++i) {
        if (g_q_bilinear[i].empty()) continue;
        Sprite* s = &g_sprites[i];
        pvr_prim(&s->hdr_bilinear, sizeof(s->hdr_bilinear));
        for (const auto& c : g_q_bilinear[i]) {
            float hw = c.w * 0.5f, hh = c.h * 0.5f;
            float cs = fcos(c.a), sn = fsin(c.a);
            float lx[4] = { -hw,  hw, -hw,  hw };
            float ly[4] = { -hh, -hh,  hh,  hh };
            float x[4], y[4];
            for (int k = 0; k < 4; ++k) {
                x[k] = c.cx + (lx[k] * cs - ly[k] * sn);
                y[k] = c.cy + (lx[k] * sn + ly[k] * cs);
            }

            pvr_vertex_t v;
            v.argb = 0xFFFFFFFF; v.oargb = 0; v.z = 1.0f;

            v.flags = PVR_CMD_VERTEX;     v.x = x[0]; v.y = y[0]; v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX;     v.x = x[1]; v.y = y[1]; v.u = 1.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX;     v.x = x[2]; v.y = y[2]; v.u = 0.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX_EOL; v.x = x[3]; v.y = y[3]; v.u = 1.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
        }
        g_q_bilinear[i].clear();
    }
}


struct InputState {
    uint32 buttons;   // cont_state_t buttons (active-low)
    int joyx;         // -128..127
    int joyy;         // -128..127
} g_in = { 0xFFFFFFFF, 0, 0 };

static cont_state_t* read_controller() {
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return NULL;
    return (cont_state_t*)maple_dev_status(dev);
}

static void poll_input() {
    cont_state_t* st = read_controller();
    if (!st) return;
    g_in.buttons = st->buttons;
    g_in.joyx = st->joyx;
    g_in.joyy = st->joyy;
}
static float g_sprite_x = 320.0f, g_sprite_y = 240.0f, g_sprite_a = 0.0f;

static int l_dbg_set_pose(lua_State* L) {
    g_sprite_x = (float)luaL_checknumber(L, 1);
    g_sprite_y = (float)luaL_checknumber(L, 2);
    g_sprite_a = (float)luaL_checknumber(L, 3);
    return 0;
}
static void draw_rect(float x, float y, float w, float h, uint32 argb) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t v;

    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);     // translucent list (safe overlay)
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    v.argb = argb; v.oargb = 0; v.z = 0.1f;

    v.flags = PVR_CMD_VERTEX;     v.x = x;     v.y = y;     pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX;     v.x = x + w;   v.y = y;     pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX;     v.x = x;     v.y = y + h;   pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL; v.x = x + w;   v.y = y + h;   pvr_prim(&v, sizeof(v));
}

static void draw_input_overlay() {
    // Draw 4 boxes: Up, Down, Left, Right
    // Bright when pressed, dark when not pressed
    auto col = [](bool pressed) { return pressed ? 0xFF00FF00 : 0xFF202020; };

    bool up = (g_in.buttons & CONT_DPAD_UP) == 0;
    bool down = (g_in.buttons & CONT_DPAD_DOWN) == 0;
    bool left = (g_in.buttons & CONT_DPAD_LEFT) == 0;
    bool right = (g_in.buttons & CONT_DPAD_RIGHT) == 0;

    // little HUD in top-left
    draw_rect(20, 20, 20, 20, col(up));
    draw_rect(20, 45, 20, 20, col(down));
    draw_rect(45, 45, 20, 20, col(left));
    draw_rect(70, 45, 20, 20, col(right));

    // Analog indicator: a horizontal bar that shifts with joyx, and vertical with joyy
    float ax = (float)g_in.joyx / 128.0f; // -1..1
    float ay = (float)g_in.joyy / 128.0f;

    // center point
    float cx = 120, cy = 45;
    draw_rect(cx + ax * 30.0f, cy + ay * 30.0f, 6, 6, 0xFFFF0000);

    float fx = fsin(g_sprite_a);
    float fy = -fcos(g_sprite_a);

    // green dot = in front of sprite
    draw_rect(g_sprite_x + fx * 40.0f - 3.0f, g_sprite_y + fy * 40.0f - 3.0f, 6, 6, 0xFF00FF00);

    // red dot = behind sprite
    draw_rect(g_sprite_x - fx * 40.0f - 3.0f, g_sprite_y - fy * 40.0f - 3.0f, 6, 6, 0xFFFF0000);
}


// ----------------- DIR INT --------------------
static void check_file(const char* path) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) {
        dbgio_printf("MISSING: %s\n", path);
    }
    else {
        dbgio_printf("FOUND:   %s\n", path);
        fs_close(f);
    }
}

// ----------------- Helper (C) -----------------
static int l_dbg_print(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    strncpy(g_dbg_line, s, sizeof(g_dbg_line) - 1);
    g_dbg_line[sizeof(g_dbg_line) - 1] = '\0';
    g_dbg_dirty = 1;
    return 0;
}

static void draw_debug_overlay_fb() {
    if (!g_dbg_dirty) return;
    g_dbg_dirty = 0;

    // Move cursor to a fixed spot and print (dbgio supports simple cursor control via \r\n poorly,
    // so we just print a prefix and keep it short).
    dbgio_printf("\n%s\n", g_dbg_line);
}

// ----------------- Sprite (C) -----------------
static int l_sprite_load(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    if (g_sprite_count >= 16) {
        lua_pushnil(L);
        lua_pushstring(L, "sprite limit reached");
        return 2;
    }

    Sprite* s = &g_sprites[g_sprite_count];
    memset(&s->img, 0, sizeof(s->img));
    s->tex = NULL;

    int rc = png_to_img(path, 0, &s->img);
    if (rc < 0 || !s->img.data || s->img.byte_count == 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "png_to_img failed for %s", path);
        return 2;
    }

    s->tex = pvr_mem_malloc(s->img.byte_count);
    if (!s->tex) {
        lua_pushnil(L);
        lua_pushstring(L, "pvr_mem_malloc failed");
        return 2;
    }

    // Upload texture
    pvr_txr_load_kimg(&s->img, s->tex, PVR_TXRLOAD_16BPP);

    // Build two headers for this sprite (ADD)
    pvr_poly_cxt_t cxt;

    // Nearest
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
        PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED,
        s->img.w, s->img.h, s->tex,
        PVR_FILTER_NONE);
    pvr_poly_compile(&s->hdr_nearest, &cxt);

    // Bilinear
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
        PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED,
        s->img.w, s->img.h, s->tex,
        PVR_FILTER_BILINEAR);
    pvr_poly_compile(&s->hdr_bilinear, &cxt);

    // Return handle (1..N)
    g_sprite_count++;
    lua_pushinteger(L, g_sprite_count);
    return 1;
}

static void draw_sprite_rot(Sprite* s, float cx, float cy, float w, float h, float angle_rad) {
    if (!s || !s->tex) return;

    float hw = w * 0.5f, hh = h * 0.5f;
    float cs = fcos(angle_rad);
    float sn = fsin(angle_rad);

    float lx[4] = { -hw,  hw, -hw,  hw };
    float ly[4] = { -hh, -hh,  hh,  hh };

    float x[4], y[4];
    for (int i = 0; i < 4; ++i) {
        x[i] = cx + (lx[i] * cs - ly[i] * sn);
        y[i] = cy + (lx[i] * sn + ly[i] * cs);
    }

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t v;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
        PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED,
        s->img.w, s->img.h, s->tex,
        PVR_FILTER_NONE);

    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    v.argb = 0xFFFFFFFF; v.oargb = 0; v.z = 1.0f;

    v.flags = PVR_CMD_VERTEX;     v.x = x[0]; v.y = y[0]; v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX;     v.x = x[1]; v.y = y[1]; v.u = 1.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX;     v.x = x[2]; v.y = y[2]; v.u = 0.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL; v.x = x[3]; v.y = y[3]; v.u = 1.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
}

static int l_sprite_draw(lua_State* L) {
    int handle = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    float a = (float)luaL_checknumber(L, 6);

    // OPTIONAL 7th arg: filter (0=nearest, 2=bilinear) (ADD)
    int filter = 0;
    if (lua_gettop(L) >= 7 && lua_isnumber(L, 7)) {
        filter = (int)lua_tointeger(L, 7);
    }

    if (handle < 1 || handle > g_sprite_count) return 0;

    int idx = handle - 1;
    SpriteCmd cmd{ x, y, w, h, a };

    SpriteFilterMode f = (filter == 2) ? SF_BILINEAR : SF_NEAREST;
    queue_sprite_cmd(idx, cmd, f);

    return 0;
}

// ----------------- Input (C) -----------------
static bool down_dpad(uint32 mask) {
    // active-low: pressed => bit is 0
    return (g_in.buttons & mask) == 0;
}

static int l_input_down(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    bool pressed = false;

    // map names to Dreamcast inputs
    if (!strcmp(key, "up")) pressed = down_dpad(CONT_DPAD_UP);
    else if (!strcmp(key, "down")) pressed = down_dpad(CONT_DPAD_DOWN);
    else if (!strcmp(key, "left")) pressed = down_dpad(CONT_DPAD_LEFT);
    else if (!strcmp(key, "right")) pressed = down_dpad(CONT_DPAD_RIGHT);

    // mirror WASD to the same meanings (so Lua can ask for "w"/"a"/"s"/"d")
    else if (!strcmp(key, "w")) pressed = down_dpad(CONT_DPAD_UP);
    else if (!strcmp(key, "s")) pressed = down_dpad(CONT_DPAD_DOWN);
    else if (!strcmp(key, "a")) pressed = down_dpad(CONT_DPAD_LEFT);
    else if (!strcmp(key, "d")) pressed = down_dpad(CONT_DPAD_RIGHT);
    else if (!strcmp(key, "fire")) pressed = down_dpad(CONT_A);

    lua_pushboolean(L, pressed);
    return 1;
}

static int l_input_axis(lua_State* L) {
    const char* ax = luaL_checkstring(L, 1);
    float v = 0.0f;

    if (!strcmp(ax, "x")) v = (float)g_in.joyx / 128.0f;
    else if (!strcmp(ax, "y")) v = (float)g_in.joyy / 128.0f;

    // deadzone
    if (v > -0.15f && v < 0.15f) v = 0.0f;

    lua_pushnumber(L, v);
    return 1;
}

// ----------------- Lua driver -----------------
static void register_api(lua_State* L) {
    // sprite table
    lua_newtable(L);
    lua_pushcfunction(L, l_sprite_load); lua_setfield(L, -2, "load");
    lua_pushcfunction(L, l_sprite_draw); lua_setfield(L, -2, "draw");
    lua_setglobal(L, "sprite");

    // input table
    lua_newtable(L);
    lua_pushcfunction(L, l_input_down); lua_setfield(L, -2, "down");
    lua_pushcfunction(L, l_input_axis); lua_setfield(L, -2, "axis");
    lua_setglobal(L, "input");

    //debug table
    lua_newtable(L);
    lua_pushcfunction(L, l_dbg_print);     lua_setfield(L, -2, "print");
    lua_pushcfunction(L, l_dbg_set_pose);  lua_setfield(L, -2, "set_pose");
    lua_setglobal(L, "dbg");

}

static void call_lua_fn_1f(lua_State* L, const char* fn, float arg) {
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, arg);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        dbgio_printf("Lua error in %s: %s\n", fn, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void call_lua_fn_0(lua_State* L, const char* fn) {
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        dbgio_printf("Lua error in %s: %s\n", fn, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void set_lua_romdisk_path(lua_State* L) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    const char* cur = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Prepend romdisk lookup
    // Lua 5.4 accepts ';' separated path templates
    std::string newPath = "/rd/?.lua;/rd/?/init.lua;";
    if (cur) newPath += cur;

    lua_pushlstring(L, newPath.c_str(), newPath.size());
    lua_setfield(L, -2, "path");

    // Optional: disable C module loading noise
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");

    lua_pop(L, 1); // pop package

    dbglog(DBG_INFO, "Lua package.path set to: %s\n", newPath.c_str());
}


int main(int, char**) {
    pvr_init_defaults();

    dbgio_init();
    dbgio_dev_select("scif");
    dbgio_printf("BOOT\n");
    dbglog(DBG_INFO, "=== DREAMCAST BOOT ===\n");


    check_file("/rd/boot.lua");
    check_file("/rd/game/init.lua");
    check_file("/rd/battle/battle.lua");
    check_file("/rd/es_sprite_64.png");

    thd_sleep(3000);

    // ---- Lua init ----
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    set_lua_romdisk_path(L);
    register_api(L);

    // Optional: prove dbg works before running the script
    dbglog(DBG_INFO, "About to load /rd/boot.lua...\n");

    if (luaL_dofile(L, "/rd/boot.lua") != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        dbglog(DBG_ERROR, "Lua load failed: %s\n", err ? err : "(nil)");
        lua_pop(L, 1);

        // freeze so you see it in Flycast logs
        while (1) { thd_pass(); }
    }

    // Ensure Lua can require() from the romdisk
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    const char* cur = lua_tostring(L, -1);
    lua_pop(L, 1);

    std::string newPath = "/rd/?.lua;/rd/?/init.lua;";
    newPath += (cur ? cur : "");

    lua_pushstring(L, newPath.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1); // pop package


    dbgio_printf("Lua loaded OK.\n");

    uint32 last = timer_ms_gettime64();

    while (1) {
        poll_input();

        uint32 now = timer_ms_gettime64();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        call_lua_fn_1f(L, "update", dt);

        pvr_wait_ready();
        pvr_scene_begin();

        pvr_list_begin(PVR_LIST_OP_POLY);
        call_lua_fn_0(L, "draw");
        flush_sprite_batches();          // ADD
        pvr_list_finish();

        // overlay primitives
        pvr_list_begin(PVR_LIST_TR_POLY);
        draw_input_overlay();
        pvr_list_finish();

        pvr_scene_finish();
        vid_waitvbl();



    }

}
