# Playtesting

Thanks for testing. This build lets 2–4 people play the single-player Cry of Fear campaign
together. It is early and experimental: you will find bugs, and finding them is the point.

## What you need

- A Windows PC with Cry of Fear installed from Steam (it is free). Keep Steam running and signed in.
- About 6 GB of free disk space.
- The package zip the host sends you, and the invite (host address and server password), which
  the host sends you separately and privately.
- A GitHub account that the host has invited to the repository, for bug reports.

You don't need Python or anything else.

## Install

1. Extract the whole zip to a normal folder, such as your Downloads folder. Not into the Steam game folder.
2. Double-click `CoF-Story-Setup.exe`. The app isn't signed, so Windows may warn you about it.
   If the zip came from the host, choose **More info**, then **Run anyway**.
3. In the setup window, **Original game** is usually filled in from Steam. If it's empty, use
   **Browse…** and pick the Cry of Fear folder (the one with `CoFLaunchApp.exe`).
   **Separate copy** is where the test copy goes. The default, `%LOCALAPPDATA%\CoF-Story`, is fine.
4. Click **Set up separate copy** and wait. Setup copies the game files, installs the matching
   engine, makes the two Simon models from your own game files, and checks everything.

Your Steam game is not changed. Your saves and settings are not copied. To uninstall, delete the
`CoF-Story` folder.

If setup refuses:

- "differs from retail Cry of Fear 1.6": setup needs the unmodified Steam game DLL. In Steam, open
  the game's Properties, then Installed Files, and verify the game files. Then try again.
- "Choose a new destination folder": that folder already exists. Delete the old `CoF-Story`
  folder or choose another place.
- Anything else: send the host the message, and `friend-setup-install.log` from the new folder if
  there is one. It shows folder paths that include your Windows user name.

## Join

When setup is done, the window asks for **Host address**, **Server password** and **Your name**.

1. Wait until the host says the game is running.
2. Enter the address and password from the invite, and the name the others will see (up to 24
   plain letters, digits and punctuation; no accents, `"`, `\` or `;`).
3. Click **Join host**. One game window opens (960×540).

Next time, double-click `JOIN.cmd` in your `CoF-Story` folder to open the join window again.
Close the game before you join again.

Each join sets mouse sensitivity back to 4. Change it in Options after you join.

When the host sends a new package, delete the old `CoF-Story` folder, extract the new zip and run
setup again. Everyone must run the same build.

## Controls

The usual game keys are set up for you: WASD, Space jump, Ctrl crouch, Shift sprint, Alt dodge,
E use, R reload, Tab inventory, B objectives, F flashlight, 1–3 quick select. Change them in
Options > Keybinds. The co-op keys:

| Key | What it does |
| --- | --- |
| J | Join your partner: go to where they are. Use it when the screen says "Press J to join". It doesn't work while you're frozen in a cutscene, on a ladder or in a puzzle view. |
| Enter | Skip the cutscene for the whole party; press it again to stop the skip and watch the rest |
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
- **Ladders:** one person at a time. **Yes/no questions:** only the person asked can answer.
- The host may turn on a **test gun**: everyone gets an M16 with infinite ammo. It's a testing
  aid, not part of the game.

## What works so far

Played live so far, with two game clients: the intro, `c_nightmare`, `c_start` and `c_start2`.
The rest of the campaign has only passed offline tests and has not been played yet.

Known problems. Report them only if they stop you from playing:

- Some effects reach only one player: the look-at in `c_village3`, the grey fade in `c_ending4`,
  camera zooms and blur.
- `c_asylumb` puzzle: a guest who solves it may need to press the puzzle's close button.
- `c_ending1c`–`c_ending3c` strangle: only the host's attack finishes it.
- Weapons dropped and saved in `c_college1` come back only for the player who saved.
- A skipped cutscene's music and voices overlap.
- Not handled for each player yet: the story switching the phone off, the hoodie, the health cap,
  the boss health bar after a respawn, and two players using telephones at once.
- One report, not confirmed yet: in `c_nightmare` the camera couldn't be added back to the inventory.

## Reporting a bug

Open an issue at <https://github.com/charliee1w/cof-story-coop/issues/new/choose> and choose
**Bug report**. One bug per issue. The template asks for:

- the package zip name, and whether you were the host or a guest;
- the map name (if you don't know it, describe the place);
- roughly when it happened (your local time, and your time zone if it differs from the host's),
  so the host can find it in the server log;
- what happened, what you expected, and the steps that led to it;
- screenshots or a video. If the game crashes with an error window, take a screenshot of it;
- your game's log from that session (see below).

**Keep the invite private.** Issues are visible to everyone with access to the repository.

- Never post the host address or the server password, not even in a screenshot or log.
- Never attach `cryoffear\cof_story_join.cfg`: it holds the password in plain text.
- Typing `password` or `setinfo` in the console prints the password. Don't do it while recording
  a log or a video.

### Your game's log

Every game you start from the join window writes `story-tester.log` in your `CoF-Story` folder
(`%LOCALAPPDATA%\CoF-Story` unless you chose another place). When you join again, the previous
log is moved to the `story-logs` folder beside it, named after the time it was last written. Pick
the one from the session where the bug happened.

Before you attach it, open it in Notepad and delete every line that contains the host address
(search for "Connecting to" and for the address itself). It may show folder paths with your
Windows user name; replace that if you like.
