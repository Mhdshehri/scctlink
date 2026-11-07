# Changelog

## 0.9.9.8 (testing)

co-op draws at the resolution you choose (or your screen's; the SCCT Fix opens a window always and is DPI aware, so a scaled 2K desktop isn't shown as 1920x1080); the FOV works: the game ignores DesiredFOV, so the SCCT Fix sets the camera's FOV (from ThirteenAG's FusionFix) - wide screens see more on the sides by themselves, and the FOV number (75 = the game's) widens the normal view, each player's own; settings also go to the copies the game really reads in ProgramData; each time Versus starts the Versus Fix sets its start resolution (Default.ini) and ScreenRes=-1 in every profile and in DefaultProfile.ini (new profiles), so a profile doesn't pick 1600x1200 again (also when Versus closes); with a resolution chosen in SCCTLink, Versus' profile scripts (a new profile, entering Multiplayer, the game's own resolution menu) no longer change it; Live stats show the connection only (no FPS), with a thicker dark edge; the co-op 60 FPS signal is also a file, System\SCCTLink.coop. The Versus Fix also brings back the mercenary's flashlight on today's graphics cards (no 3D-Analyze needed): Versus is told depth textures aren't available, so it uses its own ps 1.4 shadow buffer (Flashlight=0 in SCCTVersusFix.ini turns this off). Single player's "no limit" is now called FPS unlock (the game itself stops at 100 FPS); the SCCT Fix writes the measured FPS to SCCTFix.log every 10 seconds (up to 512 KB).

## 0.9.9.7 (testing)

Raw mouse is now in the SCCT Fix (Windows Raw Input, ported from ThirteenAG's FusionFix: no acceleration, any polling rate); the Settings tab is rebuilt (a banner for the fix, sections, I / O switches, Reset on its own); no FPS limit or widescreen FOV factor in co-op any more (the SCCT Fix keeps 60 FPS and has its own widescreen HUD); the Mods tab says when another widescreen mod is there; single player can have more FPS ("Single player: 60 FPS / screen's Hz / no limit") - during an SCCTLink co-op session the SCCT Fix goes back to 60 by itself; the first install also skips the intro videos; the window fits small screens; "Check again" finds Steam if it was started after SCCTLink; "Update" replaces an older Versus Fix.

## 0.9.9.6 (testing)

new co-op HUD fix, ported from ThirteenAG's FusionFix (MIT, see THIRD-PARTY.txt): the HUD, menus and text are drawn 4:3 in the middle instead of stretched; full-screen pictures, the scope and the loading bar fit the wide screen.

## 0.9.9.5 (testing)

"Skip intro" sets the three startup videos aside (renamed, not deleted; off gives them back); the co-op HUD fix's places can be chosen (SCCTFix.ini [HUD] Sites, testing).

## 0.9.9.4 (testing)

co-op HUD fix only for the in-game HUD (menus and videos as before); "Skip intro" (co-op Settings); Versus Fix mouse handed out one event at a time (Versus reads it so).

## 0.9.9.3 (testing)

the SCCT Fix is now our own build (fix/d3d9fix.c, same behaviour as the original) with an optional HUD fix for wide screens (co-op Settings: "HUD fix"); "Update SCCT Fix" replaces the original.

## 0.9.9.2 (testing)

the Versus Fix (Settings tab, Versus): no 30 FPS limit, the mouse as 125 reports a second (fast mice made Versus' aim zigzag), FOV for spies and mercenaries, a wider resolution, the HUD not stretched, turning speed.

## 0.9.9.1

Live stats shows Versus' frame rate (Direct3D 8, read from the graphics kernel's events); Versus' resolution field removed (Versus resets it).

## 0.9.9

Settings: mouse sensitivity, aim turn speed and Reset; Versus gets its own Settings (mouse sensitivity, mouse multiplier); the Mods tab says on top whether all party members match.

## 0.9.8.2

Raw mouse button in the Settings tab (no mouse acceleration); the SCCT Fix's source code (fix/d3d9fix.c).

## 0.9.8.1

the Mods tab in two columns (you on the left, your friend on the right); each mode compares only its own files, and Versus shows who has SCCT Versus Reloaded; Live stats float over the game with no background.

## 0.9.8

the Mods tab is simpler and in colors (names, mods, green when everything matches, red for each difference); in Versus the co-op settings and the SCCT Fix are hidden.

## 0.9.7

FOV buttons for the screen's shape in the Settings tab (4:3 = 75, 16:10 = 85, 16:9 = 91).

## 0.9.6

"Report" button in the Log tab (a GitHub problem report, filled in, plus the log file to drag into it).

## 0.9.5

opens faster (game file checksums are cached and only changed files are read again), "Live stats" window on top of the game (the game's FPS even playing alone; ping, packet loss and packets per second while linked), a reminder in co-op when the SCCT Fix isn't installed.

## 0.9.3

security review: .ini files are written to a new file first and then swapped in (a failed write never leaves half a file), oversized .ini files are left alone, less memory for a stalled TCP stream.

## 0.9.2

code clean-up (unused code removed), the SCCT Fix button also checks StartupFullscreen when the DLL is already there, guides updated.

## 0.9.1

renamed to "SCCT Fix" (it does more than the frame cap); it also sets StartupFullscreen=False, like the setup that was tested.

## 0.9

"Install FPS fix" in the Settings tab. With it, co-op works with the real game: shooting, jumping, changing maps, no disconnects.

## 0.8.5

the Mods tab shows and compares resolution, FOV and FPS limits; the Settings tab changes them (with backups); a closed lobby isn't tried again.

## 0.7.1

the Mods tab and "Check again"; works with real game folders.

## 0.7

the game files check, and scctlink.log keeps the last game packets before a disconnect, with times, so a disconnect can be traced.

## 0.6.4

every game message ends in its own checksum byte, and the game silently dropped messages whose address SCCTLink had swapped (stuck on "Joining"). SCCTLink now redoes it. Joining works with the real game.

## 0.6.3

the game also sends every network address of the PC (Wi-Fi + Ethernet, say) inside COMPRESSED messages (LZO). SCCTLink now decompresses those messages, hides every address, and sends them uncompressed.

## 0.6.2

the encrypted reply also carries an MD5 of its contents; SCCTLink now redoes that MD5 (in 0.6.1 the game stopped listing the host).

## 0.6.1

fixes the checksum at the end of the game's packets when SCCTLink swaps an address in them (otherwise the game would drop them).

## 0.6

the friend could see the host's game in the LAN list but not join. The game hides the host's real address inside an encrypted packet, so the friend's game tried to reach it directly instead of through Steam. SCCTLink decrypts that packet, puts in the address that goes through Steam, and encrypts it again.
