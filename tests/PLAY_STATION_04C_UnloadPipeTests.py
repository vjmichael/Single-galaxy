#!/usr/bin/env python3
"""PLAY-STATION-04C: real native owner/dock/ore custody through SQLite."""
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import time

fixture, host = map(Path, sys.argv[1:3])
assert fixture.is_file() and host.is_file()

with tempfile.TemporaryDirectory() as folder:
    db = str(Path(folder) / "world.sqlite")
    made = subprocess.run(
        [str(fixture), db, "--04c-fixture"],
        capture_output=True, text=True, timeout=50)
    assert made.returncode == 0, (made.stdout, made.stderr)
    line = next((s for s in made.stdout.splitlines()
                 if s.startswith("UNLOAD_FIXTURE|")), None)
    assert line is not None, made.stdout
    _, station, lot, ship, mass = line.split("|")
    station, lot, ship, mass = map(int, (station, lot, ship, mass))

    def launch():
        process = subprocess.Popen(
            [str(host), db, "--explicit-test-station"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1)
        person = process.stdout.readline().strip().split("|")
        assert person[:2] == ["PERSON", "2"] and person[6] == "4", person
        assert int(person[7]) == station and int(person[11]) == ship, person
        return process, person

    process, first = launch()
    session, generation = map(int, (first[2], first[5]))
    req = 0

    def raw(command):
        time.sleep(.065)  # Original host enforces a real 45ms command window.
        process.stdin.write(command + "\n")
        process.stdin.flush()
        frame = process.stdout.readline().strip().split("|")
        assert frame and frame[0], command
        return frame

    def snapshot(verb, number, gen, target):
        header = raw(f"{verb}|{session}|{number}|{gen}|{target}")
        assert header[:2] == ["REFHEAD", "3"] and len(header) == 18, header
        assert int(header[6]) == station, header
        lot_count = int(header[15])
        mineral_count = int(header[17])
        rows = [process.stdout.readline().strip().split("|")
                for _ in range(lot_count + mineral_count)]
        tail = process.stdout.readline().strip().split("|")
        assert tail[:2] == ["REFEND", "3"] and len(tail) == 10, tail
        assert [int(x) for x in tail[4:8]] == [
            int(header[4]), int(header[5]), lot_count, mineral_count], (header, tail)
        assert all(row[2:4] == header[2:4] for row in rows), rows
        return header, rows, tail

    req += 1
    distant = raw(f"UNLOAD|{session}|{req}|{generation}|{ship}")
    assert distant[:4] == ["DENY", "2", str(req),
                           "refinery-terminal-distance-or-custody"], distant
    # Move an actual station resident from 1600mm to the terminal's 1200mm
    # interaction range. No Godot teleport or invented station custody.
    assert int(first[9]) == 1600, first
    for _ in range(8):
        req += 1
        moved = raw(f"WALK|{session}|{req}|{generation}|100|0")
        assert moved[:2] == ["PERSON", "2"] and moved[6] == "4", moved
        generation = int(moved[5])
    assert int(moved[9]) == 2400, moved

    req += 1
    foreign = raw(f"UNLOAD|{session}|{req}|{generation}|{ship + 99999}")
    assert foreign[:4] == ["DENY", "2", str(req),
                            "no-owned-docked-ship-with-ore"], foreign
    req += 1
    stale = raw(f"UNLOAD|{session}|{req}|{generation - 1}|{ship}")
    assert stale[:4] == ["DENY", "2", str(req), "stale-world"], stale

    req += 1
    unloaded_head, unloaded_rows, unloaded = snapshot(
        "UNLOAD", req, generation, ship)
    assert unloaded[8:10] == ["3", "1"], unloaded  # exactly one tonne
    assert int(unloaded_head[13]) == 1, unloaded_head
    assert any(row[0] == "REFLOT" and int(row[4]) == lot and
               int(row[7]) == 1 for row in unloaded_rows), unloaded_rows
    generation = int(unloaded[5])

    replay = raw(f"UNLOAD|{session}|{req}|{generation}|{ship}")
    assert replay[:4] == ["DENY", "2", str(req), "replay"], replay
    req += 1
    empty = raw(f"UNLOAD|{session}|{req}|{generation}|{ship}")
    assert empty[:4] == ["DENY", "2", str(req),
                          "no-owned-docked-ship-with-ore"], empty

    req += 1
    refined_head, refined_rows, refined = snapshot(
        "REFINE", req, generation, lot)
    assert refined[8:10] == ["1", "1000"], refined
    assert int(refined_head[13]) == 0 and int(refined_head[14]) == 1000, refined_head
    products = {row[4]: int(row[5]) for row in refined_rows if row[0] == "REFMIN"}
    assert products == {"nickel_iron": 860, "troilite": 80, "olivine": 30,
                        "pyroxene": 15, "other": 15}, products

    process.stdin.write("QUIT\n")
    process.stdin.flush()
    assert process.wait(timeout=10) == 0, process.stderr.read()

    verified = subprocess.run(
        [str(fixture), db, "--03c-verify"],
        capture_output=True, text=True, timeout=45)
    assert verified.returncode == 0, (verified.stdout, verified.stderr)
    with sqlite3.connect(db) as saved:
        assert saved.execute("PRAGMA integrity_check").fetchone()[0] == "ok"

    process, first = launch()
    session, generation = map(int, (first[2], first[5]))
    assert int(first[9]) == 2400, first
    final_head, final_rows, final = snapshot("REFVIEW", 1, generation, station)
    assert int(final_head[13]) == 0 and int(final_head[14]) == 1000, final_head
    assert final[8:10] == ["0", "0"], final
    process.stdin.write("QUIT\n")
    process.stdin.flush()
    assert process.wait(timeout=10) == 0, process.stderr.read()

print("PLAY-STATION-04C: real geological 1t -> docked ship -> original UNLOAD "
      "-> sourced station lot -> 1000kg refinery, denied remote/foreign/stale/"
      "replay/empty, SQLite integrity and restart PASS")
