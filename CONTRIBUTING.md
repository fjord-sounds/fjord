# Contributing to Fjord

Thanks for your interest! Fjord is a small, calm synthesizer – ideas that keep it that way are very welcome.

By contributing you agree that your work is licensed under the [AGPL-3.0](LICENSE), like the rest of the project.

## Build

```bash
git clone https://github.com/fjord-sounds/fjord.git && cd fjord
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFJORD_COPY_PLUGIN=OFF
cmake --build build -j8
cmake --build build --target FjordRenderTest -j8
./build/FjordRenderTest_artefacts/Release/FjordRenderTest build/renders      # must print ALL PASSED
```

JUCE 8 is downloaded by CMake on the first configure (or use a local checkout: `-DFJORD_JUCE_DIR=/path/to/JUCE`).
`./build.sh` is the macOS shortcut that also installs the plugins and runs `auval`. The offline test tool
(`FjordRenderTest`) and the screenshot tooling are described in the README.

## Rules that keep old projects and presets working

- Parameter IDs never change. Ranges may grow. Presets and the project state store plain values.
- Choice lists (arp modes, mod destinations, …) are append-only.
- New parameters need a neutral default.
- If a change needs old project state to be converted, raise `kStateVersion` and add the step to `migrateState()`
  in `Source/PluginProcessor.cpp`.
- The audio thread never allocates or locks.

## Presets

Factory presets are plain XML files in `presets/` (pop / soft / disco / bedroom pop / house, one sound idea each,
nothing harsh). New presets are welcome as pull requests: save one from the plugin and add the file to the matching
category folder.

## Pull requests

- Keep them focused. Say what changes and why.
- UI changes: attach screenshots in the light **and** the dark theme (`FjordRenderTest --screenshot …`).
- Update the README and [CHANGELOG.md](CHANGELOG.md) (section "Unreleased").
- Everything in the repository is English.
