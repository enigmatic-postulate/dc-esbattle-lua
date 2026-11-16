/* shcache.c - qemu TCG plugin: a model of the SH4's operand cache, per game tick and per draw.
 *
 *   qemu-sh4 -plugin ./libshcache.so,tick=0xADDR,draw=0xADDR,end=0xADDR,out=FILE bench_sh4.elf ...
 *
 * Companion to shcount.c (same markers, same one-line-per-frame output). qemu does not model
 * caches, so instruction counts miss what SH4ZAM's cache routines are for. This plugin replays
 * every data access of the game against the Dreamcast's operand cache as KallistiOS configures
 * it: 16 KiB, direct mapped (index = address bits 13..5), 32-byte lines, copy-back with write
 * allocation. Per frame it counts, in this order:
 *   rmiss   loads that miss (the CPU stalls for a line fill from RAM)
 *   wmiss   ordinary stores that miss (copy-back allocates: the line is first read from RAM)
 *   wback   dirty lines evicted (written back to RAM; mostly hidden by the write-back buffer)
 *   alloc   MOVCA.L on a missing line (shz_dcache_alloc_line: the line is claimed, no read)
 *   pref    PREF on a missing line (SHZ_PREFETCH: the fill starts early instead of stalling)
 *   phit    first loads/stores served by a line a PREF brought in
 * PREF needs tests/sh4zam/qemu-10.1.0-sh4-fpu.patch, which makes qemu-user's PREF a non-faulting
 * one-byte load so that its address reaches the plugin (stock qemu drops PREF at translation).
 * A prefetched line is treated as arriving in time (optimistic: on the hardware a PREF issued
 * too late still hides part of the latency). Instruction fetches are not modelled.
 * Optional prof=FILE writes the misses (loads + stores) per instruction, in the block-profile format
 * tests/sh4zam/profile.py reads (so it prints misses per function).
 * Build: cc -shared -fPIC -O2 -I<qemu>/include/qemu $(pkg-config --cflags glib-2.0) shcache.c -o libshcache.so
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

enum { LINES = 16384 / 32 };
enum { K_RMISS, K_WMISS, K_WBACK, K_ALLOC, K_PREF, K_PHIT, K_N };
static const char* NAMES[K_N] = { "rmiss", "wmiss", "wback", "alloc", "pref", "phit" };

typedef struct { uint32_t tag; uint8_t valid, dirty, prefetched; } Line;
static Line g_oc[LINES];
static uint64_t g_cnt[K_N], g_last[K_N], g_tick[K_N];
static uint64_t g_mark[3];
static FILE* g_out;
static uint64_t g_frame;
static const char* g_prof;                      /* optional: misses per instruction (prof=FILE) */
typedef struct { uint64_t pc; int special; uint64_t miss; } Insn;
static Insn** g_insns;
static size_t g_ninsns, g_capinsns;
static uint64_t g_miss_before;

/* One access to the line holding `addr`. kind: 0 load, 1 store, 2 MOVCA.L, 3 PREF. */
static void touch(uint32_t addr, int kind) {
    const uint32_t tag = addr >> 5;
    Line* l = &g_oc[tag & (LINES - 1)];
    if (l->valid && l->tag == tag) {
        if (l->prefetched && kind != 3) { ++g_cnt[K_PHIT]; l->prefetched = 0; }
        if (kind == 1 || kind == 2) l->dirty = 1;
        return;
    }
    if (l->valid && l->dirty) ++g_cnt[K_WBACK];
    l->valid = 1; l->tag = tag; l->dirty = 0; l->prefetched = 0;
    switch (kind) {
    case 0: ++g_cnt[K_RMISS]; break;
    case 1: ++g_cnt[K_WMISS]; l->dirty = 1; break;
    case 2: ++g_cnt[K_ALLOC]; l->dirty = 1; break;
    case 3: ++g_cnt[K_PREF]; l->prefetched = 1; break;
    }
}

static int cacheable(uint64_t a) {
    /* qemu-user addresses: everything is ordinary memory, except the SH4 store-queue area */
    return (a >> 26) != (0xE0000000u >> 26);
}

static void on_mem(unsigned int vcpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void* ud) {
    (void)vcpu;
    if (!cacheable(vaddr)) return;
    const int store = qemu_plugin_mem_is_store(info);
    Insn* in = (Insn*)ud;
    const int special = in->special;              /* 2: MOVCA.L, 3: PREF */
    const unsigned size = 1u << qemu_plugin_mem_size_shift(info);
    const int kind = special ? special : store ? 1 : 0;
    g_miss_before = g_cnt[K_RMISS] + g_cnt[K_WMISS];
    touch((uint32_t)vaddr, kind);
    if (((vaddr & 31) + size) > 32) touch((uint32_t)vaddr + size - 1, kind);
    in->miss += g_cnt[K_RMISS] + g_cnt[K_WMISS] - g_miss_before;
}

static void on_mark(unsigned int vcpu, void* ud) {
    (void)vcpu;
    const int k = (int)(intptr_t)ud;          /* 0: tick begins, 1: draw begins, 2: frame ends */
    uint64_t d[K_N];
    for (int i = 0; i < K_N; ++i) { d[i] = g_cnt[i] - g_last[i]; g_last[i] = g_cnt[i]; }
    if (k == 1) {
        memcpy(g_tick, d, sizeof d);
    } else if (k == 2) {
        fprintf(g_out, "frame %" PRIu64 " tick", ++g_frame);
        for (int i = 0; i < K_N; ++i) fprintf(g_out, " %" PRIu64, g_tick[i]);
        fprintf(g_out, " draw");
        for (int i = 0; i < K_N; ++i) fprintf(g_out, " %" PRIu64, d[i]);
        fprintf(g_out, "\n");
    }
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    (void)id;
    const size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; ++i) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        uint16_t op = 0;
        qemu_plugin_insn_data(insn, &op, sizeof op);
        const uint64_t va = qemu_plugin_insn_vaddr(insn);
        Insn* in = calloc(1, sizeof *in);
        in->pc = va;
        in->special = (op & 0xF0FF) == 0x00C3 ? 2 : (op & 0xF0FF) == 0x0083 ? 3 : 0;   /* MOVCA.L, PREF */
        if (g_ninsns == g_capinsns) { g_capinsns = g_capinsns ? 2 * g_capinsns : 65536; g_insns = realloc(g_insns, g_capinsns * sizeof *g_insns); }
        g_insns[g_ninsns++] = in;
        qemu_plugin_register_vcpu_mem_cb(insn, on_mem, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, in);
        for (int m = 0; m < 3; ++m)
            if (va == g_mark[m]) qemu_plugin_register_vcpu_insn_exec_cb(insn, on_mark, QEMU_PLUGIN_CB_NO_REGS, (void*)(intptr_t)m);
    }
}

static void at_exit(qemu_plugin_id_t id, void* p) {
    (void)id; (void)p;
    if (g_out) fclose(g_out);
    if (g_prof) {                               /* "pc misses 1": the format profile.py reads */
        FILE* f = fopen(g_prof, "w");
        for (size_t i = 0; f && i < g_ninsns; ++i)
            if (g_insns[i]->miss) fprintf(f, "%" PRIx64 " %" PRIu64 " 1\n", g_insns[i]->pc, g_insns[i]->miss);
        if (f) fclose(f);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    (void)info;
    const char* out = "shcache.txt";
    for (int i = 0; i < argc; ++i) {
        if (!strncmp(argv[i], "tick=", 5)) g_mark[0] = strtoull(argv[i] + 5, NULL, 0);
        else if (!strncmp(argv[i], "draw=", 5)) g_mark[1] = strtoull(argv[i] + 5, NULL, 0);
        else if (!strncmp(argv[i], "end=", 4)) g_mark[2] = strtoull(argv[i] + 4, NULL, 0);
        else if (!strncmp(argv[i], "out=", 4)) out = argv[i] + 4;
        else if (!strncmp(argv[i], "prof=", 5)) g_prof = argv[i] + 5;
    }
    g_out = fopen(out, "w");
    if (!g_out) return -1;
    fprintf(g_out, "# classes:");
    for (int i = 0; i < K_N; ++i) fprintf(g_out, " %s", NAMES[i]);
    fprintf(g_out, "\n");
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
