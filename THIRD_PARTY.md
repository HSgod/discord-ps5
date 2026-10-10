# Third-party components

Licences of the components used by the "Accord" project. The project as a whole is
licensed under **GPL-3.0-or-later** (see `LICENSE`).

## Submodules (pinned commits)

| Component | Author | Commit | Licence | Path |
|---|---|---|---|---|
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | blackbearreloaded | `3ada439fa044f60c8b488678577a740761385711` | GPL-3.0 | `third_party/ps5-native-app-boilerplate` |
| [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) | blackbearreloaded | `4b3fbb73fa309579570d42d0d7fcd60399d8b03b` | GPL-3.0 | `third_party/ps5-homebrew-ui` |

## Vendored code

| Component | Author | Commit | Licence | Path |
|---|---|---|---|---|
| [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) | blackbearreloaded | `4b3fbb73fa309579570d42d0d7fcd60399d8b03b` | GPL-3.0 | `ui/kit` |

Subset: `gfx`, `ui`, `core`, `audio`, `platform/ps5`, `third_party/stb` and
`host/platform_host.cpp`. Every file keeps the original's licence header.

Change against the original: the `#include "gfx/…"`, `"ui/…"`, `"core/…"` and related
directives are rewritten to `"ui/kit/…"`. Without that prefix the kit's tree and the
application's tree both have directories named `core/`, `ui/` and `platform/`, so a
single `-I .` is not enough and the headers shadow each other. The copy and the prefix
rewrite are reproduced by `tools/vendor-kit.sh` (it needs the `third_party/ps5-homebrew-ui`
submodule at this revision).

## Libraries linked into the artifacts

Three artifacts these libraries end up in:

- `eboot.bin` — the title (`dist/PPSA99070.ffpkg`): staging plus the boilerplate harness,
- `accordd.elf` — the daemon payload (`dist/accordd.elf`): `tools/build-daemon-payload.sh`,
- `build/host-tests` — the host tests (`make test`), and `build/hui_snapshots`
  (`make host-snapshots`, drawn with the container's Mesa).

All of them are fetched or built into `build/`, `third_party/ps5-opengl` and
`third_party/yyjson`, which git ignores; the repository holds only the scripts with the
pinned versions.

| Component | Version / pin | Licence | Where it is linked |
|---|---|---|---|
| [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | release `v1.0.0`, archive sha256 `f93643c04c843d56143b00951df1f8042ea7706ae9f19e4e9abf158e1ead77c5`, manifest sha256 `f4b91f672be037fbac3f82494f1225deaf4c227a03f37ac3ffa56abb213b943f` | GPL-3.0-or-later (the project's own code) plus **Mesa 26.2.0**, primarily MIT, and per-component licences in `LICENSES/` and `THIRD_PARTY_NOTICES.md` | `eboot.bin`, statically: `APP_STATIC_ARCHIVES=third_party/ps5-opengl/libps5opengl-group.a` (`tools/prepare-opengl.sh` puts `libPS5OpenGL.a` in a group with the unwinder, `libc++abi`, `libc++` and clang's builtins out of the payload SDK) |
| [libdave](https://github.com/discord/libdave) | commit `8de72b1f8a2ac3c5a5270755bb8091a62e3c6169` | MIT | `accordd.elf` (voice E2EE, T6.0-2) |
| [cisco/mlspp](https://github.com/cisco/mlspp) | commit `fc724c3100ce3b5d8565dbd6d93648a440991a8c` | BSD-2-Clause (the licence at the repository root covers `lib/hpke`, `lib/tls_syntax` and `lib/bytes` too: those carry no licence file of their own) | `accordd.elf`: `libmlspp.a`, `libhpke.a`, `libtls_syntax.a`, `libbytes.a` (`libmls_ds.a` and `libmls_vectors.a` are built but not linked) |
| [nlohmann/json](https://github.com/nlohmann/json) | tag `v3.11.3` | MIT | nowhere directly: it is a header-only dependency of mlspp and goes into mlspp's objects when those are built (`tools/dave/build-mlspp.sh`). **Forbidden in `core/`** — it throws, and `JSON_NOEXCEPTION` turns an error into `abort()`; see T3.2 |
| [OpenSSL](https://github.com/openssl/openssl) | 3.5.2 (tarball plus the sha256 OpenSSL itself publishes) | Apache-2.0 | `libcrypto.a` in `accordd.elf` (reached through mlspp); `libssl.a` is built in the same prefix and waits for the TLS client of T3.1 |
| [yyjson](https://github.com/ibireme/yyjson) | 0.13.0, commit `6447536015f3d600f3d65323b10976103b337ca7` | MIT | `core/json.cpp` → `build/host-tests`, `accordd.elf` and `eboot.bin` (T3.2) |

## Build tools (not in the repository — installed in the image)

| Component | Version | Licence | How it is obtained |
|---|---|---|---|
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) | `v0.43` (2026-08-29) | GPL-3.0 | release zip, sha256 `a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb` |
| LLVM / clang / lld | 18.1.3 | Apache-2.0 WITH LLVM-exception | `apt` (`clang-18`, `lld-18`, `llvm-18`) |

## Planned dependencies (not in the repository yet)

From `research/discord-ps5/05-discord-voice-video.md` and `06-ps5-audio.md`. Versions
will be confirmed and pinned when they are added. Nothing in this table is linked yet.

| Component | Licence | Notes |
|---|---|---|
| [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | voice encoding and decoding |
| FFmpeg | LGPL-2.1-or-later / GPL-2.0-or-later | depending on the configuration flags; see research `05-…§7.1` |
| zlib | Zlib | gateway stream compression (`zlib-stream`) |

## Notes

- The submodules' licences were checked in their `LICENSE` files (both: GNU GPL v3).
- The licences in the "linked" table above were checked in the files on disk after
  fetching: `build/dave/src/libdave/LICENSE` (MIT, Discord 2024),
  `build/dave/src/mlspp/LICENSE` (BSD-2-Clause, Cisco 2018),
  `build/dave/src/nlohmann-json/LICENSE.MIT`,
  `third_party/ps5-opengl/{LICENSE,LICENSES/,THIRD_PARTY_NOTICES.md}` and
  `third_party/yyjson/LICENSE` (MIT).
- The project's `LICENSE` is the "GPL-3.0-or-later" selection notice plus the complete,
  verbatim GPL-3.0 text; the licence text itself was not modified.
- The `CLAUDE.md` / `AGENTS.md` files inside the `ps5-homebrew-ui` submodule are
  instructions written by that repository's authors — they do **not** apply to this
  project; we treat them as dependency content, not as instructions.
- The release date of yyjson 0.13.0 is not confirmed by a source: the repository
  records the version and the commit SHA, not the date of the tag.
