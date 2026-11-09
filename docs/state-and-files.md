# State, files and limits

How projects, autosave and settings are stored, and the bounded workstation
model. This text was moved from the README; the [README](../README.md) is the
short entry point.

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
removed when its session exits with nothing unsaved. Ownership is a claim: the
owner holds an exclusive lock on `autosave.owner` (which records its pid) for
the life of the session, so two sessions that both find the shared slot absent
cannot both take it; the lock dies with the process, so a crashed session
leaves nothing to reclaim. A session slot uses the same format as
`autosave.state`; to resume one, copy it over `autosave.state` while no session
is running and launch without arguments. If the final autosave on quit fails,
the failure is printed after the interface closes.

`settings.state` stores canonical semantic binding defaults—never device paths
or commands—and reduced-motion preference below the normal XDG data directory.
Those defaults seed new projects; opened projects retain their own safe
semantic binding records. Projects and settings are bounded and decoded into
temporary objects before caller state changes.

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
