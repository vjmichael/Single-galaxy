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
        climate_a=extract(first,"PLANET_SEASON")
        assert len(climate_a)==4,climate_a
        for index,line in enumerate(climate_a):
            parts=line.split("|")
            assert len(parts)==19 and int(parts[4])==body,parts
            assert parts[5:7]==["17","36"] and parts[7]=="1",parts
            assert parts[8]=="1" and int(parts[9])==index,parts
            assert int(parts[14])<=int(parts[15]),parts
            assert int(parts[16]) in (0,1) and int(parts[17]) in (0,1),parts
            assert int(parts[18])<=3,parts
        second=launch(exe,db,command)
        records=extract(second,"PLANET_REGION")
        assert len(records)==1,second[-3000:]
        b=records[0].split("|")
        # World tick changes on restart, environmental and geographic
        # facts MUST remain identical at the same original body and key.
        assert a[4:]==b[4:],(a,b)
        climate_b=extract(second,"PLANET_SEASON")
        assert len(climate_b)==4,climate_b
        for left,right in zip(climate_a,climate_b):
            assert left.split("|")[4:]==right.split("|")[4:],(left,right)
        invalid=launch(exe,db,f"PLANET_SURVEY|{body}|36|36\n".encode())
        assert not extract(invalid,"PLANET_REGION")
        assert not extract(invalid,"PLANET_SEASON")
        assert extract(invalid,"ERROR"),invalid[-3000:]
        forged=launch(exe,db,b"PLANET_SURVEY|9999999999999999999|17|36\n")
        assert not extract(forged,"PLANET_REGION")
        assert not extract(forged,"PLANET_SEASON")
    print("PASS PGF-01B.2 + STAGE6-ENV01 original planet/climate scan and restart")

if __name__=="__main__":
    main()
