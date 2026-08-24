# Kilix Techno project format

The portable `.ktech` format has MIME type `application/x-kilix-techno`.
Version 1 is a canonical little-endian binary record with a 1 MiB hard limit.
It contains no pointers, native padding, absolute paths, executable strings,
plug-ins, environment values, or sink configuration.

The fixed header contains the eight-byte magic `KTECHN01`, header byte count,
schema version, total byte count, payload CRC-32, project seed, tempo, swing,
master controls, active scene, and reserved-zero bytes. It is followed by all
sixteen scenes in numeric order. Every scene contains all eight tracks; every
track contains its role, allowed loop length, level, pan, mute, solo, and all
sixty-four step records. Each step stores enabled, note, velocity, gate,
probability, and ratchet in bounded integer units. The scene grid is followed
by up to 96 semantic action bindings and a final UTF-8 project name record
length-prefixed and capped at 63 bytes. Bindings may name a key/modifier pair,
an any-gamepad button, or an any-gamepad axis direction; raw device numbers and
commands are invalid.

Canonical encoding is fixed-size apart from the name. Reserved bytes and bits
must be zero, unused steps beyond a track's loop length must have canonical
defaults, booleans must be zero or one, enum values and loop lengths must be
known, and no trailing bytes are accepted. A decoder validates the complete
record and CRC into a temporary project before replacing caller state.

Schema migrations are explicit whole-record conversions. A writer emits only
the current schema after a successful decode or new-project construction.
Failed, truncated, oversized, noncanonical, or future-schema inputs never
partially update an open project.

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
