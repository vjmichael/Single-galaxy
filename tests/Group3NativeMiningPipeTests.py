#!/usr/bin/env python3
"""True C++ child stdio + original SQLite mining, not a client-side oracle."""
import subprocess,sys,tempfile,time
from pathlib import Path
seed,host=map(Path,sys.argv[1:3])
assert seed.is_file() and host.is_file()
with tempfile.TemporaryDirectory() as folder:
    db=Path(folder)/"world.sqlite"
    made=subprocess.run([str(seed),str(db)],text=True,capture_output=True,timeout=25)
    assert made.returncode==0,(made.stdout,made.stderr)
    identity=made.stdout.strip().split("|")
    assert identity[0]=="04B-FIXTURE" and len(identity)==5,identity
    station,ship,rock,ore=map(int,identity[1:])
    assert ore>50
    def start():
        child=subprocess.Popen([str(host),str(db)],stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                               text=True,bufsize=1)
        pose=child.stdout.readline().strip().split("|")
        state=child.stdout.readline().strip().split("|")
        contact=child.stdout.readline().strip().split("|")
        nav=child.stdout.readline().strip().split("|")
        assert len(nav)==11 and nav[:5]==["NAV","1",pose[2],"0",str(ship)],nav
        assert nav[5:7]==state[7:9] and int(nav[7])==station,nav
        assert pose[:2]==["POSE","1"] and int(pose[10])==ship,pose
        assert state[:2]==["SHIP","1"] and state[2]==pose[2],state
        assert contact[0] in ("ROCK","ROCKNONE"),contact
        return child,pose,state,contact
    def send(child,command,session,req):
        time.sleep(.07)
        child.stdin.write(command+"\n");child.stdin.flush()
        result=child.stdout.readline().strip().split("|")
        assert result[0] in ("ACK","SHIP","DENY"),result
        contact=None
        if result[0]=="SHIP":
            contact=child.stdout.readline().strip().split("|")
            assert contact[0] in ("ROCK","ROCKNONE"),contact
            assert contact[2]==session and int(contact[3])==req,contact
            assert int(contact[5])==int(result[7]) and int(contact[6])==int(result[8])
            nav=child.stdout.readline().strip().split("|")
            assert len(nav)==11 and nav[:5]==["NAV","1",session,str(req),str(ship)],nav
            assert nav[5:7]==result[7:9] and int(nav[7])==station,nav
        return result,contact
    child,pose,state,contact=start()
    session=pose[2]
    assert state[16]=="1" and contact[0]=="ROCKNONE"
    assert int(contact[7])==0 and int(contact[9])==0
    result,_=send(child,f"MINE|{session}|1|0|{rock}",session,1)
    assert result[0]=="DENY"
    result,_=send(child,f"SEAT|{session}|2|0|1",session,2)
    assert result[0]=="ACK" and result[11:]==["1","1"],result
    result,_=send(child,f"FIT|{session}|3|1|1",session,3)
    assert result[0]=="SHIP" and result[5]!="0",result
    result,_=send(child,f"FAB|{session}|4|1|1",session,4)
    assert result[0]=="SHIP" and result[5]=="0",result
    result,contact=send(child,f"FIT|{session}|5|1|1",session,5)
    assert result[0]=="SHIP" and result[5]=="0" and contact[9]=="1",(result,contact)
    result,_=send(child,f"MINE|{session}|6|1|{rock}",session,6)
    assert result[0]=="DENY",result  # docked: no authentic contact
    result,contact=send(child,f"FLIGHT|{session}|7|1|UND",session,7)
    assert result[0]=="SHIP" and result[5]=="0" and result[16]=="0",result
    assert contact[0]=="ROCK" and int(contact[7])==rock and int(contact[8])==ore,contact
    # PLAY-11: original 50m geology requires its own separately fitted
    # powered scanner. Mining drill type 4 is NOT scanner type 1, and the
    # Godot 8.5km render clip grants no sensor or invented survey row.
    result,_=send(child,f"SURVEY|{session}|8|1|1",session,8)
    assert result[0]=="DENY" and result[3]=="no-powered-original-survey-scanner",result
    result,contact=send(child,f"MINE|{session}|9|1|{rock}",session,9)
    assert result[0]=="SHIP" and result[5]!="0" and int(contact[8])==ore,(result,contact)
    req=9
    for _ in range(10):
        req+=1
        result,contact=send(child,f"FLIGHT|{session}|{req}|1|FWD",session,req)
        assert result[0]=="SHIP" and result[5]=="0" and contact[0]=="ROCK",(result,contact)
    assert int(contact[9])<15000,contact
    req+=1
    result,contact=send(child,f"MINE|{session}|{req}|1|{rock}",session,req)
    assert result[0]=="SHIP" and result[5]=="0" and contact[0]=="ROCK",(result,contact)
    assert int(contact[8])==ore-1 and int(contact[14])==1,contact
    mined_req=req
    result,_=send(child,f"MINE|{session}|{mined_req}|1|{rock}",session,mined_req)
    assert result[0]=="DENY",result
    req+=1
    result,contact=send(child,f"POWER|{session}|{req}|1|0",session,req)
    assert result[0]=="SHIP" and result[5]=="0" and contact[16]=="0",(result,contact)
    req+=1
    result,contact=send(child,f"MINE|{session}|{req}|1|{rock}",session,req)
    assert result[0]=="SHIP" and result[5]!="0" and int(contact[8])==ore-1,(result,contact)
    child.stdin.write("QUIT\n");child.stdin.flush()
    assert child.wait(timeout=10)==0,child.stderr.read()
    child2,pose2,state2,contact2=start()
    assert pose2[2]!=session and state2[16]=="0" and contact2[0]=="ROCK",(state2,contact2)
    assert int(contact2[8])==ore-1 and int(contact2[14])==1 and contact2[16]=="0",contact2
    result,_=send(child2,f"MINE|{session}|1|1|{rock}",pose2[2],1)
    assert result[0]=="DENY",result
    child2.stdin.write("QUIT\n");child2.stdin.flush()
    assert child2.wait(timeout=10)==0,child2.stderr.read()
print("PLAY-STATION-04B original native pipe PASS: FAB/FIT/UND, approach, exact 1t transfer, power-off, replay, SQLite restart")
