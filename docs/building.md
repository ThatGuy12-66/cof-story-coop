# Building and testing

This page takes you from an empty Windows PC to a built engine and a passing test run.
You don't need the game for any of it, only for live tests (see the end).

## What you need

- **Git for Windows.**
- **Visual Studio 2022 Build Tools**, installed in the default folder, with the
  "Desktop development with C++" workload (MSVC v143 x86/x64 tools and a Windows SDK; the build
  machine uses SDK 10.0.26100). `tools/build_enhanced.cmd` and `tests/native.py` call
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`
  directly. Visual Studio Community or Professional installs to a different folder, so these
  scripts can't find it.
- **Python 3.14, reachable as `py -3.14`.** The build runs waf with it. Install it from
  python.org and check with `py -3.14 --version`.
- **Python with Tk for the tests**, run as `python`. The suite is normally run with 3.13. The
  python.org installer includes Tk.

```powershell
winget install --id Git.Git
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

## Get the sources

Everything is in this one repository: the launcher, tools and tests at the root, the engine in
`engine/`, and the patched UI in `engine/3rdparty/mainui` and `engine/3rdparty/freevgui`. The only
other checkout is the public Cry of Fear: Enhanced repository, for the SDL2 prerequisite and its
release scripts:

```
cof-story-coop/                          this repository, branch main
  engine/                                the engine (Xash3D FWGS, Enhanced patches, story co-op)
    3rdparty/mainui                      the patched menu
    3rdparty/freevgui                    the patched VGUI
    build-local-20260927/                build output (gitignored)
  upstream/                              gitignored
    cof-enhanced/                        public hajdawery/cof-enhanced at e4cdbfa (0.4.0-test3)
      prereq/sdl2-2.30.9-vc/SDL2-2.30.9/ SDL2 development package (you download it)
```

Keep the checkout path short (for example `C:\src\cof-story-coop`). In PowerShell:

```powershell
git clone https://github.com/charliee1w/cof-story-coop.git
Set-Location cof-story-coop
git submodule update --init --recursive engine/3rdparty/MultiEmulator engine/3rdparty/bzip2/bzip2 engine/3rdparty/extras/xash-extras engine/3rdparty/libogg/libogg engine/3rdparty/library_suffix engine/3rdparty/opus/opus engine/3rdparty/opusfile/opusfile engine/3rdparty/vorbis/vorbis-src engine/3rdparty/mainui/miniutl
git clone https://github.com/hajdawery/cof-enhanced.git upstream/cof-enhanced
git -C upstream/cof-enhanced checkout --detach e4cdbfaa260696eb886cf6c9bd4c371d53377427
```

These are the third-party submodules this build uses. Don't use `--recurse-submodules` or a plain
`submodule update --init --recursive`: they also fetch `mbedtls` (unused, the build runs with
`--disable-mbedtls`), whose paths are too long for Windows, and the failure stops the other
submodules from being fetched. If git still stops with "Filename too long", turn on long paths
once with `git config --global core.longpaths true`.

`cof-enhanced` itself is only needed for `prereq/` (the build) and for its release scripts
(packaging). The engine's generated headers (`build.h`, `common/build.h`,
`engine/cof_version.h`) are committed, and the Enhanced patch stack is already applied. Don't run
Enhanced's `apply-*.ps1` scripts.

### SDL2

Configure expects the SDL2 2.30.9 Visual C++ development package at
`upstream/cof-enhanced/prereq/sdl2-2.30.9-vc/SDL2-2.30.9`:

```powershell
$prereq = 'upstream\cof-enhanced\prereq'
New-Item -ItemType Directory -Force $prereq | Out-Null
Invoke-WebRequest https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip -OutFile "$prereq\SDL2-devel-2.30.9-VC.zip"
(Get-FileHash "$prereq\SDL2-devel-2.30.9-VC.zip").Hash
# must be 8C91D91E5BCB997D062EC2B553C53832EBF95654D4AA35E8C02A954D4CE752AE
Expand-Archive "$prereq\SDL2-devel-2.30.9-VC.zip" -DestinationPath "$prereq\sdl2-2.30.9-vc"
Test-Path "$prereq\sdl2-2.30.9-vc\SDL2-2.30.9\include\SDL.h"   # True
```

## Configure once

`tools/build_enhanced.cmd` sets `WAFLOCK=.lock-waf-local-20260927` and stops with "Existing local
Waf configuration is missing" if that file isn't in the engine folder. Waf names its lock file
after `WAFLOCK` when you configure (without it, the file is `.lock-waf_win32_build`). So configure
with the same `WAFLOCK` and the same output folder. `tools/Prepare-CampaignTest.ps1` and
`tools/package_enhanced_story.py` read the DLLs from `build-local-20260927`.

Open a Command Prompt (`cmd`, not PowerShell; `vcvarsall.bat` must set up the same session) in
the checkout and run:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64_x86
cd /d engine
set "WAFLOCK=.lock-waf-local-20260927"
py -3.14 waf configure -4 --out=build-local-20260927 --sdl2=..\upstream\cof-enhanced\prereq\sdl2-2.30.9-vc\SDL2-2.30.9 -T release --notests --disable-mbedtls --enable-cof-entvars-legacy --enable-stbtt --msvc_version="msvc 17.14" --msvc_targets=x86
dir .lock-waf-local-20260927
```

`--msvc_version` selects a Visual Studio by the first two numbers of its installation version.
If configure can't find "msvc 17.14", look up yours and use that instead:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -property installationVersion
```

Run configure again only if you change configure options. Keep the same `WAFLOCK` every time.

## Build

From the checkout root, in PowerShell:

```powershell
cmd /d /c tools\build_enhanced.cmd
```

It builds the targets `xash`, `vgui`, `ref_gl` and `menu` (incrementally after the first time)
and ends with `'build' finished successfully`. The outputs, under
`engine/build-local-20260927/`:

| File | Path in the build folder |
| --- | --- |
| `xash.dll` (+ `xash.pdb`), the engine with the story co-op work | `engine\xash.dll` |
| `ref_gl.dll` | `ref\gl\ref_gl.dll` |
| `vgui.dll` | `3rdparty\freevgui\vgui.dll` |
| `menu.dll` (installed as `cryoffear\cl_dlls\menu.dll`) | `3rdparty\mainui\menu.dll` |

`xash.dll` and `vgui.dll` go together. Never copy a build into a Steam game folder.

## Run the tests

From the checkout root:

```powershell
python -m tools.run_tests                         # everything, in parallel (about 2 minutes)
python -m unittest discover -s tests              # everything, one module after another (about 12 minutes)
python -m unittest tests.test_enhanced_party -v   # one module
```

`tools/run_tests.py` runs each module in its own process, several at once, slowest first (times kept in
`evidence/test-times.json`), and splits `test_map_merge_groups`' every-group test into 8 shards
(`COF_TEST_SHARD`). Same tests, same verdict; the last lines read like unittest's. The full suite was 545
tests on 2026-10-20. It needs no game, no network (protocol
tests use loopback) and no engine build. It does need:

- **The engine sources in `engine/`.** Many tests read `engine/server/sv_cof_*` sources and
  compile pieces of them.
- **The Build Tools `vcvarsall.bat` at the default path.** `tests/native.py` writes each harness
  from `tests/engine/*.c` to a temporary folder, compiles it as x86 with
  `cl /W4 /WX` (warnings are errors) and runs it. Without that file (and no `cc` on PATH) the
  native tests are skipped, so a run without Build Tools proves much less.

Some tests are skipped unless local game files are present (`baseline/Cry of Fear`,
`runtimes/enhanced`): the retail map fixtures, the stock-DLL patch tests and the Simon model test.
These files are never in any repository.

## Reverse engineering

`tools/hlre.py` is a read-only disassembly helper for the stock `cryoffear\cl_dlls\hl.dll`. It
needs `capstone` and `pefile`; install them in a throwaway venv, not in your main Python. Point it
at your own copy of the DLL with `COF_HLDLL`:

```powershell
python -m venv "$env:TEMP\revenv"
& "$env:TEMP\revenv\Scripts\pip" install capstone pefile
$env:COF_HLDLL = '<your Steam game folder>\cryoffear\cl_dlls\hl.dll'
& "$env:TEMP\revenv\Scripts\python" tools\hlre.py exports Player
```

The docstring at the top of `tools/hlre.py` lists its commands (`factory`, `vtable`, `use`,
`slot`, `dis`, `xref`, `str`, `exports`).

The research it was used for is in `docs/research/`.

## Live tests on a dev machine

Live tests run the engine on local copies of your own Steam Cry of Fear. None of these are in a
repository:

- `baseline/Cry of Fear`: a copy of the Steam game (the retail launcher and some tests use it).
- `runtimes/enhanced`: the Enhanced runtime the host plays with.
- `evidence/story-live-20261012/host` and `guest`: the disposable pair that
  `tools/Prepare-CampaignTest.ps1` (installs a build's `xash.dll` and `vgui.dll` together, checks both SHA-256s) and
  `tools/Start-CampaignTest.ps1` use. The script that made them lives in that ignored folder.

Ask the host before you set these up. The details are in the [README](../README.md),
[docs/campaign-test-guide.md](campaign-test-guide.md) and the appendix of
[docs/enhanced-assessment.md](enhanced-assessment.md). The package
(`CoF-Story-Coop-<version>.zip`, e.g. `CoF-Story-Coop-1.0.zip`: the app `tools/coop_app.py` as one exe with the
engine payload inside, the readme `tools/player_readme.txt` and the corresponding source) is built on the host's machine,
after `tools/build_enhanced.cmd` and with everything committed:
`python -m tools.build_tester_package --out evidence\<new folder>`. It needs files from those
local copies, Enhanced's `scripts/build-release.ps1` and PyInstaller.

## Where to read next

- [AGENT_NOTES.md](../AGENT_NOTES.md): the current state of the work, live results and next steps.
- [AGENTS.md](../AGENTS.md): the rules for AI coding agents working in this project.
- The README's "What the engine work does" section maps features to the engine's
  `engine/server/sv_cof_*.c` modules.
