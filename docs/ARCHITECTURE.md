# Architecture

Kilix Techno separates mutable editor state from immutable audio snapshots.
The main thread owns the project, actions, undo history, compilation, save
requests, terminal input, and presentation. The `pcm-mixer` thread invokes a
stereo generator backed by four synchronized `chip-sequencer` engines, one per
bounded bus. Compiled songs are replaced only through their row/beat boundary
APIs. The older snapshot is reclaimed only after all four engines acknowledge
the generation; acknowledging one bus is not sufficient.

The control thread loads complete optional soundbank receipts before compiling
the first scene. `pcm-mixer`'s optional asset layer uses libsndfile for WAV and
OGG, resolves a bounded matching WAV/OGG region from SFZ, and uses FluidSynth
to render a bounded SF2 preset. Decode, downmix, rate conversion, SF2 rendering,
and uniform peak normalization happen there, never in the audio callback. The
dependency-free realtime mixer only receives the resulting immutable PCM.
Those allocations outlive every active and pending snapshot. A sample role with
no valid installed PCM is compiled as its native synth-family fallback. The
offline exporter builds its own rate-matched bank, so live playback and WAV
export resolve instruments through the same path.

Each scene has eight tracks. A control-side compiler expands their independent
allowed loop lengths into a bounded 192-row supercycle, then resolves
probability and ratchets over a fixed 64-supercycle horizon with a specified
integer generator. A separate compiler expands at most 256 arrangement bars
from named patterns and scenes. At runtime only one active and one pending song
and control snapshot exist. Track routing is one coalesced gain/pan/mute batch
per bus; solo and mute groups are application policies translated into those
batches. The control thread evaluates sparse automation and macro modulation
at playhead-row changes. Offline export calls the same control poll before each
render block.

The live callback renders all four engines, applies each bus's fixed-allocation
native effect rack, sums the buses, then applies master level, tone, saturating
drive, and a bounded tempo delay. The six effects are filter, saturator, tape
delay, room, compressor, and bitcrush. The callback does not allocate, free,
log, access files, create processes, or acquire an application lock.
`pcm-mixer` alone owns sink and transport policy. A no-sink start is a visible,
editable state rather than an error that closes the application.

Saving and export take value snapshots. Project and settings encoding and
decoding complete against temporary objects before publication. Files are
written to private sibling temporaries, synchronized, and atomically renamed.
Offline export runs on a cancellable worker with independent sequencer, bus,
effect, and DSP state, so it cannot perturb live playback. WAV supports
16/24/32-bit PCM, optional peak normalization, master bypass, effect tails, and
full arrangements. Stem files are rendered to hidden siblings and published
as a rollback-safe set. MIDI has a separate bounded SMF decoder and atomic
publisher. Completion crosses one release/acquire atomic flag before the UI
reads the worker result.

The XDG state directory contains `settings.state` and `autosave.state` CRC
records. Settings persist semantic key and any-gamepad bindings, not device
numbers or command strings. Corrupt settings remain untouched. A corrupt
autosave is quarantined to a fixed diagnostic sibling without replacement; if
that name is occupied, automatic writes remain disabled until the user makes
an explicit save decision.

Both UI paths consume the same semantic action and view model. The preferred
path composes pixels through `soft-raster` and presents through
`kitty-terminal-session`. The fallback emits a bounded ANSI grid after the
failed pixel session has restored terminal state. Terminal teardown always
precedes process exit.

Project schema 3 adds fixed-capacity patterns, arrangement entries, automation,
macros, buses/effects, export presets, and resource manifests behind the 1 MiB
decode ceiling. Legacy schemas migrate to canonical defaults. User WAV/OGG
resources are private copies; dependency-bearing SFZ/SF2 imports are rendered
to self-contained WAV. Manifest entries record bytes, role, format, license,
and SHA-256, and path traversal never follows symlinks.
