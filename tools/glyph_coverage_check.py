"""Builds and runs the glyph_coverage_check xmake target (the off-game glyph coverage check, 1.0.6) and passes its exit
code on.

package-gate.ps1's repo-script-passes row amf-every-translation-character-has-a-glyph runs a Python script; this is the
bridge to the compiled check, which builds real ImGui 1.90.8 atlases (the vendored extern/imgui) with include/GlyphCore.h
for all eleven languages and fails on any character without a glyph. The build goes through the machine's shared build
environment when MODS_BUILD_ENV names one (read from the process, else the user's saved environment); unset, plain xmake.
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


rc = run(["xmake", "build", "-y", "glyph_coverage_check"])
if rc == 0:
    rc = run(["xmake", "run", "glyph_coverage_check"])
sys.exit(rc)
