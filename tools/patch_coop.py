"""Inspect or reversibly select known local Cry of Fear DLL experiments.

No game binaries are distributed. Every result is reconstructed from the user's
byte-exact stock backup. The default is inspection only; writing needs --apply.
Plain-coop-transition is for a dedicated experimental co-op copy: it also removes
landmark handling in single player and is not a conditional multiplayer fix.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from cofcoop import core


STOCK_SHA256 = "0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63"
CHECK_OFFSET = 0x109160
LANDMARK_OFFSET = 0x109306
# The original push operand has an IMAGE_REL_BASED_HIGHLOW relocation. Replacing
# it with push 0 / NOPs must also disable that relocation, or ASLR rewrites code.
# This offset is pinned by the full stock hash and was verified in the PE table.
LANDMARK_RELOCATION_OFFSET = 0x22E996
LANDMARK_RELOCATION_RVA = 0x109F07
ORIGINAL_CHECK = bytes.fromhex("0f85b5010000")
ORIGINAL_LANDMARK = bytes.fromhex("6834102310")
ORIGINAL_RELOCATION = bytes.fromhex("073f")  # HIGHLOW (3), page offset 0xF07
DISABLED_RELOCATION = bytes.fromhex("070f")  # ABSOLUTE (0), same page offset
NOP_CHECK = bytes.fromhex("909090909090")
NULL_LANDMARK = bytes.fromhex("6a00909090")
MODES = {
    "stock": (ORIGINAL_CHECK, ORIGINAL_LANDMARK, ORIGINAL_RELOCATION),
    "allow-transition": (NOP_CHECK, ORIGINAL_LANDMARK, ORIGINAL_RELOCATION),
    "plain-coop-transition": (NOP_CHECK, NULL_LANDMARK, DISABLED_RELOCATION),
}
LEGACY_PLAIN_MODE = "legacy-plain-coop-transition"
LEGACY_PLAIN_SHA256 = "61406330be1b838926c4660938f148c8e3429c03d0f331e2bf27a4d804ff03c4"
PLAIN_SHA256 = "942586aba56ab6c3fab2dc9f1b4bf42b2dd87e3af86b67bd29687c507171afba"
PATCH_RANGES = (
    (CHECK_OFFSET, ORIGINAL_CHECK),
    (LANDMARK_OFFSET, ORIGINAL_LANDMARK),
    (LANDMARK_RELOCATION_OFFSET, ORIGINAL_RELOCATION),
)
EVIDENCE_DIR = core.PROJECT_ROOT / "evidence" / "patches"
PLAIN_WARNING = (
    "Experimental co-op copy only: plain-coop-transition removes landmark handling "
    "in single player too. It does not preserve inventory, arrival position, or hub state."
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def identify(data: bytes) -> str:
    """Recognize exact supported builds, including the repairable old ASLR bug."""
    actual = tuple(data[offset:offset + len(original)] for offset, original in PATCH_RANGES)
    known = dict(MODES)
    known[LEGACY_PLAIN_MODE] = (NOP_CHECK, NULL_LANDMARK, ORIGINAL_RELOCATION)
    matching = [mode for mode, expected in known.items() if actual == expected]
    if not matching:
        raise ValueError("DLL has unknown bytes at the patch offsets; no changes were made.")
    normalized = bytearray(data)
    for offset, original in PATCH_RANGES:
        normalized[offset:offset + len(original)] = original
    if sha256(normalized) != STOCK_SHA256:
        raise ValueError("DLL differs from stock outside the known patch bytes; refusing a tampered or unsupported build.")
    return matching[0]


def transform(data: bytes, mode: str) -> bytes:
    """Pure byte-exact conversion between known modes, including full reversal."""
    if mode not in MODES:
        raise ValueError(f"Unknown patch mode: {mode!r}")
    identify(data)
    result = bytearray(data)
    for (offset, _original), replacement in zip(PATCH_RANGES, MODES[mode]):
        result[offset:offset + len(replacement)] = replacement
    return bytes(result)


def _reject_links(path: Path, root: Path) -> None:
    for candidate in (path, *path.parents):
        if candidate.is_symlink() or (hasattr(candidate, "is_junction") and candidate.is_junction()):
            raise ValueError(f"Patch paths cannot contain symlinks or junctions: {candidate}")
        if candidate == root:
            break
    resolved = core._local_path(path)
    if not resolved.is_relative_to(root):
        raise ValueError(f"DLL path escapes the copied game directory: {path}")
    if not path.is_file():
        raise ValueError(f"Required local DLL is missing: {path}")
    if path.stat().st_nlink != 1:
        raise ValueError(f"Patch paths cannot be hard-linked: {path}")


def _paths(game_dir: Path | str) -> tuple[Path, Path, Path]:
    root = core.validate_game_dir(game_dir)
    # validate_game_dir resolves paths; inspect the supplied root separately so
    # an alias/junction cannot hide the ownership of a copy that will be patched.
    supplied = Path(game_dir).expanduser().absolute()
    for candidate in (supplied, *supplied.parents):
        if candidate.is_symlink() or (hasattr(candidate, "is_junction") and candidate.is_junction()):
            raise ValueError(f"The game directory cannot be reached through a symlink or junction: {candidate}")
    dll = root / "cryoffear" / "cl_dlls" / "hl.dll"
    backup = dll.with_name("hl.dll.orig")
    _reject_links(dll, root)
    _reject_links(backup, root)
    return root, dll, backup


def inspect_patch(game_dir: Path | str = core.DEFAULT_GAME_DIR, mode: str | None = None) -> dict:
    """Read and verify the complete current/backup DLLs without modifying files."""
    root, dll, backup = _paths(game_dir)
    stock = backup.read_bytes()
    if sha256(stock) != STOCK_SHA256:
        raise ValueError("hl.dll.orig is not the verified stock 1.6 DLL; no changes were made.")
    current = dll.read_bytes()
    current_mode = identify(current)
    # Inspection proposes repair for the recognized legacy build but never
    # writes; --apply still requires an explicit supported target mode.
    target_mode = mode or ("plain-coop-transition" if current_mode == LEGACY_PLAIN_MODE else current_mode)
    target = transform(stock, target_mode)
    changes = []
    for offset, original in PATCH_RANGES:
        width = len(original)
        before, after = current[offset:offset + width], target[offset:offset + width]
        if before != after:
            changes.append({"offset": f"0x{offset:x}", "before": before.hex(), "after": after.hex()})
    return {
        "game_dir": str(root), "dll": str(dll), "stock_backup": str(backup),
        "current_mode": current_mode, "target_mode": target_mode,
        "current_sha256": sha256(current), "target_sha256": sha256(target),
        "stock_sha256": STOCK_SHA256, "changes": changes,
        "legacy_repair_required": current_mode == LEGACY_PLAIN_MODE,
        "landmark_relocation": {
            "file_offset": f"0x{LANDMARK_RELOCATION_OFFSET:x}",
            "operand_rva": f"0x{LANDMARK_RELOCATION_RVA:x}",
            "target_type": "ABSOLUTE" if target_mode == "plain-coop-transition" else "HIGHLOW",
        },
        "warning": PLAIN_WARNING if target_mode == "plain-coop-transition" else None,
        "applied": False,
    }


def apply_mode(game_dir: Path | str, mode: str) -> dict:
    """Apply only an explicitly selected known mode while the game is closed."""
    if mode not in MODES:
        raise ValueError("An explicit stock/allow-transition/plain-coop-transition mode is required.")
    with core._LAUNCH_LOCK:
        root, dll, backup = _paths(game_dir)
        core._assert_not_running(root)
        report = inspect_patch(root, mode)
        target = transform(backup.read_bytes(), mode)
        if sha256(target) != report["target_sha256"]:
            raise ValueError("Stock backup changed while preparing the patch; no changes were made.")
        temporary: str | None = None
        if report["changes"]:
            fd, temporary = tempfile.mkstemp(prefix=".hl.dll.cofcoop-", suffix=".tmp", dir=dll.parent)
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(target)
                    stream.flush()
                    os.fsync(stream.fileno())
                _paths(root)
                if sha256(dll.read_bytes()) != report["current_sha256"]:
                    raise ValueError("The current DLL changed while preparing the patch; no changes were made.")
                core._assert_not_running(root)
                os.replace(temporary, dll)
                temporary = None
            finally:
                if temporary is not None and os.path.exists(temporary):
                    os.unlink(temporary)
        actual = sha256(dll.read_bytes())
        if actual != report["target_sha256"]:
            raise RuntimeError("Post-write DLL hash differs from the requested patch; inspect the copy before launching.")
        report.update(applied=True, changed=bool(report["changes"]), verified_sha256=actual,
                      applied_utc=datetime.now(timezone.utc).isoformat())
        evidence_dir = core._local_path(EVIDENCE_DIR)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        evidence = evidence_dir / f"{stamp}-{mode}.json"
        report["evidence_file"] = str(evidence)
        try:
            core._atomic_write(evidence, (json.dumps(report, indent=2) + "\n").encode("utf-8"))
        except OSError as exc:
            raise RuntimeError(f"DLL is verified in {mode} mode, but writing the evidence record failed: {exc}") from exc
        return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", type=Path, default=core.DEFAULT_GAME_DIR)
    parser.add_argument("--mode", choices=tuple(MODES), help="Desired state; omit to inspect the current known state")
    parser.add_argument("--apply", action="store_true", help="Explicitly apply --mode; otherwise perform a read-only dry run")
    args = parser.parse_args(argv)
    if args.apply and not args.mode:
        parser.error("--apply requires an explicit --mode")
    try:
        report = apply_mode(args.game_dir, args.mode) if args.apply else inspect_patch(args.game_dir, args.mode)
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"Patch refused: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
