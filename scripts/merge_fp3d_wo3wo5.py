from pathlib import Path
import re

EXPECTED = {
    "src/Persistence.cpp": 5,
    "src/Simulation.cpp": 8,
    "tests/V59_multi_destination_host.cpp": 1,
    "tests/WO3PlanetaryUtilityTests.cpp": 1,
    "tests/WO4ResourceDemandTests.cpp": 2,
    "tests/WO5PhysicalLogisticsTests.cpp": 1,
}
rx = re.compile(r"(?ms)^<<<<<<< HEAD\n(.*?)^=======\n(.*?)^>>>>>>> [^\n]+\n")
for path, count in EXPECTED.items():
    f = Path(path)
    contents = f.read_text()
    conflicts = list(rx.finditer(contents))
    if len(conflicts) != count:
        raise RuntimeError(f"{path}: unexpected conflict count {len(conflicts)}, expected {count}")
    choices = []
    for i, m in enumerate(conflicts):
        ours, theirs = m.group(1), m.group(2)
        if path == "src/Persistence.cpp":
            if i == 0:
                # Old WO saves may contain the other immutable provenance table.
                # Enforce BOTH validations; the group2 writer remains canonical.
                resolution = ours + "            }\n        }\n" + theirs
            else:
                # Preserve worker token, atomic native writer, and no-fabrication
                # rules, plus non-advancing snapshot/custody journal invariants.
                resolution = ours
        elif path == "src/Simulation.cpp":
            if i == 0:
                resolution = """    // Trusted pre-v71 submission must migrate complete physical registries
    // before a new-version journal write; decoding a persisted snapshot never
    // manufactures missing records (forged/missing data remains fatal).
    if(snapshot_version_>=71) {
        bool missing_physical_system=false;
        for(const auto id:systems_)
            if(!physical_celestial_systems_.count(id)) {
                missing_physical_system=true;
                break;
            }
        if(missing_physical_system)ensure_physical_celestial_systems();
        // Also preserve the separate existing-ship frame custody upgrade.
        if(upgrading_pre_frame_snapshot || missing_physical_system)
            ensure_ship_frame_registry();
    }
"""
            else:
                # Group2 owns newer snapshot versions/actions and physical
                # coordinates; non-overlapping WO changes remain auto-merged.
                resolution = ours
        elif path == "tests/WO4ResourceDemandTests.cpp":
            if i == 0:
                resolution = theirs
            else:
                resolution = ours + "        CHECK(fault.generation()==initial_generation+1);\n"
        elif path == "tests/WO5PhysicalLogisticsTests.cpp":
            resolution = theirs
        else:
            resolution = theirs
        choices.append(resolution)
    it = iter(choices)
    new_text = rx.sub(lambda _: next(it), contents)
    if "<<<<<<<" in new_text or ">>>>>>>" in new_text:
        raise RuntimeError(f"unresolved conflict in {path}")
    f.write_text(new_text)
    print(f"resolved {path}: {count} conflicts")
