#!/usr/bin/env python3
"""PGF-01B.2 real host: planetary scanner must use persisted celestial IDs."""
import pathlib
import re
import subprocess
import sys
import tempfile

def launch(executable, database, payload):
    result=subprocess.run(
        [str(executable),str(database),"20","12"],
        input=payload,capture_output=True,timeout=45,check=False)
    if result.returncode:
        raise AssertionError("Native host failed: "+result.stderr.decode(errors="replace")[-3000:])
    return result.stderr.decode("utf-8","replace")

def extract(log, name):
    return [line for line in log.splitlines() if "|"+name+"|" in line]

def main():
    exe=pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg_pgf01b2_") as folder:
        db=pathlib.Path(folder)/"world.sqlite"
        initial=launch(exe,db,b"SYSTEM_CELESTIAL\n")
        planets=[]
        for line in extract(initial,"CELESTIAL_BODY"):
            parts=line.split("|")
            if len(parts)==12 and parts[6] in ("1","2"):
                planets.append(int(parts[4]))
        assert planets,initial[-3000:]
        body=planets[0]
        assert body>999999999,body  # explicit regression for 9-digit parser
        command=f"PLANET_SURVEY|{body}|17|36\n".encode("ascii")
        first=launch(exe,db,command)
        records=extract(first,"PLANET_REGION")
        assert len(records)==1,first[-3000:]
        a=records[0].split("|")
        assert len(a)==15 and int(a[4])==body and a[5:7]==["17","36"],a
        second=launch(exe,db,command)
        records=extract(second,"PLANET_REGION")
        assert len(records)==1,second[-3000:]
        b=records[0].split("|")
        # World tick changes on restart, geographic facts must not.
        assert a[4:]==b[4:],(a,b)
        invalid=launch(exe,db,f"PLANET_SURVEY|{body}|36|36\n".encode())
        assert not extract(invalid,"PLANET_REGION")
        assert extract(invalid,"ERROR"),invalid[-3000:]
        forged=launch(exe,db,b"PLANET_SURVEY|9999999999999999999|17|36\n")
        assert not extract(forged,"PLANET_REGION")
    print("PASS PGF-01B.2 real native planetary scan, id width, recovery, input denial")

if __name__=="__main__":
    main()
