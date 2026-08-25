# Kilix Techno

Kilix Techno is an original MIT-licensed C11 electronic-music workstation for
the terminal. It combines an eight-track polymetric step sequencer, native
fallback synthesis, optional curated sample instruments, sixteen scenes,
deterministic variation, live track
mixing, atomic project saves, and deterministic stereo WAV export. Kitty pixel
graphics are preferred; the same editor remains usable through a compact ANSI
cell interface.

The application is native code. It has no browser, JavaScript runtime, local
web server, plug-in host, account, telemetry, or runtime network dependency.
Its nine original synth voices always work offline. Six separately optional,
license-audited soundbanks can be installed through the Kilix launcher; the
application only reads their normalized local WAV files.

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

The headless path can create and validate projects and render WAV files without
a terminal or audio device:

```sh
build/kilix-techno --new --headless --save "$PWD/set.ktech"
build/kilix-techno --export "$PWD/set.ktech" "$PWD/set.wav" --bars 8
```

## Controls

- The grid groups steps into four-step beats. Filled height represents
  velocity, the bright top edge represents probability, and dots identify
  extra ratchets. Instrument families retain distinct colors while the
  playhead and edit cursor use separate outlines.
- Left/right move the step cursor; up/down choose the editable field; `Tab`
  changes the selected track. The instrument field selects any native or
  curated voice. A missing optional voice plays its corresponding native synth
  fallback.
- `Enter` toggles a step; `Space` starts or pauses transport; `.` stops;
  `Ctrl+Space` restarts; `T` sets tap tempo.
- `[` and `]` select tracks; `-` and `+` edit the focused value.
- `M` toggles mute; `Shift+M` or `S` toggles solo.
- `Ctrl+Z` and `Ctrl+Y` undo and redo; `U` and `R` are compact aliases.
- `Ctrl+S` saves; `Ctrl+E` starts or cancels stereo WAV export; `F1` or `?`
  opens help.
- `,` and `>` choose a scene; `L` launches it at the project's selected
  step, beat, or four-beat boundary.
- `Q` requests quit and asks again when the project has unsaved changes.
- Left-click toggles a grid step; keep dragging to paint that resulting on/off
  state across every tile crossed. Left-click also selects tracks and scenes.
- Right-click a step or track label for track options and categorized instrument
  lists. Keep dragging instead to erase every crossed tile. Right-click a scene
  to launch it.
- The wheel edits the focused value. Middle-click is intentionally ignored.

The status line always reports the selected audio backend or the usable
no-audio state and the installed-bank count. Project editing and export do not
require an audio sink.
Kitty graphics are optional: a failed pixel-session start restores the
terminal before the bounded cell editor starts.

## State and file safety

Portable projects use the `.ktech` extension and
`application/x-kilix-techno`. Explicit saves, debounced autosave, settings, and
WAV export use private synchronized temporary files and atomic publication.
WAV work runs on a cancellable worker over a copied project snapshot. A corrupt
autosave is moved to `autosave.corrupt.state` when that diagnostic name is
available; otherwise its original bytes remain untouched and the UI reports
the exact path.

`settings.state` stores canonical semantic binding defaults—never device paths
or commands—and reduced-motion preference below the normal XDG data directory.
Those defaults seed new projects; opened projects retain their own safe
semantic binding records. Projects and settings are bounded and decoded into
temporary objects before caller state changes.

## Optional soundbanks

The Kilix Software Center and `kilix install` list the same six entries with
download and installed sizes. For example:

```sh
kilix install techno-tr808-fischer
kilix install techno-stargate
kilix install techno-scoredata-forge
kilix install techno-mechsounds
kilix install techno-karoryfer
kilix install techno-vcsl
```

Every install is explicit, checksum-pinned, staged, normalized to mono
16-bit/44.1 kHz PCM, and atomically published below
`$XDG_DATA_HOME/kilix-techno/soundbanks` (or the corresponding
`~/.local/share` path). Each pack carries its own receipt, source record,
checksums, and license notice. Details are in
[docs/SOUNDBANKS.md](docs/SOUNDBANKS.md).

## Version-one boundaries

Version 0.1 intentionally has no arbitrary sample import, microphone capture,
plug-in host, live MIDI, MIDI export, network synchronization, or
collaboration. The deterministic probability horizon repeats after 64
polymetric supercycles.

## License and provenance

Kilix Techno code is licensed under the MIT License. All application code,
native presets, colors, text, and fixtures are original to this repository.
Optional sample data retains its separately recorded CC0 dedication and is not
relicensed as MIT. Exact notices are in [THIRD_PARTY.md](THIRD_PARTY.md), and
the clean-room boundary is documented in
[docs/PROVENANCE.md](docs/PROVENANCE.md).
