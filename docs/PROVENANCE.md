# Provenance and clean-room boundary

Kilix Techno is an original terminal application written for this repository.
Its source, interaction design, project format, native patches, music
fixtures, wording, and color palette were authored for Kilix Techno under the
root MIT license.

No source, markup, script, style sheet, image, audio, preset, project data,
branding, or compatibility code from an unrelated web application is part of
this repository. The product is not a source port,
browser package, visual reproduction, or file-format clone. Public Kilix
library APIs and long-established synthesis and sequencing techniques are the
only external technical inputs beyond the dependencies listed in
`THIRD_PARTY.md`.

Native audio uses oscillator, envelope, noise, wavetable, and tracker
techniques implemented by `chip-sequencer`; it includes no recordings or
transcribed melodies. Optional recordings are downloaded only by explicit
launcher action from the pinned, redistribution-permitting sources listed in
`SOUNDBANKS.md`. They remain outside the application repository and retain
their own license receipts. Ongenet is used only as a pinned distributor of
declared-CC0 media bytes and license metadata; no Ongenet application source,
interface, presets, or project data is copied. Test arrangements are
intentionally short and original.

`make clean-room-check` scans application-owned tracked paths for excluded web
and media source types, rejects unexpected submodule pins or license digests,
and invokes the fleet repository-hygiene gate. Dependency trees are checked as
dependencies, not treated as application-authored content.
