# Building and testing

This page takes you from an empty Windows PC to a built engine and a passing test run.
You don't need the game for any of it, only for live tests (see the end).

## What you need

- **Access to four private repositories.** Accept the GitHub invitations to
  `charliee1w/cof-story-coop`, `charliee1w/cof-story-coop-engine`,
  `charliee1w/cof-story-coop-freevgui` and `charliee1w/cof-story-coop-mainui`.
- **Git for Windows.** Its Git Credential Manager asks you to sign in to GitHub the first time
  you clone a private repository. The GitHub CLI works too: `gh auth login`, then `gh auth setup-git`.
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

The engine must sit at one exact path inside the launcher checkout.
`tools/build_enhanced.cmd`, `tools/package_enhanced_story.py`, `tools/Prepare-CampaignTest.ps1`,
`tests/native.py` and several tests have this path hard-coded:

```
cof-story-coop/                          this repository, branch main
  upstream/                              gitignored; the clones below are independent
    cof-enhanced/                        public hajdawery/cof-enhanced at e4cdbfa (0.4.0-test3)
      prereq/sdl2-2.30.9-vc/SDL2-2.30.9/ SDL2 development package (you download it)
      xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/
                                         cof-story-coop-engine, branch story-coop
        3rdparty/mainui                  submodule: cof-story-coop-mainui
        3rdparty/freevgui                submodule: cof-story-coop-freevgui
        build-local-20260927/            build output
```

Keep the checkout path short (for example `C:\src\cof-story-coop`); the engine sits deep inside
it. In PowerShell:

```powershell
git clone https://github.com/charliee1w/cof-story-coop.git
Set-Location cof-story-coop
git clone https://github.com/hajdawery/cof-enhanced.git upstream/cof-enhanced
git -C upstream/cof-enhanced checkout --detach e4cdbfaa260696eb886cf6c9bd4c371d53377427
$engine = 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a'
git clone --branch story-coop https://github.com/charliee1w/cof-story-coop-engine.git $engine
git -C $engine submodule update --init --recursive 3rdparty/mainui 3rdparty/freevgui 3rdparty/MultiEmulator 3rdparty/bzip2/bzip2 3rdparty/extras/xash-extras 3rdparty/libogg/libogg 3rdparty/library_suffix 3rdparty/opus/opus 3rdparty/opusfile/opusfile 3rdparty/vorbis/vorbis-src
```

These are the submodules this build uses; `mainui` and `freevgui` come from the private
repositories and need your GitHub sign-in. Don't use `--recurse-submodules` or a plain
`submodule update --init --recursive`: they also fetch `mbedtls` (unused, the build runs with
`--disable-mbedtls`), whose paths are too long for Windows, and the failure stops the other
submodules from being fetched. If git still stops with "Filename too long", turn on long paths
once with `git config --global core.longpaths true`.

This exact sequence was checked on 2026-10-13 from a fresh clone: configure, build and the test
suite all passed.

Submodules are checked out at the engine's recorded commits (detached HEAD). To work on one,
switch it to its branch first: `git -C <submodule path> switch story-coop`.

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
cd /d upstream\cof-enhanced\xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a
set "WAFLOCK=.lock-waf-local-20260927"
py -3.14 waf configure -4 --out=build-local-20260927 --sdl2=..\prereq\sdl2-2.30.9-vc\SDL2-2.30.9 -T release --notests --disable-mbedtls --enable-cof-entvars-legacy --enable-stbtt --msvc_version="msvc 17.14" --msvc_targets=x86
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
`upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/build-local-20260927/`:

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
python -m unittest discover -s tests
python -m unittest tests.test_enhanced_party -v   # one module
```

The full suite is 156 tests and takes about 80 seconds. It needs no game, no network (protocol
tests use loopback) and no engine build. It does need:

- **The engine checkout at the path above.** Many tests read `engine/server/sv_cof_*` sources and
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
[docs/enhanced-assessment.md](enhanced-assessment.md). The tester package
(`CoF-Story-Coop-Test<N>.zip`: the app `tools/coop_app.py` as one exe with the engine payload inside,
the readme `tools/tester_readme.txt` and the corresponding source) is built on the host's machine,
after `tools/build_enhanced.cmd` and with all three repositories committed:
`python -m tools.build_tester_package --out evidence\<new folder>`. It needs files from those
local copies, Enhanced's `scripts/build-release.ps1` and PyInstaller.

## Where to read next

- [AGENT_NOTES.md](../AGENT_NOTES.md): the current state of the work, live results and next steps.
- [AGENTS.md](../AGENTS.md): the rules for AI coding agents working in this project.
- The README's "What the engine work does" section maps features to the engine's
  `engine/server/sv_cof_*.c` modules.
