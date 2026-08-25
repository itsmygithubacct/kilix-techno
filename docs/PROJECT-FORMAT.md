# Kilix Techno project format

The portable `.ktech` format has MIME type `application/x-kilix-techno`.
The format is a canonical little-endian binary record with a 1 MiB hard limit.
It contains no pointers, native padding, absolute paths, executable strings,
plug-ins, environment values, or sink configuration.

The fixed header contains the eight-byte magic `KTECHN01`, header byte count,
schema version, total byte count, payload CRC-32, project seed, tempo, swing,
master controls, active scene, and reserved-zero bytes. It is followed by all
sixteen scenes in numeric order. Every scene contains all eight tracks; every
track contains its role, allowed loop length, level, pan, mute, solo, and all
sixty-four step records. Each step stores enabled, note, velocity, gate,
probability, and ratchet in bounded integer units. The scene grid is followed
by up to 96 semantic action bindings and a UTF-8 project name record
length-prefixed and capped at 63 bytes. Bindings may name a key/modifier pair,
an any-gamepad button, or an any-gamepad axis direction; raw device numbers and
commands are invalid.

Schema 3 then carries the `KTX30001` workstation extension. Its header stores
bounded counts, performance fill/variation, selected/queued/audible arrangement
state, the scene-to-pattern table, eight track-to-bus routes, and eight mute
group masks. Counted canonical sections follow for:

- 128 named single-track patterns, each with 64 complete step records;
- 1,024 arrangement entries, additionally capped at 256 total four-beat bars;
- 32 automation lanes with at most 256 sorted points each and 8,192 total;
- eight macros with eight semantic parameter targets each;
- four buses, sixteen native effects project-wide, and four effects per chain;
- eight WAV/stems/MIDI export presets;
- 64 relative-path resources with license, SHA-256, byte count, format, and
  instrument role.

Unused capacity is not encoded. References must address a counted object or
use the defined none sentinel. Automation rows are monotonic and bounded by the
256-bar song horizon. Resource paths are relative and may not contain parent
traversal or backslashes.

Canonical encoding has a fixed scene-grid prefix and count-sized workstation
sections. Reserved bytes and bits must be zero, unused steps beyond a track's
loop length must have canonical defaults, booleans must be zero or one, enum
values and loop lengths must be known, and no trailing bytes are accepted. A
decoder validates the complete record and CRC into a temporary project before
replacing caller state.

Schema migrations are explicit whole-record conversions. A writer emits only
the current schema after a successful decode or new-project construction.
Failed, truncated, oversized, noncanonical, or future-schema inputs never
partially update an open project.

Schema 2 expanded the track-role enum from the nine native synth families to
the fixed curated identifiers documented in `SOUNDBANKS.md`; its record layout
otherwise remained unchanged. Schema-1 projects are accepted only with their
original native-role range. Schema-1 and schema-2 inputs receive canonical
schema-3 workstation defaults and are emitted as schema 3 on the next save.

## Interchange and resources

Standard MIDI import accepts bounded SMF format 0 or 1 with PPQN timing,
strict variable-length quantities, canonical track ends, at most 32 tracks,
1 MiB of input, and 8,192 note-on events. Export writes format 1 with a tempo
conductor track and eight musical tracks. MIDI conversion is deliberately
lossy and does not weaken `.ktech` validation.

The resource manifest never embeds absolute paths or grants a license. Import
requires a caller-supplied license label, a regular non-symlink source no
larger than 64 MiB, and successful bounded decoding. WAV/OGG inputs are copied;
SFZ/SF2 inputs are rendered to self-contained mono WAV so their external sample
graphs do not become hidden project dependencies. Integrity reports classify
each resource as available, missing, changed, or unsafe.

## Settings record

`settings.state` is a separate `kilix-state` CRC record. Its canonical
version-one payload begins with
`KSETN001`, a fixed header size, schema and total size, followed by bounded
repeat/dead-zone preferences, reduced-motion state, and up to 96 semantic
binding records. Each binding identifies an application action and either a
key/modifier pair, an any-gamepad button, or an any-gamepad axis direction.
Device paths, numbered gamepad identities, commands, environment values, and
trailing bytes are not accepted. Decode failure leaves the caller's settings
unchanged.

Global settings supply the binding and reduced-motion defaults copied into a
new project. An opened `.ktech` retains its own semantic binding records, while
neither record may carry a device path, executable command, or numbered
gamepad identity.
