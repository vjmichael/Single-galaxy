"""Real child-stdio test against the original C++ flight host (Python is test-only)."""
import sqlite3,subprocess,sys,tempfile,time
from pathlib import Path
from contextlib import closing

exe=Path(sys.argv[1]);assert exe.is_file(),exe
with tempfile.TemporaryDirectory() as temp:
    save=Path(temp)/"cockpit-flight.sqlite"
    def open_host():
        p=subprocess.Popen([str(exe),str(save)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,text=True,bufsize=1)
        pose=p.stdout.readline().strip().split("|")
        ship=p.stdout.readline().strip().split("|")
        rock=p.stdout.readline().strip().split("|")
        assert rock[0] in ("ROCK","ROCKNONE") and rock[1:4]==["1",pose[2],"0"],rock
        assert rock[4]==pose[10] and rock[5]==ship[7] and rock[6]==ship[8],rock
        assert len(pose)==13 and pose[:2]==["POSE","1"],pose
        assert len(ship)==17 and ship[:3]==["SHIP","1",pose[2]],ship
        assert ship[3:5]==["0","0"] and ship[6]==pose[10],ship
        return p,pose,ship
    def send(p,msg):
        time.sleep(.075)
        p.stdin.write(msg+"\n");p.stdin.flush()
        result=p.stdout.readline().strip().split("|")
        assert result and result[0],result
        if result[0]=="SHIP":
            rock=p.stdout.readline().strip().split("|")
            assert rock[0] in ("ROCK","ROCKNONE") and rock[1:4]==["1",result[2],result[3]],rock
            assert rock[4]==result[6] and rock[5]==result[7] and rock[6]==result[8],rock
        return result
    p,initial,initial_ship=open_host()
    epoch=initial[2]
    assert send(p,f"FLIGHT|{epoch}|1|0|FWD")==["DENY","1","1","not-seated-or-stale-seat"]
    seat=send(p,f"SEAT|{epoch}|2|0|1")
    assert seat[0]=="ACK" and seat[11:]==["1","1"],seat
    assert send(p,f"FLIGHT|{epoch}|3|0|UND")==["DENY","1","3","not-seated-or-stale-seat"]
    assert send(p,f"FLIGHT|{epoch}|4|1|ARBITRARY")==["DENY","1","4","invalid-flight-action"]
    undock=send(p,f"FLIGHT|{epoch}|5|1|UND")
    assert len(undock)==17 and undock[0]=="SHIP" and undock[3]=="5" and undock[5]=="0",undock
    assert undock[16]=="0","real ship must be in space"
    yaw=send(p,f"FLIGHT|{epoch}|6|1|LEFT")
    assert yaw[0]=="SHIP" and yaw[5]=="0" and yaw[15]=="15",yaw
    restored=send(p,f"FLIGHT|{epoch}|7|1|RIGHT")
    assert restored[5]=="0" and restored[15]=="0",restored
    forward=send(p,f"FLIGHT|{epoch}|8|1|FWD")
    assert forward[5]=="0" and int(forward[12])>0,forward
    port=send(p,f"FLIGHT|{epoch}|9|1|PORT")
    assert port[5]=="0" and int(port[13])>0,port
    brake=send(p,f"FLIGHT|{epoch}|10|1|BRAKE")
    assert brake[5]=="0" and abs(int(brake[12]))<abs(int(port[12])) and abs(int(brake[13]))<abs(int(port[13])),brake
    dock=send(p,f"FLIGHT|{epoch}|11|1|DCK")
    assert dock[0]=="SHIP" and dock[5]=="0" and dock[16]=="1",dock
    assert [int(dock[9]),int(dock[10]),int(dock[11])]==[0,0,0],dock
    # No client-given actor identity or arbitrary vector ever enters FLIGHT.
    assert send(p,f"FLIGHT|{epoch}|12|1|X1000")==["DENY","1","12","invalid-flight-action"]
    p.stdin.write("QUIT\n");p.stdin.flush()
    assert p.wait(timeout=4)==0,p.stderr.read()
    with closing(sqlite3.connect(save)) as db:
        rows=db.execute("SELECT action,outcome FROM historical_events WHERE command_id IS NOT NULL").fetchall()
        assert len(rows)>=6,rows
        assert db.execute("SELECT seated,seq FROM sg_fp3d03a_seat WHERE owner=7").fetchone()==(1,1)
    restarted,pose2,ship2=open_host()
    assert pose2[11:]==["1","1"],pose2
    assert ship2[15]=="0" and ship2[16]=="1",ship2
    depart=send(restarted,f"FLIGHT|{pose2[2]}|1|1|UND")
    assert depart[0]=="SHIP" and depart[5]=="0" and depart[16]=="0",depart
    assert pose2[2]!=epoch,"session epoch must change across process restart"
    stopped=send(restarted,f"SEAT|{pose2[2]}|2|1|0")
    assert stopped[:2]==["ACK","1"] and stopped[11:]==["0","2"],stopped
    restarted.stdin.close()
    assert restarted.wait(timeout=4)==0,restarted.stderr.read()
print("FP3D03B native pipe PASS: seat-gated undock + return dock, original velocity/SQLite berth recovery")
