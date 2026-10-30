# Security policy

SCCTLink relays game traffic over the internet between Steam friends and can change the game's settings files, so security matters. Thank you for helping keep its players safe.

## Supported versions

Only the latest release gets security fixes. Please update before you report a problem.

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Older versions | ❌ |

## Reporting a vulnerability

**Please do not open a public issue for a security problem.** A public report shows everyone how to use the problem before it is fixed.

Report it privately instead:

1. Go to the [Security tab](../../security) of this repository.
2. Click **Report a vulnerability**.
3. Describe what you found, how to reproduce it, and what an attacker could do with it.

You can expect a first reply within a few days. Once the problem is confirmed, a fixed release is published and you are credited, unless you prefer not to be.

## What counts

Examples of things worth reporting:

- someone who is **not** the host's Steam friend getting into the host's game
- a way to learn the other player's IP address through SCCTLink
- a crafted packet or Steam message that crashes SCCTLink or makes it misbehave
- a way to make SCCTLink load a different `steam_api.dll`, or install a different `d3d9.dll`, than the pinned ones
- a way to make SCCTLink write files outside the game's folder or its own folder

Not SCCTLink's to fix:

- bugs in Splinter Cell: Chaos Theory itself, which SCCTLink passes traffic to unchanged
- problems in Steam or in Valve's relay network
- Windows SmartScreen or antivirus warnings about the unsigned `SCCTLink.exe` (see the README for how to check the file)

## Verifying a download

The zip includes `SHA256SUMS.txt` with the SHA-256 of every file. The build is reproducible: building the source of a release gives exactly the same `SCCTLink.exe`. See the README for details.
