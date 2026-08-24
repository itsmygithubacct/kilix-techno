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
probability, and ratchet in bounded integer units. A final UTF-8 project name
record is length-prefixed and capped at 63 bytes.

Canonical encoding is fixed-size apart from the name. Reserved bytes and bits
must be zero, unused steps beyond a track's loop length must have canonical
defaults, booleans must be zero or one, enum values and loop lengths must be
known, and no trailing bytes are accepted. A decoder validates the complete
record and CRC into a temporary project before replacing caller state.

Schema migrations are explicit whole-record conversions. A writer emits only
the current schema after a successful decode or new-project construction.
Failed, truncated, oversized, noncanonical, or future-schema inputs never
partially update an open project.
