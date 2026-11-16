// anim_dump.cpp - host test tool: skin matrices of every library clip on a model at a few times,
// printed for tests/battle3d/anim_check.py (C++ anim::sample == tools/battle3d/dmfio.py sample_clip).
//   anim_dump ROMDISK MODEL_NAME > out.txt
#include "../../src/anim.h"
#include "../../src/dmf.h"
#include <stdio.h>
extern const char* g_romdisk;
extern FILE* g_trace;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    g_romdisk = argv[1];
    g_trace = stderr;
    char path[128];
    snprintf(path, sizeof path, "/rd/models/%s.dmf", argv[2]);
    static dmf::Model m;
    anim::Library L;
    if (!dmf::load(path, &m, false) || !anim::load("/rd/anims/rig_medium.dma", &L)) return 1;
    anim::Binding b;
    anim::bind(L, m, &b);
    static dmf::Pose p;
    static r3d::Mat34 skin[dmf::MAX_BONES];
    const float times[] = { 0.0f, 0.137f, 0.5f, 0.9731f, 1.61f, 3.3f };
    for (int k = 0; k < L.num_clips; ++k)
        for (float t : times) {
            anim::sample(L, b, m, k, t, &p);
            dmf::pose_skin(m, p, skin);
            printf("%s %.4f", L.clips[k].name, t);
            for (int i = 0; i < m.h->num_bones; ++i)
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) printf(" %.6g", skin[i].m[r][c]);
            printf("\n");
        }
    return 0;
}
