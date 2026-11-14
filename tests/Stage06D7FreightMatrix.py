#!/usr/bin/env python3
"""Original source-built site/shuttle -> D7 v136 native schema/restart.
A schema test cannot claim successful manufactured-source freight delivery.
"""
from pathlib import Path
import subprocess
import sys
import tempfile


def run(argv):
    result=subprocess.run(list(map(str,argv)),capture_output=True,
                          text=True,timeout=240)
    if result.returncode:
        raise AssertionError(
            f"native exit={result.returncode} command={argv}\\n"
            +result.stdout[-3000:]+"\\n"+result.stderr[-6000:])
    return result


def main():
    if len(sys.argv)!=3:
        raise SystemExit("usage: D7MATRIX FIXTURE D7_TEST")
    fixture=Path(sys.argv[1]).resolve()
    test=Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg_stage6d7_") as td:
        db=Path(td)/"original_shuttle.sqlite"
        reopened=Path(td)/"v136_geology.sqlite"
        producer=run([fixture,"--seed-orbit",db])
        assert producer.stdout.startswith("TRANSFER|"),producer.stdout
        result=run([test,db,reopened])
        assert "S6-D7 original geological shuttle" in result.stdout,result.stdout
        print(result.stdout.strip())


if __name__=="__main__":
    main()
