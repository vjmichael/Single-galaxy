#!/usr/bin/env python3
"""S6-H4 verified historical native SQLite -> independent modern replay.

This is a consumer for **original source-emitted saved SQLite**, never a
synthetic version marker or release migration. Require source-pinned original
test binaries, successful original native tests, and exact SQLite byte hashes
before invoking the modern C++ canonical-history/SQLite WAL/recovery auditor.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import subprocess

SHA40=re.compile(r'^[0-9a-f]{40}$')
SHA256=re.compile(r'^[0-9a-f]{64}$')
NAME=re.compile(r'^v(0[8-9][0-9]|1[0-2][0-9]|13[0-5])-pid[0-9]+\.sqlite$')
HEAD=re.compile(rb'^galaxy-kernel-v([0-9]{1,3})\|')

def demand(condition,message):
    if not condition: raise RuntimeError('S6-H4 verified source-produced corpus: '+message)
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for part in iter(lambda:stream.read(1024*1024),b''):h.update(part)
    return h.hexdigest()
def main(args):
    m=json.loads(Path(args.manifest).read_text())
    demand(m.get('schema')=='SG-S6H4-UNCHANGED-HISTORICAL-NATIVE-PRODUCERS-01',
           'wrong original producer schema')
    demand(m.get('source_git_status_clean') is True and
           m.get('synthetic_versions_allowed') is False,
           'source modified or synthetic version permitted')
    demand(m.get('original_source_commit')==args.source_sha and
           SHA40.fullmatch(args.source_sha) and
           SHA40.fullmatch(m.get('observer_head_commit','')),
           'original and observer Git SHAs are not exact')
    demand(SHA256.fullmatch(m.get('observer_binary_sha256','')),
           'observer library hash not attested')
    binaries=m.get('native_test_binary_sha256')
    demand(isinstance(binaries,dict) and binaries and
           all(SHA256.fullmatch(v or '') for v in binaries.values()),
           'original CTest native executable hashes missing')
    demand(args.executed_tests_successful,'original native tests did not pass')
    consumer=Path(args.consumer).resolve(strict=True)
    demand(consumer.is_file() and consumer.stat().st_size>0,
           'current genuine native recovery executable missing')
    records=m.get('verified_native_sqlite_records')
    demand(isinstance(records,list) and records,
           'no original source-produced database to verify')
    directory=Path(args.capture_dir).resolve(strict=True)
    seen=set(); accepted=[]
    for record in records:
        version=record.get('native_version')
        filename=record.get('original_sqlite_file','')
        claimed=record.get('original_sqlite_sha256','')
        demand(isinstance(version,int) and 86<=version<=135 and
               NAME.fullmatch(filename) and SHA256.fullmatch(claimed or ''),
               'invalid historical file/version/sha provenance')
        native=directory/filename
        demand(native.is_file() and native.resolve().parent==directory,
               'historical SQLite not in exact capture directory')
        demand(filename not in seen,'duplicate captured source sqlite')
        seen.add(filename)
        demand(digest(native)==claimed,'original emitted bytes do not match')
        with sqlite3.connect(native.as_uri()+'?mode=ro&immutable=1',uri=True) as db:
            demand(db.execute('PRAGMA integrity_check').fetchone()==('ok',),
                   'original SQLite file damaged')
            row=db.execute('SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1').fetchone()
            match=HEAD.match(row[0]) if row and isinstance(row[0],bytes) else None
            demand(match is not None and int(match.group(1))==version,
                   'version must come from original native snapshot bytes')
        subprocess.run([str(consumer),'--source-produced',str(native)],
                       check=True,timeout=args.timeout_seconds)
        demand(digest(native)==claimed,
               'modern replay mutated original historic SQLite source')
        accepted.append({'version':version,'sqlite_file':filename,
                         'sqlite_sha256':claimed,
                         'modern_recovery':'complete_canonical_state_WAL_rollback_restart_passed'})
    report={
       'schema':'SG-S6H4-ORIGINAL-TO-CURRENT-RECOVERY-01',
       'original_source_commit':args.source_sha,
       'original_observer_commit':m['observer_head_commit'],
       'historical_native_tests_successful':True,
       'current_consumer_sha256':digest(consumer),
       'replayed':accepted,
       'version_coverage':sorted({r['version'] for r in accepted}),
       'missing_version_coverage':[x for x in range(86,136) if x not in {r['version'] for r in accepted}],
       'full_historical_corpus_accepted':len({r['version'] for r in accepted})==50,
       'physical_pc_tested':False}
    output=Path(args.output)
    output.parent.mkdir(parents=True,exist_ok=True)
    temp=output.with_suffix('.partial')
    temp.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    temp.replace(output)
    print('S6-H4 PASS original preserved source -> modern replay versions:',
          report['version_coverage'])
if __name__=='__main__':
    p=argparse.ArgumentParser()
    for arg in ('manifest','capture-dir','source-sha','consumer','output'):
        p.add_argument('--'+arg,required=True)
    p.add_argument('--executed-tests-successful',action='store_true')
    p.add_argument('--timeout-seconds',type=int,default=120)
    main(p.parse_args())
