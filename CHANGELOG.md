# Changelog

All notable changes to Fjord. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]
- Fixed: a tempo-synced sine LFO could read past the end of its lookup table when the phase rounded to exactly 1.0 (rare NaN or crash, seen with x86 builds on Linux).

## [0.1.0] – first public release
- Subtractive/wavetable synth with 8 oscillators, 2 filters, 3 envelopes, 3 LFOs and a 16-slot modulation matrix.
- 10 effects with reorderable chain, arpeggiator, step gate, stutter and pump.
- 1,800+ factory presets plus FX chain and single effect presets.
- VST3 (macOS, Windows, Linux), Audio Unit (macOS), LV2 (Linux) and a standalone app.
