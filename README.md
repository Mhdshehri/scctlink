# SCCTLink 0.6.4

Play **Splinter Cell: Chaos Theory** co-op (2 players) or Spies vs. Mercenaries / Versus (up to 4) with Steam friends: no VPN (no Hamachi, Radmin or ZeroTier), no port forwarding, no IP addresses.

Chaos Theory's online servers are gone, so PC multiplayer only works in **LAN** mode. SCCTLink makes your friends' PCs look like they are on your LAN, and carries the game's traffic through Steam's relay network. The host invites the friend from Steam.

**Download:** `SCCTLink-0.6.4.zip` from [Releases](../../releases). Both players unzip it.

## How to play

Everyone double-clicks **SCCTLink.exe**. It opens a window (the same window as CoFLink, in green):

- The button next to **Join a friend** switches between **Co-op** and **Versus**. Everyone picks the same.
- **Host:** click **Host a game**, then **Start Splinter Cell**, and create a LAN game. The short lobby code (like `1MCE-2B2`) is copied for you.
- **Friend:** click **Join a friend**, pick the host in the list and click **Join** (or **Refresh**, or paste the lobby code and click **Join this code**). When linked, start the game and join the host from its LAN list.

Co-op lobbies hold 2 players, Versus lobbies 4. The **How to** tab ticks off the steps as they happen; **Log** shows everything (Copy log for bug reports). The host can **Kick** a friend.

See [package/README.txt](package/README.txt) for the full guide, FPS/lag tips and troubleshooting.

## How it works

```
Friend's game ──UDP/TCP──> 127.a.b.c (stands for the host)  ─┐
                                                              │ Steam relay
Host's game  <──UDP/TCP── 127.d.e.f (stands for the friend) <─┘ (ISteamNetworkingMessages)
```

The game's LAN protocol is not documented, so SCCTLink doesn't hard-code it:

- It reads which UDP/TCP ports the game (`splintercell3.exe`, or `SCCT_Versus.exe` with `--versus`) has open (the tables `netstat -ano` shows) and tells the other side.
- For every port of the other player's game it opens the **same port** on a private loopback address that stands for that player. Packets come out of the same port numbers on the other PC, so the game sees a normal LAN peer.
- LAN discovery broadcasts (the "Join game" list) are caught on those ports and passed on.
- TCP connections are carried as reliable streams; UDP is sent unreliable/no-Nagle (the game has its own reliability), unchanged and in order.
- If the game puts the PC's LAN address in a packet (as raw bytes or text), it is swapped for the loopback address that stands for that PC, so the other side can connect to it and never learns the real address. The loopback addresses are chosen so the text has the same length.
- Chaos Theory's network library (Quazal Net-Z) hides the host's address inside an **encrypted** LAN discovery reply (`prudp:/address=...;port=9103`, RC4 with a fixed key, a fresh RC4 per packet). The decrypted payload ends in an MD5 of the rest of it (Quazal's MD5Checksum). SCCTLink decrypts it, swaps the address, redoes the MD5, encrypts it again and fixes the packet's checksum. Without this, the friend sees the game in the list but can't join, because their game tries the host's real LAN address.
- Game messages are often LZO-compressed (LZO 1.08, which the game links), and the game lists every network address of the PC in them (Wi-Fi + Ethernet, say). SCCTLink decompresses such a message, hides the addresses and sends it uncompressed (ratio byte 0, which the game uses itself for small messages). An address of a different length than the stand-in gets zero-padded octets (`127.007.3.1`): Windows' `inet_addr`, which the game uses, reads them as the same address.
- Every game message inside a packet is `[u32 length][bytes][checksum]`, the checksum being the same sum with key 0; the game drops a message that doesn't match. SCCTLink redoes it for every message it changes.
- Every Net-Z (PRUDP) packet ends in a 1-byte checksum with a per-stream key byte. When SCCTLink swaps an address in a game packet, it fixes that checksum; the key byte is learned from the game's own packets.
- Lobbies, invites and the relay come from Valve's free "Spacewar" test app (AppID 480). While SCCTLink is open, Steam shows you as "playing Spacewar".

## Safety

- No IPs are exchanged and no ports are opened; Windows Firewall prompts can be cancelled. Steam's direct (ICE) connections are turned off, so traffic always goes through Valve's relay.
- The host only lets in players who are in the lobby **and** on the host's Steam friends list; the joiner only accepts the host it chose.
- Only the game process can use the relay: packets from other programs, or from other PCs on your network, are dropped.
- Oversized packets and floods are dropped.
- The game is not modified and nothing is injected. The only files written are `steam_appid.txt` and `scctlink.log` (which never contains IP addresses) in SCCTLink's own folder.
- `steam_api.dll` is Valve's signed redistributable from [Steamworks.NET 2025.164.1](https://github.com/rlabrecque/Steamworks.NET/releases/tag/2025.164.1). SCCTLink checks its SHA-256 (`fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da`) and refuses any other file.
- `SCCTLink.exe` imports only core Windows DLLs (kernel32, user32, shell32, ws2_32, msvcrt; winmm and iphlpapi loaded from System32) plus the pinned `steam_api.dll`. No third-party code.

## Building

You need clang, lld, llvm-rc and llvm-cvtres; no Windows SDK is required.

```sh
sh build_windows.sh      # -> build/win/SCCTLink.exe (32-bit)
```

The build is reproducible: building the same source gives the same SHA-256.

## Tests

The tests run on Linux as root. Each player gets their own network namespace (a separate "PC" with its own LAN and broadcast domain), a stand-in for the game, and a mock Steam:

```sh
gcc -std=c11 -fsanitize=address,undefined -o build/scctlink_mock src/scctlink.c src/ui_console.c src/lobbycode.c src/plat_posix.c test/steam_mock.c
sudo python3 test/run_tests.py   # prints ALL OK
```

They cover: LAN discovery by broadcast in both directions; the game's real encrypted Net-Z discovery reply and joining at the address inside it; addresses swapped in checksummed game packets on both sides; a second network adapter's address inside an LZO-compressed message; message checksums; the host's address hidden in raw and text form; 2000 mixed-size packets exact and in order; a 300-packet level-load burst; relay delay; TCP streams with a 300 KB message; strangers blocked; programs other than the game blocked; quit and reconnect; invites; joining by short lobby code; Refresh; finding a host Steam shows in the game instead of Spacewar; foreign lobbies refused; join confirmation; Versus with a host and two friends on three separate "PCs"; co-op/Versus lobbies kept apart; and 10% packet loss with 60 ms delay each way.

**Tested with the real game (0.5):** two PCs in different homes linked, and the friend saw the host's game in the LAN list but could not join. The packet log showed why (the encrypted address above); 0.6.4 fixes that. Joining a real game is not yet confirmed. `scctlink.log` shows where it stops if something goes wrong.

## Credits

By KaZ. The relay pattern comes from the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook (Steam Spacewar networking) and CoFLink. Splinter Cell: Chaos Theory is by Ubisoft. This project is not affiliated with Ubisoft or Valve.

SCCTLink's own code is released under the [MIT license](LICENSE). `steam_api.dll`, which ships in the release zip, is Valve Corporation's Steamworks redistributable (from Steamworks.NET 2025.164.1). It is not covered by the MIT license; it is distributed under the Steamworks SDK terms.
