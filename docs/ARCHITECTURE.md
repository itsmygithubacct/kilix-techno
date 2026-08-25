# Architecture

Kilix Techno separates mutable editor state from immutable audio snapshots.
The main thread owns the project, actions, undo history, compilation, save
requests, terminal input, and presentation. The `pcm-mixer` thread invokes a
stereo generator backed by one `chip-sequencer` engine. A compiled song is
replaced only through the sequencer's row/beat boundary API, and its generation
acknowledgement is the reclamation fence for the older snapshot.

The control thread loads complete optional soundbank receipts before compiling
the first scene. Strict canonical mono WAV decoding, integer linear resampling,
and uniform peak normalization happen there, never in the audio callback. The
resulting immutable
PCM allocations outlive every active and pending snapshot. A sample role with
no valid installed PCM is compiled as its native synth-family fallback. The
offline exporter builds its own rate-matched bank, so live playback and WAV
export resolve instruments through the same path.

Each scene has eight tracks. A control-side compiler expands their independent
allowed loop lengths into a bounded 192-row supercycle, then resolves
probability and ratchets over a fixed 64-supercycle horizon with a specified
integer generator. At runtime only the active and pending compiled snapshots
exist. Track mixer controls are submitted as one gain/pan/mute batch and
identical batches are coalesced; solo is an application policy translated into
that batch.

The live callback renders interleaved stereo samples, then applies master level,
tone, saturating drive, and a bounded tempo delay from preallocated state. It
does not allocate, log, access files, create processes, or acquire an
application lock. `pcm-mixer` alone owns audio-sink process and transport
policy. A no-sink start is a visible, editable state rather than an error that
closes the application.

Saving and export take value snapshots. Project and settings encoding and
decoding complete against temporary objects before publication. Files are
written to private sibling temporaries, synchronized, and atomically renamed.
Offline export runs on a cancellable worker with an independent sequencer and
DSP state, so it cannot move or perturb live playback. Completion crosses one
release/acquire atomic flag before the UI reads the worker result.

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
