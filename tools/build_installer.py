"""Build the OptiLord self-extracting Windows installer/uninstaller and optionally bundle a package."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
INSTALLER_DIR = ROOT / "tools/installer"

DEFAULT_VCVARS64 = Path(
    r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
VSWHERE_PATH = Path(
    r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
)

SKIP_EXTENSIONS = {".pdb"}
SKIP_FILENAMES = {"release-info.json", "thumbs.db", ".ds_store"}


def find_vcvars64() -> Path:
    """Locate vcvars64.bat using the known BuildTools path first, then vswhere."""
    if DEFAULT_VCVARS64.is_file():
        return DEFAULT_VCVARS64

    if VSWHERE_PATH.is_file():
        result = subprocess.run(
            [
                str(VSWHERE_PATH),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        for line in result.stdout.splitlines():
            candidate = Path(line.strip()) / "VC/Auxiliary/Build/vcvars64.bat"
            if candidate.is_file():
                return candidate

    for root in (
        Path(r"C:\Program Files\Microsoft Visual Studio"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio"),
    ):
        if root.is_dir():
            for edition in ("18", "2022", "2019"):
                for flavor in ("BuildTools", "Community", "Professional", "Enterprise"):
                    candidate = root / edition / flavor / "VC/Auxiliary/Build/vcvars64.bat"
                    if candidate.is_file():
                        return candidate

    raise FileNotFoundError(
        f"Could not locate vcvars64.bat (checked {DEFAULT_VCVARS64} and vswhere.exe)"
    )


def load_msvc_env(vcvars64: Path) -> dict[str, str]:
    """Capture environment variables initialized by vcvars64.bat."""
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".cmd", encoding="utf-8", delete=False
    ) as handle:
        handle.write(
            f'@echo off\r\ncall "{vcvars64}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n'
        )
        temp_cmd = Path(handle.name)

    try:
        completed = subprocess.run(
            ["cmd.exe", "/d", "/c", str(temp_cmd)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=True,
        )
    finally:
        temp_cmd.unlink(missing_ok=True)

    env = dict(os.environ)
    key_map = {k.upper(): k for k in env}
    for line in completed.stdout.splitlines():
        if "=" not in line or line.startswith("="):
            continue
        key, value = line.split("=", 1)
        upper = key.upper()
        if upper in key_map and key_map[upper] != key:
            del env[key_map[upper]]
        key_map[upper] = key
        env[key] = value
    return env


def resolve_tool(name: str, msvc_env: dict[str, str]) -> str:
    """Resolve an MSVC tool executable from the initialized vcvars PATH."""
    msvc_path = msvc_env.get("PATH") or msvc_env.get("Path") or ""
    found = shutil.which(name, path=msvc_path)
    if not found:
        raise FileNotFoundError(f"Could not find {name} in initialized MSVC PATH")
    return found


def compile_stub(configuration: str) -> Path:
    """Compile optilord_installer.rc and optilord_installer.cpp into OptiLord-Setup-Stub.exe."""
    bin_dir = ROOT / "build/bin/x64" / configuration
    obj_dir = ROOT / "build/obj/x64" / configuration / "installer"
    minizip_lib = bin_dir / "minizip.lib"
    zlib_lib = bin_dir / "zlib.lib"

    missing = [str(p) for p in (minizip_lib, zlib_lib) if not p.is_file()]
    if missing:
        raise FileNotFoundError(
            "Missing required static libraries: "
            + ", ".join(missing)
            + f". Build build/minizip.vcxproj and build/zlib.vcxproj for {configuration}|x64 first."
        )

    rc_src = INSTALLER_DIR / "optilord_installer.rc"
    cpp_src = INSTALLER_DIR / "optilord_installer.cpp"
    for src in (rc_src, cpp_src, INSTALLER_DIR / "optilord_installer.manifest"):
        if not src.is_file():
            raise FileNotFoundError(f"Missing installer source file: {src}")

    vcvars64 = find_vcvars64()
    msvc_env = load_msvc_env(vcvars64)

    obj_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)

    res_out = obj_dir / "optilord_installer.res"
    obj_out = obj_dir / "optilord_installer.obj"
    stub_out = bin_dir / "OptiLord-Setup-Stub.exe"

    rc_exe = resolve_tool("rc.exe", msvc_env)
    cl_exe = resolve_tool("cl.exe", msvc_env)

    rc_cmd = [rc_exe, "/nologo", "/DUNICODE", "/D_UNICODE"]
    if configuration == "Debug":
        rc_cmd.append("/D_DEBUG")
    rc_cmd.extend(["/fo", str(res_out), str(rc_src)])

    print("Compiling installer resources...")
    subprocess.run(rc_cmd, cwd=ROOT, env=msvc_env, check=True)

    opt_flags = ["/Od", "/MTd", "/D_DEBUG", "/Z7"] if configuration == "Debug" else ["/O2", "/MT", "/DNDEBUG"]
    cl_cmd = [
        cl_exe,
        "/nologo",
        "/std:c++20",
        *opt_flags,
        "/EHsc",
        "/W4",
        "/WX",
        "/utf-8",
        "/DUNICODE",
        "/D_UNICODE",
        "/DNOMINMAX",
        f"/I{INSTALLER_DIR}",
        f"/I{ROOT / 'deps/zlib'}",
        f"/I{ROOT / 'deps/zlib/contrib/minizip'}",
        f"/I{ROOT / 'deps/json/single_include'}",
        str(cpp_src),
        str(res_out),
        f"/Fe:{stub_out}",
        f"/Fo:{obj_out}",
        "/link",
        str(minizip_lib),
        str(zlib_lib),
        "user32.lib",
        "gdi32.lib",
        "comctl32.lib",
        "shell32.lib",
        "ole32.lib",
        "uuid.lib",
        "advapi32.lib",
        "shlwapi.lib",
        "/SUBSYSTEM:WINDOWS",
        "/ENTRY:wWinMainCRTStartup",
    ]

    print("Compiling and linking OptiLord-Setup-Stub.exe...")
    subprocess.run(cl_cmd, cwd=ROOT, env=msvc_env, check=True)
    if not stub_out.is_file():
        raise RuntimeError(f"Expected stub executable was not created: {stub_out}")
    print(f"Built stub: {stub_out} ({stub_out.stat().st_size:,} bytes)")
    return stub_out


def package_installer(stub_exe: Path, package_dir: Path, output_exe: Path) -> None:
    """Pack package_dir into a ZIP payload and append it to stub_exe."""
    package_dir = package_dir.resolve()
    if not package_dir.is_dir():
        raise FileNotFoundError(f"Package directory does not exist: {package_dir}")

    overlord_exe = package_dir / "overlord.exe"
    version_json = package_dir / "version.json"
    if not overlord_exe.is_file() or overlord_exe.stat().st_size == 0:
        raise ValueError(f"Package directory missing valid overlord.exe: {package_dir}")
    if not version_json.is_file():
        raise ValueError(f"Package directory missing version.json: {package_dir}")
    json.loads(version_json.read_text(encoding="utf-8-sig"))

    output_exe = output_exe.resolve()
    output_exe.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(
        dir=output_exe.parent, prefix="optilord_payload_", suffix=".zip", delete=False
    ) as zip_handle:
        temp_zip = Path(zip_handle.name)
    with tempfile.NamedTemporaryFile(
        dir=output_exe.parent, prefix="optilord_setup_", suffix=".tmp", delete=False
    ) as exe_handle:
        temp_exe = Path(exe_handle.name)

    try:
        archived_names: set[str] = set()
        skipped_count = 0
        with zipfile.ZipFile(temp_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(package_dir.rglob("*")):
                if not path.is_file() or path.is_symlink():
                    continue
                if path.suffix.lower() in SKIP_EXTENSIONS or path.name.lower() in SKIP_FILENAMES:
                    skipped_count += 1
                    continue
                arcname = path.relative_to(package_dir).as_posix()
                archive.write(path, arcname)
                archived_names.add(arcname)

        if "overlord.exe" not in archived_names or "version.json" not in archived_names:
            raise ValueError("Packed ZIP payload must contain overlord.exe and version.json")

        with temp_exe.open("wb") as dst:
            with stub_exe.open("rb") as src_stub:
                shutil.copyfileobj(src_stub, dst, length=1024 * 1024)
            with temp_zip.open("rb") as src_zip:
                shutil.copyfileobj(src_zip, dst, length=1024 * 1024)

        if not zipfile.is_zipfile(temp_exe):
            raise RuntimeError(f"Bundled output failed zipfile.is_zipfile check: {temp_exe}")
        with zipfile.ZipFile(temp_exe, "r") as check_zip:
            names = set(check_zip.namelist())
            if "overlord.exe" not in names or "version.json" not in names:
                raise RuntimeError("Bundled output ZIP central directory verification failed")

        temp_exe.replace(output_exe)
        msg = (
            f"Bundled self-extracting installer: {output_exe} "
            f"({output_exe.stat().st_size:,} bytes, {len(archived_names)} payload files"
        )
        if skipped_count:
            msg += f", {skipped_count} symbol/metadata file(s) excluded"
        msg += ")"
        print(msg)
    finally:
        temp_zip.unlink(missing_ok=True)
        temp_exe.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--configuration",
        choices=["Release", "RelWithDebInfo", "Debug"],
        default="Release",
        help="Build configuration matching minizip.lib and zlib.lib (default: Release)",
    )
    parser.add_argument(
        "--package-dir",
        type=Path,
        help="Optional staged/packaged directory containing overlord.exe and version.json to bundle",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output path for the bundled self-extracting Setup EXE (requires --package-dir)",
    )
    args = parser.parse_args()

    if (args.package_dir is None) != (args.output is None):
        parser.error("--package-dir and --output must be specified together")

    try:
        stub = compile_stub(args.configuration)
        if args.package_dir is not None and args.output is not None:
            package_installer(stub, args.package_dir, args.output)
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
