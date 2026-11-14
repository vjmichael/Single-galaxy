"""INT-03: independent H4 producer provenance checks, never a save converter."""
from collections import Counter
import hashlib
import re

_HEADER = re.compile(rb'^galaxy-kernel-v([0-9]{1,3})\|')


def require(condition, why):
    if not condition:
        raise RuntimeError('S6-H4 original source provenance: ' + why)


def verified_ctest_selection(declared, cases):
    """Bind a passing JUnit suite to the exact vintage CTest selection regex."""
    require(isinstance(declared, list) and len(declared) == 1 and
            isinstance(declared[0], str) and 3 <= len(declared[0]) <= 512 and
            declared[0].startswith('^') and declared[0].endswith('$'),
            'one bounded, anchored original native CTest selector is required')
    try:
        selector = re.compile(declared[0])
    except re.error as exc:
        raise RuntimeError('S6-H4 invalid native CTest selector: ' + str(exc)) from exc
    names = [case.get('name') for case in cases]
    require(bool(names) and len(names) == len(set(names)) and
            all(isinstance(name, str) and bool(name) and
                selector.fullmatch(name) for name in names),
            'passing native CTest names differ from source-pinned producer selection')
    require(all(case.find('failure') is None and
                case.find('error') is None and
                case.find('skipped') is None for case in cases),
            'failed, errored or skipped original CTest cannot prove native SQLite')
    return names


def verify_corpus_summary(manifest, records):
    require(isinstance(records, list) and bool(records) and
            all(isinstance(r, dict) and
                type(r.get('native_version')) is int for r in records),
            'original native SQLite records missing or malformed')
    known = sorted({r['native_version'] for r in records})
    require(manifest.get('verified_historical_versions') == known and
            manifest.get('saved_sqlite_artifacts') == len(records) and
            manifest.get('complete_v86_v135_coverage') is (len(known) == 50),
            'original native corpus version/coverage summary differs from records')


def verify_source_record(record, db):
    """Re-audit report metadata against the original captured SQLite bytes."""
    rows = db.execute('SELECT generation,state,checksum FROM snapshots ORDER BY generation').fetchall()
    require(bool(rows) and rows[0][0] == 1,
            'missing original native snapshot genesis')
    previous = 0
    versions = []
    for generation, state, checksum in rows:
        require(type(generation) is int and generation > previous and
                isinstance(state, bytes) and bool(checksum),
                'native snapshot generation/state/checksum invalid')
        previous = generation
        header = _HEADER.match(state)
        require(header is not None, 'native original snapshot version header absent')
        versions.append(int(header.group(1)))
    latest = rows[-1][1]
    families = Counter(line.split(b'|', 1)[0].decode('ascii', 'replace')
                       for line in latest.splitlines()[1:])
    declared = dict(
        original_generation=previous,
        original_snapshot_rows=len(rows),
        saved_prior_versions=sorted(set(versions)),
        native_version=versions[-1],
        last_snapshot_sha256=hashlib.sha256(latest).hexdigest(),
        line_families=dict(sorted(families.items())),
        original_npc_line_count=sum(n for key, n in families.items() if key.startswith('npc')),
        original_medical_line_count=sum(n for key, n in families.items() if 'medic' in key or 'health' in key),
        original_galactic_line_count=sum(n for key, n in families.items() if 'galactic' in key or 'system' in key or 'frame' in key),
        original_planetary_line_count=sum(n for key, n in families.items() if 'planet' in key or 'regional' in key or 'geolog' in key),
        committed_command_rows=db.execute('SELECT COUNT(*) FROM operation_log WHERE kind=1').fetchone()[0],
        original_receipt_rows=db.execute('SELECT COUNT(*) FROM receipts').fetchone()[0],
    )
    require(declared['committed_command_rows'] > 0 and declared['original_receipt_rows'] > 0,
            'captured source has no real original commands or receipts')
    require(all(record.get(key) == value for key, value in declared.items()),
            'attested original SQLite metadata does not match captured native bytes')
