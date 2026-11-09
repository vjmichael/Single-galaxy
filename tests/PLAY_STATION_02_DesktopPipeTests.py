"""Executes the REAL native station child and hands its save to the cockpit host."""
from pathlib import Path
import subprocess, sys, tempfile, time
station_exe, cockpit_exe = map(Path, sys.argv[1:])
assert station_exe.is_file() and cockpit_exe.is_file()
with tempfile.TemporaryDirectory() as root:
    save = str(Path(root) / 'station.sqlite')
    def start_station():
        p = subprocess.Popen([str(station_exe), save, '--explicit-test-station'],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, bufsize=1)
        first = p.stdout.readline().strip().split('|')
        assert len(first) == 13 and first[:2] == ['PERSON', '2'], first
        return p, first
    def send(p, line):
        time.sleep(.055)
        p.stdin.write(line + '\n');p.stdin.flush()
        reply=p.stdout.readline().strip().split('|')
        assert reply, line
        return reply
    p, initial = start_station()
    session = initial[2]
    generation = int(initial[5]); ship = int(initial[11])
    assert initial[6] == '4' and initial[8] == '0' and initial[9] == '1600', initial
    assert ship > 0, 'real existing owned docked ship must be discoverable'
    assert send(p, f'BOARD|{session}|1|{generation}|{ship}')[:4] == [
        'DENY', '2', '1', 'station-custody-or-distance']
    for req in range(2, 12):
        reply=send(p, f'WALK|{session}|{req}|{generation}|-100|0')
        assert reply[0]=='PERSON' and reply[6]=='4',reply
        generation=int(reply[5])
    assert reply[9]=='600',reply
    assert send(p, f'BOARD|{session}|12|{generation}|0')[:2]==['DENY','2']
    boarded=send(p, f'BOARD|{session}|13|{generation}|{ship}')
    assert boarded[0]=='PERSON' and boarded[6]=='0' and int(boarded[8])==ship,boarded
    p.stdin.write('QUIT\n');p.stdin.flush();assert p.wait(timeout=8)==0,p.stderr.read()
    restarted, person = start_station()
    assert person[2]!=session and person[6]=='0' and person[8]==str(ship),person
    restarted.stdin.write('QUIT\n');restarted.stdin.flush()
    assert restarted.wait(timeout=8)==0,restarted.stderr.read()
    # Actual original cockpit host (not a mock) must now admit the boarding receipt.
    cockpit=subprocess.Popen([str(cockpit_exe), save], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True,bufsize=1)
    pose=cockpit.stdout.readline().strip().split('|')
    physical_ship=cockpit.stdout.readline().strip().split('|')
    assert len(pose)==13 and pose[:2]==['POSE','1'] and pose[10]==str(ship),pose
    assert len(physical_ship)==17 and physical_ship[0]=='SHIP',physical_ship
    seat=send(cockpit, f'SEAT|{pose[2]}|1|0|1')
    assert seat[:2]==['ACK','1'] and seat[11:]==['1','1'],seat
    undock=send(cockpit, f'FLIGHT|{pose[2]}|2|1|UND')
    assert len(undock)==17 and undock[0]=='SHIP' and undock[5]=='0' and undock[16]=='0',undock
    cockpit.stdin.write('QUIT\n');cockpit.stdin.flush()
    assert cockpit.wait(timeout=8)==0,cockpit.stderr.read()
    after, state=start_station()
    assert state[6]=='0' and state[8]==str(ship) and state[7]=='0',state
    assert send(after, f'EXIT|{state[2]}|1|{state[5]}|{initial[7]}')[:2]==['DENY','2'], 'Cannot exit while undocked'
    after.stdin.write('QUIT\n');after.stdin.flush();assert after.wait(timeout=8)==0,after.stderr.read()
print('PLAY-STATION-02 native pipe PASS: station spawn/walk/board, restart, real cockpit seat+undock, no exit in space')
