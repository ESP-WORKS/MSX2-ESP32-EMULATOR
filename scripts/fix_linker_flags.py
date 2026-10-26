"""Remove the ESP32 linker map-file option immediately before linking.

PlatformIO may append the -Map option after extra scripts are initialized.
Therefore filtering LINKFLAGS at script-load time is too early.  This script
installs a pre-action on the firmware ELF target so the final linker flags are
filtered immediately before ld.exe runs.
"""
Import("env")


def _without_map_flags(flags):
    if isinstance(flags, str):
        flags = flags.split()
    result = []
    skip_next = False
    for flag in flags:
        if skip_next:
            skip_next = False
            continue
        text = str(flag)
        if text in ("-Wl,-Map", "-Wl,--Map", "-Map", "--Map"):
            skip_next = True
            continue
        # Covers -Wl,-Map,<file>, -Wl,-Map=<file>, --Map=<file>, etc.
        if "-Map" in text or "--Map" in text:
            continue
        result.append(flag)
    return result


def _remove_map_before_link(target, source, env):
    for key in ("LINKFLAGS", "ASLINKFLAGS"):
        if key in env:
            env[key] = _without_map_flags(env[key])

    # Be defensive if a PlatformIO version has already expanded the linker
    # command itself and embedded a map option in LINKCOM.
    linkcom = str(env.get("LINKCOM", ""))
    if linkcom:
        import re
        linkcom = re.sub(r"\s+-Wl,(?:-)?-?Map(?:=|,)[^\s]+", "", linkcom)
        linkcom = re.sub(r"\s+--?Map(?:=|,)[^\s]+", "", linkcom)
        env["LINKCOM"] = linkcom


target = "$BUILD_DIR/${PROGNAME}.elf"
env.AddPreAction(target, _remove_map_before_link)
print("[fix_linker_flags] linker map option will be removed immediately before ELF link")
