# Native baseline inventory

This document records the feature surface inherited for the first PUSH. native
baseline. It is an inventory, not a claim that every path has completed PUSH.
runtime testing.

## Launcher and installation

- Supported Steam build validation and installation
- Offline launch and local saves
- Launcher update plumbing, disabled for local builds until PUSH. releases are published
- Mod scanning, ordering, conflict reporting, profiles, drag-and-drop installation and live apply
- Thunderstore browser using the existing `reskate` community for package compatibility
- Controller-aware launcher UI and configurable menu/console binds

## In-game systems

- Native fullscreen/windowed menu and command console
- Map selection, fast travel, time and world population controls
- Park Editor, object sets, snapping, freecam and undo
- First-person camera, movement, boosts and noclip
- Hall of Meat, Road Rash, board breaking and grind exits
- Progression, controls, graphics and multiplayer settings
- Local music, playlists, artwork and supported streaming helpers

## Multiplayer and servers

- Steam lobbies for up to 32 players
- Dedicated server browser and Windows/Linux server targets
- Proximity voice, text chat, emotes, parties and throwdowns
- Co-op challenges, votes, roles, moderation and rotating parks

## Tooling

- Native runtime DLL and launcher
- Dedicated server
- Built-in content packer and emote packer
- Regression-test targets across launcher, VFS, resource merging, multiplayer and gameplay systems

## First PUSH. identity pass

- Public binaries renamed to `PUSHLauncher`, `PUSH.dll`, `PUSHServer`,
  `PUSHEmotePacker` and `PUSHBuiltinPacker`
- Launcher, startup window, Insert menu and server UI rebranded to PUSH.
- Shared UI accent changed from blue to orange, with black and white retained
- Supplied PUSH. wordmark embedded in the launcher
- Local user data moved to `%LOCALAPPDATA%\PUSH`, with one-time migration of compatible ReSkate saves/settings
- ReSkate-owned crash, legal, announcement and official-mod endpoints are not consumed
- Update ownership points to `indicouchcoding/PUSH.`; binary replacement remains off by default for development builds

See [COMPATIBILITY.md](COMPATIBILITY.md) for identifiers deliberately retained.
