# Third-party software

Kilix Techno uses the following MIT-licensed Kilix libraries as exact Git
submodules. Each project retains its own copyright notices and root `LICENSE`.
The SHA-256 values below identify those license files.

| Component | Repository | Commit | SPDX | License SHA-256 |
| --- | --- | --- | --- | --- |
| chip-sequencer | `https://github.com/itsmygithubacct/chip-sequencer.git` | `972e67b9d24bc1e35aef4fdaaad96922cb3fe033` | MIT | `ff220b9002c501264f5d8e0985a1699e4a9ebc6ef803b4152eb2f18cf6ba71c9` |
| pcm-mixer | `https://github.com/itsmygithubacct/pcm-mixer.git` | `0f391617a1363bc15c347abedfad19a18745240a` | MIT | `ef34479183d442f9ec37c2c53154fd8f1477c575dab7aa403660f342fba74ca3` |
| kitty-terminal-session | `https://github.com/itsmygithubacct/kitty-terminal-session.git` | `b6b4e1e4b6933d60bfc919a90d8e1f6f86b20e77` | MIT | `938e0db7e6acd7e99d205863bcbdeda47f5fbd053802519f411dbefe0799071e` |
| soft-raster | `https://github.com/itsmygithubacct/soft-raster.git` | `2b0c241729dec182a87ebab78edd10b142edf828` | MIT | `94f7ac89fd4e70ab0e837fa993772c21059576904f395186b257c5975910a6e2` |
| kilix-state | `https://github.com/itsmygithubacct/kilix-state.git` | `35f742d6cf2f7635b43157fcf6f2e0264bf0d761` | MIT | `938e0db7e6acd7e99d205863bcbdeda47f5fbd053802519f411dbefe0799071e` |

`kitty-terminal-session` recursively pins these transitive MIT dependencies:

| Component | Repository | Commit | SPDX | License SHA-256 |
| --- | --- | --- | --- | --- |
| kitty-framebuffer | `https://github.com/itsmygithubacct/kitty-framebuffer.git` | `43277b4339133c473763fad28ef6057f2819fb9a` | MIT | `127d7d3f19cc8fde3f5c48c99a59befb38fb305e318dff778628bda1d4ece453` |
| kitty-input | `https://github.com/itsmygithubacct/kitty-input.git` | `fca32e2d0f641344eeadb8f242147225d41bff55` | MIT | `938e0db7e6acd7e99d205863bcbdeda47f5fbd053802519f411dbefe0799071e` |

The application requires the system C/POSIX libraries, pthreads, libm, zlib,
libsndfile (`LGPL-2.1-or-later`), and FluidSynth
(`LGPL-2.1-or-later`). The last two back the control-thread WAV/OGG/SFZ/SF2
asset layer and are dynamically linked platform packages, not bundled source.

## Optional sample data

No sample data is committed or covered by the root MIT license. The Kilix
launcher can explicitly install checksum-pinned subsets from these sources;
each installed directory retains a source record, file checksums, and license
notice.

| Pack | Pinned revision/release | Sample license |
| --- | --- | --- |
| TidalCycles TR-808 Fischer | `85fbecf1bec32553395625ea659e2a56dfd7c0e1` | CC0-1.0 |
| Stargate Sample Pack | `dbfd6ec52d4ed53b60bdbea5fc6adf295127c027` | CC0-1.0 |
| scoredata-forge outputs | `2ff340297d6e70fe5ce3fee257fcf1abe0ee273d` | CC0-1.0 outputs; MIT generator |
| MechSounds | `260301` | CC0-1.0 sounds |
| Karoryfer Cowsynth | `v1.001` | CC0-1.0 |
| VCSL | `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e` | CC0-1.0 |
| Ongenet procedural kit | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 |
| VCSL acoustic SFZ subset via Ongenet | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 |
| VSCO 2 CE SFZ subset via Ongenet | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 |
| ChaosBank SF2 via Ongenet / bratpeki mirror | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 as declared by distributor |
| JNS-GM 2 SF2 via Ongenet / bratpeki mirror | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 as declared by distributor |
| Masterpiece SF2 via Ongenet / bratpeki mirror | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 as declared by distributor |
| Unison SF2 via Ongenet / bratpeki mirror | `4ac5ff452134866e923d19f58451aad64804cba1` | CC0-1.0 as declared by distributor |
