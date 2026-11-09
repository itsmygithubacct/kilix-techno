# Build and headless use

Building, the qualification gates and the headless command line. This text was
moved from the README; the [README](../README.md) is the short entry point.

## Build

Clone recursively, then build offline:

```sh
git clone --recursive https://github.com/itsmygithubacct/kilix-techno.git
cd kilix-techno
make all
make test
```

The build uses the system `libsndfile` and `FluidSynth` development packages
for the optional-format asset layer. Plebian-OS already provisions them.

The runtime binary is `build/kilix-techno`. Use `--help` for command-line
options. `make sanitize`, `make tsanitize`, `make fuzz`, `make benchmark`, and
`make clean-room-check` run the maintained qualification gates.

`make realtime-check` intercepts allocation, lock, file, and process operations
around the generator path and runs the 10,000-block deadline benchmark.

The headless path can create and validate projects and render WAV files without
a terminal or audio device:

```sh
build/kilix-techno --new --headless --save "$PWD/set.ktech"
build/kilix-techno --export "$PWD/set.ktech" "$PWD/set.wav" --bars 8
mkdir "$PWD/stems"
build/kilix-techno --export-stems "$PWD/set.ktech" "$PWD/stems"
build/kilix-techno --export-midi "$PWD/set.ktech" "$PWD/set.mid"
build/kilix-techno --import-midi "$PWD/set.mid" "$PWD/imported.ktech"
```

`--export-arrangement` renders the complete arrangement, and
`--export-preset IN OUT NAME` applies a saved WAV, stems, or MIDI preset.
