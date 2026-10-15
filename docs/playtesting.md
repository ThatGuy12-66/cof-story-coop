# Playtesting

Thanks for testing. This build lets 2–4 people play the single-player Cry of Fear campaign
together, or one person with an AI companion. It is early and experimental: you will find bugs,
and finding them is the point.

## What you need

- A Windows 10 or 11 PC with Cry of Fear installed from Steam (it is free). Keep Steam running
  and signed in.
- About 6 GB of free disk space.
- The tester zip (`CoF-Story-Coop-Test1.zip`). Everyone who plays together needs the same one.

You don't need Python or anything else.

## Install

1. Extract the whole zip to a normal folder, such as your Downloads folder. Not into the Steam
   game folder.
2. Double-click `CoF Story Co-op.exe`. The app isn't signed, so Windows may warn you about it:
   choose **More info**, then **Run anyway**.
3. **Cry of Fear folder** is usually filled in from Steam. If it's empty, use **Browse…** and pick
   the Cry of Fear folder (the one with `CoFLaunchApp.exe`). **Co-op copy** is where the separate
   copy goes; the default, `%LOCALAPPDATA%\CoF-Story-Coop`, is fine.
4. Click **Set up** and wait. Setup copies the game files, installs the co-op engine, makes the
   two Simon models and the bigger inventory's art from your own game files, and checks
   everything. It ends with a **Cry of Fear Co-op** shortcut on your desktop and in the Start menu.

Your Steam game is not changed. Your saves and settings are not copied. To uninstall, delete the
`CoF-Story-Coop` folder and the two shortcuts.

If setup refuses:

- "differs from retail Cry of Fear 1.6": setup needs the unmodified Steam game. In Steam, open
  the game's Properties, then Installed Files, and verify the game files. Then try again.
- "Choose a new destination folder": that folder already exists. Delete the old `CoF-Story-Coop`
  folder or choose another place.
- Anything else: send the message, and `friend-setup-install.log` from the new folder if there is
  one. It shows folder paths that include your Windows user name.

**Updates:** when you get a newer zip, run its `CoF Story Co-op.exe` and click **Update**. Your
saves and settings are kept.

## Play

Start the app from the shortcut, type the name the others will see, and pick one:

- **Play solo**: you and the AI companion, on your PC only.
- **Host a game**: friends join you. Choose 2–4 players, an optional password, and whether the AI
  companion keeps you company until a friend takes its place. After the game starts, the app shows
  a **join code**: send it (and the password) to your friends.
  - The app asks your router to open UDP 27015 (UPnP) and closes it again when the game ends.
    If the router doesn't, the app says so: forward UDP 27015 to your PC in the router, or all
    use a VPN such as Tailscale, ZeroTier or Radmin and share that address instead.
  - If Windows Firewall asks about Cry of Fear, allow it.
- **Join a game**: paste the host's join code (or address) and the password, if any. The app
  checks first that the host answers, runs the same version and has room.

Solo and Host both start with **New game** (the intro), a **chapter**, or **Continue** from the
last co-op checkpoint (experimental). The game makes a checkpoint by itself after every level
change. Continue gives your items back only under the name you saved with.

The first game opens full screen at your desktop's size; change it in the game's Options.

## Controls

The usual game keys are set up for you: WASD, Space jump, Ctrl crouch, Shift sprint, Alt dodge,
E use, R reload, Tab inventory, B objectives, F flashlight, 1–3 quick select. Change them in
Options > Keybinds. The co-op keys:

| Key | What it does |
| --- | --- |
| J | Join your partner: go to where they are. Use it when the screen says "Press J to join". It doesn't work while you're frozen in a cutscene, on a ladder or in a puzzle view. |
| Enter | Skip the cutscene for the whole party; press it again to stop the skip and watch the rest |
| H | Tell the AI companion to wait or to follow |
| Y | Chat |
| K | Voice chat, hold to talk |
| Space, when dead | Respawn next to your partner, after about 10 seconds |

**Skipping cutscenes.** Anyone can skip a cutscene for the whole party. It then plays about ten
times faster to its end, and every scripted event still happens. You'll see
"*name* skips the cutscene (Enter: watch it)".

## How co-op behaves

- **Cutscenes:** everyone watches them, frozen and safe from monsters.
- **Level exits:** when one player reaches an exit, the level waits. They see "Please wait until
  everyone is ready!", the others see "*name* is at the exit. Press J to join." After about 10
  seconds the waiting player can press J to go on alone.
- **Death:** after about 10 seconds, press jump to respawn next to your partner with your items.
  A partner can also revive you.
- **Story items:** keys, notes and codes count for everyone. Weapons the story hands out go to everyone.
- **Inventory:** 12 slots instead of 6.
- **Ladders:** one person at a time. **Yes/no questions:** only the person asked can answer.
- **AI companion:** it follows you, opens doors, fights, heals you with syringes and revives you.
  It never takes exits, items or story triggers.

## What works so far

Played live so far: the intro, `c_nightmare`, `c_start`, `c_start2` and the first apartment maps,
with two players and with the AI companion. The rest of the campaign has only passed offline
tests and has not been played yet.

Known problems. Report them only if they stop you from playing:

- The AI companion sometimes teleports behind you instead of walking, and can stand in your view
  right after a cutscene.
- Some effects reach only one player: the look-at in `c_village3`, the grey fade in `c_ending4`,
  camera zooms and blur.
- `c_asylumb` puzzle: a guest who solves it may need to press the puzzle's close button.
- `c_ending1c`–`c_ending3c` strangle: only the host's attack finishes it.
- Weapons dropped and saved in `c_college1` come back only for the player who saved.
- A skipped cutscene's music and voices overlap.
- Not handled for each player yet: the story switching the phone off, the hoodie, the health cap,
  the boss health bar after a respawn, and two players using telephones at once.

## Reporting a bug

Click **Report a bug** in the app (it opens the issue page; the host invites you to the
repository), or send the details to whoever gave you the zip. One bug per report:

- the zip name, and whether you played solo, hosted or joined;
- the map name (if you don't know it, describe the place);
- roughly when it happened (your local time, and your time zone);
- what happened, what you expected, and the steps that led to it;
- screenshots or a video. If the game crashes with an error window, take a screenshot of it;
- your game's log from that session (see below).

**Keep your game private.** Never post your password or join code publicly, not even in a
screenshot or log.

### Your game's log

**Open logs folder** in the app shows `story-session.log`, the log of your latest game. Each new
game first moves the previous log into `story-logs` beside it, named after the time it was last
written. Pick the one from the session where the bug happened. Before you send it, open it in
Notepad and delete every line with "Connecting to" or the host's address. It may show folder
paths with your Windows user name; replace that if you like.
