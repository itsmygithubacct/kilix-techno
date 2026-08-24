# Kilix Techno

Kilix Techno is an original MIT-licensed C11 electronic-music workstation for
the terminal. It combines an eight-track polymetric step sequencer, synthesized
factory instruments, sixteen scenes, deterministic variation, live track
mixing, atomic project saves, and deterministic stereo WAV export. Kitty pixel
graphics are preferred; the same editor remains usable through a compact ANSI
cell interface.

The application is native code. It has no browser, JavaScript runtime, local
web server, sample pack, plug-in host, account, telemetry, or runtime network
dependency.

## Build

Clone recursively, then build offline:

```sh
git clone --recursive https://github.com/itsmygithubacct/kilix-techno.git
cd kilix-techno
make all
make test
```

The runtime binary is `build/kilix-techno`. Use `--help` for command-line
options. `make sanitize`, `make tsanitize`, `make fuzz`, `make benchmark`, and
`make clean-room-check` run the maintained qualification gates.

## Controls

- Arrow keys move the step cursor; `Tab` changes the selected track.
- `Enter` toggles a step; `Space` starts or pauses transport; `.` stops.
- `[` and `]` select tracks; `-` and `+` edit the focused value.
- `M` toggles mute; `S` toggles solo; `U` and `R` undo and redo.
- `Ctrl+S` saves; `E` exports a stereo WAV; `F1` or `?` opens help.
- `Q` requests quit and asks again when the project has unsaved changes.

The status line always reports the selected audio backend or the usable
no-audio state. Project editing and export do not require an audio sink.

## License and provenance

Kilix Techno is licensed under the MIT License. All application code, factory
presets, colors, text, and fixtures are original to this repository. Exact
dependency notices are in [THIRD_PARTY.md](THIRD_PARTY.md), and the clean-room
boundary is documented in [docs/PROVENANCE.md](docs/PROVENANCE.md).
