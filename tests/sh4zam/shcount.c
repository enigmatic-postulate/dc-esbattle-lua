/* shcount.c - qemu TCG plugin: SH4 instructions executed per game tick and per draw.
 *
 *   qemu-sh4 -plugin ./libshcount.so,tick=0xADDR,draw=0xADDR,end=0xADDR,out=FILE bench_sh4.elf ...
 *
 * The three addresses are bench_sh4.cpp's bench_mark_tick/draw/end. Every translated block adds
 * its instruction count (total and per class) to a scoreboard with inline ops; the markers take
 * a snapshot, so each output line holds one frame:
 *   frame <n> tick <class counts...> draw <class counts...>
 * Classes: all fdiv fsqrt fsrra fsca ftrv fipr farith fmem branch imem  (see classify()).
 * Optional prof=FILE writes "block-pc instructions executions" per translated block (whole run),
 * for attributing the cost to functions with the ELF symbol table (tests/sh4zam/profile.py).
 * Build: cc -shared -fPIC -O2 -I<qemu>/include/qemu $(pkg-config --cflags glib-2.0) shcount.c -o libshcount.so
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

enum { C_ALL, C_FDIV, C_FSQRT, C_FSRRA, C_FSCA, C_FTRV, C_FIPR, C_FARITH, C_FMEM, C_BRANCH, C_IMEM, C_N };
static const char* NAMES[C_N] = { "all", "fdiv", "fsqrt", "fsrra", "fsca", "ftrv", "fipr", "farith", "fmem", "branch", "imem" };

typedef struct { uint64_t c[C_N]; } Counts;
static struct qemu_plugin_scoreboard* g_sb;
static qemu_plugin_u64 g_ent[C_N];
static uint64_t g_mark[3];
static FILE* g_out;
static Counts g_last, g_tick;
static uint64_t g_frame;
static const char* g_prof;                      /* optional: per-block profile file */
typedef struct { uint64_t pc, n; struct qemu_plugin_scoreboard* sb; } Block;
static Block* g_blocks;
static size_t g_nblocks, g_capblocks;

static int classify(uint16_t op) {
    if ((op & 0xF000) == 0xF000) {
        if ((op & 0xF00F) == 0xF003) return C_FDIV;
        if ((op & 0xF0FF) == 0xF06D) return C_FSQRT;
        if ((op & 0xF0FF) == 0xF07D) return C_FSRRA;
        if ((op & 0xF1FF) == 0xF0FD) return C_FSCA;
        if ((op & 0xF3FF) == 0xF1FD) return C_FTRV;
        if ((op & 0xF0FF) == 0xF0ED) return C_FIPR;
        switch (op & 0xF00F) {
        case 0xF000: case 0xF001: case 0xF002: case 0xF00E: return C_FARITH;
        case 0xF006: case 0xF007: case 0xF008: case 0xF009: case 0xF00A: case 0xF00B: return C_FMEM;
        }
        return -1;
    }
    switch (op & 0xFF00) {
    case 0x8900: case 0x8B00: case 0x8D00: case 0x8F00: return C_BRANCH;        /* bt bf bt/s bf/s */
    }
    if ((op & 0xF000) == 0xA000 || (op & 0xF000) == 0xB000) return C_BRANCH;    /* bra bsr */
    if ((op & 0xF0FF) == 0x402B || (op & 0xF0FF) == 0x400B) return C_BRANCH;    /* jmp jsr */
    if (op == 0x000B || (op & 0xF0FF) == 0x0023 || (op & 0xF0FF) == 0x0003) return C_BRANCH; /* rts braf bsrf */
    switch (op & 0xF000) {
    case 0x1000: case 0x5000: case 0x9000: case 0xD000: return C_IMEM;          /* mov.l disp, mov.w/l @(disp,PC) */
    case 0x2000: if ((op & 0xF) <= 0x6 && (op & 0xF) != 0x3) return C_IMEM; break;
    case 0x6000: if ((op & 0xF) <= 0x2 || ((op & 0xF) >= 0x4 && (op & 0xF) <= 0x6)) return C_IMEM; break;
    case 0x0000: if (((op & 0xF) >= 0x4 && (op & 0xF) <= 0x6) || (op & 0xF) >= 0xC) return C_IMEM; break;
    case 0x8000: if ((op & 0x0F00) == 0x0000 || (op & 0x0F00) == 0x0100 || (op & 0x0F00) == 0x0400 || (op & 0x0F00) == 0x0500) return C_IMEM; break;
    case 0xC000: if ((op & 0x0F00) <= 0x0200 || ((op & 0x0F00) >= 0x0400 && (op & 0x0F00) <= 0x0600)) return C_IMEM; break;
    }
    return -1;
}

static void snapshot(Counts* s) {
    for (int i = 0; i < C_N; ++i) s->c[i] = qemu_plugin_u64_sum(g_ent[i]);
}

static void print_counts(const Counts* d) {
    for (int i = 0; i < C_N; ++i) fprintf(g_out, " %" PRIu64, d->c[i]);
}

static void on_mark(unsigned int vcpu, void* ud) {
    (void)vcpu;
    const int k = (int)(intptr_t)ud;          /* 0: tick begins, 1: draw begins, 2: frame ends */
    Counts now, d;
    snapshot(&now);
    for (int i = 0; i < C_N; ++i) d.c[i] = now.c[i] - g_last.c[i];
    g_last = now;
    if (k == 1) {
        g_tick = d;
    } else if (k == 2) {
        fprintf(g_out, "frame %" PRIu64 " tick", ++g_frame);
        print_counts(&g_tick);
        fprintf(g_out, " draw");
        print_counts(&d);
        fprintf(g_out, "\n");
    }
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    (void)id;
    uint64_t per[C_N] = { 0 };
    const size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; ++i) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        uint16_t op = 0;
        qemu_plugin_insn_data(insn, &op, sizeof op);
        ++per[C_ALL];
        const int c = classify(op);
        if (c >= 0) ++per[c];
        const uint64_t va = qemu_plugin_insn_vaddr(insn);
        for (int m = 0; m < 3; ++m)
            if (va == g_mark[m]) qemu_plugin_register_vcpu_insn_exec_cb(insn, on_mark, QEMU_PLUGIN_CB_NO_REGS, (void*)(intptr_t)m);
    }
    for (int c = 0; c < C_N; ++c)
        if (per[c]) qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64, g_ent[c], per[c]);
    if (g_prof) {
        if (g_nblocks == g_capblocks) { g_capblocks = g_capblocks ? 2 * g_capblocks : 4096; g_blocks = realloc(g_blocks, g_capblocks * sizeof *g_blocks); }
        Block* b = &g_blocks[g_nblocks++];
        b->pc = qemu_plugin_tb_vaddr(tb); b->n = n; b->sb = qemu_plugin_scoreboard_new(sizeof(uint64_t));
        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64, qemu_plugin_scoreboard_u64(b->sb), 1);
    }
}

static void at_exit(qemu_plugin_id_t id, void* p) {
    (void)id; (void)p;
    if (g_out) fclose(g_out);
    if (g_prof) {
        FILE* f = fopen(g_prof, "w");
        for (size_t i = 0; f && i < g_nblocks; ++i)
            fprintf(f, "%" PRIx64 " %" PRIu64 " %" PRIu64 "\n", g_blocks[i].pc, g_blocks[i].n, qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(g_blocks[i].sb)));
        if (f) fclose(f);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    (void)info;
    const char* out = "shcount.txt";
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
    for (int i = 0; i < C_N; ++i) fprintf(g_out, " %s", NAMES[i]);
    fprintf(g_out, "\n");
    g_sb = qemu_plugin_scoreboard_new(sizeof(Counts));
    for (int i = 0; i < C_N; ++i) { g_ent[i].score = g_sb; g_ent[i].offset = i * sizeof(uint64_t); }
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
