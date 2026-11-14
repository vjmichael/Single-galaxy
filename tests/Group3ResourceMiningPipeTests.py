#!/usr/bin/env python3
"""Authenticated original-world ore collection through a genuine native pilot."""
from pathlib import Path
import subprocess,sys,tempfile,time
from contextlib import ExitStack
seed,host=map(Path,sys.argv[1:3])
assert seed.is_file() and host.is_file()
with tempfile.TemporaryDirectory() as folder, ExitStack() as owned:
    db=Path(folder)/"world.sqlite"
    built=subprocess.run([str(seed),str(db)],capture_output=True,text=True,timeout=25)
    assert built.returncode==0,(built.stdout,built.stderr)
    identity=built.stdout.strip().split("|")
    assert len(identity)==5 and identity[0]=="GROUP3-ORE-FIXTURE",identity
    station,ship,rock,ore=map(int,identity[1:])
    assert ore>1
    def stop_owned(child):
        if child.poll() is None:
            child.kill()
        child.wait(timeout=10)
        for stream in (child.stdin,child.stdout,child.stderr):
            if stream is not None: stream.close()
    def read_onboard(child,session,req,ship):
        row=child.stdout.readline().strip().split("|")
        assert len(row)==17 and row[:5]==["ONBOARD","1",session,str(req),str(ship)],row
        return row
    def start():
        c=subprocess.Popen([str(host),str(db)],stdin=subprocess.PIPE,
           stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
        owned.callback(stop_owned,c)
        pose=c.stdout.readline().strip().split("|")
        state=c.stdout.readline().strip().split("|")
        onboard=read_onboard(c,pose[2],0,ship)
        assert onboard[5:7]==state[7:9],onboard
        contact=c.stdout.readline().strip().split("|")
        nav=c.stdout.readline().strip().split("|")
        assert pose[:2]==["POSE","1"] and int(pose[10])==ship,pose
        assert len(state)==17 and state[:2]==["SHIP","1"],state
        assert contact[0] in ("ROCK","ROCKNONE"),contact
        assert len(nav)==11 and nav[:5]==["NAV","1",pose[2],"0",str(ship)],nav
        return c,pose,state,contact
    def ask(c,query,session,request):
        time.sleep(.07)
        c.stdin.write(query+"\n");c.stdin.flush()
        result=c.stdout.readline().strip().split("|")
        assert result[0] in ("ACK","SHIP","DENY"),(query,result)
        rock_state=None
        if result[0]=="SHIP":
            onboard=read_onboard(c,session,request,ship)
            assert onboard[5:7]==result[7:9],onboard
            rock_state=c.stdout.readline().strip().split("|")
            nav=c.stdout.readline().strip().split("|")
            assert rock_state[0] in ("ROCK","ROCKNONE"),rock_state
            assert rock_state[2]==session and int(rock_state[3])==request
            assert nav[:5]==["NAV","1",session,str(request),str(ship)],nav
            assert nav[5:7]==result[7:9],(result,nav)
        return result,rock_state
    c,pose,state,contact=start()
    session=pose[2]
    assert contact[0]=="ROCKNONE"
    result,_=ask(c,f"MINE|{session}|1|0|{rock}",session,1)
    assert result[0]=="DENY",result
    result,_=ask(c,f"SEAT|{session}|2|0|1",session,2)
    assert result[0]=="ACK" and result[11:]==["1","1"],result
    result,_=ask(c,f"SURVEY|{session}|3|1|1",session,3)
    assert result[:4]==["DENY","1","3","no-powered-original-survey-scanner"],result
    result,contact=ask(c,f"FLIGHT|{session}|4|1|UND",session,4)
    assert result[0]=="SHIP" and result[5]=="0" and result[16]=="0",result
    assert contact[0]=="ROCK" and int(contact[7])==rock,contact
    req=4
    # Original native 3D movement is finite per verified flight tick.
    # The historic 10th FWD ends 15,202mm away, outside the true 15m
    # mining cutoff. Follow only real decreasing ROCK distance receipts.
    previous_range=int(contact[9])
    assert previous_range>=15000 and contact[0]=="ROCK",contact
    completed_approaches=0
    for _ in range(16):
        if previous_range<15000: break
        req+=1
        result,contact=ask(c,f"FLIGHT|{session}|{req}|1|FWD",session,req)
        assert result[0]=="SHIP" and result[5]=="0" and contact[0]=="ROCK",(result,contact)
        next_range=int(contact[9])
        assert 0<=next_range<previous_range,(previous_range,contact)
        previous_range=next_range
        completed_approaches+=1
    assert completed_approaches>=1 and previous_range<15000,contact
    req+=1
    result,contact=ask(c,f"MINE|{session}|{req}|1|{rock}",session,req)
    assert result[0]=="SHIP" and result[5]=="0",(result,contact)
    assert contact[0]=="ROCK" and int(contact[8])==ore-1,contact
    assert int(contact[14])==1,contact
    mined_req=req
    result,_=ask(c,f"MINE|{session}|{mined_req}|1|{rock}",session,mined_req)
    assert result[0]=="DENY",result
    c.stdin.write("QUIT\n");c.stdin.flush()
    assert c.wait(timeout=10)==0,c.stderr.read()
    c2,pose2,state2,contact2=start()
    assert pose2[2]!=session and contact2[0]=="ROCK",contact2
    assert int(contact2[8])==ore-1 and int(contact2[14])==1,contact2
    result,_=ask(c2,f"MINE|{session}|1|1|{rock}",pose2[2],1)
    assert result[0]=="DENY",result
    c2.stdin.write("QUIT\n");c2.stdin.flush()
    assert c2.wait(timeout=10)==0,c2.stderr.read()
print("GROUP3 ORIGINAL ORE PIPE PASS: no prototype module, physical flight, exact 1t ROCK->SHIP, replay denial, SQLite restart")
