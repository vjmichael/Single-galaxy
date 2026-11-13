#!/usr/bin/env python3
"""S6-H4 source-pinned historical native SQLite capture; no invented versions."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sqlite3
VERSION=re.compile(rb'^galaxy-kernel-v([0-9]{1,3})\|')
SHA=re.compile(r'^[0-9a-f]{40}$')
def fail(why):raise RuntimeError('S6-H4 original source provenance: '+why)
def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):digest.update(block)
    return digest.hexdigest()
def inspect(path):
    with path.open('rb') as stream:
        if stream.read(16)!=b'SQLite format 3\0':fail('invalid SQLite magic '+str(path))
    with sqlite3.connect(path.resolve().as_uri()+'?mode=ro&immutable=1',uri=True) as db:
        if db.execute('PRAGMA integrity_check').fetchone()!=('ok',):fail('damaged SQLite pages '+str(path))
        tables={x[0] for x in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        if not {'snapshots','world_meta','operation_log','receipts'}.issubset(tables):
            fail('missing original native journal '+str(path))
        rows=db.execute('SELECT generation,state,checksum FROM snapshots ORDER BY generation').fetchall()
        if not rows or rows[0][0]!=1:fail('missing original native genesis '+str(path))
        versions=[]
        previous=0
        for generation,state,checksum in rows:
            if generation<=previous or not isinstance(state,bytes) or not checksum:
                fail('invalid original journal generation/state/checksum '+str(path))
            previous=generation
            found=VERSION.match(state)
            if not found:fail('absent native snapshot version '+str(path))
            versions.append(int(found.group(1)))
        version=versions[-1]
        if version<86 or version>135 or not re.search(r'/v%03d-pid\d+\.sqlite$'%version,str(path)):
            fail('source-produced version and saved filename do not agree '+str(path))
        operations=db.execute('SELECT COUNT(*) FROM operation_log WHERE kind=1').fetchone()[0]
        receipts=db.execute('SELECT COUNT(*) FROM receipts').fetchone()[0]
        if operations<1 or receipts<1:fail('no committed native commands/receipts '+str(path))
        families=Counter(line.split(b'|',1)[0].decode('ascii','replace')
                         for line in rows[-1][1].splitlines()[1:])
        return dict(native_version=version,original_generation=previous,
                    original_snapshot_rows=len(rows),saved_prior_versions=sorted(set(versions)),
                    committed_command_rows=operations,original_receipt_rows=receipts,
                    last_snapshot_sha256=hashlib.sha256(rows[-1][1]).hexdigest(),
                    line_families=dict(sorted(families.items())),
                    original_npc_line_count=sum(count for key,count in families.items() if key.startswith('npc')),
                    original_medical_line_count=sum(count for key,count in families.items() if 'medic' in key or 'health' in key),
                    original_galactic_line_count=sum(count for key,count in families.items() if 'galactic' in key or 'system' in key or 'frame' in key),
                    original_planetary_line_count=sum(count for key,count in families.items() if 'planet' in key or 'regional' in key or 'geolog' in key))
def main(args):
    if not SHA.fullmatch(args.source_sha) or not SHA.fullmatch(args.current_sha):
        fail('exact source SHA required')
    if not args.source_clean:fail('historical Git tree modified')
    observer=Path(args.observer)
    if not observer.is_file():fail('observer executable missing')
    binaries=[Path(x) for x in args.producer_executables]
    if not binaries or any(not x.is_file() for x in binaries):
        fail('original test executable missing')
    directory=Path(args.capture_dir)
    if not directory.is_dir():fail('capture directory missing')
    records=[]
    for f in sorted(directory.glob('v*-pid*.sqlite')):
        item=inspect(f)
        item.update(original_sqlite_file=f.name,original_sqlite_sha256=sha(f))
        records.append(item)
    known=sorted({r['native_version'] for r in records})
    report=dict(schema='SG-S6H4-UNCHANGED-HISTORICAL-NATIVE-PRODUCERS-01',
                original_source_commit=args.source_sha,observer_head_commit=args.current_sha,
                source_git_status_clean=True,observer_binary_sha256=sha(observer),
                native_test_binary_sha256={x.name:sha(x) for x in binaries},
                original_executed_test_names=args.test_names,
                instrumentation='LD_PRELOAD observes only successful sqlite3_exec(COMMIT) and independently copies original SQLite; original game and tests unchanged',
                saved_sqlite_artifacts=len(records),verified_historical_versions=known,
                verified_native_sqlite_records=records,
                complete_v86_v135_coverage=len(known)==50,
                synthetic_versions_allowed=False,physical_pc_tested=False)
    output=Path(args.output)
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=output.with_name(output.name+'.partial')
    temporary.write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
    temporary.replace(output)
    print('S6-H4 original source-produced SQLite versions:',known)
    if not records:fail('no authentic historical SQLite was emitted at this source')
if __name__=='__main__':
    p=argparse.ArgumentParser()
    for field in ('source-sha','current-sha','capture-dir','observer','output'):
        p.add_argument('--'+field,required=True)
    p.add_argument('--test-names',nargs='+',required=True)
    p.add_argument('--producer-executables',nargs='+',required=True)
    p.add_argument('--source-clean',action='store_true')
    main(p.parse_args())
