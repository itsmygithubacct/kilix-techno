# Optional soundbanks

Kilix Techno exposes 28 curated sample instruments in six independently lazy
installed banks. The nine native synth roles remain built in. New projects use
a selection of sample roles immediately; before their bank is installed they
resolve to the matching kick, snare, clap, hat, percussion, bass, chord, or
texture synth without changing the project.

| Launcher ID | Curated voices | Download | Canonical samples | License |
| --- | ---: | ---: | ---: | --- |
| `techno-tr808-fischer` | 6 | 538.6 KiB | 538.6 KiB | CC0-1.0 |
| `techno-stargate` | 6 | 1.3 MiB | 438.7 KiB | CC0-1.0 |
| `techno-scoredata-forge` | 2 | 27.0 KiB | 134.5 KiB | CC0-1.0 samples; MIT generator |
| `techno-mechsounds` | 6 | 165.4 MiB | 1.1 MiB | CC0-1.0 sounds |
| `techno-karoryfer` | 4 | 13.4 MiB | 3.5 MiB | CC0-1.0 |
| `techno-vcsl` | 4 | 5.8 MiB | 3.2 MiB | CC0-1.0 |

The installed number covers normalized sample files; receipts and short text
notices add a few kilobytes. The large MechSounds difference is intentional:
upstream distributes one archive, while Kilix retains only six selected files.
Stargate, TR-808 Fischer, and VCSL are fetched as exact individual files.
scoredata-forge downloads an exact source archive and deterministically builds
only its growl and house-stab recipes. Karoryfer and MechSounds download exact
release archives and retain only the listed subset.

Every input is size- and SHA-256-pinned. Installation downloads into a private
temporary directory, decodes bounded PCM WAV, downmixes and resamples to
mono/16-bit/44.1 kHz, writes per-file hashes and provenance, then publishes the
complete bank with one rename. The `.kilix-bank` receipt is written last. The
application ignores a directory without that receipt and rejects symlinked or
non-regular sample files.

Installed data lives below
`$XDG_DATA_HOME/kilix-techno/soundbanks`, defaulting to
`~/.local/share/kilix-techno/soundbanks`. `KILIX_TECHNO_SOUNDBANK_DIR` is an
absolute-path override used by qualification and controlled deployments.
