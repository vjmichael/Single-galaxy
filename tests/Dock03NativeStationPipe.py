#!/usr/bin/env python3
"""DOCK-03 native station pipe contract, staged for later consolidated execution.

Original native child is sole writer. This checks real PERSON custody,
shore transfer, durable worker shift and restart. No Godot fake population.
"""
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time

def check(ok, reason):
    if not ok:
        raise AssertionError(reason)

def station_session(executable, db_path):
    child = subprocess.Popen(
        [str(executable), str(db_path), "--explicit-test-station"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        encoding="ascii", errors="strict", bufsize=1)
    genesis = child.stdout.readline().strip().split("|")
    check(len(genesis) == 13 and genesis[:2] == ["PERSON", "2"],
          "native station genesis is not original PERSON/2")
    check(genesis[3] == "0" and genesis[6] == "4",
          "original player is not at local colony")
    station = int(genesis[7])
    return child, int(genesis[2]), int(genesis[5]), station

def command(child, op, session, request, generation, source, amount=None):
    time.sleep(0.07)  # native host conservatively rejects <45ms bursts
    line = f"{op}|{session}|{request}|{generation}|{source}"
    if amount is not None:
        line += f"|{amount}"
    child.stdin.write(line + "\n")
    child.stdin.flush()
    answer = child.stdout.readline().strip().split("|")
    check(len(answer) == 32 and answer[:2] == ["DOCK","1"],
          f"{op} failed to return an original DOCK/1 receipt: {answer}")
    check(int(answer[2]) == session and int(answer[3]) == request,
          "dock response is from a different original session/request")
    check(int(answer[6]) > 0, "dock service omitted station identity")
    check(0 <= int(answer[7]) <= 16, "unbounded active workers")
    check(0 <= int(answer[15]) <= int(answer[7]) and 0 <= int(answer[16]) <= 4,
          "unverified dock qualification/throughput")
    return answer

def quit_native(child):
    child.stdin.write("QUIT\n")
    child.stdin.flush()
    check(child.wait(timeout=15) == 0,
          "native station child failed to close its original world cleanly")

def main(executable):
    with tempfile.TemporaryDirectory(prefix="sg_dock03_") as temp:
        db = pathlib.Path(temp) / "dock03_original.sqlite"
        child, session, gen, station = station_session(executable, db)
        try:
            status = command(child, "DOCKVIEW",session,1,gen,station)
            check(int(status[7]) == 0, "starter station created fictitious workers")
            gen = int(status[5])
            time.sleep(0.07)
            child.stdin.write(f"DOCKDESTS|{session}|2|{gen}|{station}\n")
            child.stdin.flush()
            possible=child.stdout.readline().strip().split("|")
            check(len(possible)>=8 and possible[:2]==["DDEST","1"] and
                  int(possible[2])==session and int(possible[3])==2,
                  "invalid original-world receiving colony list")
            check(int(possible[7])<=4 and
                  len(possible)==8+int(possible[7])*2,
                  "receiving station list unbounded or malformed")
            time.sleep(0.07)
            child.stdin.write(f"DOCKREGIONS|{session}|3|{gen}|1\n")
            child.stdin.flush()
            regional=child.stdout.readline().strip().split("|")
            check(len(regional)==14 and regional[:2]==["DREG","1"]
                  and int(regional[2])==session and int(regional[3])==3,
                  "cold-region source was not conservatively projected")
            hired = command(child,"DOCKASSIGN",session,4,gen,0)
            check(int(hired[7]) == 1 and int(hired[14]) == 1,
                  "original named ship crew did not physically staff the dock")
            check(int(hired[5]) > gen,"shore transfer lacked original STEP generation")
            gen = int(hired[5])
            released = command(child,"DOCKRELEASE",session,5,gen,0)
            check(int(released[7]) == 0 and int(released[14]) == 2,
                  "dock worker did not physically return to ship")
        finally:
            quit_native(child)
        with sqlite3.connect(db) as conn:
            move_count, distinct_people = conn.execute(
                "SELECT COUNT(*),COUNT(DISTINCT npc_id) FROM sg_dock_worker_moves"
            ).fetchone()
            check((move_count, distinct_people) == (2,1),
                  "ship-to-dock-to-ship did not preserve one unique named NPC")
        child, session, gen, station = station_session(executable, db)
        try:
            restored = command(child,"DOCKVIEW",session,1,gen,station)
            check(int(restored[7]) == 0 and int(restored[14]) == 0,
                  "restart invented an active dock shift")
        finally:
            quit_native(child)
    print("DOCK-03 native pipe assertions passed (original shift / return / restart)")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: Dock03NativeStationPipe.py <native_studio_host>")
    main(sys.argv[1])
