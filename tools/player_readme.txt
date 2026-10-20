Cry of Fear Co-op 1.1.1

Play the Cry of Fear campaign together (2 to 4 players), or alone with AI companions.

WHAT'S NEW IN 1.1.1
- J works in rooms that only fit one player, like the last room of the nightmare
  level: you arrive on your partner's spot instead of getting "No room next to ...".
- 1.1 and 1.1.1 can play together. The fix is in the host's game, so the host
  should have 1.1.1.

WHAT'S NEW IN 1.1
- Night vision no longer flickers. The green light around you was drawn twice on
  about every other frame, so walls and floors strobed.
- Levels keep all of their decals (posters, blood, signs). The joined subway level
  had more than the game could hold and lost some.
- Setup and Update show what they are doing as they do it: each step, the files being
  copied, the models and maps being made, and the time so far. The same lines are kept
  in cof-story-setup.log in the co-op copy.
Coming from 1.0 or 1.1? Run the new "CoF Story Co-op.exe" and click "Update".

YOU NEED
- Windows 10 or 11, and Cry of Fear installed from Steam (it's free). Keep Steam running.
- About 6 GB of free disk space.

INSTALL
1. Extract this zip to a normal folder, such as Downloads (not into the game folder).
2. Double-click "CoF Story Co-op.exe". Windows may warn you because the app isn't signed:
   click "More info", then "Run anyway".
3. Click "Set up". The app finds your game through Steam (or use Browse to pick the
   Cry of Fear folder) and makes a separate copy for co-op. Your Steam game, saves and
   settings are not changed.
4. Afterwards, start it with the "Cry of Fear Co-op" shortcut on your desktop or in the
   Start menu.

PLAY
- Play solo: you and one to three AI companions.
- Host a game: friends join you. Send them the join code the app shows.
  If they can't connect, your router didn't open the port by itself: forward UDP 27015
  to your PC in the router, or all use a VPN such as Tailscale, ZeroTier or Radmin and
  share that address. If Windows Firewall asks about Cry of Fear, allow it.
- Join a game: paste the host's join code (or address) and their password, if any.
Everyone needs the same version of Cry of Fear Co-op.

Start a new game (the intro), any chapter, or Continue from the last co-op checkpoint.
The game makes a co-op checkpoint by itself after every level change.
Tick "Bonus room unlocked" to open the bonus room (FAMAS, axe and more) for everyone.
Dual wield (two pistols, or a pistol and a light) works from the inventory menu.
"Merged maps" (on by default) loads neighbouring levels as one map, so there are fewer
loading screens. If a merged level misbehaves, the host can untick it and start that
chapter again.

CO-OP KEYS
J      join your partner (when the screen says "Press J to join")
Enter  skip a cutscene for everyone (press again to watch the rest)
H      tell the AI companions to wait or to follow
Space  when dead: respawn next to your partner after about 10 seconds

UPDATES
The app checks for a newer version when it opens. If there is one, click "Update now":
it downloads the new version, closes, and the new version updates your copy.
Your saves and settings are kept. (A newer zip's "CoF Story Co-op.exe" and its
"Update" button do the same.)

UNINSTALL
Delete the CoF-Story-Coop folder (%LOCALAPPDATA%\CoF-Story-Coop unless you chose
another place) and the two "Cry of Fear Co-op" shortcuts.

KNOWN ISSUES
- At the college, the doors of the classroom where you find the screwdriver can keep
  bringing you back into the same room. If it happens to you, please report it.

REPORTING BUGS
Join the Discord: https://discord.gg/4uwV6jmmY7 ("Report a bug" in the app opens it).
Say what happened, the map or the place, roughly when, and whether "Merged maps" was on,
and attach the log ("Open logs folder" in the app; the log may show your Windows user name).
If Setup or Update stops, attach cof-story-setup.log from the co-op copy folder.
Never post your password or join code publicly.

Cry of Fear Co-op is a fan project, not made or endorsed by Team Psykskallar, the makers
of Cry of Fear. Cry of Fear itself is not included: the app uses your own Steam copy.
The engine is free software (GPL); its source is in the source-and-licences folder.
