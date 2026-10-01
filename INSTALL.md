# Installing Lazer UI

Lazer UI is not on Geode's mod index, so you won't find it in Geode's in-game mod browser. You download one file from GitHub and install it yourself. This page walks through it step by step for **Windows**, **Android** and **macOS**.

> [!WARNING]
> Lazer UI is an **alpha** for testing and can crash. Back up your save first (GD's account page: **Save**, or copy your save files), and report crashes with the crash log (see [When something goes wrong](#when-something-goes-wrong)).

**Contents**

- [What you need](#what-you-need)
- [Step 1: download the mod](#step-1-download-the-mod)
- [Windows](#windows)
- [Android](#android)
- [macOS](#macos)
- [First start](#first-start)
- [Updating](#updating)
- [Turning it off or uninstalling](#turning-it-off-or-uninstalling)
- [When something goes wrong](#when-something-goes-wrong)

## What you need

- **Geometry Dash 2.2081**, the current version. Steam on Windows and macOS, Google Play on Android.
- **Geode 5.10.1 or newer** (a Geode 5 version). Geode is the mod loader; Lazer UI runs on top of it.
- Two mods Lazer UI needs, both installed from inside Geode's own mod browser: **Image Plus** and **Custom Keybinds**. The steps below cover when to install them.

iOS isn't supported.

## Step 1: download the mod

There is **one file for every platform**: `kamol1dn.lazer-ui.geode`. The same file works on Windows, Android and macOS.

1. Open the [Releases page](https://github.com/kamol1dn/GD-lazer/releases).
2. The release at the **top** is the newest. They're all marked "Pre-release", which is normal for this mod.
3. Under that release, open **Assets** and click `kamol1dn.lazer-ui.geode` to download it.

Keep the file's name and its `.geode` ending. If your browser saved it as `kamol1dn.lazer-ui.geode.zip` or `kamol1dn.lazer-ui.zip`, rename it back to `kamol1dn.lazer-ui.geode`.

**Want something newer than the latest release?** Every change gets built on the [Actions page](https://github.com/kamol1dn/GD-lazer/actions). Open the newest run with a green check, scroll to **Artifacts** and download `lazer-ui`. You need to be signed in to GitHub for this. It downloads as a `.zip`: open it and take the `kamol1dn.lazer-ui.geode` out. These builds are less tested than releases.

## Windows

### 1. Install Geode

If you already have Geode (you see a Geode button on GD's main menu), skip to step 2.

1. Close Geometry Dash.
2. Go to [geode-sdk.org](https://geode-sdk.org/) and download the installer for Windows.
3. Run it. It finds your Steam copy of GD by itself; if it doesn't, point it at the folder that contains `GeometryDash.exe`.
4. Start GD. A Geode loading bar on startup and a Geode button on the main menu mean it worked.

### 2. Install Image Plus and Custom Keybinds

1. On GD's main menu, press the **Geode** button.
2. Open the **Download** tab and search for **Image Plus**. Install it.
3. Search for **Custom Keybinds** and install it.
4. Restart GD when Geode asks.

### 3. Install Lazer UI

**The easy way, from inside GD:**

1. Press the **Geode** button on the main menu.
2. Press the **gear** button in Geode's mod list. This opens Geode's own settings.
3. Press **Install From File**. The first time, Geode shows a note about manually installed mods: press OK, then press **Install From File** again.
4. Pick the `kamol1dn.lazer-ui.geode` you downloaded.
5. Restart GD.

**Or copy it into the mods folder yourself:**

1. Close GD.
2. In Steam, right-click Geometry Dash, then **Manage > Browse local files**.
3. Open the `geode` folder, then `mods`.
4. Copy `kamol1dn.lazer-ui.geode` into it.
5. Start GD.

The mods folder is also one button away inside GD: Geode's settings (step 2 above) have **Open Mods Folder**.

Continue with [First start](#first-start).

## Android

### 1. Install Geode

If you already open GD through the Geode launcher, skip to step 2.

1. You need the full Geometry Dash from Google Play, version 2.2081. Lite isn't supported.
2. Go to [geode-sdk.org](https://geode-sdk.org/) on your phone and download the **Geode launcher** app for Android. Your phone will ask you to allow installing apps from your browser: allow it for this install.
3. Open the Geode launcher and follow its setup. It uses your installed copy of GD.
4. From now on, **always start GD from the Geode launcher**, not from GD's own icon. Starting it from GD's icon runs it without any mods.

### 2. Install Image Plus and Custom Keybinds

1. Start GD from the Geode launcher.
2. On the main menu, tap the **Geode** button.
3. Open the **Download** tab, search for **Image Plus** and install it.
4. Search for **Custom Keybinds** and install it.
5. Restart GD when Geode asks.

### 3. Install Lazer UI

**The easy way, from inside GD:**

1. Download `kamol1dn.lazer-ui.geode` on your phone ([Step 1](#step-1-download-the-mod)). It lands in your **Downloads**.
2. Start GD from the Geode launcher and tap the **Geode** button.
3. Tap the **gear** button in Geode's mod list. This opens Geode's own settings.
4. Tap **Install From File**. The first time, Geode shows a note about manually installed mods: tap OK, then tap **Install From File** again.
5. Android's file picker opens. Go to **Downloads** and pick `kamol1dn.lazer-ui.geode`.
6. Restart GD: close it fully (swipe it away from your recent apps), then open it again from the Geode launcher.

**Or copy it with a file manager:**

1. Close GD fully.
2. In a file manager, open your phone's internal storage, then `Android/media/com.geode.launcher/game/geode/mods`.
3. Copy `kamol1dn.lazer-ui.geode` into it.
4. Start GD from the Geode launcher.

Some file managers can't open `Android/media`. Use the in-game way above if yours can't.

### Phone notes

- The menus are bigger on phones by default (UI scale 150%). Change it in Lazer's settings under **Layout > UI scale**.
- The background follows your phone's tilt instead of a mouse. Turn it off with **Tilt parallax**.
- The osu! cursor is a PC thing and doesn't appear on phones.

Continue with [First start](#first-start).

## macOS

Lazer UI runs on Apple Silicon and Intel Macs. The osu! cursor and Lazer's graphics and parental dialogs are Windows-only for now: on a Mac you get the normal system cursor and GD's own versions of those dialogs.

### 1. Install Geode

If you already have Geode (you see a Geode button on GD's main menu), skip to step 2.

1. Quit Geometry Dash.
2. Go to [geode-sdk.org](https://geode-sdk.org/) and download the installer for macOS.
3. Open it and follow its steps. It finds your Steam copy of GD; if it asks, choose `Geometry Dash.app` in Steam's game folder.
4. Start GD. A Geode loading bar on startup and a Geode button on the main menu mean it worked.

### 2. Install Image Plus and Custom Keybinds

1. On GD's main menu, click the **Geode** button.
2. Open the **Download** tab, search for **Image Plus** and install it.
3. Search for **Custom Keybinds** and install it.
4. Restart GD when Geode asks.

### 3. Install Lazer UI

**The easy way, from inside GD:**

1. Click the **Geode** button on the main menu.
2. Click the **gear** button in Geode's mod list. This opens Geode's own settings.
3. Click **Install From File**. The first time, Geode shows a note about manually installed mods: click OK, then click **Install From File** again.
4. Choose the `kamol1dn.lazer-ui.geode` you downloaded (usually in **Downloads**).
5. Restart GD.

**Or copy it into the mods folder yourself:**

On macOS, Geode's folder lives inside the game's app.

1. Quit GD.
2. In Steam, right-click Geometry Dash, then **Manage > Browse local files**. Finder opens Steam's Geometry Dash folder.
3. Right-click (or Control-click) `Geometry Dash.app` and choose **Show Package Contents**.
4. Open `Contents`, then `geode`, then `mods`.
5. Copy `kamol1dn.lazer-ui.geode` into it.
6. Start GD.

Geode's settings (step 2 above) also have **Open Mods Folder**, which opens that folder for you.

Continue with [First start](#first-start).

## First start

1. GD starts with a black loading screen and a spinner, then Lazer UI's intro. Click or tap the big logo to go to the menu.
2. The main menu is the button bar in the middle: **settings**, **play**, **create**, **browse**, **icons** and **exit**. Play opens your levels (classic, platformer, daily, weekly, event), browse opens online levels, create opens your own levels and lists.
3. The bar along the top holds everything else: gauntlets and map packs on the left, then achievements, statistics, leaderboards, quests, paths, chests, the music player and the **mods** button (the puzzle piece, which opens Geode's mod list) on the right. Your account is on the far right.
4. **Settings** has both Lazer UI's options and GD's own. Use the search box at the top to find anything.

Things worth knowing:

- **Lazer's pause menu and results screen are off by default.** Turn them on in settings with **Lazer pause and results**.
- The menu plays songs from levels you've downloaded. Turn it off with **Menu music player** if you want GD's menu loop.
- **Level Thumbnails** (`cdc.level_thumbnails`, from Geode's mod browser) is optional but recommended: it puts each level's picture on song select, the level page and the background.

## Updating

Lazer UI checks for updates itself every time GD starts. When there's a new version, it shows what changed and offers to install it: accept, then restart GD. You can also check by hand: Lazer's settings, **Updates**, **Check for updates**.

To update by hand instead, download the new `kamol1dn.lazer-ui.geode` and **replace** the old file in the mods folder (same name, overwrite it). Don't keep two copies of it in the mods folder.

On Android, close GD fully after updating (swipe it away from recent apps) and open it again from the Geode launcher.

## Turning it off or uninstalling

- **Turn it off for a while:** in Geode's mod list, disable Lazer UI, or open its settings there and switch off **Enable Lazer menu**. Restart GD.
- **Uninstall:** in Geode's mod list, open Lazer UI and uninstall it, or delete `kamol1dn.lazer-ui.geode` from the mods folder. Restart GD.

Your GD progress is GD's own save. Lazer UI doesn't touch it, so removing the mod leaves your levels, stars and icons as they were.

## When something goes wrong

**GD crashed.** Geode tells you on the next start. The crash log is in Geode's `crashlogs` folder, next to `mods` (Windows: `Geometry Dash/geode/crashlogs`; Android: `Android/media/com.geode.launcher/game/geode/crashlogs`; macOS: inside `Geometry Dash.app/Contents/geode/crashlogs`). [Open an issue](https://github.com/kamol1dn/GD-lazer/issues) with that file and what you were doing.

**The menus still look like normal GD.**
- Check that Lazer UI is in Geode's mod list and enabled.
- If Geode lists it with a problem, it's usually a missing dependency: install **Image Plus** and **Custom Keybinds** from Geode's mod browser and restart.
- "Unsupported version" means GD or Geode is older than what Lazer UI needs. Update both.
- On Android, make sure you started GD from the Geode launcher.

**"Invalid file" when installing.** The download got broken or renamed. Download it again and make sure it ends in `.geode`.

**An update didn't seem to apply.** Restart GD once more. On Android, close it from recent apps first. If it's still the old version (the **Check for updates** button in Lazer's settings shows the version you're running), replace the file by hand as described in [Updating](#updating).

**Another mod's menu or button is missing or misbehaves with Lazer UI.** Lazer UI replaces GD's menus, so mods that add buttons there can end up somewhere else (most move to the toolbar or the matching page) or not show at all. [Open an issue](https://github.com/kamol1dn/GD-lazer/issues) and name the mod.
