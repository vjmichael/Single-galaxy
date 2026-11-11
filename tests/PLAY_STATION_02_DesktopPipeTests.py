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
    # Two native hosts may never simultaneously own the same SQLite world.
    # The denial must precede opening the original PersistentWorld save.
    for competing_executable, args in (
        (station_exe, [save, '--explicit-test-station']),
        (cockpit_exe, [save])):
        contender = subprocess.Popen([str(competing_executable), *args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        stdout, stderr = contender.communicate(timeout=6)
        assert contender.returncode != 0 and 'native writer already active' in stderr, (stdout, stderr)
        assert not stdout, 'a rejected competing writer emitted native authority frames'
    # Kernel-released custody after abnormal termination: a process crash
    # cannot permanently lock the save or duplicate the existing owned ship.
    original_ship = initial[11]
    original_session = initial[2]
    p.kill()
    assert p.wait(timeout=5) != 0
    p, recovered = start_station()
    assert recovered[2] != original_session and recovered[6] == '4'
    assert recovered[11] == original_ship and recovered[9] == '1600', recovered
    initial = recovered
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
    initial_rock=cockpit.stdout.readline().strip().split('|')
    assert len(pose)==13 and pose[:2]==['POSE','1'] and pose[10]==str(ship),pose
    assert len(physical_ship)==17 and physical_ship[0]=='SHIP',physical_ship
    assert initial_rock[0] in ('ROCK','ROCKNONE') and initial_rock[1:4]==['1',pose[2],'0'],initial_rock
    assert initial_rock[4]==str(ship) and initial_rock[5:7]==physical_ship[7:9],initial_rock
    nav=cockpit.stdout.readline().strip().split('|')
    assert len(nav)==11 and nav[:5]==['NAV','1',pose[2],'0',str(ship)],nav
    assert nav[5:7]==physical_ship[7:9] and int(nav[7])>0,nav
    seat=send(cockpit, f'SEAT|{pose[2]}|1|0|1')
    assert seat[:2]==['ACK','1'] and seat[11:]==['1','1'],seat
    undock=send(cockpit, f'FLIGHT|{pose[2]}|2|1|UND')
    assert len(undock)==17 and undock[0]=='SHIP' and undock[5]=='0' and undock[16]=='0',undock
    rock_after_undock=cockpit.stdout.readline().strip().split('|')
    assert rock_after_undock[0] in ('ROCK','ROCKNONE') and rock_after_undock[1:4]==['1',pose[2],'2'],rock_after_undock
    assert rock_after_undock[4]==str(ship) and rock_after_undock[5:7]==undock[7:9],rock_after_undock
    nav_after=cockpit.stdout.readline().strip().split('|')
    assert len(nav_after)==11 and nav_after[:5]==['NAV','1',pose[2],'2',str(ship)],nav_after
    assert nav_after[5:7]==undock[7:9] and int(nav_after[7])>0,nav_after
    cockpit.stdin.write('QUIT\n');cockpit.stdin.flush()
    assert cockpit.wait(timeout=8)==0,cockpit.stderr.read()
    after, state=start_station()
    assert state[6]=='0' and state[8]==str(ship) and state[7]=='0',state
    assert send(after, f'EXIT|{state[2]}|1|{state[5]}|{initial[7]}')[:2]==['DENY','2'], 'Cannot exit while undocked'
    after.stdin.write('QUIT\n');after.stdin.flush();assert after.wait(timeout=8)==0,after.stderr.read()
print('PLAY-STATION-02 native pipe PASS: station spawn/walk/board, restart, real cockpit seat+undock, no exit in space')
