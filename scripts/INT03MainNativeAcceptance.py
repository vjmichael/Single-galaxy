#!/usr/bin/env python3
"""Manual INT-03 original SQLite/native candidate, pinned to current main.

This is an end-of-group SUBSET runner, not proof of the missing v86-v135
source-produced database corpus. Never run from CI push/PR events.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

V85_COMMIT = '23bad9be38936d61b8c29b5be5f3900bec87fa6d'
SHA40 = re.compile(r'^[0-9a-f]{40}$')
TARGETS = (
    'galaxy_wo3_colony_operations_tests',
    'galaxy_wo3_planetary_housing_tests',
    'galaxy_wo3_planetary_utility_tests',
    'galaxy_wo3_historical_sqlite_import_tests',
    'galaxy_stage06h4_historical_recovery_tests',
    'galaxy_stage06g_consolidated_migration_tests',
    'galaxy_int02_two_household_tests',
    'galaxy_colony_auto04d4d2c_mixed_guardians_tests',
)
NAMES = (
    'galaxy_wo3_colony_operations',
    'galaxy_wo3_planetary_housing',
    'galaxy_wo3_planetary_utility',
    'galaxy_wo3_historical_sqlite_import',
    'galaxy_stage06h4_provenance_contracts',
    'galaxy_stage06h4_current_wal_recovery',
    'galaxy_stage06h4_v85_source_wal_recovery',
    'galaxy_stage06g_consolidated_migration',
    'galaxy_int02_original_two_households',
    'galaxy_colony_auto04d4d2c_mixed_guardians',
)


def run(*cmd, cwd=None, env=None):
    print('+', ' '.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, check=True)


def capture(*cmd, cwd=None):
    return subprocess.check_output(list(map(str, cmd)), cwd=cwd, text=True).strip()


def require(ok, why):
    if not ok:
        raise RuntimeError('INT-03 main-native source gate: ' + why)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def configure(source, build, *, windows, toolchain):
    cmd = ['cmake', '-S', source, '-B', build, '-DBUILD_TESTING=ON',
           '-DBUILD_BENCHMARKS=OFF']
    if windows:
        require(bool(toolchain) and Path(toolchain).is_file(),
                'Windows vcpkg SQLite toolchain not provisioned')
        cmd += ['-G', 'Visual Studio 17 2022', '-A', 'x64',
                '-DCMAKE_TOOLCHAIN_FILE=' + toolchain,
                '-DVCPKG_TARGET_TRIPLET=x64-windows']
    else:
        cmd += ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release']
    run(*cmd)


def built(build, target, windows):
    return Path(build) / ('Release/' if windows else '') / (
        target + ('.exe' if windows else ''))


def read_native_junit(path):
    cases = list(ET.parse(path).iter('testcase'))
    names = [case.get('name') for case in cases]
    require(len(cases) == len(NAMES) and set(names) == set(NAMES) and
            len(names) == len(set(names)),
            'genuine CTest cases missing, renamed, unexpected or duplicated')
    require(all(case.find('failure') is None and case.find('error') is None and
                case.find('skipped') is None for case in cases),
            'native group has failed or skipped CTest')
    return names


def main(args):
    require(len(NAMES) == 10 and len(TARGETS) == 8, 'original suite inventory drift')
    if args.check_contract:
        print('INT03 contract: 8 original C++ targets and 10 exact native/SQLite/fixture CTests;'
              ' source-produced historical v86-v135 remains UNACCEPTED')
        return
    require(SHA40.fullmatch(args.expected_sha), 'exact forty-hex main SHA required')
    root = Path(__file__).resolve().parent.parent
    require(capture('git', 'rev-parse', 'HEAD', cwd=root) == args.expected_sha,
            'checked out source differs from requested main commit')
    require(capture('git', 'rev-parse', '--abbrev-ref', 'HEAD', cwd=root) == 'HEAD',
            'acceptance checkout must be an immutable detached exact SHA')
    require(not capture('git', 'status', '--porcelain', '--untracked-files=no', cwd=root),
            'tracked native source is dirty')
    windows = os.name == 'nt'
    artifact = Path(args.evidence_dir).resolve()
    artifact.mkdir(parents=True, exist_ok=True)
    build = artifact / 'current'
    native = artifact / 'historical-v85'
    old_build = artifact / 'v85-build'
    run('python', root / 'tests/Stage06H4ProvenanceContractsTests.py')
    configure(root, build, windows=windows, toolchain=args.toolchain)
    run('cmake', '--build', build, '--config', 'Release', '--parallel', '2',
        '--target', *TARGETS)
    run('git', 'cat-file', '-e', V85_COMMIT + '^{commit}', cwd=root)
    run('git', 'worktree', 'add', '--detach', native, V85_COMMIT, cwd=root)
    require(capture('git', 'rev-parse', 'HEAD', cwd=native) == V85_COMMIT,
            'historical source commit differs from original pin')
    run(sys.executable, root / 'tests/WO3HistoricalNativeProducer.py',
        'prepare', native)
    configure(native, old_build, windows=windows, toolchain=args.toolchain)
    vintage_target = 'galaxy_play13d_regional_facilities_tests'
    run('cmake', '--build', old_build, '--config', 'Release', '--parallel', '2',
        '--target', vintage_target)
    vintage_exe = built(old_build, vintage_target, windows)
    require(vintage_exe.is_file(), 'original native producer executable missing')
    v85 = artifact / 'historical-original-v85.sqlite'
    producer_env = dict(os.environ, SG_WO3_HISTORICAL_CAPTURE_OUTPUT=str(v85))
    run(vintage_exe, env=producer_env)
    manifest = artifact / 'historical-original-v85-producer.json'
    envfile = artifact / 'original-producer-env.txt'
    envfile.write_text('', encoding='utf-8')
    run(sys.executable, root / 'tests/WO3HistoricalNativeProducer.py',
        'attest', native, '--binary', vintage_exe, '--sqlite', v85,
        '--manifest', manifest, '--github-env', envfile)
    selected = dict(os.environ)
    for line in envfile.read_text(encoding='utf-8').splitlines():
        key, _, val = line.partition('=')
        require(key.startswith('SG_WO3_V85_') and bool(val),
                'historic producer environment receipt malformed')
        selected[key] = val
    require(capture('git', 'rev-parse', 'HEAD', cwd=root) == args.expected_sha,
            'main SHA advanced during original producer execution')
    ctest = '|'.join(re.escape(n) for n in NAMES)
    junit = artifact / ('int03-windows-junit.xml' if windows else 'int03-linux-junit.xml')
    run('ctest', '--test-dir', build, '-C', 'Release', '--no-tests=error',
        '--output-on-failure', '--timeout', '600', '--output-junit', junit,
        '-R', '^(' + ctest + ')$', env=selected)
    names = read_native_junit(junit)
    provenance = json.loads(manifest.read_text(encoding='utf-8'))
    require(provenance.get('producer_source_commit') == V85_COMMIT and
            provenance.get('archived_artifact') is False and
            provenance.get('sqlite_sha256') == digest(v85),
            'source-era v85 producer attestation differs from SQLite bytes')
    report = dict(schema='SG-INT03-MAIN-NATIVE-SUBSET-01',
                  status='native_subset_only_h4_full_corpus_open',
                  source_sha=args.expected_sha, original_v85_source_sha=V85_COMMIT,
                  original_v85_sqlite_sha256=digest(v85),
                  original_v85_binary_sha256=digest(vintage_exe),
                  original_ctests_passed=names,
                  original_ctest_junit_sha256=digest(junit),
                  h4_v86_to_v135_corpus_accepted=False,
                  full_int03_accepted=False,
                  installed_windows_or_gpu_accepted=False)
    (artifact / 'int03-native-subset-receipt.json').write_text(
        json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print('INT-03 native subset passed at exact main SHA, H4 full corpus still OPEN')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--check-contract', action='store_true')
    p.add_argument('--expected-sha')
    p.add_argument('--toolchain')
    p.add_argument('--evidence-dir', default='build/int03-main-evidence')
    main(p.parse_args())
