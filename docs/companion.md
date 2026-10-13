# The AI companion

For playing the story campaign with company but without a second person. The engine adds an AI
player beside you: no second window, no second copy of the game. It is a full player in the game's
own rules: it walks, shoots, reloads, revives and uses syringes through the same player code you use.
The story co-op engine drives it. It is experimental and not yet tested in a live game.

## Turning it on

- Test copies: `.\tools\Start-CampaignTest.ps1 -Mode companion` (one window, the companion joins
  once the map has loaded).
- Any story co-op host (engine with the companion, `maxplayers` 2 or more): in the console
  `cof_companion 1` (or `cof_companion_cmd add`); `cof_companion 0` sends it away.
- If a friend connects while the server is full, the companion leaves to make room for them.

## What it does

- **Follows you.** It walks the path you walked, so it gets round corners and through rooms the way
  you did. If it falls behind, gets stuck or loses you (a door, a ladder, a scripted move), it
  reappears behind you, out of your view where it can.
- **Fights.** It takes on hostile monsters it can see near you, the ones already after one of you
  first. With a gun it raises the sights for a distant target, fires one shot per trigger pull and
  reloads when the gun is empty; it backs off from anything too close while shooting. Without a gun
  it uses its best melee weapon (axe, nightstick, branch, then the switchblade). It holds fire while
  you are in the line of fire. Your shots cannot hurt it, and its shots cannot hurt you.
- **Leaves friends alone.** Scripted actors and allies (the game's own "friendly" monsters, the
  ghosts, the boss's shared health in the last fight) and bosses waiting for their entrance are not
  targets.
- **Fights back when grabbed.** When a monster turns its gun on it, it struggles, the way you mash
  the attack button.
- **Revives you.** When you are down, it says so, comes to you and does the game's own CPR (5
  seconds; you get up with 30 health). Anything attacking right beside it comes first.
- **Patches you up.** When you are at half health or less and nothing is around, it walks up and
  gives you a syringe of its own (the game's own partner heal), at most once every 90 seconds.
  Both of you are held still for a moment while it goes in.
- **Looks after itself.** It injects a syringe when badly hurt and safe, recovers slowly out of a
  fight, and never runs out of rounds. It gets up after a hard landing.
- **Lights the way.** It keeps the co-op flashlight on, which you see as its torch beam.
- **Shares the story's weapons.** It gets the phone and switchblade and the weapons the story hands
  out, as a second player would. It never picks anything up: keys, notes, ammo and syringes on the
  map are all yours.
- **Respawns** beside you after a death, unless you are reviving it.

## What it never does

- It never sets off the story: no scripted triggers, no level exits, no pickups. Doors and scenes
  are yours to start. It cannot pull you into the next map by walking into the exit first.
- It does not fall to its death in the story's pits (those are story triggers too): if it drops into
  one, it reappears behind you.
- It does not solve puzzles, use keys, read notes or answer the story's questions.
- It does not hold level exits: when you leave a map, it comes with you and reappears in the next.

## Orders

- **H** (`cof_companion_order`): wait here / follow me. Waiting, it holds its spot, still fights what
  comes near, still revives you wherever you fall, and gives you a syringe when you come to it.
- Host console: `cof_companion_cmd wait | follow | status`.

## Settings (host console)

| Cvar | Default | Meaning |
|---|---|---|
| `cof_companion` | 0 | How many companions to keep (each needs a free player slot; the game takes four players at most). |
| `cof_companion_name` | Companion | Its name. |
| `cof_companion_medic` | 90 | Seconds between the syringes it gives you; 0: never (and it carries none). |
| `cof_companion_ammo` | 1 | It never runs out of rounds; 0: it has only what the story gives it. |
| `cof_companion_light` | 1 | It keeps its flashlight on. |
| `cof_companion_regen` | 3 | Health it recovers each second out of a fight. |
| `cof_companion_drive` | 0 | Testing: the AI also plays a second person's client while its controls rest. |
| `cof_companion_idle` | 8 | Seconds of rest before the AI takes such a client over. |

## Limits

- No navigation map exists for the story maps, so it follows your path rather than planning its own;
  it catches up by reappearing behind you when that fails.
- The game counts it as a player: monsters hit about 17% harder than when you play alone, exactly as
  they would with a friend in the game. In exchange it keeps the game going while you are down: you
  get "Game over" only when both of you are.
- The game prints "Companion has joined the game" each time you enter a new map (it rejoins there).
- Driving a person's client (`cof_companion_drive`), it uses that person's real ammo and syringes and
  never turns their flashlight on or off. With the test gun on (`cof_test_gun`, the campaign test
  default), that person's ammo is infinite and their best gun is the test gun.
