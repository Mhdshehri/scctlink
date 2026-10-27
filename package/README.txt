SCCTLink 0.1 - Splinter Cell: Chaos Theory co-op with a Steam friend
=====================================================================

No VPN (no Hamachi, Radmin or ZeroTier), no port forwarding, no IP addresses.
The game's own LAN co-op mode is used; SCCTLink carries it through Steam.

Both players need:
  * Splinter Cell: Chaos Theory, the SAME version and the SAME mods
    (see "Before you play" - a mismatch is the #1 cause of co-op disconnects)
  * Steam running and logged in (online, not offline mode)
  * each other on their Steam friends list
  * this folder unzipped somewhere (Desktop is fine). Keep all files together.


HOW TO PLAY
-----------

HOST (the one whose game the mission runs on)
  1. Double-click Host-SCCT.bat. Wait for "Lobby is ready".
  2. Start Splinter Cell: Chaos Theory (or press G in the SCCTLink window).
  3. In the game: Co-op -> LAN -> Create game.
  4. Leave the SCCTLink window open while you play.

FRIEND
  1. Double-click Join-SCCT.bat. It finds the host by itself and asks
     "Join their game?" - type Y and press Enter.
     Not found? Press L and paste the lobby id from the host's window,
     or accept the host's Steam invite (the host presses I to send one).
  2. When it says "Connected", Splinter Cell starts through Steam
     (Ubisoft Connect / Epic copy: start it yourself).
  3. In the game: Co-op -> LAN -> Join game. The host's game is in the list.
  4. Leave the SCCTLink window open while you play.

Windows Firewall may ask about Splinter Cell or SCCTLink. You can press
Cancel: the game traffic only goes over this PC's own loopback addresses
(127.x.x.x), so nothing has to be opened to your network or the internet.

Keys in the SCCTLink window:
  S = status (ping, packets, packet loss)   D = details   Q = quit
  Host only: I = send a Steam invite   G = start the game
  Friend only: L = paste a lobby id


BEFORE YOU PLAY (avoids disconnects and desync)
-----------------------------------------------
  * Same files on both PCs: same patch, same language, and either both use
    the ThirteenAG Widescreen Fix (same version) or neither does. Players
    report co-op dropping after about a minute when only one PC has it.
  * Same field of view on both PCs. If you edit FOV by hand, it is in
    SplinterCell3User.ini (DesiredFOV / DefaultFOV) - both must match.
  * Cap the frame rate (see FPS tips). Very high FPS breaks AI and physics.
  * If co-op saves/settings act strange: back up, then delete the game's
    settings folder in %PROGRAMDATA%\Ubisoft\ on both PCs so both start fresh.


FPS / LAG TIPS
--------------
  * Frame rate: the ThirteenAG Widescreen Fix has an FPS limit
    (FPSLimit in scripts\SplinterCellChaosTheory.WidescreenFix.ini).
    Set 60 on BOTH PCs (30 if a PC struggles). Do not leave it at 1000.
    Official download: github.com/ThirteenAG/WidescreenFixesPack/releases (tag "scct")
  * Stutter / bad mouse aim: set your mouse to 125 Hz polling in its software,
    and set every BiasCut value in SplinterCell3Settings.ini to 0.0
    (removes mouse acceleration).
  * Lower "Shadow resolution" / turn off shadow filtering if FPS dips.
  * AMD GPUs with broken thermal vision need a community fix - search the
    Steam guide "Improvements for Splinter Cell: Chaos Theory".
  * Lag: close downloads/streams on both PCs; use a wired connection if you can.
    Press S in SCCTLink to see the ping and packet loss between you.
    Steam picks the relay route; SCCTLink itself adds about 1-2 ms.
  Only download mods from the official pages above, and keep both PCs identical.


SAFETY
------
  * Neither player ever sees the other's IP address. If the game writes your
    LAN address into a packet, SCCTLink replaces it before it leaves your PC.
  * Nothing is installed, no ports are opened, no router settings change.
    SCCTLink only writes steam_appid.txt and scctlink.log in its own folder.
  * Only the host's Steam friends who joined the lobby get through. The lobby
    is friends-only; anyone else is blocked and shown in the window.
  * Only Splinter Cell itself can use the connection: packets from any other
    program on your PC are dropped (and noted in scctlink.log).
  * The game is not modified and nothing is injected into it. Chaos Theory's
    LAN mode has no anti-cheat and its online servers are gone, so there is
    nothing to be banned from. Steam shows you as "playing Spacewar" - that's
    Valve's free test app that SCCTLink uses for the lobby and relay; this is
    normal for friend-to-friend mods.
  * steam_api.dll is Valve's signed file (Steamworks.NET 2025.164.1).
    SCCTLink checks its SHA-256 and refuses any other copy:
    fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da
  * Your antivirus may warn about an unsigned new .exe. The full source is at
    github.com/Mhdshehri/scctlink - you can build it yourself.


TROUBLESHOOTING
---------------
  "Steam is not running" / "offline mode"  -> start Steam, go online, retry.
  The friend's SCCTLink doesn't find the host -> press L and paste the lobby
    id, or have the host press I and send an invite.
  The host's game is not in the LAN list:
    1. The host must be in Co-op -> LAN -> Create game first.
    2. Both press D: "Your game: running" must show on both sides, and the
       friend should see the host's ports listed.
    3. Restart the friend's game while SCCTLink stays connected.
    4. Send both scctlink.log files to the developer. They contain no IP
       addresses (they are written as LAN-IP#1 etc).
  Disconnects after a minute -> see "Before you play" (mods/FOV/FPS).
  "different SCCTLink version" -> both need the same SCCTLink zip.
  If your game file isn't splintercell3.exe:
    SCCTLink.exe host --exe <name>.exe   (or join)


VERSION 0.1 - WHAT IS TESTED
----------------------------
Tested end to end on a simulated network: two separate "PCs", a stand-in for
the game that finds LAN games by broadcast, embeds its address in packets and
uses UDP and TCP, and a stand-in for Steam with 10% packet loss and 60 ms delay.
Packets arrived byte-for-byte exact and in order; the relay added about 1.5 ms.
It has NOT yet been tried with the real Chaos Theory on two real PCs - you two
are the first. If something doesn't work, the D key and scctlink.log show
exactly where it stops.


SCCTLink by Mhdshehri. MIT license. Not affiliated with Ubisoft or Valve.
