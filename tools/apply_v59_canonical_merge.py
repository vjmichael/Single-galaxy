#!/usr/bin/env python3
"""Apply the verified v59->unified game-kernel overlay to an existing source checkout.

Fails closed when the target has diverged from the frozen canonical source. Does
not fetch code or perform a git push. The copy is performed after preflight.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
FOCUSED_SRC = [
    'Simulation.cpp','Persistence.cpp','Inventory.cpp','InventoryLogistics.cpp',
    'QuantifiedProduction.cpp','FactoryLayout.cpp','ColonyPolicy.cpp',
    'PopulationCohorts.cpp','ColdColonyNeeds.cpp','MacroNeeds.cpp',
    'NpcAppearanceGenome.cpp','MedicalSocietyBridge.cpp',
    'DependentIncidentAccounting.cpp','DependentIncidentJournal.cpp',
    'DependentGuardianship.cpp','SplitRescueIntent.cpp',
    'RegionalCheckpointStore.cpp','RegionalPopulationAudit.cpp',
    'RegionalEventScheduler.cpp','RegionalModeAuthority.cpp',
    'RegionalFactoryHandoff.cpp','RegionalProductionNetwork.cpp',
    'RegionalShipmentReconciliation.cpp','ShipmentManifest.cpp',
    'ThirdPartyShipment.cpp','CanonicalShippingHost.cpp'
]
# Original game regional implementation must share one PersistentWorld and one
# RegionalModeAuthority implementation with the freight code.
REGIONAL_SRC = [
    'RegionalHostCommands.cpp','RegionalInterestManager.cpp',
    'RegionalCustodyLedger.cpp','RegionalPersistentWorldBridge.cpp'
]
EXTRA_TESTS = [x.name for x in sorted((HERE/'tests').glob('V59_*.cpp'))]
LEGACY_TESTS = [
    'B70nWorkforceTests.cpp','ECON02B1aOutpostWorkshopTests.cpp',
    'ECON02B1bConnectedLineTests.cpp','ECON02B1cCargoVolumeTests.cpp',
    'ECON02B2bStorageTests.cpp','ECON02B2c2PersistentNodesTests.cpp',
    'ECON02B2c3AutomaticLinksTests.cpp','ECON02B2d2WorkshopLinksTests.cpp',
    'ECON02B2d3aInstalledPlacementTests.cpp','ECON02B2d3bSpatialConveyorTests.cpp'
]
NEW_TEST = 'CanonicalMergeLegacySnapshotTests.cpp'
SRC_LIST = [f'src/{x}' for x in FOCUSED_SRC + REGIONAL_SRC]
HEADER_LIST = [f'include/galaxy/{x.name}' for x in sorted((HERE/'include/galaxy').glob('*.hpp'))]
TEST_LIST = [f'tests/{x}' for x in EXTRA_TESTS + LEGACY_TESTS + [NEW_TEST]]
FIXTURE_LIST = ['tests/fixtures/canonical_merge/legacy_v20_genesis.snap']
COPY_LIST = list(dict.fromkeys(SRC_LIST + HEADER_LIST + TEST_LIST + FIXTURE_LIST))
CMAKE_SOURCE_LIST = list(dict.fromkeys(SRC_LIST))


def git_sha(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def unified_cmake(src: str, target: Path) -> str:
    if 'galaxy_canonical_legacy_v20_replay' in src:
        raise RuntimeError('checkout already has the merge; review it rather than reapply')
    if 'add_library(galaxy_kernel STATIC' not in src:
        raise RuntimeError('main CMake galaxy_kernel library not found')
    # Remove the old regional STATIC library's implementation objects. All
    # regional sources join galaxy_kernel, and regional dependents link its
    # compatibility interface. There cannot be two regional mode authorities.
    pat = re.compile(r'add_library\(galaxy_regional STATIC\b.*?\nendif\(\)\n(?=if\(BUILD_TESTING\))', re.S)
    match = pat.search(src)
    if match:
        src = src[:match.start()] + (
            'add_library(galaxy_regional INTERFACE)\n'
            'target_link_libraries(galaxy_regional INTERFACE galaxy_kernel SQLite::SQLite3)\n'
        ) + src[match.end():]
    elif 'add_library(galaxy_regional' in src:
        raise RuntimeError('unexpected existing regional target; manual CMake audit needed')
    else:
        src += ('\nadd_library(galaxy_regional INTERFACE)\n'
                'target_link_libraries(galaxy_regional INTERFACE galaxy_kernel SQLite::SQLite3)\n')
    pat = re.compile(r'(add_library\(galaxy_kernel STATIC\b.*?)(\)\s*\n\s*target_include_directories\(galaxy_kernel)',re.S)
    m=pat.search(src)
    if not m: raise RuntimeError('cannot locate exact kernel source-list closing parenthesis')
    original=m.group(1)
    missing=[x for x in CMAKE_SOURCE_LIST if x not in original]
    # Some local experimental source trees have further cold/detailed industry
    # modules. Do not drop them if the source actually contains them.
    for x in ('RegionalColdIndustry.cpp','RegionalWarmRuntime.cpp'):
        if (target/'src'/x).is_file() and f'src/{x}' not in original:
            missing.append('src/'+x)
    src=src[:m.start()] + original + ('\n  '+' '.join(missing) if missing else '') + m.group(2) + src[m.end():]
    src+='\n# Unified v59 shipping, regional ownership and legacy-snapshot migration tests.\nif(BUILD_TESTING)\n'
    for x in EXTRA_TESTS:
        name=x.removesuffix('.cpp').lower()
        src+=f'  add_executable({name} tests/{x})\n  target_link_libraries({name} PRIVATE galaxy_kernel)\n  add_test(NAME {name} COMMAND {name})\n'
    src+=('  add_executable(galaxy_canonical_legacy_v20_replay tests/CanonicalMergeLegacySnapshotTests.cpp)\n'
          '  target_link_libraries(galaxy_canonical_legacy_v20_replay PRIVATE galaxy_kernel)\n'
          '  add_test(NAME galaxy_canonical_legacy_v20_replay COMMAND galaxy_canonical_legacy_v20_replay\n'
          '    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/canonical_merge/legacy_v20_genesis.snap)\n'
          'endif()\n')
    return src


def main() -> None:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('target',type=Path,help='existing vjmichael/Single-galaxy checkout')
    ap.add_argument('--apply',action='store_true',help='perform copy after all checks pass')
    args=ap.parse_args()
    target=args.target.resolve()
    if target==HERE.resolve(): raise SystemExit('source and destination must be separate')
    if not (target/'CMakeLists.txt').is_file() or not (target/'src/Simulation.cpp').is_file():
        raise SystemExit('target does not look like the existing Single-galaxy repository')
    manifest=json.loads((HERE/'tools/canonical_merge_expected_git_blobs.json').read_text())
    failures=[]
    changed=[]
    for path in COPY_LIST:
        source=HERE/path
        if not source.exists(): failures.append(f'missing source: {path}');continue
        dest=target/path
        if dest.exists():
            old=git_sha(dest.read_bytes());new=git_sha(source.read_bytes())
            allowed=manifest.get(path,[])
            if old!=new and old not in allowed: failures.append(f'unexpected modified file: {path} (git blob {old})')
            if old!=new:changed.append(path)
        else: changed.append(path)
    try:
        cmake=unified_cmake((target/'CMakeLists.txt').read_text(),target)
    except Exception as e: failures.append(str(e));cmake=''
    if failures:
        for x in failures: print('BLOCKED',x)
        raise SystemExit(f'preflight rejected {len(failures)} differences; NOTHING WRITTEN')
    print(f'CHECKED: {len(COPY_LIST)} authoritative source/header/test/fixture paths; {len(changed)} changes; unified CMake generated')
    if args.apply:
        for path in changed:
            dest=target/path;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(HERE/path,dest)
        (target/'CMakeLists.txt').write_text(cmake)
        print('APPLIED: single galaxy_kernel with one regional authority. Run fresh CMake configure, build, and CTest.')
    else:
        print('DRY RUN ONLY. Use --apply after reviewing target and creating a backup/branch.')

if __name__=='__main__':
    main()
