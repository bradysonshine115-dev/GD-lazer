# Changelog

## v0.5.5

- Song select has osu!'s scrollbar: drag it, or tap beside it to jump. Held, it widens, follows your finger and shows where you are in the list: the position, first letter, difficulty or progress, by the sort
- Delete a saved level from song select with the bin next to the heart
- A level whose song isn't downloaded asks first: download and play, play without music, or cancel, instead of downloading everything straight away
- Song select's confirmations are osu!'s dialogs now. Hold the red button to confirm a deletion
- Blocked songs page (Settings > Lazer UI > Music > Blocked songs): unblock songs one by one or all at once
- GD's popups are easier to read: text keeps its colours and stays centred, a dark outline keeps it readable over bright images, and button text is no longer oversized
- Fixed: the menu underneath GD's popups (daily, weekly, gauntlets...) still reacted to clicks
- Fixed: a profile opened from level comments showed behind the level info
- Fixed: the osu! cursor was drawn under Eclipse's menu (PC)

## v0.5.4

- The osu! cursor tilts as it moves, more the faster you move it. "Cursor rotation" in Settings > Lazer UI > Cursor turns off both the tilt and the drag spin
- Fixed: the main levels' songs didn't play in the menu music player or song select on Android
- The main levels' songs show their level in the now playing card

## v0.5.3

- Sharper text everywhere in Lazer UI: letters and icons are now drawn from distance fields, so they stay crisp at any size instead of going blotchy when shrunk
- The cube on the logo has a clean edge instead of a stair-stepped one
- osu!'s cursor on PC: it shrinks and glows pink when you click, turns to follow a drag, and taps. It appears once the game has loaded. Settings > Lazer UI > Cursor turns it off, changes its size or turns off the drag rotation
- Lazer UI's settings are now the first section in settings

## v0.5.2

- <cr>**IMPORTANT (Android): if nothing below shows up after updating, update by hand once.**</c> Older versions of the updater change the version number but keep running the old code. Close Geometry Dash, download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases, **delete** the old one in `Android/media/com.geode.launcher/game/geode/mods` and copy the new one in (don't just overwrite). After that, updates apply properly
- Play a level that isn't downloaded yet straight from song select: the loader downloads the level and its song with a progress bar, then starts it (no more detour through GD's level page)
- A close button on every page, and a back button at the bottom of settings (phones had no visible way out)
- The main levels' songs play in the menu music player too
- Like and dislike posts on other players' profiles
- Fixed: the loading circle wobbled instead of spinning in place

## v0.5.1

- <cr>**IMPORTANT (Android): if nothing below shows up after updating, update by hand once.**</c> Older versions of the updater change the version number but keep running the old code. Close Geometry Dash, download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases, **delete** the old one in `Android/media/com.geode.launcher/game/geode/mods` and copy the new one in (don't just overwrite). After that, updates apply properly
- Fixed: Enter and Space on the main menu opened GD's main levels; they now press the logo, like osu!
- Fixed: the home button in the toolbar didn't close the open page; it now closes it, and goes back a menu when nothing is open
- Fixed: Escape did nothing in song select
- Fixed: closing friend requests, friends or messages from your profile opened a second profile page
- Fixed: "view profile" in the account card did nothing while statistics, achievements, rewards or quests were open
- Fixed: the settings (and other pages) stopped scrolling with the mouse wheel after another page had been opened
- Stronger parallax by default: background 6% and menu 1.5% on PC, 8% and 2% on phones (reset the sliders in Settings > Lazer UI > Parallax to get the new values)

## v0.5.0

- <cr>**IMPORTANT (Android): if nothing below shows up after updating, update by hand once.**</c> Older versions of the updater change the version number but keep running the old code. Close Geometry Dash, download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases, **delete** the old one in `Android/media/com.geode.launcher/game/geode/mods` and copy the new one in (don't just overwrite). After that, updates apply properly
- Tilt parallax on phones: tilt the phone and the menu background moves with it, like the mouse does on PC (the gravity sensor or accelerometer; can be turned off)
- The menu buttons move with the parallax too, a little less than the background
- Settings > Lazer UI > Parallax: background and menu parallax amounts, and the tilt toggle on phones
- Music carries over between the menu and song select: entering song select keeps the menu's song playing and selects its level; going back, the menu keeps playing what song select was on

## v0.4.3

- <cr>**IMPORTANT!!! Android players: this updater was broken.**</c> Updates since v0.3.0 changed the version number but kept running the old v0.3.0 code. If your logo is still pink with "GD" on it, updating here won't fix it. Reinstall once by hand:
    1. Close Geometry Dash completely
    2. Download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases
    3. In a file manager, open `Android/media/com.geode.launcher/game/geode/mods`, **delete** the old `kamol1dn.lazer-ui.geode`, then copy the downloaded one in (delete first, don't just overwrite)
    4. Start the game: the logo shows your cube in your colours. From then on the updater works properly (Windows was never affected)

## v0.4.2

- Fixed: on Android, updates from the in-game updater could keep running the old version (the version number changed but nothing else did). If you're on Android and don't have the new logo, reinstall this version once by hand; later updates apply properly

## v0.4.1

- Starting a level plays osu!'s loader: song select fades away, the level's card scales in over its background, then the level starts (back cancels)

## v0.4.0

- The logo takes your icon's colours, with your cube in place of the "GD" text
- New intro: the logo draws itself to the opening of osu!'s triangles theme; outro says "see you next time"
- RobTop's levels (main levels and the Tower) have screenshots in song select, as panel thumbnails and the background
- Song select groups: saved (the default), official and liked; "all" is gone
- Folder dropdown for GD's saved-level folders
- Heart levels from song select; "liked" shows only hearted levels
- Delete unhearted levels (keeps levels in folders), from the footer
- Level details: a song card (download, extra songs and SFX, and Jukebox's song switching when it's installed), per-level low detail mode and disable shake, and the level's leaderboard on request (loading it syncs your progress)
- Attempts, jumps, downloads and likes on one line; the details scroll
- Fixed: pressing play let the preview song run on into the level

## v0.3.1

- Quests page fits small screens (phones with a big UI scale)
- Update prompts use the Lazer popup style
- Settings sidebar stays collapsed on touchscreens instead of covering the settings

## v0.3.0

- Play menu: separate song selects for classic and platformer levels (replacing "main levels" and "the tower")
- Platformer song select includes the Tower's levels and shows moons and best times
- Quests page in the Lazer style: quest cards with progress bars, diamond rewards and claiming, opened from the toolbar
- Updates itself: checks GitHub on start and offers to install new versions (Lazer settings > Updates)

## v0.2.0

- Android support (arm64 and armv7); releases ship one .geode for Windows and Android
- UI scale setting (Lazer UI > Layout), 150% by default on Android where screens are small
- Fixed a crash when pressing back on Android
- Toolbar buttons work on touchscreens
- Popups from other mods keep their own design on Android too

## v0.1.0

First alpha release, for testing. Expect crashes.

- osu!lazer-style main menu: logo, visualiser, button bar with play / create / browse submenus, toolbar
- Menu music player with level thumbnail backgrounds, now-playing card, song blocking
- Song select for RobTop's levels and saved levels
- Overlays: settings, daily chests, achievements, statistics, account card, profiles
- Restyled popups
- Loading screen, intro and outro
- UI sounds
- Optional integrations: Level Thumbnails, Separate Dual Icons, Better Progression, Globed
