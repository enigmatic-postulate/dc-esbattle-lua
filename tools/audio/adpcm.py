"""adpcm.py - the reference Yamaha (AICA) 4-bit ADPCM codec and the .adp container (slice 3D-7,
docs/NEXT_SLICE_SOUND_SAVE.md 6.2 and 13.2). Python 3 standard library only.

The decoder is the AICA's as KOS models it (utils/wav2adpcm/wav2adpcm.c `adpcm2pcm` of KOS 2.3.0, the same
arithmetic as MAME's aica.cpp): per 4-bit code,
    x    = trunc(x * 254 / 256)                       # the chip's leak ("high pass")
    x    = clamp(x +- min(((2 * mag + 1) * step) >> 3, 32767), -32768, 32767)
    step = clamp((STEP_SCALE[mag] * step) >> 8, 127, 24576)
starting from x = 0, step = 127 at key-on. A byte holds two codes, the LOW nibble first.
src/adpcm.h is the same decoder in C++; tests/sound_save checks they agree sample for sample.

The encoder predicts with that leaky decoder (KOS's wav2adpcm predicts without the leak, so its output drifts
from what the chip plays).

Loop seams ("reset seams"). The chip restarts its decoder (x = 0, step = 127) at key-on only, and a stream is one
continuous code sequence, so the first pass of a loop starts from the reset state and every later pass from
whatever state the end of the loop left. A loop is seamless exactly when those two states are equal. encode()
makes them equal by construction: the last samples before `loop_end` (and before `loop_start`, when it is not 0)
are shaped down to silence over a few milliseconds and the final codes are chosen by search so that the decoder
lands exactly on (0, 127). Every pass then decodes to the same samples, on any decoder, whether or not it resets at
the loop point. The cost is a dip of about 6 ms at the seam (make_audio.py reports when the source was not already
quiet there); put loop points in a breath of the music.
"""
import struct
import zlib

STEP_SCALE = (230, 230, 230, 230, 307, 409, 512, 614)
STEP_MIN, STEP_MAX = 127, 24576
RESET = (0, 127)


def _leak(x):
    return (x * 254) // 256 if x >= 0 else -((-x * 254) // 256)      # C division truncates toward zero


def step_code(x, step, code):
    """One decoder step: (x, step), code 0..15 -> (x, step)."""
    mag = code & 7
    diff = ((2 * mag + 1) * step) >> 3
    if diff > 32767:
        diff = 32767
    x = _leak(x)
    x = x - diff if code & 8 else x + diff
    x = -32768 if x < -32768 else (32767 if x > 32767 else x)
    step = (STEP_SCALE[mag] * step) >> 8
    step = STEP_MIN if step < STEP_MIN else (STEP_MAX if step > STEP_MAX else step)
    return x, step


def decode(data, samples, state=RESET):
    """ADPCM bytes -> (list of int16 samples, final state)."""
    x, step = state
    out = [0] * samples
    for i in range(samples):
        b = data[i >> 1]
        x, step = step_code(x, step, (b >> 4) if (i & 1) else (b & 15))
        out[i] = x
    return out, (x, step)


def _encode_run(pcm, state):
    """Greedy encoder: per sample the code whose decoded value is nearest the target. -> (codes, state)."""
    x, step = state
    codes = [0] * len(pcm)
    scale = STEP_SCALE
    for i, target in enumerate(pcm):
        lx = (x * 254) // 256 if x >= 0 else -((-x * 254) // 256)
        d = target - lx
        ad = -d if d < 0 else d
        mag = (ad << 2) // step                 # (2 mag + 1) step / 8 brackets |d|
        if mag > 7:
            mag = 7
        elif mag > 0:                           # the lower neighbour may be nearer
            hi = ((2 * mag + 1) * step) >> 3
            lo = ((2 * mag - 1) * step) >> 3
            if ad - lo < hi - ad:
                mag -= 1
        diff = ((2 * mag + 1) * step) >> 3
        if diff > 32767:
            diff = 32767
        if d < 0:
            x = lx - diff
            if x < -32768:
                x = -32768
            codes[i] = mag | 8
        else:
            x = lx + diff
            if x > 32767:
                x = 32767
            codes[i] = mag
        step = (scale[mag] * step) >> 8
        if step < 127:
            step = 127
        elif step > 24576:
            step = 24576
    return codes, (x, step)


SEAM_FADE = 66          # samples shaped down to silence before a seam (3 ms at 22,050 Hz)
SEAM_REST = 60          # samples of silence after the fade: the step size decays to its minimum (x 0.9 a sample)
SEAM_LAND = 24          # the last codes before a seam are searched to land on (0, 127)


def _land(state, n):
    """n codes that take `state` (step already 127) to exactly (0, 127); None if there is no such sequence."""
    x0, step = state
    if step != STEP_MIN:
        return None
    # breadth-first over x; codes with magnitude 0..3 keep the step at its minimum
    moves = [c for c in range(16) if (c & 7) < 4]
    frontier = {x0: []}
    for k in range(n):
        nxt = {}
        for x, path in frontier.items():
            for c in moves:
                nx, _ = step_code(x, STEP_MIN, c)
                if abs(nx) <= 4096 and nx not in nxt:
                    nxt[nx] = path + [c]
        frontier = nxt
    return frontier.get(0)


def _seam(pcm, end):
    """Shapes the samples before `end` down to silence, in place (the caller encodes them and lands)."""
    a = max(0, end - SEAM_LAND - SEAM_REST - SEAM_FADE)
    b = max(0, end - SEAM_LAND - SEAM_REST)
    for i in range(a, b):
        t = (b - i) / float(b - a + 1)
        pcm[i] = int(round(pcm[i] * t * t))
    for i in range(b, end):
        pcm[i] = 0


def seam_fade_in(pcm, start, n=SEAM_FADE):
    for i in range(start, min(len(pcm), start + n)):
        t = (i - start + 1) / float(n + 1)
        pcm[i] = int(round(pcm[i] * t * t))


def encode(pcm, loop_start=None, loop_end=None):
    """int16 samples -> (bytes, info). With a loop: reset seams at loop_start (if > 0) and loop_end (see the module
    docstring); the samples after loop_end are dropped. info: samples, loop_start, loop_end, seam_loss (the largest
    |sample| the seam shaping removed, 0 if the source was silent there)."""
    pcm = [int(v) for v in pcm]
    n = len(pcm)
    loss = 0
    if loop_end is None:
        loop_start = loop_end = n
        segs = [(0, n, False)]
    else:
        if not (0 <= loop_start < loop_end <= n):
            raise ValueError('bad loop points')
        n = loop_end
        pcm = pcm[:n]
        segs = ([(0, loop_start, True)] if loop_start > 0 else []) + [(loop_start, loop_end, True)]
    if n & 1:
        raise ValueError('an odd number of samples (two codes share a byte)')
    codes = []
    state = RESET
    for a, b, seam in segs:
        if seam:
            if b - a < 4 * (SEAM_FADE + SEAM_REST + SEAM_LAND):
                raise ValueError('loop segment too short for a seam')
            lo = max(a, b - SEAM_LAND - SEAM_REST - SEAM_FADE)
            before = max(abs(v) for v in pcm[lo:b])
            _seam(pcm, b)
            if a > 0 or loop_start == 0:
                seam_fade_in(pcm, a)
            loss = max(loss, before)
            body, st = _encode_run(pcm[a:b - SEAM_LAND], state if a == 0 and state == RESET else RESET)
            land = _land(st, SEAM_LAND)
            if land is None:
                raise ValueError('seam: the decoder did not settle (step %d, x %d) - is the seam region loud?' % (st[1], st[0]))
            codes += body + land
            state = RESET
        else:
            body, state = _encode_run(pcm[a:b], state)
            codes += body
    data = bytearray((len(codes) + 1) // 2)
    for i, c in enumerate(codes):
        data[i >> 1] |= c << 4 if (i & 1) else c
    return bytes(data), {'samples': n, 'loop_start': loop_start, 'loop_end': loop_end, 'seam_loss': loss}


# ---------------------------------------------------------------------------- .adp container (plan 6.2)
MAGIC = b'ADP1'
HEADER = 32
FMT_ADPCM, FMT_PCM8 = 0, 1


def pack(data, rate, samples, loop_start, loop_end, fmt=FMT_ADPCM):
    pad = (-len(data)) % 32
    data = data + bytes(pad)                    # ADPCM code 0 / PCM8 0: never played (beyond `samples`)
    hdr = MAGIC + struct.pack('<IIIIHHII', rate, samples, loop_start, loop_end, fmt, 1, zlib.crc32(data) & 0xFFFFFFFF, 0)
    assert len(hdr) == HEADER
    return hdr + data


def unpack(blob):
    if len(blob) < HEADER or blob[:4] != MAGIC:
        raise ValueError('not an .adp file')
    rate, samples, ls, le, fmt, ch, crc, _ = struct.unpack('<IIIIHHII', blob[4:HEADER])
    data = blob[HEADER:]
    if zlib.crc32(data) & 0xFFFFFFFF != crc:
        raise ValueError('.adp: CRC mismatch')
    need = (samples + 1) // 2 if fmt == FMT_ADPCM else samples
    if ch != 1 or fmt not in (FMT_ADPCM, FMT_PCM8) or len(data) < need or len(data) % 32 or not (ls <= le <= samples):
        raise ValueError('.adp: bad header')
    return {'rate': rate, 'samples': samples, 'loop_start': ls, 'loop_end': le, 'format': fmt, 'data': data}


def decode_adp(blob):
    """-> (info, int16 samples of one pass, state at loop_start, state at loop_end)."""
    a = unpack(blob)
    if a['format'] == FMT_PCM8:
        return a, [(b - 256 if b > 127 else b) << 8 for b in a['data'][:a['samples']]], RESET, RESET
    x, step = RESET
    out = []
    at_start = RESET
    d = a['data']
    for i in range(a['samples']):
        if i == a['loop_start']:
            at_start = (x, step)
        b = d[i >> 1]
        x, step = step_code(x, step, (b >> 4) if (i & 1) else (b & 15))
        out.append(x)
    return a, out, at_start, (x, step)


if __name__ == '__main__':
    import sys
    for p in sys.argv[1:]:
        info, pcm, s0, s1 = decode_adp(open(p, 'rb').read())
        print('%s rate=%d samples=%d loop=%d..%d fmt=%d pcm_crc=%08x seam=%s' % (
            p, info['rate'], info['samples'], info['loop_start'], info['loop_end'], info['format'],
            zlib.crc32(struct.pack('<%dh' % len(pcm), *pcm)) & 0xFFFFFFFF,
            'none' if info['loop_start'] == info['samples'] else ('ok' if s0 == s1 == RESET else 'MISMATCH %r %r' % (s0, s1))))
