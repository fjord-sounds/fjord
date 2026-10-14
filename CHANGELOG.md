# Changelog

All notable changes to Fjord. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] – 2026-10-14
- Built on [fjord-base](https://github.com/fjord-sounds/fjord-base), the code shared by all Fjord plugins: clone it next to
  this repository (or CMake downloads the pinned release). Header, preset bar, browser, controls, theme, the FX page,
  build scripts, CI workflows and the screenshot tooling now come from there.
- **Unified effects**: fifteen effects and the clipper in every Fjord plugin – Saturation, Lo-Fi, Transient, Compressor,
  Squash, EQ, Filter, Motion, Delay, Reverb, Width and the new Tremolo / Auto-Pan, Frequency Shifter, Exciter and
  Harmonizer – with at least 40 presets per effect and 35 effect chains (**FX CHAIN**, **Lock FX**). Existing effects were
  extended (e.g. filter LFO and per-note envelope, delay modes, reverb modes with freeze, EQ slopes, motion modes).
- The lock menu is built on fjord-base (`LockSet`, `LockMenu`, `LockButton`); *Window size* in the options menu (75–150 %).
- The FX page is the same in all plugins: live tiles in two rows, big graph, per-effect preset menu.
- Effect parameter ids are now `fx_<effect>_<param>` and the factory presets use them; the effect order is stored as ids.
- The effect chain of Fjord (drive, crush, chorus, phaser, flanger …) was replaced by the shared effects: drive became
  Saturation, crush Lo-Fi, chorus / phaser / flanger Motion modes. The compressor page uses the shared compressor.
- The logo dot flashes with every note, undo / redo, tooltips and the reduce-motion setting like in the other plugins.
- The preset level is applied after the effects; the master gain is no longer part of presets.
- The screenshot tooling moved to fjord-base (`tools/screenshots/shoot.sh`, `shots.conf`).
- Fixed: a tempo-synced sine LFO could read past the end of its lookup table when the phase rounded to exactly 1.0 (rare NaN or crash, seen with x86 builds on Linux).

## [0.1.0] – first public release
- Subtractive/wavetable synth with 8 oscillators, 2 filters, 3 envelopes, 3 LFOs and a 16-slot modulation matrix.
- 10 effects with reorderable chain, arpeggiator, step gate, stutter and pump.
- 1,800+ factory presets plus FX chain and single effect presets.
- VST3 (macOS, Windows, Linux), Audio Unit (macOS), LV2 (Linux) and a standalone app.
