#!/usr/bin/env python3
"""B5.3 whole logistics journey through authoritative C++ host and optional loopback relay.
No fabricated state: frame reader follows committed SGVIEW data. Browser rendering is not claimed.
"""
import argparse
import base64
import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from contextlib import closing
from B52InteractiveEndToEndTests import request, post, collect, extract_sse, check

class Frames:
    def __init__(self,p):
        self.p=p
        self.cv=threading.Condition()
        self.frames=[]
        self.current=[]
        self.entities={}
        self.docks={}
        self.hangars={}
        self.markets={}
        self.wallets={}
        self.commands=[]
        self.error=None
        self.reader=threading.Thread(target=self.run,daemon=True)
        self.reader.start()
    def run(self):
        try:
            for line in self.p.stdout:
                data=line.decode('ascii')
                if data=='END\n':
                    frame=''.join(self.current)+data
                    with self.cv:
                        for item in self.current[1:]:
                            f=item.rstrip('\n').split('|')
                            if f[0]=='E': self.entities[int(f[1])]=f
                            if f[0]=='D': self.entities.pop(int(f[1]),None)
                            if f[0]=='Q': self.docks[int(f[1])]=f
                            if f[0]=='Z': self.docks.pop(int(f[1]),None)
                            if f[0]=='H': self.hangars[(int(f[1]),int(f[2]))]=f
                            if f[0]=='M': self.markets[int(f[1])]=f
                            if f[0]=='W': self.wallets[int(f[1])]=f
                            if f[0]=='C': self.commands.append(f)
                        self.frames.append(frame)
                        self.current=[]
                        self.cv.notify_all()
                else:self.current.append(data)
        except Exception as exc:
            with self.cv:
                self.error=exc
                self.cv.notify_all()
    def until(self,pred,label,limit=3.0):
        end=time.monotonic()+limit
        with self.cv:
            while not pred():
                if self.error:raise AssertionError(f'frame reader failed: {self.error}')
                remain=end-time.monotonic()
                if remain<=0:raise AssertionError('timed out: '+label+' frames='+str(len(self.frames))+' commands='+str(self.commands[-3:]))
                self.cv.wait(min(remain,.05))
    def send(self,cmd):
        self.p.stdin.write((cmd+'\n').encode('ascii'));self.p.stdin.flush()

def direct(exe,save_recording=None):
    with tempfile.TemporaryDirectory(prefix='b53-e2e-') as tmp:
        db=pathlib.Path(tmp)/'world.sqlite'
        proc=subprocess.Popen([str(exe),str(db),'40','125'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        frames=Frames(proc)
        def entity():return frames.entities.get(2,[])
        def dock(id,state):return frames.docks.get(2,[])[:4]==['Q','2',str(state),str(id)]
        def cargo(n):return len(entity())==12 and int(entity()[10])==n
        try:
            frames.until(lambda:dock(1,1),'initial dock')
            check(frames.markets[1][-1]=='2' and frames.markets[4][-1]=='5','station prices missing')
            frames.send('UNDOCK');frames.until(lambda:2 not in frames.docks,'undocked')
            frames.send('MINE|4');frames.until(lambda:cargo(20),'mined 20')
            frames.send('DOCK');frames.until(lambda:dock(1,1),'docked A')
            frames.send('UNLOAD|1000000000');frames.until(lambda:frames.hangars.get((1,7),[])[-2:-1]==['20'],'unload A')
            frames.send('LOAD');frames.until(lambda:cargo(20) and frames.hangars[(1,7)][3]=='0','load A')
            frames.send('UNDOCK');frames.until(lambda:2 not in frames.docks,'second undock')
            frames.send('TRAVEL_B')
            frames.until(lambda:len(entity())==12 and abs(int(entity()[5])-10500)<=250 and abs(int(entity()[6])-7000)<=250 and int(entity()[7])==0 and int(entity()[8])==0,'physical travel B')
            frames.send('DOCK');frames.until(lambda:dock(4,1),'docked B')
            frames.send('UNLOAD|1000000000');frames.until(lambda:frames.hangars.get((4,7),[])[3:4]==['20'],'unload B')
            frames.send('SELL');frames.until(lambda:frames.wallets.get(7,[])[-1:]==['100'] and frames.markets.get(4,[])[2:4]==['20','900'],'sale B')
            check(cargo(0) and frames.hangars[(1,7)][3]=='0' and frames.hangars[(4,7)][3]=='0','bad final cargo')
            proc.stdin.close()
            check(proc.wait(timeout=12)==0, 'host failed: '+proc.stderr.read().decode(errors='replace'))
            frames.reader.join(timeout=2)
            check(len(frames.frames)==126,'incomplete stream')
            if save_recording:
                save_recording.write_text(''.join(frames.frames),encoding='ascii')
            print('PASS B5.3 live process: mine 20, unload/load A, physical travel to B, sell for 100 credits')
            return ''.join(frames.frames)
        finally:
            if proc.poll() is None:proc.kill();proc.wait(timeout=3)

def relay(exe,relay_path):
    with closing(socket.socket()) as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    base=f'http://127.0.0.1:{port}'
    proc=subprocess.Popen([sys.executable,str(relay_path),'--host-exe',str(exe),'--port',str(port),'--tick-ms','40','--max-ticks','70'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(90):
            try:
                check(b'interactive relay alive' in request(base,'/healthz'),'relay unhealthy');break
            except (urllib.error.URLError,ConnectionError):time.sleep(.04)
        else:raise AssertionError('relay not started')
        session=json.loads(request(base,'/session'));token=session['token']
        check({'load','sell','travelA','travelB'}.issubset(session['commands']),'commands missing from capabilities')
        html=request(base,'/viewer/interactive.html')
        check(all(f'id="{x}"'.encode() in html for x in ['load','sell','travelA','travelB','walletCredits','marketPrice']), 'UI controls missing')
        good={'token':token}
        for doc,expected in [({**good,'action':'sell','station':4},400),
                             ({**good,'action':'load','actor':2},400),
                             ({**good,'action':'travelB','target':4},400),
                             ({'token':'bad','action':'sell'},403)]:
            code,_=post(base,doc);check(code==expected,'malformed command reached host')
        recv=[]
        thread=threading.Thread(target=collect,args=(base,recv),daemon=True);thread.start()
        for action in ['load','sell','travelA']:
            code,_=post(base,{**good,'action':action});check(code==202,action+' not accepted')
        thread.join(12)
        check(not thread.is_alive(),'relay stream hung')
        stream=extract_sse(recv[0]);check(len(stream)==71,'missing relay frames')
        check(stream==extract_sse(request(base,'/events').decode('ascii')),'late viewer replay differs')
        check(any('W|7|0' in f for f in stream),'no wallet telemetry')
        check(any('M|4|0|1000|5' in f for f in stream),'no station B market telemetry')
        check(any('|6|0' in f for f in stream if 'C|' in f),'expected no-resource rejection missing')
        print('PASS B5.3 relay: new commands schema-checked, local auth enforced; 2 observers see same committed data')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate()

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--executable',required=True,type=pathlib.Path);ap.add_argument('--relay',required=True,type=pathlib.Path)
    ap.add_argument('--recording',type=pathlib.Path,default=None)
    a=ap.parse_args();direct(a.executable.resolve(),a.recording);relay(a.executable.resolve(),a.relay.resolve())
