// adpcm.h - the AICA's 4-bit Yamaha ADPCM decoder and the .adp header (slice 3D-7, docs/NEXT_SLICE_SOUND_SAVE.md
// 6.2). The reference is tools/audio/adpcm.py; tests/sound_save checks the two agree sample for sample. On the
// Dreamcast the chip decodes: this code runs on the host (parity test, the --audio-wav mixdown) and reads headers.
#pragma once
#include <stdint.h>
#include <string.h>
namespace adpcm {

struct State { int x, step; };                       // at key-on: { 0, 127 }
const State RESET = { 0, 127 };

// One code (0..15): the chip's leak, the difference, the step adaptation. Integer only; `/` truncates toward zero
// (as the chip and the Python reference do).
inline int decode(State& s, unsigned code) {
    static const int SCALE[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };
    const int mag = (int)(code & 7u);
    int diff = ((2 * mag + 1) * s.step) >> 3;
    if (diff > 32767) diff = 32767;
    int x = s.x * 254 / 256;
    x = (code & 8u) ? x - diff : x + diff;
    s.x = x < -32768 ? -32768 : (x > 32767 ? 32767 : x);
    const int st = (SCALE[mag] * s.step) >> 8;
    s.step = st < 127 ? 127 : (st > 24576 ? 24576 : st);
    return s.x;
}
// Sample i of a code stream: two codes per byte, the low nibble first.
inline unsigned code_at(const uint8_t* data, uint32_t i) { return (i & 1u) ? (unsigned)(data[i >> 1] >> 4) : (unsigned)(data[i >> 1] & 15u); }

const int HEADER_BYTES = 32;
enum Format : uint16_t { FMT_ADPCM = 0, FMT_PCM8 = 1 };
struct Header { uint32_t rate, samples, loop_start, loop_end; uint16_t format, channels; uint32_t crc; };

inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
// The 32 header bytes -> h. false if it is not an .adp header this code plays (magic, mono, a known format, sane loop).
inline bool parse_header(const uint8_t* p, Header* h) {
    if (memcmp(p, "ADP1", 4) != 0) return false;
    h->rate = rd32(p + 4); h->samples = rd32(p + 8); h->loop_start = rd32(p + 12); h->loop_end = rd32(p + 16);
    h->format = (uint16_t)(p[20] | p[21] << 8); h->channels = (uint16_t)(p[22] | p[23] << 8); h->crc = rd32(p + 24);
    return h->channels == 1 && h->format <= FMT_PCM8 && h->rate >= 4000 && h->rate <= 48000 && h->samples > 0 &&
           h->loop_start <= h->loop_end && h->loop_end <= h->samples;
}
inline uint32_t data_bytes(const Header& h) { const uint32_t n = h.format == FMT_ADPCM ? (h.samples + 1) / 2 : h.samples; return (n + 31u) & ~31u; }

}
