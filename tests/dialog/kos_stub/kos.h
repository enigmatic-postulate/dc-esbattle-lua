// kos.h - a STUB of the KallistiOS declarations platform/kos/main.cpp uses, for a host syntax check only
// (tests/dialog/run.sh: g++ -fsyntax-only). Types and signatures follow KOS 2.x; nothing here is linked or run.
// The real build uses DreamSDK / KOS headers (make under DreamSDK); this only catches typos and type errors.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef uint32_t uint32; typedef uint64_t uint64; typedef uint16_t uint16; typedef uint8_t uint8;
typedef void* pvr_ptr_t;
typedef enum { PVR_LIST_OP_POLY = 0, PVR_LIST_OP_MOD = 1, PVR_LIST_TR_POLY = 2, PVR_LIST_TR_MOD = 3, PVR_LIST_PT_POLY = 4 } pvr_list_t;
typedef struct { uint32_t flags; float x, y, z, u, v; uint32_t argb, oargb; } pvr_vertex_t;
typedef struct { uint32_t cmd, mode1, mode2, mode3, d1, d2, d3, d4; } pvr_poly_hdr_t;
typedef struct { int list_type; struct { int alpha, shading, fog_type, culling, color_clamp, clip_mode, modifier_mode, specular, alpha2, fog_type2, color_clamp2; } gen;
                 struct { int src, dst, src_enable, dst_enable, src2, dst2, src_enable2, dst_enable2; } blend;
                 struct { int color, uv, modifier; } fmt; struct { int comparison, write; } depth;
                 int txr_enable, txr2_enable; struct { int enable, filter, mipmap, mipmap_bias, uv_flip, uv_clamp, alpha, env, width, height, format; pvr_ptr_t base; } txr, txr2; } pvr_poly_cxt_t;
typedef struct { uint32_t opb_sizes[5]; int vertex_buf_size, dma_enabled, fsaa_enabled, autosort_disabled, opb_overflow_count; } pvr_init_params_t;
typedef struct { uint32_t enabled_list_mask, vbl_count; int frame_last_time, reg_last_time, rnd_last_time, buf_last_time; size_t frame_count, vtx_buffer_used, vtx_buffer_used_max; float frame_rate; uint32_t flags; } pvr_stats_t;
typedef struct { uint32_t fmt, w, h; void* data; uint32_t byte_count; } kos_img_t;
typedef struct { uint32_t buttons; int ltrig, rtrig, joyx, joyy, joy2x, joy2y; } cont_state_t;
typedef struct maple_device maple_device_t;
typedef int file_t;
#define PVR_CMD_VERTEX 0xe0000000u
#define PVR_CMD_VERTEX_EOL 0xf0000000u
#define PVR_BINSIZE_0 0
#define PVR_BINSIZE_16 2
#define PVR_TXRFMT_TWIDDLED 0
#define PVR_TXRFMT_RGB565 (1 << 27)
#define PVR_TXRFMT_ARGB1555 (0 << 27)
#define PVR_TXRFMT_ARGB4444 (2 << 27)
#define PVR_TXRFMT_VQ_ENABLE (1 << 30)
#define PVR_FILTER_NONE 0
#define PVR_FILTER_BILINEAR 2
#define PVR_CULLING_NONE 1
#define PVR_CULLING_CW 3
#define PVR_CULLING_CCW 2
#define PVR_BLEND_SRCALPHA 4
#define PVR_BLEND_ONE 1
#define PVR_BLEND_ZERO 0
#define PVR_BLEND_DESTCOLOR 2
#define PVR_FOG_TABLE 0
#define PVR_FOG_DISABLE 2
#define PVR_TXRLOAD_16BPP 2
#define KOS_IMG_FMT_RGB565 1
#define KOS_IMG_FMT_ARGB4444 2
#define KOS_IMG_FMT_ARGB1555 3
#define CONT_DPAD_UP (1 << 4)
#define CONT_DPAD_DOWN (1 << 5)
#define CONT_DPAD_LEFT (1 << 6)
#define CONT_DPAD_RIGHT (1 << 7)
#define CONT_A (1 << 2)
#define CONT_B (1 << 1)
#define CONT_Y (1 << 9)
#define CONT_START (1 << 3)
#define MAPLE_FUNC_CONTROLLER 0x01000000
#define DBG_INFO 6
#define O_RDONLY 0
int pvr_init(const pvr_init_params_t*); int pvr_init_defaults(void);
void pvr_prim(const void*, size_t);
void pvr_poly_compile(pvr_poly_hdr_t*, const pvr_poly_cxt_t*);
void pvr_poly_cxt_txr(pvr_poly_cxt_t*, pvr_list_t, int, int, int, pvr_ptr_t, int);
void pvr_poly_cxt_col(pvr_poly_cxt_t*, pvr_list_t);
void pvr_txr_load_kimg(const kos_img_t*, pvr_ptr_t, uint32_t);
void pvr_txr_load(const void* src, pvr_ptr_t dst, uint32_t count);
pvr_ptr_t pvr_mem_malloc(size_t); size_t pvr_mem_available(void);
void pvr_set_bg_color(float, float, float);
void pvr_fog_table_color(float a, float r, float g, float b); void pvr_fog_table_linear(float start, float end);
int pvr_wait_ready(void); int pvr_scene_begin(void); int pvr_list_begin(pvr_list_t); int pvr_list_finish(void); int pvr_scene_finish(void);
int pvr_get_stats(pvr_stats_t*);
maple_device_t* maple_enum_type(int, uint32_t); void* maple_dev_status(maple_device_t*);
int dbgio_init(void); int dbgio_dev_select(const char*); int dbgio_printf(const char*, ...) __attribute__((format(printf, 1, 2))); void dbgio_flush(void);
void dbglog(int, const char*, ...);
uint64_t timer_ms_gettime64(void); uint64_t timer_us_gettime64(void);
file_t fs_open(const char*, int); ssize_t fs_read(file_t, void*, size_t); int fs_close(file_t);
