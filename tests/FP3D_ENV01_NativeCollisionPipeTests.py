#!/usr/bin/env python3
"""Real original SQLite->C++ pilot->solid asteroid->durable receipt test."""
import subprocess,sys,tempfile,time
from pathlib import Path
seed,host=map(Path,sys.argv[1:3])
assert seed.is_file() and host.is_file()
with tempfile.TemporaryDirectory() as folder:
    save=Path(folder)/"world.sqlite"
    made=subprocess.run([str(seed),str(save)],capture_output=True,text=True,timeout=20)
    assert made.returncode==0,(made.stdout,made.stderr)
    fixture=made.stdout.strip().split("|")
    assert fixture[0]=="04A-FIXTURE" and len(fixture)==5,fixture
    _,ship,rock,ore=map(int,fixture[1:])
    def start():
        p=subprocess.Popen([str(host),str(save)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
        pose=p.stdout.readline().strip().split("|")
        ship_state=p.stdout.readline().strip().split("|")
        frame=p.stdout.readline().strip().split("|")
        assert pose[:2]==["POSE","1"] and int(pose[10])==ship,pose
        assert ship_state[:2]==["SHIP","1"] and ship_state[2]==pose[2],ship_state
        assert frame[0] in ("ROCK","ROCKNONE") and len(frame) in (12,19) and frame[-2:]==["0","0"],frame
        nav=p.stdout.readline().strip().split("|")
        assert len(nav)==11 and nav[:5]==["NAV","1",pose[2],"0",str(ship)],nav
        assert nav[5:7]==ship_state[7:9] and int(nav[7])>0,nav
        return p,pose,ship_state,frame
    def cmd(p,command):
        time.sleep(.075)
        p.stdin.write(command+"\n");p.stdin.flush()
        response=p.stdout.readline().strip().split("|")
        assert response and response[0] in ("ACK","DENY","SHIP"),response
        contact=None
        if response[0]=="SHIP":
            contact=p.stdout.readline().strip().split("|")
            assert contact[0] in ("ROCK","ROCKNONE") and len(contact) in (12,19),contact
            assert contact[2:4]==response[2:4] and contact[5:7]==response[7:9],(response,contact)
            nav=p.stdout.readline().strip().split("|")
            assert len(nav)==11 and nav[:5]==["NAV","1",response[2],response[3],str(ship)],nav
            assert nav[5:7]==response[7:9] and int(nav[7])>0,nav
            assert sum(int(v)**2 for v in nav[8:11])<=1000000000**2,nav
        return response,contact
    child,pose,ship0,frame0=start()
    session=pose[2]
    seated,_=cmd(child,f"SEAT|{session}|1|0|1")
    assert seated[0]=="ACK" and seated[11:]==["1","1"],seated
    undock,contact=cmd(child,f"FLIGHT|{session}|2|1|UND")
    assert undock[0]=="SHIP" and undock[5]=="0" and undock[16]=="0",undock
    assert contact[0]=="ROCK" and int(contact[7])==rock and int(contact[8])==ore and contact[-2:]==["0","0"],contact
    x=int(undock[9]);hit=False
    for req in range(3,75):
        vessel,contact=cmd(child,f"FLIGHT|{session}|{req}|1|FWD")
        assert vessel[0]=="SHIP" and vessel[5]=="0",(req,vessel)
        assert contact[0]=="ROCK" and int(contact[8])==ore,(req,contact)
        assert int(vessel[9])>=x,(req,vessel,x)
        x=int(vessel[9])
        if int(contact[-2])==rock:
            assert int(contact[-1])==int(vessel[7]) and int(contact[-1])>0,contact
            assert list(map(int,vessel[12:15]))==[0,0,0],vessel
            assert int(contact[9])>=11000 and x<14000,(vessel,contact)
            hit=True;break
        assert contact[-2:]==["0","0"],contact
    assert hit,"failed to receive native solid-contact event"
    replay,other=cmd(child,f"FLIGHT|{session}|{req}|1|FWD")
    assert replay[0]=="DENY" and other is None,replay
    child.stdin.write("QUIT\n");child.stdin.flush()
    assert child.wait(timeout=10)==0,child.stderr.read()
    child2,pose2,vessel2,contact2=start()
    assert pose2[2]!=session and int(vessel2[9])==x,vessel2
    assert int(contact2[8])==ore and contact2[-2:]==["0","0"],contact2
    child2.stdin.write("QUIT\n");child2.stdin.flush()
    assert child2.wait(timeout=10)==0,child2.stderr.read()
print("FP3D-ENV-01 true SQLite child PASS: collision event/tick, stopped hull, finite ore, anti-replay and restart")
