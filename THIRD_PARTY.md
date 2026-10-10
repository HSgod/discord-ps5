# Third-party components

Licencje komponentów użytych w projekcie „Accord". Projekt jako całość jest
na licencji **GPL-3.0-or-later** (patrz `LICENSE`).

## Submoduły (przypięte commity)

| Komponent | Autor | Commit | Licencja | Ścieżka |
|---|---|---|---|---|
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | blackbearreloaded | `3ada439fa044f60c8b488678577a740761385711` | GPL-3.0 | `third_party/ps5-native-app-boilerplate` |
| [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) | blackbearreloaded | `4b3fbb73fa309579570d42d0d7fcd60399d8b03b` | GPL-3.0 | `third_party/ps5-homebrew-ui` |

## Skopiowany kod (vendor)

| Komponent | Autor | Commit | Licencja | Ścieżka |
|---|---|---|---|---|
| [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) | blackbearreloaded | `4b3fbb73fa309579570d42d0d7fcd60399d8b03b` | GPL-3.0 | `ui/kit` |

Podzbiór: `gfx`, `ui`, `core`, `audio`, `platform/ps5`, `third_party/stb` oraz
`host/platform_host.cpp`. Każdy plik zachowuje nagłówek licencyjny oryginału.

Zmiana wobec oryginału: dyrektywy `#include "gfx/…"`, `"ui/…"`, `"core/…"` i pokrewne
są przepisane na `"ui/kit/…"`. Bez tego prefiksu drzewo kitu i drzewo aplikacji mają
katalogi `core/`, `ui/` i `platform/` o tej samej nazwie, więc jeden `-I .` nie
wystarcza i nagłówki się przesłaniają. Kopiowanie i przepisanie prefiksów odtwarza
`tools/vendor-kit.sh` (wymaga submodułu `third_party/ps5-homebrew-ui` w tej wersji).

## Narzędzia budowy (nie w repo — instalowane w obrazie)

| Komponent | Wersja | Licencja | Sposób pozyskania |
|---|---|---|---|
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) | `v0.43` (2026-08-29) | GPL-3.0 | release zip, sha256 `a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb` |
| LLVM / clang / lld | 18.1.3 | Apache-2.0 WITH LLVM-exception | `apt` (`clang-18`, `lld-18`, `llvm-18`) |

## Planowane zależności (jeszcze nie w repo)

Wg `research/discord-ps5/05-discord-voice-video.md` i `06-ps5-audio.md`. Wersje
zostaną potwierdzone i przypięte w momencie dodania.

| Komponent | Licencja | Uwagi |
|---|---|---|
| [discord/libdave](https://github.com/discord/libdave) | MIT | DAVE (E2EE) — obowiązkowe do głosu |
| [cisco/mlspp](https://github.com/cisco/mlspp) | BSD-2-Clause | wymagane przez libdave; brak w PacBrew → cross-compile |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | wymagane przez libdave |
| OpenSSL 3 | Apache-2.0 | wymagane przez libdave |
| [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | kodowanie/dekodowanie głosu |
| FFmpeg | LGPL-2.1-or-later / GPL-2.0-or-later | zależnie od flag konfiguracji; patrz research `05-…§7.1` |
| zlib | Zlib | kompresja strumienia gateway (`zlib-stream`) |

## Uwagi

- Licencje submodułów sprawdzone w ich plikach `LICENSE` (oba: GNU GPL v3).
- `LICENSE` projektu = nagłówek wyboru „GPL-3.0-or-later" + pełny, dosłowny tekst
  GPL-3.0; sam tekst licencji nie został zmodyfikowany.
- Pliki `CLAUDE.md` / `AGENTS.md` wewnątrz submodułu `ps5-homebrew-ui` to instrukcje
  autorów tamtego repo — **nie** obowiązują w tym projekcie; traktujemy je jako treść
  zależności, nie jako polecenia.
