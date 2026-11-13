#!/usr/bin/env python3
"""S6-H4: report only authentic source-pinned historical SQLite producer coverage."""
import argparse, hashlib, json, re, sqlite3
from pathlib import Path
SOURCE="23bad9be38936d61b8c29b5be5f3900bec87fa6d"
BLOB="6add9dfbf4cbb0a19794abaadd7e699239923e08"
SCHEMA="SG-WO3-HISTORICAL-NATIVE-V85-REPRODUCTION-01"
def digest(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda:f.read(1<<20),b""): h.update(chunk)
    return h.hexdigest()
def check(test,reason):
    if not test: raise RuntimeError("S6-H4 historical producer: "+reason)
def audit(native,attestation,output):
    native=Path(native);proof=json.loads(Path(attestation).read_text())
    check(proof.get("schema")==SCHEMA,"wrong producer schema")
    check(proof.get("producer_source_commit")==SOURCE and
          proof.get("original_test_git_blob")==BLOB and
          proof.get("archived_artifact") is False,"wrong source/blob/provenance")
    claimed=proof.get("sqlite_sha256")
    check(isinstance(claimed,str) and re.fullmatch("[0-9a-f]{64}",claimed),
          "source digest absent")
    check(native.is_file() and digest(native)==claimed,"source bytes not attested")
    uri=native.resolve().as_uri()+"?mode=ro&immutable=1"
    with sqlite3.connect(uri,uri=True) as db:
        check(db.execute("PRAGMA integrity_check").fetchone()==("ok",),
              "original native SQLite damaged")
        versions=set()
        rows=db.execute("SELECT generation,state FROM snapshots ORDER BY generation").fetchall()
        check(len(rows)>1,"historical journal missing native generations")
        for generation,state in rows:
            check(generation>=1 and isinstance(state,bytes),"invalid original native generation")
            version=re.match(rb"galaxy-kernel-v(\d+)\|",state)
            check(version,"original native header missing")
            versions.add(int(version.group(1)))
        check(85 in versions and max(versions)==85,
              "not a producer of the original native v85 journal")
        commands=db.execute("SELECT COUNT(*) FROM operation_log WHERE kind=1").fetchone()[0]
        receipts=db.execute("SELECT COUNT(*) FROM receipts").fetchone()[0]
        check(commands>0 and receipts>0,"no original commands/receipts")
    report={
      "schema":"SG-S6H4-HISTORICAL-COVERAGE-01",
      "status":"partial_source_authenticated_corpus_only",
      "original_native_sqlite_versions":sorted(versions),
      "missing_authenticated_sqlite_producers_v86_to_v135":list(range(86,136)),
      "v85_to_v86_upgrade_in_memory_is_not_original_v86_sqlite":True,
      "original_producer_source_commit":SOURCE,"original_test_git_blob":BLOB,
      "original_sqlite_sha256":claimed,"original_command_rows":commands,
      "original_receipt_rows":receipts,"archived_artifact_claimed":False,
      "installed_game_or_release_accepted":False}
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    tmp=output.with_name(output.name+".tmp")
    tmp.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    tmp.replace(output)
    print("S6-H4 genuine historical v85 attested; v86 through v135 remain open")
if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--v85-sqlite",required=True)
    p.add_argument("--v85-attestation",required=True)
    p.add_argument("--output",required=True)
    a=p.parse_args();audit(a.v85_sqlite,a.v85_attestation,a.output)
