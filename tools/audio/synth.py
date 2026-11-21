"""synth.py - a small additive synthesizer and sequencer for the game's music (slice 3D-7). Python 3 + NumPy.
music.py writes the tracks with it; make_standins.py renders them to .adp. Deterministic: every noise source is a
seeded numpy RandomState owned by the Song.

Instruments: whistle, flute, brass, strings, accordion, pluck, bass, bell. Drums: k (kick), s (snare), t (frame drum /
tom), r (rim). A Song counts time in beats at its own tempo; note names are C4, F#5, Bb3 (A4 = 440 Hz).
"""
import numpy as np

R = 22050
_NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def hz(name):
    n = _NOTE[name[0]]
    i = 1
    if name[i] == '#':
        n += 1
        i += 1
    elif name[i] == 'b':
        n -= 1
        i += 1
    return 440.0 * 2 ** ((12 * (int(name[i:]) + 1) + n - 69) / 12.0)


def shift(name, octaves):
    return name[:-1] + str(int(name[-1]) + octaves)


def adsr(n, a, d, s, r):
    t = np.arange(n) / R
    e = np.where(t < a, t / a, s + (1 - s) * np.exp(-(t - a) / max(d, 1e-3)))
    return e * np.clip((n / R - t) / r, 0, 1)


def harm(f, n, amps, vib=0.0, vib_hz=5.2, vib_delay=0.15, detune=0.0):
    t = np.arange(n) / R
    v = vib * np.clip((t - vib_delay) / 0.2, 0, 1) * np.sin(2 * np.pi * vib_hz * t)
    ph = 2 * np.pi * np.cumsum(f * (1 + detune) * (1 + v)) / R
    x = np.zeros(n)
    for k, a in enumerate(amps, 1):
        if a and k * f < R * 0.45:
            x += a * np.sin(k * ph)
    return x


def flute(f, n):
    return harm(f, n, [1, .28, .10, .03], vib=.006) * adsr(n, .05, .3, .8, .08)


def whistle(f, n):
    return harm(f, n, [1, .12, .04], vib=.008, vib_hz=6) * adsr(n, .02, .2, .85, .05)


def brass(f, n):
    return harm(f, n, [1.0 / k for k in range(1, 11)], vib=.004) * adsr(n, .035, .15, .75, .06)


def strings(f, n):
    a = [1.0 / k ** 1.2 for k in range(1, 9)]
    return (harm(f, n, a, detune=.004) + harm(f, n, a, detune=-.004) + harm(f, n, a)) / 3 * adsr(n, .18, .5, .85, .25)


def accordion(f, n):
    a = [1, .5, .45, .2, .25, .1, .1]
    return (harm(f, n, a, detune=.003) + harm(f, n, a, detune=-.003)) / 2 * adsr(n, .06, .4, .9, .12)


def pluck(f, n):
    t = np.arange(n) / R
    x = np.zeros(n)
    for k in range(1, 9):
        if k * f < R * .45:
            x += np.sin(2 * np.pi * k * f * t) * np.exp(-t * (2.2 + 1.3 * k)) / k
    return x * np.minimum(1, t * 600)


def bass(f, n):
    return harm(f, n, [1, 0, 1 / 9.0, 0, 1 / 25.0, .15]) * adsr(n, .01, .25, .6, .05)


def bell(f, n):
    t = np.arange(n) / R
    return (np.sin(2 * np.pi * f * t) + .5 * np.sin(2 * np.pi * 2.76 * f * t) * np.exp(-t * 6)
            + .25 * np.sin(2 * np.pi * 5.4 * f * t) * np.exp(-t * 10)) * np.exp(-t * 3.2) * np.minimum(1, t * 800)


def kick(n, rng):
    t = np.arange(n) / R
    return np.sin(2 * np.pi * np.cumsum(45 + 90 * np.exp(-t * 28)) / R) * np.exp(-t * 11)


def snare(n, rng):
    t = np.arange(n) / R
    nz = rng.uniform(-1, 1, n)
    nz = nz - np.concatenate([[0], nz[:-1]]) * .5
    return nz * np.exp(-t * 22) * .8 + .5 * np.sin(2 * np.pi * 185 * t) * np.exp(-t * 30)


def tom(n, rng):
    t = np.arange(n) / R
    return np.sin(2 * np.pi * np.cumsum(80 + 60 * np.exp(-t * 14)) / R) * np.exp(-t * 7)


def rim(n, rng):
    t = np.arange(n) / R
    return np.sin(2 * np.pi * 820 * t) * np.exp(-t * 60)


INST = dict(flute=flute, whistle=whistle, brass=brass, strings=strings, accordion=accordion, pluck=pluck, bass=bass, bell=bell)
DRUM = dict(k=kick, s=snare, t=tom, r=rim)
TAIL = R * 2                                   # room for notes and the reverb to ring past the last beat


class Song:
    def __init__(self, bpm, beats, seed=1):
        self.spb = 60.0 / bpm
        self.n = int(beats * self.spb * R) & ~1
        self.x = np.zeros(self.n + TAIL + R)
        self.rng = np.random.RandomState(seed)

    def _add(self, a, y):
        if a >= self.n:
            return
        y = y[:len(self.x) - a]
        self.x[a:a + len(y)] += y

    def note(self, inst, name, beat, dur, vol, tail=0.0):
        n = int((dur * self.spb + tail) * R)
        self._add(int(beat * self.spb * R), vol * INST[inst](hz(name), n))

    def line(self, inst, seq, vol, start=0.0, octave=0, gate=0.95, soft_above=None, tail=0.0):
        """seq: 'A4:1 C5:.5 -:2' (name:beats, '-' rests; a bare name lasts 1 beat). soft_above: notes above this
        pitch play softer the higher they are (a whistle's top notes sit back in the mix). Returns the end beat."""
        b = start
        top = hz(soft_above) if soft_above else None
        for item in seq.split():
            name, d = item.split(':') if ':' in item else (item, '1')
            d = float(d)
            if name != '-':
                if octave:
                    name = shift(name, octave)
                v = vol * (min(1.0, (top / hz(name)) ** 1.4) if top else 1.0)
                self.note(inst, name, b, d * gate, v, tail)
            b += d
        return b

    def chord(self, inst, names, beat, dur, vol, tail=0.15):
        for name in names.split():
            self.note(inst, name, beat, dur, vol, tail)

    def drum(self, d, beat, vol, dur=0.3):
        self._add(int(beat * self.spb * R), vol * DRUM[d](int(dur * R), self.rng))

    def render(self, loop, peak=0.71):
        """-> float samples of length n. A small room (four echoes); loop=True folds everything that rings past the
        end back onto the start, so the wrap carries the tail of the last bar; loop=False fades the last 0.25 s."""
        x = self.x
        y = x.copy()
        for dl, g in ((0.031, .22), (0.047, .18), (0.073, .14), (0.113, .10)):
            k = int(dl * R)
            y[k:] += g * x[:-k]
        out = y[:self.n].copy()
        if loop:
            out[:TAIL] += y[self.n:self.n + TAIL]
        else:
            f = int(0.25 * R)
            out[-f:] *= np.linspace(1.0, 0.0, f)
        return out * (peak / np.max(np.abs(out)))
