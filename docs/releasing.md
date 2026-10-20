# Releasing a version

One version goes out in four steps. Only the last two are public.

| Step | Who | Public? |
| --- | --- | --- |
| 1. Prepare the version and commit it | an agent or the owner | no |
| 2. `python -m tools.release build` | the script (about 20–25 minutes, no game launched) | no |
| 3. Add the file on ModDB | in the owner's browser, the owner confirms before saving | yes |
| 4. `python -m tools.release publish` | the script | yes |

After step 4, nothing else is needed: the app shows **Update now** to everyone on an older version
(any version after 1.1.1), and the Discord bot posts the version in #downloads and #announcements within
5 minutes (`cof-coop-bot`, `cogs/builds.py`, the releases-page watcher).

Run the script from the repository root with the Python the packages are built with (miniconda
3.13, which has Tk and PyInstaller): `%USERPROFILE%\miniconda3\python.exe -m tools.release build`.
It runs itself and everything it starts at BelowNormal priority, and puts conda's `Library\bin` on
PATH for PyInstaller: without it the OpenSSL DLLs stay out of the exe and the packaged app can't
make HTTPS requests (seen 2026-10-20 in `evidence/app-update-20261020/frozen/`).

## Where things live

- **The public releases page**: <https://github.com/charliee1w/cof-story-coop-releases/releases>.
  It holds only the release zips and a `release.json` per version; the code repositories stay
  private. Each zip already carries the engine's source (GPL) in `source-and-licences`.
- **`release.json`** (`tools/app_update.release_info`): label, join version, build, zip name, size,
  SHA-256. The app reads the latest one from
  `https://github.com/charliee1w/cof-story-coop-releases/releases/latest/download/release.json`
  (no GitHub API, no rate limit) and offers the update when its build is higher than its own.
- **ModDB**: <https://www.moddb.com/mods/cry-of-fear-story-co-op>. No upload API, so step 3 is
  done in the browser.

## 1. Prepare

In one commit ("Release X: …"), with everything for the version already committed in all three
repositories (launcher, engine, FreeVGUI) and the engine built (`tools\build_enhanced.cmd`):

- `tools/coop_play.py`: `VERSION_LABEL` = X; `BUILD` + 1 (a higher build is what makes older
  copies offer the update); `VERSION` changes only when old and new can't play together (it is
  the join check: 1.1.1 kept "1.1").
- `tools/player_readme.txt`: first line `Cry of Fear Co-op X`; a `WHAT'S NEW IN X` section of
  bullets at the top; a `Coming from …?` line after the what's-new sections. The ModDB text is
  made from these.
- `tests/test_coop_app.py`: the footer test names X.
- `docs/releases/X.md`: the notes players read on Discord and GitHub. Casual and lowercase like
  the owner's Discord posts (`docs/releases/1.1.1.md` is the model): a **X** section with what
  changed, **updating**, **known issue(s)**.

## 2. Build and check

```
python -m tools.release build
```

It stops at the first group of problems and says which. In order:

1. **Preflight**: the readme names X and has its what's-new, the notes file exists and is
   committed, and X is newer (label and build) than the latest version on the releases page.
2. **The full test suite** (`full-suite.log`).
3. **The package** (`tools/build_tester_package.py`) into `evidence/release-X/package/`.
   Untracked files outside `tools/`, `cofcoop/` and `tests/` (research notes) are kept out of the
   builder's dirty check for the build and `.git/info/exclude` is put back afterwards.
4. **The release checks**: the zip's files and CRC, the readme is `tools/player_readme.txt` with no
   test or dev wording, the payload (no retired files, the engine matches its record), the
   extracted exe's `--self-test`, the extracted exe reads the releases page
   (`--update-check`: network and TLS from the packaged exe), a fresh install from
   `evidence/tester-package-20261015/smoke-source/Cry of Fear` (or `--source <Cry of Fear folder>`), and the
   players' path: the version now on the releases page is downloaded, installed, and updated by X
   (a save must survive). The test copies are deleted.
5. **The files for steps 3 and 4**: `release.json`, `moddb-text.txt`, `build-state.json`.

Everything is in `evidence/release-X/` (`release.log` has every line). The last line says
`RESULT: all checks passed` or lists what failed. A built version can't be built again into the
same folder: bump the version, or move `evidence/release-X/package` aside.

## 3. ModDB

On <https://www.moddb.com/mods/cry-of-fear-story-co-op>, add a file (category Full Version) and
fill it from `evidence/release-X/moddb-text.txt` (name, summary, description). The zip is
`evidence/release-X/package/CoF-Story-Coop-X.zip` (about 106 MB): browser tools can't attach a
file that big, so it goes in through the Windows file dialog that ModDB's uploader opens (type the
full path there). Upload, wait until the upload finishes, and let the owner confirm before saving.

## 4. Publish

```
python -m tools.release publish            # or --dry-run to check everything and print the command
```

It refuses unless step 2 passed (with the tests) for exactly the commits checked out now and the
zip is unchanged. Then `gh release create vX` on the releases page with the zip, `release.json`
and the notes, marked latest, and it waits until the latest-release link serves X. `gh` must be
logged in as the owner (`gh auth status`).

Then: the AGENT_NOTES handoff (what went out, hashes, evidence folder).

## When something goes wrong

- **A broken version is out**: release a fixed one with a higher build. The app never offers a
  lower build, so pulling a release only stops new downloads of it. To pull one anyway:
  `gh release delete vX --repo charliee1w/cof-story-coop-releases` (the previous one becomes the
  latest again) and hide the ModDB file.
- **The bot didn't post it**: its log (`cof-coop-bot\logs\bot.log`) says why. `/build post` in
  Discord posts by hand; the watcher then skips that version.
- **Players can't update in the app**: they can always download the zip (ModDB or the releases
  page), run its exe and click **Update**.
