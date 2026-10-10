# Accord

Natywna aplikacja homebrew na PlayStation 5 (OnionHEN): **nieoficjalny klient zgodny z Discordem**.
Nazwa aplikacji to **Accord**; słowo „Discord" występuje w tym repozytorium wyłącznie jako nazwa usługi
i protokołu, z którym się łączymy.

To nie jest oficjalna aplikacja Discorda i nie ma z Discord Inc. żadnego związku ani zgody tej firmy.
Klient loguje się zwykłym kontem użytkownika, co może naruszać regulamin Discorda — decyzja o użyciu
należy do osoby, która go uruchamia.

## Jak to jest zbudowane

Jeden rdzeń, dwa fronty (decyzja z 2026-10-10):

| Część | Co robi | Czego nie robi |
|---|---|---|
| `core/` | protokół, JSON, stan czatu, bufory audio — bez zależności od PS5 | nie zna frontu ani procesu |
| `daemon/` (payload `.elf`) | **jedyny** właściciel sesji: token, Gateway, REST, głos, audio; API HTTP dla frontów | nie rysuje OpenGL |
| tytuł `PPSA99070` (`.ffpkg`) | front poza grą: pokazuje stan demona i wysyła mu polecenia przez `127.0.0.1` | nie loguje się, nie trzyma tokenu, nie otwiera portów audio |

## Budowanie

Buduje się w kontenerze (Docker; na macOS przez colima). `scripts/dev.sh` montuje repozytorium w `/work`.

```
make test             testy jednostkowe hosta (natywny toolchain, bez kontenera)
make payload          demon -> dist/accordd.elf        (payload SDK)
make app              tytuł -> dist/PPSA99070          (boilerplate + kit UI)
make ffpkg            tytuł spakowany -> dist/PPSA99070.ffpkg
make host-snapshots   podgląd sześciu ekranów -> build/snapshots/*.png
```

Log demona na konsoli trafia do `/data/accord/daemon.log`.

## Licencja

GPL-3.0-or-later — patrz `LICENSE`. Wynika to z użycia kitu `ps5-homebrew-ui` (GPL-3.0-or-later).
Licencje wszystkich komponentów zewnętrznych: `THIRD_PARTY.md`.
