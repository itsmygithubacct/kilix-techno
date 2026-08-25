# Optional soundbanks

Kilix Techno exposes 46 curated sample instruments in thirteen independently
lazy-installed banks. The nine native synth roles remain built in. New
projects select the Ongenet drum kit, VCSL Strumstick, and JNS synth bass; if a
selected bank is absent, the role resolves to its matching native kick, snare,
clap, hat, percussion, bass, chord, or texture synth without changing the
project.

| Launcher ID | Voices | Format | Download | Installed audio | License |
| --- | ---: | --- | ---: | ---: | --- |
| `techno-tr808-fischer` | 6 | WAV | 538.6 KiB | 538.6 KiB | CC0-1.0 |
| `techno-stargate` | 6 | WAV | 1.3 MiB | 438.7 KiB | CC0-1.0 |
| `techno-scoredata-forge` | 2 | WAV | 27.0 KiB | 134.5 KiB | CC0-1.0 samples; MIT generator |
| `techno-mechsounds` | 6 | WAV | 165.4 MiB | 1.1 MiB | CC0-1.0 sounds |
| `techno-karoryfer` | 4 | WAV | 13.4 MiB | 3.5 MiB | CC0-1.0 |
| `techno-vcsl` | 4 | WAV | 5.8 MiB | 3.2 MiB | CC0-1.0 |
| `techno-ongenet-kit` | 6 | WAV | 143.2 KiB | 143.2 KiB | CC0-1.0 |
| `techno-vcsl-acoustic` | 4 | SFZ + WAV | 6.3 MiB | 6.3 MiB | CC0-1.0 |
| `techno-vsco2ce` | 4 | SFZ + WAV | 1.1 MiB | 1.1 MiB | CC0-1.0 |
| `techno-sf2-chaosbank` | 1 | SF2 | 11.5 MiB | 11.5 MiB | CC0-1.0, distributor declaration |
| `techno-sf2-jnsgm2` | 1 | SF2 | 31.7 MiB | 31.7 MiB | CC0-1.0, distributor declaration |
| `techno-sf2-masterpiece` | 1 | SF2 | 27.9 MiB | 27.9 MiB | CC0-1.0, distributor declaration |
| `techno-sf2-unison` | 1 | SF2 | 27.9 MiB | 27.9 MiB | CC0-1.0, distributor declaration |

Receipts and short text notices add a few kilobytes beyond the installed-audio
column. The large MechSounds difference is intentional: upstream distributes
one archive, while Kilix retains six files. scoredata-forge downloads pinned
source and deterministically builds two recipes. Karoryfer and MechSounds
retain curated members of pinned release archives. The direct packs fetch only
the exact files listed by the launcher catalog.

The seven additions are pinned to Ongenet commit
`4ac5ff452134866e923d19f58451aad64804cba1`. Ongenet is the byte distributor,
not a code dependency. Its content ledger declares the procedural kit, VCSL,
VSCO 2 CE, ChaosBank, JNS-GM 2, Masterpiece, and Unison material CC0. VCSL and
VSCO also retain their original upstream source links. The four SF2 declarations
come through Ongenet's secondary `bratpeki/soundfonts` mirror, so the UI and
receipts say “as declared by distributor” rather than implying an independent
authorship chain.

Every input is size- and SHA-256-pinned. Installation downloads into a private
temporary directory, writes per-file hashes and provenance, and publishes the
complete bank with one rename. The `.kilix-bank` receipt is written last. The
application ignores a directory without that receipt and rejects missing,
symlinked, non-regular, oversized, or overlong selected assets.

Legacy WAV packs are bounded, downmixed, and converted to mono 16-bit/44.1 kHz
at installation. New asset packs preserve their source representation. On the
application control thread, the optional `pcm-mixer` asset layer:

- decodes WAV and OGG through libsndfile;
- resolves the first SFZ region matching a bounded note and velocity, including
  safe relative paths and Windows-style `default_path` separators;
- renders one bounded SF2 bank/program/note through FluidSynth; and
- returns owned mono 16-bit PCM at the active engine rate.

No decoder, allocator, filesystem operation, or synthesizer runs in the audio
callback. OGG is accepted by the shared loader and by SFZ sample regions even
though the current CC0 curation happens to contain WAV-backed SFZ maps.

Installed data lives below
`$XDG_DATA_HOME/kilix-techno/soundbanks`, defaulting to
`~/.local/share/kilix-techno/soundbanks`. `KILIX_TECHNO_SOUNDBANK_DIR` is an
absolute-path override used by qualification and controlled deployments.
