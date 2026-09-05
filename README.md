# Kilix Techno

Kilix Techno is an original MIT-licensed C11 electronic-music workstation for
the terminal. It combines an eight-track polymetric step sequencer, reusable
patterns, a 256-bar arrangement, piano roll, bounded automation and macros,
four-bus native effects, native fallback synthesis, optional curated sample
instruments, sixteen live scenes, deterministic variation, atomic project
saves, stems, MIDI interchange, and deterministic 16/24/32-bit WAV export. Kitty pixel
graphics are preferred; the same editor remains usable through a compact ANSI
cell interface.

The application is native code. It has no browser, JavaScript runtime, local
web server, plug-in host, account, telemetry, or runtime network dependency.
Its nine original synth voices always work offline. Thirteen separately
optional, license-audited soundbanks can be installed through the Kilix
launcher. A reusable Kilix audio layer loads bounded local WAV, OGG, SFZ, and
SF2 assets on the control thread and hands immutable PCM to the realtime path.

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
- `1`, `2`, `3`, and `4` switch between the step grid, piano roll,
  arrangement, and mixer. Piano-roll tiles support left-drag note painting,
  right-drag erasing, and Shift-drag range selection in both UI modes.
- Shift+arrows extends a rectangular selection. `Ctrl+C`, `Ctrl+X`, and
  `Ctrl+V` copy, cut, and paste it. `Alt+F/C/I/M` fills, clears, inverts, or
  mirrors it; Alt+Left/Right rotates it.
- `Ctrl+P` turns the selected track into a named linked pattern; `Ctrl+D`
  creates an independent pattern copy. `Alt+A` appends the current scene to
  the arrangement and Alt+Enter launches the arrangement.
- `,` and `>` choose a scene; `L` launches it at the project's selected
  step, beat, or four-beat boundary.
- `Q` requests quit and asks again when the project has unsaved changes.
- Left-click toggles a grid step; keep dragging to paint that resulting on/off
  state across every tile crossed. Left-click also selects tracks and scenes.
- Right-click a step or track label for track options and categorized instrument
  lists. Press `/` in that menu to search; Space previews and `F` toggles a
  favorite. Selecting an uninstalled instrument offers its one-action lazy
  install while retaining an audible native fallback. Keep right-dragging a
  grid instead to erase every crossed tile. Right-click a scene to launch it.
- The wheel edits the focused value. Middle-click is intentionally ignored.

The status line always reports the selected audio backend or the usable
no-audio state and the installed-bank count. Project editing and export do not
require an audio sink.
Kitty graphics are optional: a failed pixel-session start restores the
terminal before the bounded cell editor starts.

## State and file safety

Portable projects use the `.ktech` extension and
`application/x-kilix-techno`. Explicit saves, debounced autosave, settings, and
WAV and MIDI export use private synchronized temporary files and atomic publication.
WAV work runs on a cancellable worker over a copied project snapshot. A corrupt
autosave is moved to `autosave.corrupt.state` when that diagnostic name is
available; otherwise its original bytes remain untouched and the UI reports
the exact path.

`autosave.state` is shared by every launch, so it belongs to the session that
loaded it (a launch with neither `--new` nor `--open`) or to the first session
to find it absent. A `--new` or `--open` session started while it holds a
project never writes it: that session autosaves to its own
`autosave.session-<time>-<pid>.state` beside it and the status line says so,
`Ctrl+S` asks for a second press before replacing the shared slot, and `Q` `Q`
quits leaving the unsaved changes in the session slot. A session slot is
removed when its session exits with nothing unsaved.

`settings.state` stores canonical semantic binding defaults—never device paths
or commands—and reduced-motion preference below the normal XDG data directory.
Those defaults seed new projects; opened projects retain their own safe
semantic binding records. Projects and settings are bounded and decoded into
temporary objects before caller state changes.

## Optional soundbanks

The Kilix Software Center and `kilix install` list the same thirteen entries with
download and installed sizes. For example:

```sh
kilix install techno-tr808-fischer
kilix install techno-stargate
kilix install techno-scoredata-forge
kilix install techno-mechsounds
kilix install techno-karoryfer
kilix install techno-vcsl
kilix install techno-ongenet-kit
kilix install techno-vcsl-acoustic
kilix install techno-vsco2ce
kilix install techno-sf2-chaosbank
kilix install techno-sf2-jnsgm2
kilix install techno-sf2-masterpiece
kilix install techno-sf2-unison
```

Every install is explicit, checksum-pinned, staged, and atomically published below
`$XDG_DATA_HOME/kilix-techno/soundbanks` (or the corresponding
`~/.local/share` path). Each pack carries its own receipt, source record,
checksums, and license notice. WAV-only legacy packs are normalized at install;
new packs preserve their WAV, SFZ, and SF2 source format and are decoded to
rate-matched mono PCM at application startup. New projects select the compact
Ongenet kit, VCSL Strumstick, and JNS synth bass, with native fallback until
those lazy packs are installed. Details are in
[docs/SOUNDBANKS.md](docs/SOUNDBANKS.md).

## Bounded workstation model

Version 0.2.1 accepts schema-1 and schema-2 projects and emits canonical
schema 3. Limits are explicit: 128 patterns, 1,024 arrangement entries but at
most 256 bars, 32 automation lanes with 256 points each, eight macros with
eight targets each, four buses, sixteen effects total and four per chain,
eight export presets, and 64 project resources. The decoder ceiling is 1 MiB.

User sample ingestion is a control-thread library operation with a 64 MiB
source limit and required license label. WAV and OGG remain private copies;
SFZ and SF2 are decoded and stored as self-contained WAV resources. A SHA-256
manifest report distinguishes available, missing, changed, and unsafe files.
Arbitrary plug-in hosting, microphone capture, live MIDI devices, networking,
and collaboration remain outside the core release. The deterministic
probability horizon repeats after 64 polymetric supercycles.

## License and provenance

Kilix Techno code is licensed under the MIT License. All application code,
native presets, colors, text, and fixtures are original to this repository.
Optional sample data retains its separately recorded CC0 dedication and is not
relicensed as MIT. Exact notices are in [THIRD_PARTY.md](THIRD_PARTY.md), and
the clean-room boundary is documented in
[docs/PROVENANCE.md](docs/PROVENANCE.md). Research and design records are kept
outside the publication repository.
