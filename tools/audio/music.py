"""music.py - the game's soundtrack, written note by note for synth.py (slice 3D-7). One family of sounds: a
whistle or flute lead, strings, bass and a frame drum, in folk modes - the direction the project owner picked from
the samples of 2026-11-19 ("village D2": a 6/8 dance in A dorian, wary rather than cheerful; nothing above B5).

    TRACKS[name] = (builder, loops)      builder() -> a rendered Song (float samples at synth.R)

Lengths are part of the budget (plan 7.2, 8: 10.8 KiB a second, 2.0 MiB of music in all); tools/audio/lint_cues.py
holds the total. Edit the note strings and run tools/audio/make_standins.py; tests/sound_save checks the files match.
"""
from synth import Song

# chord: (bass note, the pad's voicing)
CH = {
    'Am': ('A2', 'A3 C4 E4'), 'G': ('G2', 'B3 D4 G4'), 'C': ('C3', 'C4 E4 G4'), 'Em': ('E3', 'G3 B3 E4'),
    'D': ('D3', 'A3 D4 F#4'), 'F': ('F2', 'A3 C4 F4'), 'E': ('E2', 'G#3 B3 E4'), 'A': ('A2', 'A3 C#4 E4'),
    'Eml': ('E2', 'E3 G3 B3'), 'Dl': ('D2', 'D3 F#3 A3'), 'Cl': ('C2', 'E3 G3 C4'), 'Gl': ('G2', 'G3 B3 D4'), 'Aml': ('A2', 'E3 A3 C4'),
}


def bars(*b):
    return '  '.join(b)


# ---------------------------------------------------------------------------- the village theme (and the title's)
VILLAGE_PROG = ['Am', 'Am', 'G', 'Am', 'Am', 'C', 'G', 'Am', 'C', 'G', 'Am', 'Em', 'Am', 'G', 'Em', 'Am']
VILLAGE_TUNE = [
    'A4 C5 E5 A5 E5 C5', 'B4 C5 E5 F#5:2 E5', 'B4 D5 G5 B5 G5 D5', 'A5:2 E5 C5:2 A4',
    'A4 C5 E5 A5 G5 E5', 'G5:2 E5 C5:2 E5', 'D5 B4 G4 B4 D5 G5', 'E5:3 A4:3',
    'C5 E5 G5 E5:2 C5', 'B4 D5 G5 D5:2 B4', 'A4 C5 E5 A5 E5 C5', 'B4 E5 G5 B5:2 G5',
    'A5 E5 C5 A4 C5 E5', 'G4 B4 D5 G5 D5 B4', 'E5 G5 B4 E5 D5 B4', 'A4:6']


def bodhran(s, b, bar, vol=1.0):
    s.drum('t', b, .42 * vol, .28)
    s.drum('t', b + 3, .30 * vol, .28)
    for i in (2, 5):
        s.drum('t', b + i, .12 * vol, .15)
    if bar % 4 == 3:
        s.drum('t', b + 4, .2 * vol, .2)
        s.drum('t', b + 5, .26 * vol, .2)


def village():
    s = Song(264, 192, seed=11)                    # beats are eighth notes: 6 to the bar, 16 bars, twice (43.6 s)
    tune = bars(*VILLAGE_TUNE)
    s.line('whistle', tune, .50, soft_above='G5')
    s.line('whistle', tune, .46, start=96, soft_above='G5')
    s.line('flute', tune, .22, start=96, octave=-1)         # the second time an octave below joins
    for bar in range(32):
        root, tri = CH[VILLAGE_PROG[bar % 16]]
        b = bar * 6
        s.note('bass', root, b, 2.7, .40)
        s.note('bass', root, b + 3, 2.7, .26)
        s.chord('strings', tri, b, 6, .11)
        bodhran(s, b, bar)
    return s


def title():
    s = Song(200, 96, seed=12)                     # the village's tune, slower and fuller, no dance in it (28.8 s)
    tune = bars(*VILLAGE_TUNE)
    s.line('flute', tune, .50, gate=1.0)
    s.line('whistle', bars(*VILLAGE_TUNE[8:]), .20, start=48, soft_above='E5')
    for bar in range(16):
        root, tri = CH[VILLAGE_PROG[bar]]
        b = bar * 6
        s.note('bass', root, b, 5.6, .36)
        s.chord('strings', tri, b, 6, .17, tail=.3)
        s.chord('strings', ' '.join(n[:-1] + str(int(n[-1]) + 1) for n in tri.split()[:2]), b, 6, .05, tail=.3)
        if bar % 8 == 0:
            s.note('bell', 'A3', b, 6, .20, tail=2.0)
        if bar >= 8:
            s.drum('t', b, .22, .28)
            s.drum('t', b + 3, .12, .28)
    return s


# ---------------------------------------------------------------------------- the barrow at night
def barrow():
    s = Song(176, 96, seed=13)                     # 16 bars of 6/8 in E minor, slow (32.7 s)
    prog = ['Eml', 'Eml', 'Dl', 'Eml', 'Eml', 'Cl', 'Dl', 'Eml', 'Gl', 'Dl', 'Eml', 'Cl', 'Aml', 'Eml', 'Dl', 'Eml']
    tune = bars('E4:3 G4:3', 'F#4:3 B4:3', 'A4:2 F#4:1 D4:3', 'E4:6', 'E4:3 B4:3', 'C5:2 B4:1 G4:3', 'A4:3 F#4:3', 'E4:6',
                'G4:3 B4:3', 'A4:2 F#4:1 D4:3', 'E4:2 G4:1 B4:3', 'C5:3 G4:3', 'A4:3 C5:3', 'B4:2 G4:1 E4:3', 'F#4:3 D4:3', 'E4:6')
    s.line('flute', tune, .50, gate=1.0)
    # a distant whistle answers in the long notes
    for bar, ans in ((3, 'B5:2 G5:1 E5:3'), (7, 'G5:2 F#5:1 E5:3'), (11, 'E5:2 G5:1 B5:3'), (15, 'B5:2 A5:1 G5:3')):
        s.line('whistle', ans, .10, start=bar * 6, tail=.4)
    for bar in range(16):
        root, tri = CH[prog[bar]]
        b = bar * 6
        s.note('bass', root, b, 5.6, .34)
        s.chord('strings', tri, b, 6, .15, tail=.4)
        if bar % 4 == 0:
            s.note('bell', 'E3' if bar % 8 == 0 else 'B2', b, 6, .22, tail=2.5)
        s.drum('t', b, .20, .3)                    # a slow heartbeat
        s.drum('t', b + 1, .10, .3)
    return s


# ---------------------------------------------------------------------------- battles
def drums44(s, b, bar, kick=.55, snare=.26, fill=True):
    s.drum('k', b, kick)
    s.drum('k', b + 2, kick * .9)
    s.drum('k', b + 2.5, kick * .5)
    s.drum('s', b + 1, snare)
    s.drum('s', b + 3, snare)
    for i in range(8):
        s.drum('r', b + i * .5, .05, .05)
    if fill and bar % 4 == 3:
        for i in range(4):
            s.drum('t', b + 3 + i * .25, .22 + .05 * i, .18)


def battle():
    s = Song(150, 64, seed=14)                     # 16 bars of 4/4 in A minor (25.6 s)
    prog = ['Am', 'Am', 'F', 'G', 'Am', 'Am', 'F', 'E', 'C', 'G', 'Am', 'Em', 'F', 'G', 'E', 'Am']
    tune = bars('A4:.5 C5:.5 E5:1 A5:1 E5:1', 'G5:.5 E5:.5 C5:1 E5:2', 'F5:.5 A5:.5 C5:1 F5:1 A5:1', 'G5:1 D5:1 B4:2',
                'A4:.5 C5:.5 E5:1 A5:1 B5:1', 'A5:.5 G5:.5 E5:1 C5:2', 'F5:1 A5:1 F5:.5 E5:.5 D5:1', 'E5:2 G#5:1 B5:1',
                'C5:.5 E5:.5 G5:1 E5:1 C5:1', 'B4:.5 D5:.5 G5:1 D5:2', 'A4:.5 C5:.5 E5:1 A5:2', 'G5:1 E5:1 B4:2',
                'F5:.5 A5:.5 F5:1 C5:1 F5:1', 'G5:.5 B5:.5 G5:1 D5:1 G5:1', 'G#5:1 E5:1 B4:1 E5:1', 'A5:3 -:1')
    s.line('whistle', tune, .46, soft_above='G5')
    s.line('brass', tune, .20, octave=-1)
    for bar in range(16):
        root, tri = CH[prog[bar]]
        b = bar * 4
        for i in range(8):
            s.note('bass', root, b + i * .5, .42, .36 if i % 2 == 0 else .22)
        s.chord('strings', tri, b, 4, .12)
        drums44(s, b, bar)
    return s


def boss():
    s = Song(132, 64, seed=15)                     # 16 bars of 4/4, E minor with the flat second (29.1 s)
    prog = ['Eml', 'Eml', 'F', 'Eml', 'Eml', 'F', 'Dl', 'Eml', 'Cl', 'Dl', 'Eml', 'Eml', 'Aml', 'F', 'Dl', 'Eml']
    tune = bars('E4:1.5 G4:.5 B4:2', 'A4:.5 G4:.5 E4:1 B3:2', 'F4:1.5 A4:.5 C5:2', 'B4:1 G4:1 E4:2',
                'E4:1.5 G4:.5 B4:1 E5:1', 'F5:1 C5:1 A4:2', 'D5:1.5 A4:.5 F#4:1 A4:1', 'E4:3 -:1',
                'C5:1 E5:1 G5:1.5 E5:.5', 'D5:1 F#5:1 A5:2', 'G5:.5 F#5:.5 E5:1 B4:2', 'E5:1 B4:1 G4:2',
                'A4:1 C5:1 E5:1.5 C5:.5', 'F5:1 A4:1 C5:2', 'D5:1 F#4:1 A4:1 D5:1', 'E4:4')
    s.line('brass', tune, .44)
    s.line('flute', bars(*tune.split('  ')[8:]), .18, start=32, octave=1, soft_above='G5')
    for bar in range(16):
        root, tri = CH[prog[bar]]
        b = bar * 4
        for i in range(8):
            s.note('bass', root, b + i * .5, .42, .40 if i in (0, 3, 6) else .20)
        s.chord('strings', tri, b, 4, .15)
        s.drum('k', b, .6)
        s.drum('k', b + 1.5, .45)
        s.drum('k', b + 2, .55)
        s.drum('t', b + 3, .34, .25)
        s.drum('t', b + 3.5, .26, .25)
        s.drum('s', b + 1, .16)
        if bar % 8 == 0:
            s.note('bell', 'E3', b, 4, .26, tail=2.5)
        if bar % 4 == 3:
            for i in range(6):
                s.drum('t', b + 2.5 + i * .25, .2 + .04 * i, .2)
    return s


# ---------------------------------------------------------------------------- the ending
def dawn():
    s = Song(216, 72, seed=16)                     # the village's dance turned to A major, 12 bars (20.0 s)
    prog = ['A', 'D', 'A', 'E', 'A', 'D', 'E', 'A', 'D', 'A', 'E', 'A']
    tune = ['A4 C#5 E5 A5 E5 C#5', 'B4 D5 F#5 A5:2 F#5', 'A5:2 E5 C#5:2 E5', 'B4 E5 G#5 B5:2 G#5',
            'A4 C#5 E5 A5 E5 C#5', 'D5 F#5 A5 F#5:2 D5', 'E5 G#5 B5 G#5 E5 B4', 'A5:3 E5:3',
            'F#5 D5 A4 D5 F#5 A5', 'E5:2 C#5 A4:2 C#5', 'B4 E5 G#5 E5 B4 G#4', 'A4:6']
    s.line('flute', bars(*tune), .48)
    s.line('whistle', bars(*tune[4:]), .30, start=24, soft_above='G5')
    for bar in range(12):
        root, tri = CH[prog[bar]]
        b = bar * 6
        s.note('bass', root, b, 2.7, .36)
        s.note('bass', root, b + 3, 2.7, .22)
        s.chord('strings', tri, b, 6, .14, tail=.3)
        if bar >= 4:
            bodhran(s, b, bar, .7)
        if bar in (0, 8):
            s.note('bell', 'A4', b, 6, .14, tail=2.0)
    return s


def victory():
    s = Song(160, 9.33, seed=17)                   # 3.5 s: up the A major chord and home
    s.line('whistle', 'A4:.5 C#5:.5 E5:.5 A5:1.5 G#5:.5 A5:3', .50, soft_above='A5', tail=.4)
    s.line('brass', 'A3:.5 C#4:.5 E4:.5 A4:1.5 E4:.5 A4:3', .24, tail=.4)
    s.chord('strings', 'A3 C#4 E4', 0, 3, .16)
    s.chord('strings', 'A3 D4 F#4', 3, .5, .16)
    s.chord('strings', 'A3 C#4 E4 A4', 3.5, 4, .18, tail=.6)
    s.note('bass', 'A2', 0, 3, .4)
    s.note('bass', 'A2', 3.5, 4, .4)
    for i in range(6):
        s.drum('t', i * .25, .16 + .04 * i, .2)
    s.drum('k', 1.5, .5)
    s.drum('k', 3.5, .55)
    s.note('bell', 'A5', 3.5, 4, .12, tail=1.0)
    return s


def death():
    s = Song(96, 5.6, seed=18)                     # 3.5 s: down the E minor chord into the low bell
    s.line('flute', 'E5:1 D5:.5 B4:.5 G4:1 E4:2.6', .50, gate=1.0, tail=.3)
    s.chord('strings', 'E3 G3 B3', 0, 3, .18, tail=.5)
    s.chord('strings', 'E3 G3 B3 E4', 3, 2.6, .14, tail=.5)
    s.note('bass', 'E2', 0, 5.6, .40)
    s.note('bell', 'E3', 0, 5.6, .30, tail=1.5)
    s.note('bell', 'B2', 3, 2.6, .22, tail=1.5)
    s.drum('t', 0, .4, .35)
    s.drum('t', 3, .3, .35)
    return s


TRACKS = {
    'title': (title, True), 'village': (village, True), 'barrow': (barrow, True), 'battle': (battle, True),
    'boss': (boss, True), 'dawn': (dawn, True), 'victory': (victory, False), 'death': (death, False),
}
