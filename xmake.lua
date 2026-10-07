-- Apocrypha Menu Framework for The Witcher 3: Wild Hunt - Remastered (patch 5.0, DX12). An .asi plugin loaded by an ASI
-- loader as bin\x64_dx12\dinput8.dll (Ultimate ASI Loader, or the AMFLoader target below). Its own project beside the Skyrim
-- AMF and the Oblivion Remastered AMF; the name stays AMF (the owner, 2026-10-05). Plan: 4. plans\amf-witcher3\PLAN.md.
-- There is no script extender or CommonLib for Witcher 3 5.0: the engine side is our own (Red3), found at run time.
-- rule 45: no build-machine paths in any compiled object. /d1trimfile strips the project folder from __FILE__ and
-- std::source_location; wrapped in a TABLE so xmake passes it as one argument (the path has spaces). /PDBALTPATH keeps only
-- the PDB's file name in the debug directory.
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

set_project("ApocryphaMenuFramework")
set_version("1.0.3")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")
set_arch("x64")
set_runtimes("MT")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("minhook", "spdlog", {configs = {runtimes = "MT"}})
add_requireconfs("spdlog", {configs = {header_only = false, fmt_external = false, std_format = false}})

-- Dear ImGui 1.90.8 docking - the version Skyrim AMF and the Oblivion AMF embed and the tag the vendored cimgui (src/cimgui)
-- was generated for, so the ig* export surface consumers resolve by name carries over unchanged. Built WITHOUT
-- IMGUI_DISABLE_OBSOLETE_FUNCTIONS. IMGUI_IMPL_WIN32_DISABLE_GAMEPAD: the framework reads XInput itself (Input.cpp), and the
-- backend polling it too would hand ImGui every real press twice (logic library: "a REAL controller reaches ImGui twice").
target("imgui")
    set_kind("static")
    set_warnings("none")
    add_files("extern/imgui/imgui.cpp", "extern/imgui/imgui_draw.cpp", "extern/imgui/imgui_tables.cpp", "extern/imgui/imgui_demo.cpp",
              "extern/imgui/imgui_widgets.cpp", "extern/imgui/backends/imgui_impl_dx12.cpp",
              "extern/imgui/backends/imgui_impl_win32.cpp")
    add_includedirs("extern/imgui", "extern/imgui/backends", {public = true})
    add_defines("IMGUI_IMPL_WIN32_DISABLE_GAMEPAD")

target("ApocryphaMenuFramework")
    set_kind("shared")
    set_extension(".asi")
    add_deps("imgui")
    add_packages("minhook", "spdlog")
    add_syslinks("d3d12", "dxgi", "dxguid", "d3dcompiler", "user32", "ole32", "shell32", "shlwapi", "windowscodecs", "advapi32", "delayimp")
    -- dxgi.dll delay-loaded: a static import would bind to a dxgi.dll proxy in bin\x64_dx12 (ReShade, OptiScaler) at load, while the
    -- game's own DXGI calls go to System32's through Streamline; Overlay::Install loads System32's by full path first.
    add_shflags("/DELAYLOAD:dxgi.dll", {force = true})
    on_load(function (target)
        target:add("defines", "AMF_VERSION=\"" .. (target:version() or "0.0.0") .. "\"")
        -- res/version.rc: the numbers come from set_version, which only the version gate writes, so the built file
        -- reports the same version as the package (rule 6)
        local major, minor, patch = (target:version() or "0.0.0"):match("^(%d+)%.(%d+)%.(%d+)")
        target:add("defines", "AMF_VER_MAJOR=" .. (major or "0"), "AMF_VER_MINOR=" .. (minor or "0"), "AMF_VER_PATCH=" .. (patch or "0"))
        target:add("defines", "AMF_FILE_DESCRIPTION=\"Apocrypha Menu Framework\"")
    end)
    add_files("res/version.rc")
    add_defines("NOMINMAX")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("include", "src")
    set_pcxxheader("src/pch.h")

-- AMF's own ASI loader: a dinput8.dll proxy for players without the Ultimate ASI Loader. The exe imports DINPUT8.dll
-- (DirectInput8Create) and dinput8 is not a KnownDLL, so this file beside witcher3.exe is loaded at start-up. It forwards the
-- DirectInput exports to the system dinput8.dll and, at the game's entry point (loader lock released), loads every *.asi in
-- its folder - the same contract as the Ultimate ASI Loader, so either one runs AMF.
target("AMFLoader")
    set_kind("shared")
    set_basename("dinput8")
    set_warnings("allextra")
    add_files("loader/dinput8.cpp", "loader/dinput8.def", "res/version.rc")
    on_load(function (target)
        target:add("defines", "AMF_VERSION=\"" .. (target:version() or "0.0.0") .. "\"")
        -- res/version.rc: the numbers come from set_version, which only the version gate writes, so the built file
        -- reports the same version as the package (rule 6)
        local major, minor, patch = (target:version() or "0.0.0"):match("^(%d+)%.(%d+)%.(%d+)")
        target:add("defines", "AMF_VER_MAJOR=" .. (major or "0"), "AMF_VER_MINOR=" .. (minor or "0"), "AMF_VER_PATCH=" .. (patch or "0"))
        target:add("defines", "AMF_FILE_DESCRIPTION=\"Apocrypha Menu Framework ASI loader (forwards to Windows' dinput8.dll)\"")
    end)
    add_syslinks("user32", "kernel32")
    add_defines("UNICODE", "_UNICODE", "NOMINMAX", "WIN32_LEAN_AND_MEAN")

-- The SDK example (sdk/example) still targets OBSE; it returns as an .asi example in M2 (PLAN.md).

-- The mod-menu reader run outside the game: `xmake build modmenus_test` then
-- `xmake run modmenus_test "<a Witcher 3 MO2 mods folder>" [settings file]` prints every Mods.* page it would build.
target("modmenus_test")
    set_kind("binary")
    set_default(false)
    add_files("tools/modmenus_test.cpp", "src/ModMenusParse.cpp")
    add_includedirs("include")
    add_defines("NOMINMAX")

-- The engine bridge's resolution run outside the game: `xmake build red3_test` then
-- `xmake run red3_test "<...\The Witcher 3\bin\x64_dx12\witcher3.exe>"` maps the exe as an image (none of its code runs) and
-- finds the config natives in it exactly as AMF does in game.
target("red3_test")
    set_kind("binary")
    set_default(false)
    add_packages("spdlog")
    add_files("tools/red3_test.cpp", "src/Red3.cpp")
    add_includedirs("include", "src")
    add_defines("NOMINMAX")

-- The 3D preview outside the game: `xmake build preview_test` then
-- `xmake run preview_test <file.amfprev> <out prefix> [debug]` runs src/Preview3D.cpp on a D3D12 device of its own and
-- writes the model from three sides (tools/preview_export.py makes .amfprev files from the item-model prototype).
target("preview_test")
    set_kind("binary")
    set_default(false)
    add_packages("spdlog")
    add_files("tools/preview_test.cpp", "src/Preview3D.cpp")
    add_includedirs("include", "src")
    add_defines("NOMINMAX")
    add_syslinks("d3d12", "dxgi", "dxguid", "d3dcompiler")

-- The HDR composite outside the game: `xmake build hdr_test` then `xmake run hdr_test [debug]` checks the pass's numbers
-- against tools/hdr_expected.py.
target("hdr_test")
    set_kind("binary")
    set_default(false)
    add_packages("spdlog")
    add_files("tools/hdr_test.cpp", "src/HdrComposite.cpp")
    add_includedirs("include", "src")
    add_defines("NOMINMAX")
    add_syslinks("d3d12", "dxgi", "dxguid", "d3dcompiler")
