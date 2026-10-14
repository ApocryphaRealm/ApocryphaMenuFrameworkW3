"""Builds and runs the open_request_check xmake target (the off-game check of include/OpenRequestCore.h, 1.0.6) and passes
its exit code on - the game-menu entry's request: a "true" saved from an earlier session never opens the window and is
cleared only once the game is settled (the 2026-10-14 00:23 start-up crash). Same bridge shape as glyph_coverage_check.py:
the build goes through the machine's shared build environment when MODS_BUILD_ENV names one; unset, plain xmake.
"""
import os
import subprocess
import sys

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build_env():
    env = os.environ.get("MODS_BUILD_ENV")
    if not env and sys.platform == "win32":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as k:
                env = winreg.QueryValueEx(k, "MODS_BUILD_ENV")[0]
        except OSError:
            env = None
    return env if env and os.path.isfile(env) else None


def run(args):
    env = build_env()
    cmd = (['"' + env + '"'] if env else []) + args
    proc = subprocess.run("cmd /c \"" + " ".join(cmd) + "\"", cwd=repo, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", shell=False)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


rc = run(["xmake", "build", "-y", "open_request_check"])
if rc == 0:
    rc = run(["xmake", "run", "open_request_check"])
sys.exit(rc)
