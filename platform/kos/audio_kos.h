// audio_kos.h - the AICA sound backend's counters for the DIAG report (platform/kos/audio_kos.cpp).
#pragma once
#include <stdint.h>
struct SndKosStats {
    uint32_t poll_gap_ms;      // the longest time between two polls of the music stream since the last call
    uint32_t poll_errors;      // snd_stream_poll() results below 0 since boot
    uint32_t starts;           // tracks started since boot
    uint32_t sfx_played;       // effects started since boot
    uint32_t sound_ram_free;   // bytes (snd_mem_available) after the boot
    int      stream_ms;        // the stream buffer's duration at the playing track's rate (0: no music)
};
SndKosStats snd_kos_stats();   // resets poll_gap_ms
