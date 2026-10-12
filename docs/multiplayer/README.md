# Wi-Fi multiplayer (experimental)

Phone-vs-phone play over the game's own multiplayer (the lobby, chat, hosting and joining are upstream code).
Branch `feature/lan-multiplayer`; contract in `docs/LAN-MULTIPLAYER-CONTRACT.md`. **Nothing here has been run on two phones yet.**

## How to play

1. Put the phones on the same Wi-Fi network, or turn on a hotspot on one phone and connect the others to it.
2. Every phone runs the same APK (the game refuses a different build or version when joining).
3. Main menu: **Multiplayer**, then **New game** (or **Load game** to continue a saved multiplayer game).
4. Choose how to meet:
   - **Local Area Network**: the host picks **Create**, everyone else picks **Join** and taps the game in the list.
     The list refreshes about every 5 seconds.
   - **Enter Address**: the host picks **Create**; a joiner picks **Join**, then types the host's address
     (shown on the host's screens as "Your address: 192.168.x.x"). The box starts with the last address joined
     in this run, or empty. Use this if the list stays empty (some routers and hotspots drop broadcasts).
5. In the lobby choose nation and colour, chat, tap **Ready**; the host taps **Start**.
6. Player name, game name, password, address and chat all raise the phone keyboard when tapped.

There is no 7kfans.com button (it needs libcurl, not built in) and no leaderboard.

Ports: UDP **19255** (game) and UDP **19383** (session broadcast, host to 255.255.255.255). Nothing else.
Permissions (install time, no prompt): INTERNET, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE,
CHANGE_WIFI_MULTICAST_STATE. The app holds a Wi-Fi MulticastLock only while the multiplayer object exists
(from the session screens until you leave multiplayer); single player never holds it. Android 16 (target SDK 36)
needs no local-network permission; it becomes mandatory only for apps targeting SDK 37 (ACCESS_LOCAL_NETWORK),
see developer.android.com/privacy-and-security/local-network-permission.

## Limits

- Same APK only. No PC players (the PC build is x86; this one is arm64 and may differ in float results).
- Keep the app open (below). Screen-off counts as leaving.
- Cheats are locked in a match (the game does this itself: `!remote.is_enable()`).
- Games out of sync are reported, not repaired.

## Leaving the app during a match

What the code does:

- On leaving the app, `port/android/sk_lifecycle.cpp` calls `sys.pause()`. In a multiplayer game that is a networked
  pause: `Sys::pause` -> `Sys::set_speed(0)` (OSYS.cpp:1137-1145) broadcasts `MSG_SET_SPEED` (OSYS.cpp:2654-2662),
  and the other phones apply it even while frozen (`RemoteMsg::set_speed`, OREMOTEM.cpp:411-417; polled in
  `Sys::yield`, OSYS.cpp:1214). So everyone freezes, and nobody sits in "Waiting for X's Kingdom".
- ENet only queues that packet. We call `enet_host_flush` (`sk_net_flush`) right after `sys.pause()`, before SDL blocks
  the main thread (`SDL_HINT_ANDROID_BLOCK_ON_PAUSE`), otherwise it would go out only on return.
- While the app is in the background nothing services the network. ENet drops a silent peer after 5-30 s by default,
  and that is the real "connection lost" trigger: the macro `CONNECTION_LOST_TIME_OUT` (OSYS.cpp:1279) is defined but
  never used. Raising it would do nothing. The patch instead sets `enet_peer_timeout(peer, 32, 60000, 60000)` on every
  connected peer (`MultiPlayer::receive`, `ENET_EVENT_TYPE_CONNECT`), so a phone may be away about 60 s.
- On return the game stays paused (as in single player); tap Pause to resume. Resuming sends the speed back to the others.
- After the limit: ENet reports the peer lost; the first synchronised frame runs `news_array.multi_connection_lost`
  and turns that nation into an AI nation (OSYS.cpp:1431-1439). Both phones then carry on separately; the phone that
  was away sees the other nation as AI too. There is no rejoin.
- Caveats: the pause is a toggle in upstream (a phone that was already paused by hand is resumed by the message);
  the waiting phone gets no message saying why it is paused; Android may throttle the network of a backgrounded app,
  so the 60 s is a target, not a guarantee. A real network drop now also takes up to 60 s to be noticed.

Notices (news ticker, `NEWS_SK_NOTICE`, only when `remote.is_enable()`): at match start "Keep the app open: leaving
it for more than a minute hands your kingdom to the computer."; and once per match on the first mismatch of the random
seed or object CRC: "Games out of sync: this match may play differently on each phone." The game also already draws
"Multiplayer Random Seed Sync Error" / "Object Sync Error" at the top of the map once `sync_test_level` has failed
(OSYS2.cpp:1026-1036).

## Determinism risk (not fixed in this branch)

Lockstep multiplayer needs every phone to compute the same simulation. The same APK has the same machine code, but
`libm` comes from the phone's Android (bionic), so phones on different Android versions can round `sin`, `cos`,
`atan2` or `sqrt` differently. Sim call sites (verified in the patched tree):

| File:line | Use |
|---|---|
| OTORNADO.cpp:68-69, 87-88 | tornado damage point and movement (`sin`/`cos`) |
| OWEATHER.cpp:70, 207 | temperature amplitude, seasonal base temperature (`sin`). Line 272 is only the degrees-to-radians conversion; its callers use `sin`/`cos` |
| OW_FIRE.cpp:128-129 | fire spread from wind (`cos`/`sin` of `wind_direct_rad()`) |
| OLIGHTN.cpp:55, 145-148 | lightning length (`sqrt`), direction (`atan2`, `cos`, `sin`) |
| OLIGHTN2.cpp:102-107 | lightning branches (`atan2`, `cos`, `sin`) |

Also affected by the weather values, but drawing only: OWORLD_Z.cpp:587, 713, 1758. Integer maths (`Misc::sqrt`,
pathfinding) is exact. Mitigation if it bites: use phones on the same Android version, or replace these calls with a
fixed implementation in a later branch.

## Two-phone test checklist (UNVERIFIED until done)

1. Both phones: install the same debug/release APK; join the same Wi-Fi. Open Multiplayer, New game, LAN. "Your address"
   shows a 192.168.x.x/10.x line on every multiplayer screen (service, mode, game list, lobby) without covering controls.
2. Host: Create. Joiner: Join; the game appears in the list within ~10 s. Repeat with the host on a hotspot.
3. Joiner: Enter Address with the host's IP. Retry after a typo: "Unable to find that address". Second join in the same run
   starts with the last good address.
4. Keyboard: name, game name, password, address, lobby chat (send a message each way) and the map-seed field.
5. Lobby: ready/start, three players if possible. The 7kfans button is absent.
6. Match start: the "Keep the app open" notice shows on both. Cheats panel does not allow cheats.
7. Play 20-30 minutes; no "Sync Error" text, no desync notice, units agree on both screens.
8. Switch away on phone B for 20 s, then 50 s: phone A freezes (paused), B resumes after tapping Pause, play continues.
9. Away for about 90 s: expect the other nation to turn AI, with a "connection lost" news line.
10. Screen-off on one phone, Wi-Fi off/on, host leaves from the lobby.
11. Phones on different Android versions: watch for the desync notice (see Determinism above).
12. Single player afterwards: no multicast lock held (`adb shell dumpsys wifi | grep -i multicast`), behaviour unchanged.

## Code map

`patches/7kaa/0012-android-multiplayer.patch` (OGAMEMP.cpp screens, enet/multiplayer.cpp hooks, news notices, OSYS.cpp match
start), `port/android/sk_net.{h,cpp}` (lock via JNI, own address, last address, flush), `SevenKingdomsActivity.java`
(`acquireMulticastLock`, `releaseMulticastLock`, `getLocalIpv4Addresses`), `port/CMakeLists.txt` (`USE_ENET`, link enet).
The patch also shifts the game-list rows by the centred page origin (`MUPG_X1/MUPG_Y1`), which upstream omitted and which
matters on the wide screen.
