"""Real process, stdin commands, two sessions, actual SQLite recovery."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sqlite3

p=argparse.ArgumentParser()
p.add_argument('--executable',required=True)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='sg_b70_cli_') as root:
    save=str(Path(root)/'player.sqlite')
    def run(script):
        proc=subprocess.run([a.executable,save],input=script,text=True,capture_output=True,timeout=20)
        assert proc.returncode==0,(proc.stdout,proc.stderr)
        return proc.stdout
    first=run('scan\nmine 2 8\nrefine 8\nfabricate 4\nbuild\nscan\nquit\n')
    assert first.startswith('NEW_COLONY'),first
    assert 'crew=24 raw_ore=0 refined=0 modules=0' in first
    assert 'asteroid=2 ore=40' in first
    assert 'asteroid=4' not in first  # distant resource isn't pre-revealed
    assert first.count('RESULT APPLIED')==4,first
    assert 'outposts=1' in first,first
    assert 'outpost=5 owner=7' in first,first
    assert 'total_ore_equivalent=170' in first,first
    again=run('status\nscan\nquit\n')
    assert again.startswith('RESUMED'),again
    assert 'outposts=1' in again and 'outpost=5 owner=7' in again,again
    # Real thrust alters position; a remote ore deposit enters sensor range only
    # after the ship travels. No scan can reveal it from the starting position.
    travel=run('thrust -1 0\n'*8 + 'step 15\nscan\nquit\n')
    assert 'asteroid=4 ore=100' in travel, travel
    assert 'RESULT APPLIED' in travel,travel
    with sqlite3.connect(save) as db:
        c=db.execute("SELECT reason,count(*) FROM resource_ledger GROUP BY reason").fetchall()
        assert dict(c)['refining_transfer']==1,c
        assert dict(c)['fabrication_transfer']==1,c
        assert dict(c)['construction_embodiment']==1,c
        assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
print('PASS B7.0a terminal play loop: scan -> mine -> refine -> fabricate -> construct -> restart; SQLite ledger audited')
