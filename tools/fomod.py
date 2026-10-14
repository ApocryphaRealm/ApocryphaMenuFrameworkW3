"""Turn a staged Apocrypha Menu Framework (Witcher 3) package folder into a FOMOD installer, in place.

    python tools/fomod.py "<package folder>" <version>

Why: the ASI loader (dinput8.dll) has to be a real file in the game's bin\\x64_dx12, and where a mod manager puts a
file depends on the manager. Mod Organizer 2 reaches the real game folder only through Root Builder (a Root folder);
Vortex deploys a mod's files into the game folder itself, so a Root folder lands as <game>\\Root\\... and the loader
is never loaded. One question in the installer puts the loader where the player's manager needs it - and a player
who already runs the Ultimate ASI Loader can leave it out. Vortex runs a FOMOD before its Witcher 3 installers
(priority 10 against 20), and Mod Organizer 2 runs it as usual.

Before: bin\\..., Mods\\..., Root\\bin\\x64_dx12\\dinput8.dll, docs.
After:  bin\\..., Mods\\..., loader\\dinput8.dll, fomod\\info.xml, fomod\\ModuleConfig.xml, docs.
"""
import os
import shutil
import sys
from xml.sax.saxutils import escape

INFO = """<?xml version="1.0" encoding="UTF-8"?>
<fomod>
  <Name>Apocrypha Menu Framework</Name>
  <Author>Apocrypha_Realm</Author>
  <Version>{version}</Version>
  <Description>One in-game settings menu for every mod, for The Witcher 3: Wild Hunt - Remastered.</Description>
</fomod>
"""

LOADER_CHOICES = [
    ("Vortex, or installing by hand",
     "Puts the ASI loader (dinput8.dll) in bin\\x64_dx12, where the game loads it. Choose this with Vortex, or when "
     "copying the files into the game folder yourself.",
     "bin\\x64_dx12\\dinput8.dll", "Recommended"),
    ("Mod Organizer 2 with Root Builder",
     "Puts the ASI loader in the mod's Root folder. Root Builder copies it into the real game folder when the game "
     "starts and removes it when it closes - Mod Organizer 2's virtual folder is not in place when Windows loads "
     "dinput8.dll, so without Root Builder it would never be found.",
     "Root\\bin\\x64_dx12\\dinput8.dll", "Optional"),
    ("I already have an ASI loader",
     "Leaves the loader out. Choose this when the Ultimate ASI Loader (or another dinput8.dll ASI loader) is "
     "already installed: it loads ApocryphaMenuFramework.asi the same way.",
     None, "Optional"),
]


def module_config(version):
    plugins = []
    for name, desc, dest, kind in LOADER_CHOICES:
        files = ('<files><file source="loader\\dinput8.dll" destination="%s" /></files>' % escape(dest)) if dest else ""
        plugins.append(
            """          <plugin name="{name}">
            <description>{desc}</description>
            {files}
            <typeDescriptor><type name="{kind}" /></typeDescriptor>
          </plugin>""".format(name=escape(name), desc=escape(desc), files=files, kind=kind))
    return """<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
  <moduleName>Apocrypha Menu Framework {version}</moduleName>
  <requiredInstallFiles>
    <folder source="bin" destination="bin" />
    <folder source="Mods" destination="Mods" />
  </requiredInstallFiles>
  <installSteps order="Explicit">
    <installStep name="ASI loader">
      <optionalFileGroups order="Explicit">
        <group name="Where should the ASI loader go?" type="SelectExactlyOne">
          <plugins order="Explicit">
{plugins}
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
</config>
""".format(version=escape(version), plugins="\n".join(plugins))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    pkg, version = sys.argv[1], sys.argv[2]
    src = os.path.join(pkg, "Root", "bin", "x64_dx12", "dinput8.dll")
    if not os.path.isfile(src):
        sys.exit("no Root\\bin\\x64_dx12\\dinput8.dll in " + pkg)
    for need in ("bin", "Mods"):
        if not os.path.isdir(os.path.join(pkg, need)):
            sys.exit("no %s folder in %s" % (need, pkg))
    os.makedirs(os.path.join(pkg, "loader"), exist_ok=True)
    shutil.move(src, os.path.join(pkg, "loader", "dinput8.dll"))
    shutil.rmtree(os.path.join(pkg, "Root"))
    os.makedirs(os.path.join(pkg, "fomod"), exist_ok=True)
    # FOMOD readers expect UTF-8 with these declarations; Mod Organizer 2 also reads UTF-16, Vortex reads both
    open(os.path.join(pkg, "fomod", "info.xml"), "w", encoding="utf-8", newline="\r\n").write(INFO.format(version=escape(version)))
    open(os.path.join(pkg, "fomod", "ModuleConfig.xml"), "w", encoding="utf-8", newline="\r\n").write(module_config(version))
    print("fomod written: " + os.path.join(pkg, "fomod"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
