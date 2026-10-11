#!/usr/bin/env python3
"""Renders the audio examples for the README: short melodies played with factory presets.

Every demo is a list of layers (preset + notes). The plugin itself renders each layer offline
(FjordRenderTest --demo, with a running host transport so arps, gates and synced LFOs lock to the beat),
ffmpeg mixes the layers, matches the loudness and writes
    docs/audio/<id>.mp3          the audio example (linked from the README)
    docs/audio/video/<id>.mp4    the same with a waveform picture – drag these into the README editor on
                                 github.com to get inline players (GitHub only plays uploaded media)

Usage:  python3 tools/demos/make_demos.py            # all demos
        python3 tools/demos/make_demos.py pad bass   # only some
Requires: a built FjordRenderTest (cmake --build build --target FjordRenderTest) and ffmpeg.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "build/FjordRenderTest_artefacts/Release/FjordRenderTest"
OUT = ROOT / "docs/audio"
FONT = ROOT / "resources/fonts/Manrope-Medium.ttf"
BPM = 112

# ---------------------------------------------------------------- the song: A minor, vi - IV - I - V, twice
CHORDS = [[57, 60, 64], [57, 60, 65], [55, 60, 64], [55, 59, 62]]      # Am, F/A, C/G, G
ROOTS = [45, 41, 48, 43]                                               # A, F, C, G
BARS = 8


def chord_of(bar):
    return CHORDS[bar % 4]


def held_chords(octave=0, vel=96, length=4.0):
    return [(b * 4.0, length, n + 12 * octave, vel) for b in range(BARS) for n in chord_of(b)]


def stabs(vel=92):
    """Keys: a chord on the one, then off-beat stabs."""
    notes = []
    for b in range(BARS):
        for start, length, v in ((0.0, 1.2, vel), (1.5, 0.4, vel - 14), (2.5, 0.4, vel - 8), (3.5, 0.4, vel - 14)):
            notes += [(b * 4 + start, length, n, v) for n in chord_of(b)]
    return notes


def octave_bass(vel=104):
    """Disco: root and octave in eighth notes."""
    return [(b * 4 + i * 0.5, 0.38, ROOTS[b % 4] + (12 if i % 2 else 0), vel - (0 if i % 2 == 0 else 10))
            for b in range(BARS) for i in range(8)]


def pluck_line(vel=96):
    """Eighth-note broken chords, one octave up."""
    shape = [0, 1, 2, 1, 0, 2, 1, 2]
    return [(b * 4 + i * 0.5, 0.45, chord_of(b)[k] + 12, vel - (i % 2) * 12) for b in range(BARS) for i, k in enumerate(shape)]


# Lead melody (beat, length, note); the second half answers the first
MELODY = [
    (0, 1, 76), (1, 0.5, 74), (1.5, 0.5, 72), (2, 1, 74), (3, 1, 76),
    (4, 1.5, 72), (5.5, 0.5, 69), (6, 1, 72), (7, 1, 74),
    (8, 1, 76), (9, 1, 79), (10, 1, 76), (11, 1, 74),
    (12, 2, 74), (14, 1, 71), (15, 1, 67),
    (16, 1, 76), (17, 0.5, 74), (17.5, 0.5, 72), (18, 1, 74), (19, 1, 76),
    (20, 1.5, 77), (21.5, 0.5, 76), (22, 1, 74), (23, 1, 72),
    (24, 1, 72), (25, 1, 74), (26, 1, 76), (27, 1, 79),
    (28, 1.5, 74), (29.5, 0.5, 72), (30, 2, 69),
]


def lead(vel=100):
    return [(s, l * 0.95, n, vel - (8 if s % 1 else 0)) for s, l, n in MELODY]


def kick():
    return [(b * 4 + i, 0.3, 36, 112) for b in range(BARS) for i in range(4)]


def clap():
    return [(b * 4 + i, 0.3, 60, 100) for b in range(BARS) for i in (1, 3)]


def hats():
    return [(b * 4 + i + 0.5, 0.15, 60, 90) for b in range(BARS) for i in range(4)]


# ---------------------------------------------------------------- demos
# layer: (preset, notes, gain in dB)
DEMOS = [
    {"id": "pad",     "title": "Velvet Galaxy", "category": "Pad",   "layers": [("Velvet Galaxy", held_chords(), 0)]},
    {"id": "keys",    "title": "Cassette Rhodes", "category": "Keys", "layers": [("Cassette Rhodes", stabs(), 0)]},
    {"id": "bass",    "title": "Disco Octave", "category": "Bass",   "layers": [("Disco Octave", octave_bass(), 0)]},
    {"id": "lead",    "title": "Disco Lead", "category": "Lead",     "layers": [("Disco Lead", lead(), 0)]},
    {"id": "pluck",   "title": "Crystal Pluck", "category": "Pluck", "layers": [("Crystal Pluck", pluck_line(), 0)]},
    {"id": "arp",     "title": "Disco Arp", "category": "Arp",       "layers": [("Disco Arp", held_chords(), 0)]},
    {"id": "arp-polaroid", "title": "Polaroid Afternoon", "category": "Arp", "layers": [("Polaroid Afternoon", held_chords(), 0)]},
    {"id": "arp-small-hours", "title": "Small Hours Arp", "category": "Arp", "layers": [("Small Hours Arp", held_chords(octave=-1), 0)]},
    {"id": "gated",   "title": "French House Chords", "category": "Gated", "layers": [("French House Chords", held_chords(), 0)]},
    {"id": "atmos",   "title": "Aurora Haze", "category": "Atmos",   "layers": [("Aurora Haze", [(b * 4.0, 8.0, n - 12, 90) for b in range(0, BARS, 2) for n in chord_of(b)], 0)]},
    {"id": "drums",   "title": "Disco Kick + Disco Clap + Closed Hat", "category": "Drums",
     "layers": [("Disco Kick", kick(), 0), ("Disco Clap", clap(), -4), ("Closed Hat", hats(), -8)]},
    {"id": "sketch",  "title": "Song sketch: pad, bass, lead, drums", "category": "Mix",
     "layers": [("Velvet Galaxy", held_chords(), -7), ("Disco Octave", octave_bass(), -3), ("Disco Lead", lead(), -5),
                ("Disco Kick", kick(), -2), ("Disco Clap", clap(), -7), ("Closed Hat", hats(), -11)]},
]

THEME = {"bg": "0xedf3f4", "wave": "0x0e7566", "text": "0x15253a", "dim": "0x6f8291"}


def run(cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def render_layer(preset, notes, wav, tmp):
    score = tmp / (wav.stem + ".txt")
    score.write_text(f"bpm {BPM}\n" + "\n".join(f"{s:.4f} {l:.4f} {n} {v}" for s, l, n, v in notes) + "\n")
    subprocess.run([str(TOOL), "--demo", preset, str(score), str(wav)], check=True)


def make(demo, tmp):
    layers = []
    for i, (preset, notes, gain) in enumerate(demo["layers"]):
        wav = tmp / f"{demo['id']}_{i}.wav"
        render_layer(preset, notes, wav, tmp)
        layers.append((wav, gain))

    # Mix (layer gains), then match loudness: every demo at about -16 LUFS
    inputs = sum((["-i", str(w)] for w, _ in layers), [])
    gains = "".join(f"[{i}:a]volume={g}dB[a{i}];" for i, (_, g) in enumerate(layers))
    mix = "".join(f"[a{i}]" for i in range(len(layers)))
    graph = gains + f"{mix}amix=inputs={len(layers)}:normalize=0,loudnorm=I=-16:TP=-1.5:LRA=11[out]"
    mixed = tmp / f"{demo['id']}.wav"
    run(["ffmpeg", "-y", *inputs, "-filter_complex", graph, "-map", "[out]", "-ar", "48000", str(mixed)])

    OUT.mkdir(parents=True, exist_ok=True)
    mp3 = OUT / f"{demo['id']}.mp3"
    run(["ffmpeg", "-y", "-i", str(mixed), "-codec:a", "libmp3lame", "-b:a", "128k",
         "-metadata", f"title=Fjord – {demo['title']}", "-metadata", "artist=Fjord", str(mp3)])

    # Video: waveform picture + title, for inline players on GitHub
    video_dir = OUT / "video"
    video_dir.mkdir(exist_ok=True)
    title = demo["title"].replace(":", "\\:").replace("'", "")
    font = str(FONT).replace(":", "\\:")
    cover = tmp / f"{demo['id']}.png"
    run(["ffmpeg", "-y", "-i", str(mixed), "-filter_complex",
         f"[0:a]aformat=channel_layouts=mono,showwavespic=s=1200x180:colors={THEME['wave']}:scale=sqrt,format=rgba[w];"
         f"color=c={THEME['bg']}:s=1280x360[bg];[bg][w]overlay=40:150,"
         f"drawtext=fontfile='{font}':text='fjord.':fontcolor={THEME['text']}:fontsize=34:x=44:y=36,"
         f"drawtext=fontfile='{font}':text='{demo['category'].upper()}':fontcolor={THEME['wave']}:fontsize=17:x=46:y=98,"
         f"drawtext=fontfile='{font}':text='{title}':fontcolor={THEME['text']}:fontsize=24:x=150:y=92",
         "-frames:v", "1", str(cover)])
    run(["ffmpeg", "-y", "-loop", "1", "-framerate", "2", "-i", str(cover), "-i", str(mixed),
         "-c:v", "libx264", "-tune", "stillimage", "-pix_fmt", "yuv420p", "-crf", "30", "-r", "2",
         "-c:a", "aac", "-b:a", "160k", "-movflags", "+faststart", "-shortest", str(video_dir / f"{demo['id']}.mp4")])
    shutil.copy(cover, video_dir / f"{demo['id']}.png")
    print(f"  {mp3.relative_to(ROOT)}")


def main():
    if not TOOL.exists():
        sys.exit("FjordRenderTest not found – build it first: cmake --build build --target FjordRenderTest")
    if shutil.which("ffmpeg") is None:
        sys.exit("ffmpeg not found (brew install ffmpeg)")
    only = sys.argv[1:]
    demos = [d for d in DEMOS if not only or d["id"] in only]
    with tempfile.TemporaryDirectory() as t:
        for d in demos:
            make(d, Path(t))


if __name__ == "__main__":
    main()
