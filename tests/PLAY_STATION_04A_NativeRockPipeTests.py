#!/usr/bin/env python3
"""Real OS child stdio + original SQLite asteroid contact, no client oracle."""
from pathlib import Path
import subprocess,sys,tempfile,time
seed_exe,host=map(Path,sys.argv[1:3])
assert seed_exe.is_file() and host.is_file()
with tempfile.TemporaryDirectory() as directory:
    db=Path(directory)/"original.sqlite"
    seeded=subprocess.run([str(seed_exe),str(db)],capture_output=True,text=True,timeout=15)
    assert seeded.returncode==0,(seeded.stdout,seeded.stderr)
    fields=seeded.stdout.strip().split("|")
    assert fields[0]=="04A-FIXTURE" and len(fields)==5,fields
    ship,rock,ore=map(int,fields[2:5])
    def start():
        child=subprocess.Popen([str(host),str(db)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,text=True,bufsize=1)
        pose=child.stdout.readline().strip().split("|")
        original_ship=child.stdout.readline().strip().split("|")
        contact=child.stdout.readline().strip().split("|")
        assert pose[:2]==["POSE","1"] and int(pose[10])==ship,pose
        assert original_ship[:2]==["SHIP","1"] and original_ship[2]==pose[2],original_ship
        assert contact[:2]==["ROCKNONE","1"] if original_ship[16]!="0" else contact[:2]==["ROCK","1"],contact
        return child,pose,original_ship,contact
    def cmd(p,command,session,req):
        time.sleep(.065)
        p.stdin.write(command+"\n");p.stdin.flush()
        primary=p.stdout.readline().strip().split("|")
        assert primary and primary[0],primary
        contact=None
        if primary[0]=="SHIP":
            contact=p.stdout.readline().strip().split("|")
            assert contact[0] in ("ROCK","ROCKNONE") and contact[2]==session and int(contact[3])==req,contact
            assert int(contact[5])==int(primary[7]) and int(contact[6])==int(primary[8]),contact
        return primary,contact
    child,pose,initial,none=start()
    session=pose[2]
    assert none[0]=="ROCKNONE" and int(none[7])==0 and int(none[8])==24,none
    denied,extra=cmd(child,f"FLIGHT|{session}|1|0|FWD",session,1)
    assert denied[:2]==["DENY","1"] and extra is None,denied
    seat,extra=cmd(child,f"SEAT|{session}|2|0|1",session,2)
    assert seat[0]=="ACK" and seat[11:]==["1","1"] and extra is None,seat
    undock,rock_contact=cmd(child,f"FLIGHT|{session}|3|1|UND",session,3)
    assert undock[0]=="SHIP" and undock[16]=="0",undock
    assert rock_contact[0]=="ROCK" and int(rock_contact[7])==rock and int(rock_contact[8])==ore,rock_contact
    assert int(rock_contact[9])==25000 and int(rock_contact[12])==10000,rock_contact
    assert int(rock_contact[13])==2 and int(rock_contact[16])==0,rock_contact # NO free mining module
    moved,after_move=cmd(child,f"FLIGHT|{session}|4|1|FWD",session,4)
    assert moved[0]=="SHIP" and int(moved[12])>0,moved
    assert after_move[0]=="ROCK" and int(after_move[7])==rock and int(after_move[8])==ore,after_move
    assert int(after_move[9])<int(rock_contact[9]),(after_move,rock_contact)
    child.stdin.write("QUIT\n");child.stdin.flush()
    assert child.wait(timeout=6)==0,child.stderr.read()
    child2,pose2,ship2,rock2=start()
    assert pose2[2]!=session and rock2[0]=="ROCK" and int(rock2[8])==ore,rock2
    child2.stdin.write("QUIT\n");child2.stdin.flush()
    assert child2.wait(timeout=6)==0,child2.stderr.read()
print("PLAY-STATION-04A native pipe PASS: original seeded asteroid, no docked ore oracle, seated undock, live 3D offset change, no gifted mining tool or ore, SQLite restart")
