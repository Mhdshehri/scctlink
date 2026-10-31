<div align="center">

# SCCTLink

**Play Splinter Cell: Chaos Theory co-op and Versus with Steam friends.**
No VPN. No port forwarding. No IP addresses.

[**Download the latest release**](../../releases/latest) · Windows · Free · MIT license

</div>

---

Chaos Theory's online servers are gone, so its co-op and Versus only work on a LAN, and people usually reach for Hamachi or Radmin. SCCTLink does the job without a VPN. The host plays the game's normal LAN mode, and SCCTLink carries its traffic through Steam's own relay network to friends on your Steam friends list.

You don't install anything and you don't touch your router. Unzip it and open it.

## Quick start

Both players: download `SCCTLink-<version>.zip` from [Releases](../../releases), unzip it anywhere, and open **SCCTLink.exe**. Windows asks for administrator rights, because SCCTLink can change the game's settings files, which are in Program Files. Steam must be running, you both need the same SCCTLink version, and the host must be on your Steam friends list. Co-op is 2 players; Versus (Spies vs. Mercenaries) is up to 4.

### 🔧 First time (co-op)

Close the game, open the **Settings** tab and click **Install SCCT Fix**. Both players do this once. Without it, co-op disconnects as soon as someone draws a weapon ([why](#scct-fix)).

### 🎮 Host

1. Click **Host a game**.
2. Click **Start Splinter Cell**, then **Co-op → LAN → Create game**.
3. Keep SCCTLink open while you play.

When a friend links, they show up in your list. SCCTLink also flashes in the taskbar with a short sound. To remove someone, pick them and click **Kick**.

### 🤝 Friend

1. Click **Join a friend**, pick the host and click **Join**.
   The host isn't listed yet? Click **Refresh**, or paste their short lobby code (like `1MCE-2B2`) and click **Join this code**.
2. Wait until the log says the host's game is up. SCCTLink flashes and plays a sound when it is.
3. Click **Start Splinter Cell**, then **Co-op → LAN → Join game** and pick the host.

For Versus, both pick **Versus** with the button next to **Join a friend** first. SCCTLink starts `SCCT_Versus.exe` for you.

> The full player guide, with troubleshooting and lag tips, is in the zip as `README.txt` ([read it here](package/README.txt)).

## SCCT Fix

Chaos Theory's co-op runs the same simulation on both PCs (Quazal's *SyncSim*) and ends the session the moment the two drift apart. In practice that happened as soon as someone drew a weapon. The SCCT Fix stops it.

**Install SCCT Fix** (Settings tab) copies `SCCTFix-d3d9.dll` into the game's `System` folder as `d3d9.dll` and sets `StartupFullscreen=False` in `SplinterCell3.ini`. The DLL caps the game at 60 FPS, runs it in a borderless window, keeps Alt+Tab working and writes `System\SCCTFix.log` if the game crashes. With it on both PCs, co-op works: shooting, jumping, changing maps, no disconnects.

- SCCTLink copies the DLL only if its SHA-256 is `2f05d8e9…cae9`. It was checked: it imports no network functions, starts no programs and writes no file except its log.
- A `d3d9.dll` that is already there (another mod) is kept as `d3d9.dll.scctlink-backup`. **Remove SCCT Fix** puts it back and sets fullscreen again.
- The **Mods** tab checks that your friend has it too.

## Features

- **Mods tab:** compares both games when you link: same game version, same mods, same files, and the settings that must match (FOV, FPS limits, the SCCT Fix, fullscreen). Shows exactly what differs, like `only on your PC: system/dinput8.dll`. **Check again** after you change something.
- **Settings tab:** change resolution, FOV and FPS limit (and the widescreen fix's FOV factor) without editing `.ini` files. Your friend's values are shown beside yours; **Use friend's** copies them into the fields. Every changed file is backed up first.
- **Live stats:** a small window that stays on top of the game with the game's **frame rate**, even when you play the story alone or sit in the menu, and while linked the ping, packet loss and packets per second for each friend. The frame rate comes from Windows' own performance events (the same ones PresentMon and Xbox Game Bar read), so nothing is put into the game, and only while the window is open.
- **SCCT Fix reminder:** in co-op, SCCTLink tells you (in the Log) when the SCCT Fix isn't installed.
- **How to** shows your steps as you go (`[x]` done, `[>]` do this now). **Log** shows everything that happened, with a red dot when something needs a look, a **Copy log** button, and **Report**, which opens a filled-in problem report here on GitHub and the folder with `scctlink.log` to drag into it.
- Finds friends who are hosting automatically, with a **Refresh** button and short lobby codes.
- Alerts when a friend links, or when the host's game is up. **Kick** for the host.
- Opens fast: game file checksums are cached, so only files that changed are read again.
- Tells you when your friend has a newer or older SCCTLink.
- A dark theme by default, and a light theme.

## How it works

```
 Friend's PC                                              Host's PC
┌─────────────────┐  UDP/TCP  ┌──────────┐            ┌──────────┐  UDP/TCP  ┌─────────────────┐
│ Splinter Cell   │ ────────▶ │ SCCTLink │ ═ Steam ══▶│ SCCTLink │ ────────▶ │ Splinter Cell   │
│ (joins the LAN  │ ◀──────── │ 127.x    │ ◀═ relay ═ │ 127.y    │ ◀──────── │ (hosts the LAN  │
│  game)          │           └──────────┘            └──────────┘           │  game)          │
└─────────────────┘                                                         └─────────────────┘
```

- **A LAN on your own PC.** SCCTLink reads which ports the game has open and gives each other player a private loopback address (`127.x.y.z`) with the same ports. To your game, your friend's game looks like another PC on your LAN.
- **LAN discovery** (the game's "Join game" list) is caught and passed on, so the host's game shows up in the list.
- **Addresses swapped, never sent.** Chaos Theory's network library (Quazal Net-Z) writes the PC's LAN address into its packets, including an RC4-encrypted discovery reply and LZO-compressed game messages. SCCTLink decrypts or decompresses them, swaps the address for the loopback stand-in, and redoes the checksums and the MD5, so your friend's game connects through Steam and never learns your real address.
- **Lobbies and the relay** come from Valve's free *Spacewar* test app (AppID 480), which every Steam account can use. While SCCTLink is open, Steam shows you as "playing Spacewar". Nobody has to install or buy anything extra.
- **Packets arrive unchanged and in order**, adding about 1–2 ms.

## Safety and privacy

| | |
|---|---|
| 🔒 **Your IP stays private** | Traffic always goes through Valve's relay. SCCTLink turns off Steam's direct connections, so neither player sees the other's IP, whatever Steam's "IP sharing" setting is. If the game writes your LAN address into a packet, it is replaced before it leaves your PC. |
| 👥 **Friends only** | The host only lets in people who are in its friends-only lobby **and** on its Steam friends list. Everyone else is blocked and logged. |
| 🎯 **You choose who to link to** | The friend links only to the host they picked, and only accepts data from that host. |
| 🚪 **Kick** | The host can kick a friend. A kicked friend can't link again until the host clicks Stop. |
| 🧱 **Nothing opened** | No ports are opened on your PC or router. SCCTLink only listens on loopback addresses (your own PC). |
| 🎮 **Only the game** | Packets from other programs, or from other PCs on your network, are dropped. |
| 📦 **Packets checked** | Oversized packets and floods are dropped; compressed and encrypted data is bounds-checked before it is read. |
| ✅ **Pinned DLLs** | `steam_api.dll` is Valve's signed file from [Steamworks.NET 2025.164.1](https://github.com/rlabrecque/Steamworks.NET/releases/tag/2025.164.1); SCCTLink checks its SHA-256 (`fdafb7c6…eb65da`) and refuses anything else. The SCCT Fix DLL is pinned the same way. |
| 🧩 **No third-party code** | `SCCTLink.exe` uses only core Windows DLLs (kernel32, advapi32, user32, gdi32, comctl32, shell32, ws2_32, msvcrt). advapi32 is used only to read the game's frame rate for Live stats. A few more (winmm, iphlpapi, dwmapi, uxtheme) are loaded from System32 only. |
| ✍️ **Writes only when you click** | In its own folder: `steam_appid.txt`, `scctlink.log` (never contains IP addresses), `scctlink_cache.txt` (game file checksums) and `scctlink_theme.txt`. In the game folder only when you click **Apply** or **Install/Remove SCCT Fix**, always with backups. To remove SCCTLink, click **Remove SCCT Fix** and delete its folder. |
| 🛡️ **No ban risk** | Chaos Theory's LAN mode has no anti-cheat and its online servers are gone. SCCTLink doesn't inject anything into the game. |

Windows SmartScreen may warn you because SCCTLink is new and not code-signed. Check the files first if you like: open a command prompt in the folder and compare `certutil -hashfile SCCTLink.exe SHA256` with `SHA256SUMS.txt`.

As with any game, only play with people you trust. Found a security problem? See [SECURITY.md](SECURITY.md).

## Building from source

You need `clang`, `lld`, `llvm-dlltool`, `llvm-rc` and `llvm-cvtres`. No Windows SDK is needed: the few Windows functions SCCTLink uses are declared by hand.

```sh
sh build_windows.sh                    # -> build/win/SCCTLink.exe (32-bit, windowed)
sh package.sh path/to/steam_api.dll    # -> dist/SCCTLink-<version>.zip (checks both DLLs' SHA-256)
```

The build is **reproducible**: the same source always gives the same SHA-256.

## Tests

The tests run on Linux as root, with a mock Steam and a stand-in for the game, under AddressSanitizer and UBSan. Each player gets their own network namespace: a separate "PC" with its own LAN and broadcast domain.

```sh
mkdir -p build
gcc -std=c11 -fsanitize=address,undefined -o build/scctlink_mock src/scctlink.c src/ui_console.c src/lobbycode.c src/plat_posix.c src/sha256.c test/steam_mock.c
sudo python3 test/run_tests.py   # prints ALL OK
```

The tests cover:
- **Play:** LAN discovery in both directions; the game's real encrypted Net-Z discovery reply; joining at the address inside it; 2000 mixed-size packets byte-exact and in order; a level-load burst; TCP streams; latency; 10% packet loss with 60 ms delay each way; Versus with a host and two friends on three "PCs".
- **Privacy:** the host's address hidden in raw, text and LZO-compressed form, with every checksum redone; a second network adapter's address; Steam's direct connections turned off.
- **Safety:** strangers blocked; programs other than the game blocked; foreign lobbies refused; kicking.
- **Finding the host:** every discovery path, including a host Steam shows in the game instead of Spacewar; lobby codes; Refresh; invites; join confirmation.
- **Game check and settings:** a mod on one PC only, another patch, identical games, Check again; the settings comparison; the Settings tab (files written in full, backups kept, bad values refused); installing and removing the SCCT Fix and its reminder; live stats (frame rate alone, ping and loss while linked); the checksum cache.

**Tested with the real game:** a host and a friend in different homes play co-op with the SCCT Fix on both PCs: joining, shooting, jumping and changing maps without disconnects.

## Project layout

| File | What it does |
|---|---|
| `src/scctlink.c` | The engine: lobbies, discovery, relay, address swapping, safety checks, game files check, settings, SCCT Fix |
| `src/ui_win.c` | The Windows window and the live stats window |
| `src/ui_console.c` | A text front end that the tests drive |
| `src/steam_win.c` | Loads the pinned `steam_api.dll` |
| `src/plat_win.c` | Windows sockets, the game's open ports and frame rate, clipboard and file helpers |
| `src/lobbycode.c` | Short lobby codes like `1MCE-2B2` |
| `src/sha256.c` | Checks the pinned DLLs and game files |
| `src/crt0_win.c`, `src/*.h` | Program start-up and the small headers |
| `res/` | Icon, Windows manifest and version info |
| `package/` | The player guide and the SCCT Fix DLL that go in the zip |
| `build_windows.sh`, `package.sh` | Build `SCCTLink.exe` and the release zip |
| `test/` | Mock Steam, the game stand-in and the end-to-end tests |

## Credits

By [Mhdshehri](https://github.com/Mhdshehri). The relay pattern comes from the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook (Steam Spacewar networking). Splinter Cell: Chaos Theory is by Ubisoft. SCCTLink is not affiliated with Ubisoft or with Valve.

SCCTLink's own code is released under the [MIT license](LICENSE). `steam_api.dll`, which ships in the release zip, is Valve Corporation's Steamworks redistributable (from Steamworks.NET 2025.164.1). It is not covered by the MIT license; it is distributed under the Steamworks SDK terms.
