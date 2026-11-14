#!/usr/bin/env python3
"""INT-02: real original C++ host pipe and conserved SQLite mining, modern v126+.
The bundled finite source fixture is the ONLY origin of stock. No Godot,
mock transport, synthetic ore writer, or acceptance claim from parsing alone.
"""
import subprocess
import sys
import tempfile
import time
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit("usage: test.py ORIGINAL_NATIVE_SEED ORIGINAL_PLAYER_HOST")
seed, host = (Path(x).resolve() for x in sys.argv[1:])
assert seed.is_file() and host.is_file(), "original native binaries missing"


def frame(child):
    text = child.stdout.readline()
    assert text, f"original host pipe closed: {child.poll()}"
    value = text.strip().split("|")
    assert len(value) >= 2 and value[1] == "1", value
    return value


def ship_bundle(child, expected_session, expected_request):
    # The modern original host emits one atomic native-world observation as
    # SHIP, ONBOARD, ROCK/ROCKNONE, NAV/NAVNONE. The older fork mistakenly
    # treated SHIP -> ROCK as consecutive, dropping the material source.
    ship = frame(child)
    onboard = frame(child)
    rock = frame(child)
    nav = frame(child)
    assert ship[0] == "SHIP" and len(ship) == 17, ship
    assert onboard[0] == "ONBOARD" and len(onboard) == 17, onboard
    assert rock[0] in ("ROCK", "ROCKNONE"), rock
    assert nav[0] in ("NAV", "NAVNONE"), nav
    for entry in (ship, onboard, rock, nav):
        assert int(entry[2]) == expected_session, entry
        assert int(entry[3]) == expected_request, entry
    assert int(ship[6]) == int(onboard[4]) == int(rock[4]) == int(nav[4])
    assert int(ship[7]) == int(onboard[5]) == int(rock[5]) == int(nav[5])
    assert int(ship[8]) == int(onboard[6]) == int(rock[6]) == int(nav[6])
    assert len(rock) in ((17, 19) if rock[0] == "ROCK" else (10, 12)), rock
    return ship, onboard, rock, nav


with tempfile.TemporaryDirectory(prefix="sg_int02_original_mining_") as directory:
    db = Path(directory) / "one_original_world.sqlite"
    made = subprocess.run([str(seed), str(db)], text=True, capture_output=True, timeout=30)
    assert made.returncode == 0, (made.stdout, made.stderr)
    identity = made.stdout.strip().split("|")
    assert len(identity) == 5 and identity[0] == "04B-FIXTURE", identity
    station, ship_id, rock_id, original_ore = map(int, identity[1:])
    assert min(station, ship_id, rock_id, original_ore) > 0
    assert db.is_file()

    def start():
        child = subprocess.Popen(
            [str(host), str(db)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1,
        )
        pose = frame(child)
        assert pose[0] == "POSE" and len(pose) == 13, pose
        session = int(pose[2])
        ship, stock, contact, nav = ship_bundle(child, session, 0)
        assert int(pose[10]) == ship_id and int(ship[6]) == ship_id
        assert int(ship[16]) in (0, 1, 2)
        return child, session, pose, ship, stock, contact

    def submit(child, session, request, verb, seat, value):
        # The native player host enforces a finite monotonic session sequence.
        # Separate commands by its real cooldown rather than modifying a clock.
        time.sleep(0.075)
        child.stdin.write(f"{verb}|{session}|{request}|{seat}|{value}\n")
        child.stdin.flush()
        response = frame(child)
        assert response[0] in ("ACK", "SHIP", "DENY"), response
        if response[0] == "SHIP":
            # SHIP is the FIRST frame; this bundle has already consumed it.
            stock, rock, nav = frame(child), frame(child), frame(child)
            assert stock[0] == "ONBOARD" and len(stock) == 17, stock
            assert rock[0] in ("ROCK", "ROCKNONE"), rock
            assert nav[0] in ("NAV", "NAVNONE"), nav
            for entry in (stock, rock, nav):
                assert int(entry[2]) == session and int(entry[3]) == request, entry
                assert int(entry[4]) == int(response[6]), entry
                assert int(entry[5]) == int(response[7]), entry
                assert int(entry[6]) == int(response[8]), entry
            return response, stock, rock
        return response, None, None

    child, session, pose, state, stock, contact = start()
    assert int(state[16]) == 1, state  # prebuilt original docked ship
    assert contact[0] == "ROCKNONE" and int(contact[9]) == 0, contact
    response, _, _ = submit(child, session, 1, "MINE", 0, rock_id)
    assert response[0] == "DENY", response  # no player seat
    response, _, _ = submit(child, session, 2, "SEAT", 0, 1)
    assert response[0] == "ACK" and response[-2:] == ["1", "1"], response
    response, _, _ = submit(child, session, 3, "FIT", 1, 1)
    assert response[0] == "SHIP" and response[5] != "0", response
    response, _, _ = submit(child, session, 4, "FAB", 1, 1)
    assert response[0] == "SHIP" and response[5] == "0", response
    response, stock, rock = submit(child, session, 5, "FIT", 1, 1)
    assert response[0] == "SHIP" and response[5] == "0", response
    assert rock[0] == "ROCKNONE" and int(rock[9]) == 1, rock
    response, _, _ = submit(child, session, 6, "MINE", 1, rock_id)
    assert response[0] == "DENY", response  # native dock permission, no visual mining
    response, _, contact = submit(child, session, 7, "FLIGHT", 1, "UND")
    assert response[0] == "SHIP" and response[5] == "0" and response[16] == "0", response
    assert contact[0] == "ROCK" and int(contact[7]) == rock_id
    assert int(contact[8]) == original_ore
    response, _, contact = submit(child, session, 8, "MINE", 1, rock_id)
    assert response[0] == "SHIP" and response[5] != "0", response  # physically out of range
    request = 8
    for _ in range(10):
        request += 1
        response, _, contact = submit(child, session, request, "FLIGHT", 1, "FWD")
        assert response[0] == "SHIP" and response[5] == "0", response
        assert contact[0] == "ROCK", contact
    assert 0 < abs(int(contact[9])) < 15000, contact
    request += 1
    response, stock, contact = submit(child, session, request, "MINE", 1, rock_id)
    assert response[0] == "SHIP" and response[5] == "0", response
    assert contact[0] == "ROCK" and int(contact[8]) == original_ore - 1, contact
    assert int(contact[14]) == 1 and int(stock[7]) == 1, (contact, stock)
    mined_request = request
    response, _, _ = submit(child, session, mined_request, "MINE", 1, rock_id)
    assert response[0] == "DENY", response  # immutable exact request replay
    request += 1
    response, _, contact = submit(child, session, request, "POWER", 1, 0)
    assert response[0] == "SHIP" and response[5] == "0", response
    assert contact[-3 if len(contact) == 19 else -1] == "0", contact
    request += 1
    response, _, contact = submit(child, session, request, "MINE", 1, rock_id)
    assert response[0] == "SHIP" and response[5] != "0", response
    assert int(contact[8]) == original_ore - 1, contact
    child.stdin.write("QUIT\n")
    child.stdin.flush()
    assert child.wait(timeout=15) == 0, child.stderr.read()
    child, next_session, pose, state, stock, contact = start()
    assert next_session != session, "host replayed prior one-use session"
    assert contact[0] == "ROCK" and int(contact[8]) == original_ore - 1, contact
    assert int(contact[14]) == 1 and int(contact[16]) == 0, contact
    response, _, _ = submit(child, next_session, 1, "MINE", 1, rock_id)
    assert response[0] == "DENY", response  # old seat/capability untrusted
    response, _, _ = submit(child, session, 2, "MINE", 1, rock_id)
    assert response[0] == "DENY", response  # stale session after genuine restart
    child.stdin.write("QUIT\n")
    child.stdin.flush()
    assert child.wait(timeout=15) == 0, child.stderr.read()
print("INT02 original mining pipe: finite manufacture, fit, native travel, one-tonne receipt, replay and SQLite restart PASS")
