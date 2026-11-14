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
import shutil
import tempfile
import xml.etree.ElementTree as ET
from Stage06H4ProvenanceChecks import verified_ctest_selection, verify_corpus_summary, verify_source_record

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
           SHA40.fullmatch(m.get('observer_head_commit','')) and
           (args.current_sha is None or
            (SHA40.fullmatch(args.current_sha) and
             m.get('observer_head_commit')==args.current_sha)),
           'original and observer Git SHAs are not exact')
    demand(SHA256.fullmatch(m.get('observer_binary_sha256','')),
           'observer library hash not attested')
    adapter=m.get('historical_compiler_adapter')
    demand(isinstance(adapter,dict) and
           adapter.get('schema')=='SG-S6H4-VINTAGE-SHADOW-COMPILER-01' and
           adapter.get('historical_commit')==args.source_sha and
           adapter.get('original_git_unchanged') is True and
           adapter.get('no_synthetic_snapshot_versions') is True and
           adapter.get('no_data_schema_rewrite') is True,
           'compiler adapter original source provenance missing')
    demand(m.get('pristine_historical_executable') is
           (not adapter.get('compiled_using_adapter')),
           'pristine historical binary flag contradicts compiler provenance')
    binaries=m.get('native_test_binary_sha256')
    demand(isinstance(binaries,dict) and binaries and
           all(SHA256.fullmatch(v or '') for v in binaries.values()),
           'original CTest native executable hashes missing')
    junit=Path(args.test_junit).resolve(strict=True)
    xml=ET.parse(junit)
    cases=list(xml.iter('testcase'))
    observed_tests=verified_ctest_selection(m.get('original_executed_test_names'),cases)
    observer=Path(args.observer).resolve(strict=True)
    demand(digest(observer)==m['observer_binary_sha256'],
           'observer bytes do not match original attestation')
    observed_binaries={}
    for binary in args.producer_executables:
        original=Path(binary).resolve(strict=True)
        demand(original.is_file(),'original native binary missing')
        observed_binaries[original.name]=digest(original)
    demand(len(observed_binaries)==len(args.producer_executables) and
           observed_binaries==binaries,
           'original native executable names or bytes do not match attestation')
    consumer=Path(args.consumer).resolve(strict=True)
    demand(consumer.is_file() and consumer.stat().st_size>0,
           'current genuine native recovery executable missing')
    records=m.get('verified_native_sqlite_records')
    verify_corpus_summary(m,records)
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
            verify_source_record(record,db)
            demand(db.execute('PRAGMA integrity_check').fetchone()==('ok',),
                   'original SQLite file damaged')
            row=db.execute('SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1').fetchone()
            match=HEAD.match(row[0]) if row and isinstance(row[0],bytes) else None
            demand(match is not None and int(match.group(1))==version,
                   'version must come from original native snapshot bytes')
        # This native recovery executable opens its input read/write while
        # exercising WAL checkpoints. Never give it the attested original.
        with tempfile.TemporaryDirectory(prefix='s6h4-modern-replay-') as scratch:
            isolated=Path(scratch)/filename
            shutil.copyfile(native,isolated)
            demand(digest(isolated)==claimed,
                   'isolated input differs from attested historical SQLite')
            subprocess.run([str(consumer),'--source-produced',str(isolated)],
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
       'historical_compiler_adapter':adapter,
       'historical_kernel_pristine_binary':not adapter['compiled_using_adapter'],
       'original_passed_test_names':observed_tests,
       'original_junit_sha256':digest(junit),
       'original_binary_hashes_independently_rechecked':observed_binaries,
       'original_observer_hash_independently_rechecked':digest(observer),
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
    p.add_argument('--test-junit',required=True)
    p.add_argument('--current-sha',help='Exact current observer main SHA when supplied')
    p.add_argument('--observer',required=True)
    p.add_argument('--producer-executables',nargs='+',required=True)
    p.add_argument('--timeout-seconds',type=int,default=120)
    main(p.parse_args())
