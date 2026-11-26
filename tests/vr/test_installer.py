"""End-to-end CLI and clean-upgrade verification tests for OptiLord Installer & Uninstaller."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def run_cmd(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
        check=False,
    )


def parse_status_line(stdout: str) -> dict[str, str]:
    tokens: dict[str, str] = {}
    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("valid="):
            for part in line.split():
                if "=" in part:
                    key, value = part.split("=", 1)
                    tokens[key] = value
            return tokens
    raise AssertionError(f"Could not find status line in stdout:\n{stdout}")


def create_dummy_setup_from_stub(stub_exe: Path, work_dir: Path) -> Path:
    """Create a self-extracting test Setup EXE with a minimal synthetic v0.5.0 payload."""
    pkg_dir = work_dir / "dummy_pkg"
    for sub in ("h2-mod/scripts", "steamvr", "vr_input"):
        (pkg_dir / sub).mkdir(parents=True, exist_ok=True)

    # Create > 1 MB dummy overlord.exe and minimal package layout
    (pkg_dir / "overlord.exe").write_bytes(b"MZ" + os.urandom(128) + b"\x00" * (1_100_000))
    (pkg_dir / "openxr_loader.dll").write_bytes(b"MZ" + b"\x00" * 4096)
    (pkg_dir / "overlord.vrmanifest").write_text(
        json.dumps({"applications": [{"binary_path_windows": "overlord.exe"}]}),
        encoding="utf-8",
    )
    (pkg_dir / "version.json").write_text(
        json.dumps({"name": "0.5.0", "file_version": [0, 5, 0, 0], "prerelease": True}) + "\n",
        encoding="utf-8",
    )
    (pkg_dir / "h2-mod/scripts/init.lua").write_text("-- optilord v0.5.0 test\n", encoding="utf-8")
    (pkg_dir / "steamvr/cover.png").write_bytes(b"\x89PNG\r\n\x1a\nTEST")
    (pkg_dir / "vr_input/actions.json").write_text("{}", encoding="utf-8")

    zip_path = work_dir / "dummy_payload.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for file_path in sorted(pkg_dir.rglob("*")):
            if file_path.is_file():
                zf.write(file_path, file_path.relative_to(pkg_dir).as_posix())

    setup_exe = work_dir / "OptiLord-Test-Setup.exe"
    with setup_exe.open("wb") as dst:
        with stub_exe.open("rb") as src_stub:
            shutil.copyfileobj(src_stub, dst, length=1024 * 1024)
        with zip_path.open("rb") as src_zip:
            shutil.copyfileobj(src_zip, dst, length=1024 * 1024)
    return setup_exe


def run_tests(setup_exe: Path) -> None:
    if not setup_exe.is_file():
        raise FileNotFoundError(f"Setup executable not found: {setup_exe}")
    if not zipfile.is_zipfile(setup_exe):
        raise AssertionError(f"Setup executable does not contain an embedded ZIP payload: {setup_exe}")

    with tempfile.TemporaryDirectory(prefix="optilord_installer_test_") as tmp_str:
        tmp_root = Path(tmp_str)
        invalid_dir = tmp_root / "empty_invalid"
        invalid_dir.mkdir(parents=True, exist_ok=True)

        fake_game = tmp_root / "MW2CR_Sandbox"
        (fake_game / "players2").mkdir(parents=True, exist_ok=True)
        (fake_game / "Data").mkdir(parents=True, exist_ok=True)
        (fake_game / "h2-mod").mkdir(parents=True, exist_ok=True)
        (fake_game / "steamvr").mkdir(parents=True, exist_ok=True)
        (fake_game / "diagnose").mkdir(parents=True, exist_ok=True)

        protected_bytes = {
            fake_game / "MW2CR.exe": b"MW2CR-TEST-BINARY-" + os.urandom(512),
            fake_game / "players2/save_01.bin": b"SAVE-DATA-" + os.urandom(256),
            fake_game / "Data/cas_01.dat": b"CAS-DATA-" + os.urandom(256),
            fake_game / "diagnose/keep_me.log": b"DIAG-KEEP-LOG-2026\n",
        }
        for path, payload in protected_bytes.items():
            path.write_bytes(payload)

        # Seed stale/obsolete Overlord artifacts
        (fake_game / "overlord.exe").write_bytes(b"OLD" * 170 + b"XX")  # 512 bytes
        assert (fake_game / "overlord.exe").stat().st_size == 512
        (fake_game / "overlord.pdb").write_bytes(b"FAKE_STALE_PDB_DATA")
        (fake_game / "h2-mod/obsolete_test_file.lua").write_text("-- obsolete\n", encoding="utf-8")
        (fake_game / "steamvr/old.txt").write_text("stale steamvr\n", encoding="utf-8")

        # 1. --detect
        res = run_cmd([str(setup_exe), "--detect"])
        assert res.returncode == 0, f"--detect failed ({res.returncode}): {res.stderr}\n{res.stdout}"
        print("[PASS] --detect exited 0")

        # 2. --install on invalid empty folder must fail
        res = run_cmd([str(setup_exe), "--install", str(invalid_dir), "--no-registry", "--no-shortcut"])
        assert res.returncode != 0, f"--install on invalid folder unexpectedly succeeded:\n{res.stdout}"
        print("[PASS] --install rejected invalid folder without MW2CR.exe")

        # 3. --status on stale/legacy sandbox
        res = run_cmd([str(setup_exe), "--status", str(fake_game)])
        assert res.returncode == 0, f"--status failed ({res.returncode}): {res.stderr}\n{res.stdout}"
        status = parse_status_line(res.stdout)
        assert status.get("valid") == "1", f"Expected valid=1, got {status}"
        assert status.get("legacy_or_stale") == "1", f"Expected legacy_or_stale=1, got {status}"
        print(f"[PASS] --status detected legacy/stale installation: {status}")

        # 4. --install clean upgrade on sandbox
        res = run_cmd([str(setup_exe), "--install", str(fake_game), "--no-registry", "--no-shortcut"])
        assert res.returncode == 0, f"--install failed ({res.returncode}): {res.stderr}\n{res.stdout}"

        for removed in (
            fake_game / "overlord.pdb",
            fake_game / "h2-mod/obsolete_test_file.lua",
            fake_game / "steamvr/old.txt",
        ):
            assert not removed.exists(), f"Stale file was not removed during clean upgrade: {removed}"

        new_exe = fake_game / "overlord.exe"
        assert new_exe.is_file() and new_exe.stat().st_size > 1_000_000, (
            f"Expected overlord.exe > 1 MB, got {new_exe.stat().st_size if new_exe.exists() else 'missing'}"
        )

        ver_file = fake_game / "version.json"
        assert ver_file.is_file(), "version.json missing after install"
        ver_data = json.loads(ver_file.read_text(encoding="utf-8-sig"))
        assert ver_data.get("name") == "0.5.0", f"Expected version 0.5.0, got {ver_data}"

        manifest_file = fake_game / "optilord-install-manifest.json"
        assert manifest_file.is_file(), "optilord-install-manifest.json missing after install"
        manifest_data = json.loads(manifest_file.read_text(encoding="utf-8-sig"))
        assert isinstance(manifest_data.get("files"), list) and len(manifest_data["files"]) > 0, (
            f"Invalid install manifest: {manifest_data}"
        )

        uninstall_exe = fake_game / "OptiLord-Uninstall.exe"
        assert uninstall_exe.is_file(), "OptiLord-Uninstall.exe missing after install"
        uninstall_size = uninstall_exe.stat().st_size
        assert 0 < uninstall_size < 2 * 1024 * 1024, (
            f"OptiLord-Uninstall.exe unexpectedly large ({uninstall_size} bytes); ZIP tail was not trimmed"
        )
        assert not zipfile.is_zipfile(uninstall_exe), (
            "OptiLord-Uninstall.exe still appears to be a ZIP payload!"
        )

        for path, expected_bytes in protected_bytes.items():
            assert path.is_file() and path.read_bytes() == expected_bytes, (
                f"Protected file altered or missing after install: {path}"
            )
        print(
            f"[PASS] --install clean upgrade succeeded (uninstall stub={uninstall_size:,} bytes, "
            f"manifest files={len(manifest_data['files'])}, game/save files byte-identical)"
        )

        # 5. --status after clean install
        res = run_cmd([str(setup_exe), "--status", str(fake_game)])
        assert res.returncode == 0, f"--status post-install failed ({res.returncode}): {res.stderr}\n{res.stdout}"
        status = parse_status_line(res.stdout)
        assert status.get("valid") == "1", f"Expected valid=1, got {status}"
        assert status.get("installed") == "1", f"Expected installed=1, got {status}"
        assert status.get("legacy_or_stale") == "0", f"Expected legacy_or_stale=0, got {status}"
        assert status.get("version") == "0.5.0", f"Expected version=0.5.0, got {status}"
        assert status.get("manifest") == "1", f"Expected manifest=1, got {status}"
        assert status.get("pdb") == "0", f"Expected pdb=0, got {status}"
        print(f"[PASS] --status post-install verified: {status}")

        # 6. --uninstall using installed OptiLord-Uninstall.exe
        res = run_cmd([str(uninstall_exe), "--uninstall", str(fake_game), "--no-registry"])
        assert res.returncode == 0, f"--uninstall failed ({res.returncode}): {res.stderr}\n{res.stdout}"

        for must_be_gone in (
            fake_game / "overlord.exe",
            fake_game / "openxr_loader.dll",
            fake_game / "h2-mod",
            fake_game / "steamvr",
            fake_game / "vr_input",
            fake_game / "version.json",
            fake_game / "optilord-install-manifest.json",
        ):
            assert not must_be_gone.exists(), f"Mod artifact still present after uninstall: {must_be_gone}"

        for path, expected_bytes in protected_bytes.items():
            assert path.is_file() and path.read_bytes() == expected_bytes, (
                f"Protected file altered or missing after uninstall: {path}"
            )

        # Wait briefly for detached self-delete cmd to remove OptiLord-Uninstall.exe
        deadline = time.monotonic() + 6.0
        while uninstall_exe.exists() and time.monotonic() < deadline:
            time.sleep(0.2)
        assert not uninstall_exe.exists(), "OptiLord-Uninstall.exe did not self-delete cleanly"

        # 7. --status post-uninstall
        res = run_cmd([str(setup_exe), "--status", str(fake_game)])
        assert res.returncode == 0, f"--status post-uninstall failed ({res.returncode}): {res.stderr}\n{res.stdout}"
        status = parse_status_line(res.stdout)
        assert status.get("valid") == "1" and status.get("installed") == "0", (
            f"Expected clean uninstalled state, got {status}"
        )
        print("[PASS] --uninstall removed all mod artifacts, self-deleted stub, and preserved game/save/diagnostic files")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--setup",
        type=Path,
        default=None,
        help="Path to self-extracting OptiLord Setup EXE (if omitted, bundles a synthetic test setup from Release stub)",
    )
    args = parser.parse_args()

    try:
        if args.setup is not None:
            run_tests(args.setup.resolve())
        else:
            stub = ROOT / "build/bin/x64/Release/OptiLord-Setup-Stub.exe"
            if not stub.is_file():
                raise FileNotFoundError(
                    f"No --setup passed and stub executable not found: {stub}"
                )
            with tempfile.TemporaryDirectory(prefix="optilord_dummy_setup_") as tmp_dir:
                setup_exe = create_dummy_setup_from_stub(stub, Path(tmp_dir))
                run_tests(setup_exe)
        print("test_installer: ALL CHECKS PASSED")
        return 0
    except (AssertionError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"test_installer: FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
