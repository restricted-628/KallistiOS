#!/usr/bin/env python3
"""Check the initial graphics extraction inventory; does not change files.

This reads literal source lists, not arbitrary Make syntax. Unknown list syntax,
new objects, and unexpected direct driver-private dependencies fail closed.
Passing means ownership coverage, NOT a completed or standalone addon.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CORE = {
    "PVR": set("""pvr_mem_core pvr_mem pvr_mem_reservation pvr_buffers pvr_irq
        pvr_multipass_layout pvr_init_shutdown pvr_globals pvr_misc pvr_events
        pvr_send_to_ta pvr_fog pvr_palette pvr_prim pvr_scene pvr_material
        pvr_texture pvr_texture_layout pvr_texture_reservation pvr_texture_request
        pvr_dma""".split()),
    "MATH": {"fmath", "math", "matrix", "matrix3d"},
}
DIRECTORIES = {
    "PVR": ROOT / "kernel/arch/dreamcast/hardware/pvr",
    "MATH": ROOT / "kernel/arch/dreamcast/math",
}
# Explicit unresolved exception, not permission for new private dependencies.
KNOWN_BLOCKERS = {"pvr_geometry.c", "pvr_chunk_render.c"}

def literal_list(file, variable, suffix):
    values = []
    source = file.read_text().replace("\\\n", " ")
    for line in source.splitlines():
        match = re.match(r"^" + re.escape(variable) + r"\s*([:+]?=)\s*(.*?)\s*$",
                         line.split("#", 1)[0])
        if not match:
            continue
        operator, body = match.groups()
        if operator != "+=":
            if values:
                raise ValueError(f"{file}: unexpected repeated assignment for {variable}")
        for token in body.split():
            if not re.fullmatch(r"[a-z0-9_]+\." + suffix, token):
                raise ValueError(f"{file}: unsupported source-list token {token!r}")
            values.append(token.rsplit(".", 1)[0])
    if not values or len(values) != len(set(values)):
        raise ValueError(f"{file}: empty or duplicate {variable} entries")
    return set(values)

def main():
    manifest = ROOT / "addons/libdcgfx/sources.mk"
    found_blockers = set()
    total_addon = 0
    for group, directory in DIRECTORIES.items():
        built = literal_list(directory / "Makefile", "OBJS", "o")
        addon = literal_list(manifest, "DCGFX_" + group + "_SOURCES", "c")
        core = CORE[group]
        if core & addon or built != core | addon:
            raise ValueError(f"{group}: ownership mismatch: overlap={sorted(core & addon)}, "
                             f"unclassified={sorted(built - core - addon)}, "
                             f"not-built={sorted((core | addon) - built)}")
        for unit in sorted(addon):
            source = directory / (unit + ".c")
            text = source.read_text()
            if re.search(r'#\s*include\s*[<"]pvr_internal\.h[>"]|\bpvr_state\b', text):
                found_blockers.add(source.name)
        total_addon += len(addon)
        print(f"{group}: {len(core)} core + {len(addon)} proposed addon units")
    unexpected = found_blockers - KNOWN_BLOCKERS
    if unexpected:
        raise ValueError("New direct PVR-private dependencies: " + ", ".join(sorted(unexpected)))
    for name in sorted(found_blockers):
        print(f"OPEN EXTRACTION BLOCKER: {name} accesses private PVR state")
    print(f"PASS: inventory covers {total_addon} proposed addon units; build still integrated")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
