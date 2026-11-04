# Changelog

## 1.8.0 – 2026-11-04
- **MULTIPLAYER ONLINE opens the online servers** (Cloudflare Worker, the same rooms as the web version and
  MultiHero Android 1.8.0): 4 servers (Asia Tenggara, Asia Timur, US, Eropa) with ping and players online, 3 public
  rooms per server, private rooms with a code, recent rooms. PC, phone and browser players play together.
- Lobby buttons: **server** (server picker), **epic online** (Epic Online Services lobby; **online servers** switches
  back), **name**; **enter room code** opens a keypad (typing on the keyboard works too).
- Fixed: two mouse pointers (the system cursor and the game's own) over the game window. The system cursor is now
  hidden over the window, also when it is not full screen.
- New WebSocket + TLS client (`runtime/net/ws_client.cpp`, Mbed TLS 3.6, built-in root certificates).

## 1.7.0 – 2026-11-04
- **CHEATS** page in the main menu with ON/OFF items you can click (mouse, keyboard, touch, pads):
  INFINITE HEALTH, INFINITE JUTSU (Adventure mode) and UNLOCK ALL CHARACTERS + MAPS.
- Online: a notice when a player leaves the room during a match (the CPU takes over that fighter, all players
  agree on the same frame so the match stays in sync), and a status bar with every player's connection light
  (P1–P4) and ping.
- Online room: the stage is chosen from a grid with small pictures of every stage, like the character picker;
  the chosen stage is shown as a small picture for everyone.

## 1.6.0 – 2026-11-03
First release of MultiHero for Windows and Linux, based on the MultiHero Android port 1.6.0
([richardyusan/multihero-android](https://github.com/richardyusan/multihero-android)).
- Windows x64 (MinGW-w64) and Linux x64 builds with SDL2: resizable window with sharp scaling, Alt+Enter full
  screen, sound, keyboard, mouse, touch screens.
- Real-time online multiplayer with Epic Online Services, crossplay with the Android version (identical game
  state on x86-64 and ARM64 tested with checksums).
- Up to 4 game pads, each its own player (also in the menus).
- Everything of the Android 1.6.0 port: online room with character / lives / stage choice, adventure cheats,
  fixed enemies (Venom, Shredder, Thug, Pig).
