# Compatibility policy

PUSH. has its own public identity, binaries, local storage root and release
ownership. Some inherited identifiers remain intentionally unchanged because
they are file formats or protocols used by existing community content.

## Retained for compatibility

- Thunderstore community slug: `reskate`
- Mod metadata such as `reskate-mod.json`, `reskate-levels.json` and `reskate-music.json`
- Existing `.reskate-*` staging/cache names used inside mod workflows
- Multiplayer packet and lobby compatibility identifiers
- Legacy `RESKATE_*` environment variables and exported symbols
- Existing `ReSkate.*` profile/config keys where changing them would invalidate user data
- Legacy map prefixes accepted by the native map loader

These are compatibility APIs, not visible product branding. New names may be
added with fallback support, but old names should not be removed until a
versioned migration exists and community packages have had time to move.

## PUSH.-owned surfaces

- Executable and DLL file names
- Window titles, wordmarks, menu copy and logs
- `%LOCALAPPDATA%\PUSH`
- GitHub releases and update metadata
- Legal, privacy, crash-reporting and announcement infrastructure

## Service boundary

PUSH. does not impersonate or silently consume private ReSkate project
services. Thunderstore remains connected to the public `reskate` community by
explicit project policy. Other network services must be PUSH.-owned or disabled.
