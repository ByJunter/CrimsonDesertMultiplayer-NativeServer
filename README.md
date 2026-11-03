# Crimson Desert Multiplayer: Native Server Version (cdmp)

An experimental co-op mod for **Crimson Desert** (PC, Steam build 1.0.0.2976), written from scratch in
C++20. It is loaded into the game as `cdmp.asi` by Ultimate ASI Loader.

This is the successor to [CrimsonDesertMultiplayer](https://github.com/ByJunter/CrimsonDesertMultiplayer).
Instead of copying characters between two separate games, the guest joins the host's world through the
game's own internal server.

> **Status: alpha, not maintained.** This is a research project published as-is. It only works with game
> build 1.0.0.2976 (PE timestamp `0x6AB28F00`); every address and signature in the code is specific to that
> build. Expect crashes, and back up your saves before trying it.

## Requirements

- A legitimately purchased copy of **Crimson Desert** on Steam, build 1.0.0.2976. The mod does not work
  without the original game, and this repository contains **no game code, assets or data**.
- Windows 10/11 x64.
- Visual Studio 2022 or the VS 2022 Build Tools with the "Desktop development with C++" workload (MSVC
  x64, CMake). The build downloads Dear ImGui and safetyhook with CMake `FetchContent`.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) v9.7.4 to install the mod
  (see [third_party/ultimate-asi-loader/README.md](third_party/ultimate-asi-loader/README.md)).

## What works

The game keeps the client/server architecture of an MMO internally: the client talks to an in-process
server through ~1,100 internal "Troc" messages. The current approach ("option D") lets the **host's
internal server accept a second player**: the guest's game client connects to the host's server through a
Steam (or UDP) tunnel.

Validated live (two game copies on one PC, and partly between two PCs over Steam):

- the guest logs in to the host's world and plays alongside the host's character;
- movement, actions, climbing, gliding, mounts (mount, ride and dismount in both directions), pets;
- NPCs, schedules and travellers in the shared field; fast travel inside the host's field;
- doors, and the host's scenes and cutscenes kept from taking over the guest's client;
- separate save handling for the guest (the host's saves are protected from the guest's writes).

Known gaps: combat latency, time-of-day and weather sync, the world far from the host, shopkeepers (work in
progress, off by default) and many smaller issues. See [docs/ROADMAP.md](docs/ROADMAP.md) and
[docs/OPCION-D-EMULADOR.md](docs/OPCION-D-EMULADOR.md). Most design and research notes are in Spanish, under
[docs/research/](docs/research/).

## Building

```powershell
.\scripts\build.ps1              # configure, build (RelWithDebInfo) and run the unit tests
.\scripts\build.ps1 -SkipTests
```

The output goes to `build\bin\<Config>\` (`cdmp.asi`, `cdmp_peer.exe`, `cdmp_host.exe`, `cdmp_probe.exe`
and the tests).

## Installing

```powershell
.\scripts\deploy.ps1                                  # copies cdmp.asi and the loader (as winmm.dll) to bin64
.\scripts\deploy.ps1 -GameDir "<Steam>\steamapps\common\Crimson Desert\bin64"
.\scripts\deploy.ps1 -Remove                          # uninstall
```

The mod is configured with `bin64\cdmp.ini` (examples in `kit/` and `docs/research/ini/`) and logs to
`bin64\cdmp.log`. Two copies of the game on one PC can be started with `scripts\launch-2inst.ps1`.

## Repository layout

| Folder | Contents |
|---|---|
| `src/common/` | Protocol, reliable channel, session, transports, save guards (with tests) |
| `src/mod/` | The mod: startup, config, overlay, memory reading, network and the option-D session code |
| `tools/` | `cdmp_peer` (relay/test peer), `cdmp_host` (loads the mod without the game), `cdmp_probe` (read-only external reader), helper scripts |
| `tests/` | Unit and integration tests (run by `ctest`) |
| `scripts/` | Build, install, packaging and two-instance launch scripts |
| `kit/` | Example configurations and instructions for testers |
| `docs/` | Roadmap, test plans and research notes |
| `third_party/` | safetyhook patch (Boost Software License 1.0) and the ASI loader instructions |

## Legal

This is an unofficial fan project, not affiliated with or endorsed by Pearl Abyss. Crimson Desert is a
trademark of Pearl Abyss Corp. The repository does not include decompiled game code, game binaries,
extracted game files or save data. The source and research notes reference addresses (RVAs), function
names and short byte signatures of the game executable, as needed to hook it.

## License

GPL v3: see [LICENSE](LICENSE). `third_party/safetyhook-patch` keeps its own license (BSL-1.0).

Author: **ByJunter**.
