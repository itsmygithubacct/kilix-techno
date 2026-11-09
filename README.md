# Kilix Techno

Installable on demand from the Kilix content catalog as `kilix-techno`
(`kilix install kilix-techno`, then `kilix app run kilix-techno`). The catalog
pins the exact commit, builds it with `make all` and runs `build/kilix-techno`
in a terminal pane; its soundbanks are separate, optional catalog installs.

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

## Quick start

Clone recursively, then build offline:

```sh
git clone --recursive https://github.com/itsmygithubacct/kilix-techno.git
cd kilix-techno
make all
make test
build/kilix-techno --new
```

The build uses the system `libsndfile` and `FluidSynth` development packages
for the optional-format asset layer. Plebian-OS already provisions them.
Sanitizer, fuzz, benchmark, realtime and clean-room gates are described in
[docs/build.md](docs/build.md).

## Usage

| Command | What it does |
|---|---|
| `kilix-techno` | open the shared autosave, or a new project when there is none |
| `kilix-techno --new` / `--open PATH` | start a new project, or open one `.ktech` file |
| `--new --headless --save PATH` | create and save a project without a terminal |
| `--export IN OUT [--bars N]` | render the active scene to stereo WAV |
| `--export-arrangement IN OUT` | render the whole arrangement |
| `--export-stems IN DIR` | write eight track stems |
| `--export-midi IN OUT` / `--import-midi IN OUT` | Standard MIDI out and in |
| `--export-preset IN OUT NAME` | export with a saved WAV, stems or MIDI preset |
| `--cell`, `--no-audio` | force the cell interface; skip the audio sink |
| `--sample-rate HZ`, `--seed N` | audio and export rate; seed for a new project |

Main editor keys (the full list is in [docs/controls.md](docs/controls.md)):

| Key | Action |
|---|---|
| arrows, `Tab`, `[` `]` | move the cursor, choose the field, select tracks |
| `Enter` | toggle a step |
| `Space` / `.` | play or pause / stop |
| `-` `+`, wheel | edit the focused value |
| `1` `2` `3` `4` | step grid, piano roll, arrangement, mixer |
| `M`, `S` | mute, solo |
| `Ctrl+Z` `Ctrl+Y` | undo, redo |
| `Ctrl+S`, `Ctrl+E` | save, export WAV |
| `F1` or `?`, `Q` | help, quit |

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

## How it works

```
 terminal input --> project + undo (main thread) --> compiler --> immutable song snapshot
                                                                     |
 soundbank receipts --> pcm-mixer asset layer --> immutable PCM ----+
                                                                     v
                 pcm-mixer thread: 4 chip-sequencer engines (one per bus) --> effects --> master
```

The main thread owns the project, undo history, compilation, saves and the
interface. A control-side compiler expands each scene's eight tracks into a
bounded supercycle and the arrangement into at most 256 bars, then hands an
immutable snapshot to the audio side. The `pcm-mixer` thread drives four
synchronized `chip-sequencer` engines, one per bus, applies each bus's native
effect rack and the master stage, and never allocates, logs, touches files or
takes an application lock. Optional WAV, OGG, SFZ and SF2 soundbanks are decoded
on the control thread; a missing sample voice plays its native synth fallback.
The offline exporter uses the same path, so live playback and exported WAV
resolve instruments identically. The interface draws pixels through
`kitty-terminal-session` and `soft-raster`, and falls back to the ANSI cell
editor. Details are in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Documentation

- [docs/build.md](docs/build.md): build, qualification gates and headless use.
- [docs/controls.md](docs/controls.md): every editor key and mouse action.
- [docs/state-and-files.md](docs/state-and-files.md): projects, autosave
  ownership, settings and the bounded workstation limits.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): threads, snapshots, compiler
  and effects.
- [docs/PROJECT-FORMAT.md](docs/PROJECT-FORMAT.md): the `.ktech` format.
- [docs/SOUNDBANKS.md](docs/SOUNDBANKS.md): the optional soundbanks.
- [docs/PROVENANCE.md](docs/PROVENANCE.md): the clean-room boundary.
- [presets/README.md](presets/README.md): the built-in synthesis bank.
- [THIRD_PARTY.md](THIRD_PARTY.md): notices.

## Related repositories

Submodules under `third_party/`:

- [chip-sequencer](https://github.com/itsmygithubacct/chip-sequencer#readme):
  the synth and pattern sequencer engines.
- [pcm-mixer](https://github.com/itsmygithubacct/pcm-mixer#readme): the audio
  thread, sink and asset layer.
- [kitty-terminal-session](https://github.com/itsmygithubacct/kitty-terminal-session#readme):
  Kitty framebuffer and keyboard lifecycle.
- [soft-raster](https://github.com/itsmygithubacct/soft-raster#readme): the
  pixel rasterizer.
- [kilix-state](https://github.com/itsmygithubacct/kilix-state#readme):
  crash-safe bounded state storage.

[kilix-content](https://github.com/itsmygithubacct/kilix-content#readme) holds
the catalog entry and [kilix](https://github.com/itsmygithubacct/kilix#readme)
the `kilix install` verb that also installs the soundbanks.

## Part of Plebian-OS

Kilix Techno is one of the repositories behind
[Plebian-OS](https://github.com/itsmygithubacct/plebian-os#readme), a Debian
install whose desktop is [Kilix](https://github.com/itsmygithubacct/kilix#readme)
running the [Kilix 95](https://github.com/itsmygithubacct/kilix-95#readme)
desktop through [Pleb](https://github.com/itsmygithubacct/pleb#readme).
It is installed on demand from the Kilix content catalog, not shipped in the
installer image. The map of every repository and how it ships is on the
[components page](https://github.com/itsmygithubacct/plebian-os/blob/main/docs/components.md).

## License and provenance

Kilix Techno code is licensed under the MIT License. All application code,
native presets, colors, text, and fixtures are original to this repository.
Optional sample data retains its separately recorded CC0 dedication and is not
relicensed as MIT. Exact notices are in [THIRD_PARTY.md](THIRD_PARTY.md), and
the clean-room boundary is documented in
[docs/PROVENANCE.md](docs/PROVENANCE.md). Research and design records are kept
outside the publication repository.
