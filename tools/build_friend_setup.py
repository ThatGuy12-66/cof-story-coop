"""Build the standalone setup/join UI with the already installed PyInstaller."""
from importlib.metadata import distribution
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/friend-setup-build"
INPUTS = ("tools/friend_setup.py", "tools/prepare_simon_models.py", "cofcoop/core.py", "cofcoop/__init__.py")


def main():
    version = distribution("pyinstaller").version
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onefile", "--windowed", "--name", "CoF-Story-Setup",
               "--distpath", str(OUT / "dist"), "--workpath", str(OUT / "work"), "--specpath", str(OUT),
               str(ROOT / "tools/friend_setup.py")]
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "build.log").open("wb") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    exe = OUT / "dist/CoF-Story-Setup.exe"
    subprocess.run([str(exe), "--self-test"], check=True, timeout=30)
    notices = OUT / "notices"
    notices.mkdir(exist_ok=True)
    licenses = {"PYTHON-LICENSE.txt": Path(sys.base_prefix) / "LICENSE_PYTHON.txt",
                "TK-LICENSE.txt": Path(sys.base_prefix) / "Library/lib/tk8.6/license.terms"}
    package = distribution("pyinstaller")
    licenses["PYINSTALLER-COPYING.txt"] = next(package.locate_file(f) for f in package.files if str(f).endswith("licenses/COPYING.txt"))
    for name, path in licenses.items():
        shutil.copyfile(path, notices / name)
    report = {"python": sys.version, "pyinstaller": version, "self_test_passed": True,
              "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "source_sha256": {rel: hashlib.sha256((ROOT / rel).read_bytes()).hexdigest() for rel in INPUTS}}
    (OUT / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
