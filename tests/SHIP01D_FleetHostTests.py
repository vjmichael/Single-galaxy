#!/usr/bin/env python3
"""SHIP-01D native host smoke: fleet authority survives command/restart paths.

Runs the genuine C++ SQLite host with a fresh world. Does not substitute a
fake relay, seeded free freighter, mock simulation or bypass owner validation.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

def launch(host: pathlib.Path, save: pathlib.Path, commands: list[str]) -> str:
    proc=subprocess.run(
        [str(host),str(save),"20","45","--authenticated-transport"],
        input=("".join(c+"\n" for c in commands)).encode("ascii"),
        stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,
        timeout=35,check=False,
    )
    error=proc.stderr.decode("utf-8","replace")
    if proc.returncode!=0:
        raise RuntimeError(f"Native host exit {proc.returncode}: {error[-4000:]}")
    return error

def main(host: pathlib.Path):
    with tempfile.TemporaryDirectory(prefix="sg-ship01d-") as work:
        save=pathlib.Path(work)/"colony.sqlite"
        initial=launch(host,save,[
            "AUTH|7|FLEET_STATUS",
            "AUTH|7|FLEET_SELECT|1",
            "AUTH|7|FLEET_SELECT|2",  # real asteroid, NOT a player hull
            "AUTH|8|FLEET_SELECT|1",  # untrusted foreign account
            "AUTH|7|FLEET_STATUS",
            "AUTH|7|THRUST|1|0",
            "AUTH|7|YAW|15",
            # Cannot assign founding ship or an asteroid as a freighter.
            "AUTH|7|FLEET_WORK_POST|77007|1|2|4294967300|17|36|NUTRIENTS|100",
            "AUTH|7|FLEET_WORK_POST|77008|2|1|4294967300|17|36|NUTRIENTS|100",
        ])
        if len(re.findall(r"\|FLEET_RESET\n",initial))<2:
            raise AssertionError("Real host did not enumerate fleet twice")
        if not re.search(r"\|FLEET_ITEM\|1\|0\|1\|1\|24\|",initial):
            raise AssertionError("Founding colony unavailable from native fleet")
        if not re.search(r"\|FLEET_DONE\|1\n",initial):
            raise AssertionError("Unexpected free vessel in initial fleet")
        if not re.search(r"\|FLEET_SELECTED\|1\|1\|[1-9][0-9]*\n",initial):
            raise AssertionError("Owned home cockpit selection did not commit")
        if "|ERROR|FLEET_SELECTION_DENIED" not in initial:
            raise AssertionError("Non-ship fleet selection was not denied")
        if "|ERROR|UNAUTHORIZED_PRINCIPAL" not in initial:
            raise AssertionError("Foreign account was not denied")
        if not re.search(r"\|FLEET_PILOT_ITEM\|1\|",initial):
            raise AssertionError("Real piloted hull telemetry was not published")
        if len(re.findall(r"\\|ERROR\\|FLEET_WORK_DENIED",initial))<2:
            raise AssertionError("Fake/actively piloted fleet work assignment was not denied")
        if "|RESULT|" not in initial or "|APPLIED|" not in initial:
            raise AssertionError("Native owner flight commands were not committed")
        restarted=launch(host,save,["AUTH|7|FLEET_STATUS"])
        if "|RESUMED" not in restarted:
            raise AssertionError("Real SQLite world did not reopen")
        if not re.search(r"\|FLEET_ITEM\|1\|0\|1\|1\|24\|",restarted):
            raise AssertionError("World restart lost original home colony identity")
        if not re.search(r"\|FLEET_SELECTED\|1\|1\|[1-9][0-9]*\n",restarted):
            raise AssertionError("Restart did not revalidate the home cockpit")
        print("PASS SHIP-01D native real host: owner fleet, foreign rejection,"
              " flight controls, SQLite restart")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--host",type=pathlib.Path,required=True)
    args=parser.parse_args()
    main(args.host.resolve())
