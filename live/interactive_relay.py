#!/usr/bin/env python3
"""B5.2 LOCAL-ONLY demonstration controller for ONE SQLite-backed C++ world.

Browser POSTs choose a small allowlisted action, never player identity, ship ID,
command ID, target ID, tick, or SQL. Not an untrusted multiplayer authority.
"""
import argparse
import base64
import hmac
import hashlib
import time
import re
import http.server
import json
import os
import pathlib
import secrets
import subprocess
import tempfile
import threading
import urllib.parse

ROOT=pathlib.Path(__file__).resolve().parents[1]
STATIC={
    '/viewer/interactive.html':'viewer/interactive.html',
    '/viewer/interactive.js':'viewer/interactive.js',
    '/viewer/survey.js':'viewer/survey.js',
    '/viewer/interpolation.js':'viewer/interpolation.js',
    '/viewer/contract.js':'viewer/contract.js',
    '/viewer/recordings.js':'viewer/recordings.js',
    '/viewer/viewer.js':'viewer/viewer.js',
    '/viewer/colony.js':'viewer/colony.js',
    '/viewer/opening-guide.js':'viewer/opening-guide.js',
    '/viewer/flight-controls.js':'viewer/flight-controls.js',
    '/viewer/flight-camera.js':'viewer/flight-camera.js',
    '/viewer/exploration-journal.js':'viewer/exploration-journal.js',
    '/viewer/travel-assist.js':'viewer/travel-assist.js',
    '/viewer/cargo-return.js':'viewer/cargo-return.js',
    '/viewer/expedition-feedback.js':'viewer/expedition-feedback.js',
    '/viewer/colony-shipping.js':'viewer/colony-shipping.js',
}
MIME={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8'}
MAX_FRAME=4*1024*1024
MAX_FRAMES=1202

# Reuse the v63 Node session account file: scrypt(N=16384,r=8,p=1,64 bytes).
# The browser never supplies a player identity; it only supplies credentials.
ACCOUNT_NAME=re.compile(r'^[a-zA-Z][a-zA-Z0-9_.-]{2,31}$')
COOKIE_NAME='sgcolsid'
SESSION_TTL=20*60

def load_accounts(file):
    file=pathlib.Path(file)
    st=file.lstat()
    if not file.is_file() or st.st_size>32768 or (os.name!='nt' and (st.st_mode & 0o077)):
        raise ValueError('Invalid private accounts file (0600 required on Unix)')
    doc=json.loads(file.read_text('utf-8'))
    rows=doc.get('accounts') if isinstance(doc,dict) and doc.get('version')==1 else None
    if not isinstance(rows,list) or not 1<=len(rows)<=64:raise ValueError('Invalid accounts manifest')
    users=set();players=set()
    for row in rows:
        if (not isinstance(row,dict) or not ACCOUNT_NAME.fullmatch(str(row.get('username',''))) or
            type(row.get('playerId'))!=int or row['playerId']<1 or row['playerId']>9007199254740991 or
            not re.fullmatch(r'[0-9a-f]{64}',str(row.get('salt',''))) or
            not re.fullmatch(r'[0-9a-f]{128}',str(row.get('hash',''))) or
            row['username'] in users or row['playerId'] in players):
            raise ValueError('Invalid or duplicate account')
        users.add(row['username']);players.add(row['playerId'])
    return {row['username']:row for row in rows}

def authenticate_account(rows,username,password):
    if (type(username)!=str or not ACCOUNT_NAME.fullmatch(username) or type(password)!=str):return None
    try: pw=password.encode('utf-8')
    except UnicodeEncodeError:return None
    if len(pw)>512:return None
    row=rows.get(username)
    salt=bytes.fromhex(row['salt']) if row else bytes(32)
    expected=bytes.fromhex(row['hash']) if row else bytes(64)
    candidate=hashlib.scrypt(pw,salt=salt,n=16384,r=8,p=1,dklen=64,maxmem=32*1024*1024)
    return row if hmac.compare_digest(candidate,expected) else None

class SharedRuntime:
    def __init__(self,exe,db,tick_ms,max_ticks,qa_mode=False,authenticated=False):
        self.cv=threading.Condition()
        self.frames=[]
        self.survey_events=[]
        self.colony_events=[]
        self.finished=False
        self.error=None
        self.stderr=[]
        self.cmd_lock=threading.Lock()
        self.token=secrets.token_urlsafe(32)
        self.authenticated=authenticated
        env=os.environ.copy()
        # The relay is local-only and QA commands are disabled unless the
        # explicit developer CLI switch is present. Not a multiplayer authority.
        env['SINGLE_GALAXY_QA_MODE']='1' if qa_mode else '0'
        self.proc=subprocess.Popen([str(exe),str(db),str(tick_ms),str(max_ticks)]+
            (['--authenticated-transport'] if authenticated else []),
            env=env,
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        threading.Thread(target=self._read,daemon=True).start()
        threading.Thread(target=self._read_stderr,daemon=True).start()

    def _read_stderr(self):
        for raw in self.proc.stderr:
            line=raw.decode('ascii',errors='replace').rstrip('\n')
            if line.startswith('COLONY|1|'):
                if len(line)<=180 and re.fullmatch(r'[a-zA-Z0-9_|-]+',line):
                    with self.cv:
                        if len(self.colony_events)<4096:
                            self.colony_events.append(base64.b64encode(line.encode('ascii')).decode('ascii'))
                            self.cv.notify_all()
                continue
            if line.startswith('SURVEY|1|'):
                # Host-originated, bounded, numeric/allowlisted data only. Never expose
                # arbitrary stderr lines or client-provided text as survey events.
                if len(line)<=180 and re.fullmatch(r'[A-Z0-9_|-]+',line):
                    with self.cv:
                        if len(self.survey_events)<4096:
                            self.survey_events.append(base64.b64encode(line.encode('ascii')).decode('ascii'))
                            self.cv.notify_all()
                continue
            self.stderr.append(line[:200])
            if len(self.stderr)>24:self.stderr.pop(0)

    def _read(self):
        current=bytearray()
        try:
            for raw in self.proc.stdout:
                if len(raw)>MAX_FRAME or b'\r' in raw or len(current)+len(raw)>MAX_FRAME:
                    raise ValueError('Noncanonical or oversized SGVIEW line')
                current.extend(raw)
                if raw==b'END\n':
                    if not current.startswith(b'SGVIEW|1|'):
                        raise ValueError('Bad SGVIEW header')
                    with self.cv:
                        if len(self.frames)>=MAX_FRAMES:raise ValueError('Frame limit exceeded')
                        self.frames.append(base64.b64encode(current).decode('ascii'))
                        self.cv.notify_all()
                    current.clear()
            if current:raise ValueError('Truncated final frame')
            result=self.proc.wait(timeout=3)
            if result:raise RuntimeError('C++ simulation exited with code '+str(result))
        except Exception as exc:
            self.error=str(exc)
            if self.proc.poll() is None:self.proc.terminate()
        finally:
            with self.cv:
                self.finished=True
                self.cv.notify_all()

    def send(self,command,principal=None):
        if self.authenticated:
            if principal!=7:raise PermissionError('This colony ship belongs to player 7')
            command='AUTH|'+str(principal)+'|'+command
        with self.cmd_lock:
            if self.finished or self.proc.poll() is not None:
                raise RuntimeError('Simulation is not running')
            if len(command)>64 or '\n' in command or '\r' in command:
                raise ValueError('Invalid command')
            self.proc.stdin.write((command+'\n').encode('ascii'))
            self.proc.stdin.flush()

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        try:self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait(timeout=3)
        for stream in (self.proc.stdin,self.proc.stdout,self.proc.stderr):
            try:stream.close()
            except OSError:pass

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,fmt,*args):pass

    def _host_ok(self):
        return self.headers.get('Host')=='127.0.0.1:'+str(self.server.server_port)

    def _send(self,status,body,content_type,headers=None):
        self.send_response(status)
        self.send_header('Content-Type',content_type)
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Cache-Control','no-store')
        self.send_header('Referrer-Policy','no-referrer')
        self.send_header('X-Frame-Options','DENY')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        for key,value in (headers or {}).items():self.send_header(key,value)
        self.send_header('Content-Length',str(len(body)))
        self.end_headers();self.wfile.write(body)

    def _json(self,status,data,headers=None):
        self._send(status,json.dumps(data,separators=(',',':')).encode('utf-8'),'application/json',headers)

    def _principal(self):
        if not self.server.accounts:return None
        cookie=self.headers.get('Cookie','')
        match=re.search(r'(?:^|;\s*)'+COOKIE_NAME+r'=([0-9a-f]{64})(?:;|$)',cookie)
        if not match:return None
        with self.server.session_lock:
            row=self.server.sessions.get(match.group(1))
            if not row:return None
            if row['expires']<=time.time():
                self.server.sessions.pop(match.group(1),None);return None
            return dict(row,session_id=match.group(1))

    def _session_payload(self,principal):
        return {'ok':True,'username':principal['username'],'playerId':principal['playerId'],
                'csrf':principal['csrf'],'writes':False,'colonyWrites':principal['playerId']==7,'colony_mode':principal['playerId']==7,
                'tick_ms':self.server.tick_ms,'max_ticks':self.server.max_ticks}

    def _same_origin(self):
        return (self.headers.get('Origin')=='http://127.0.0.1:'+str(self.server.server_port) and
            self.headers.get('Content-Type')=='application/json' and
            self.headers.get('Sec-Fetch-Site','same-origin')=='same-origin')

    def _credentials_post(self,path):
        if not self._same_origin():self._json(403,{'ok':False,'error':'Same-origin JSON required'});return
        try:
            size=int(self.headers.get('Content-Length','-1'))
            if not 2<=size<=4096:raise ValueError('Invalid request size')
            data=json.loads(self.rfile.read(size).decode('utf-8'))
            if type(data)!=dict:raise ValueError('Invalid JSON')
        except (ValueError,UnicodeDecodeError,json.JSONDecodeError):
            self._json(400,{'ok':False,'error':'Invalid request'});return
        if path=='/api/login':
            if set(data)!={'username','password'}:
                self._json(400,{'ok':False,'error':'Invalid login'});return
            now=time.time()
            with self.server.session_lock:
                recent=[t for t in self.server.failures if now-t<300]
                self.server.failures=recent
                if len(recent)>=8:
                    self._json(429,{'ok':False,'error':'Too many attempts'});return
            row=authenticate_account(self.server.accounts,data['username'],data['password'])
            if not row:
                with self.server.session_lock:self.server.failures.append(now)
                self._json(401,{'ok':False,'error':'Invalid credentials'});return
            with self.server.session_lock:
                self.server.failures=[]
                self.server.sessions={k:v for k,v in self.server.sessions.items() if v['expires']>now}
                if len(self.server.sessions)>=64:
                    self._json(503,{'ok':False,'error':'Session capacity reached'});return
                sid=secrets.token_hex(32)
                record={'playerId':row['playerId'],'username':row['username'],
                        'csrf':secrets.token_hex(32),'expires':now+SESSION_TTL}
                self.server.sessions[sid]=record
            self._json(200,self._session_payload(record),{'Set-Cookie':
                       COOKIE_NAME+'='+sid+'; HttpOnly; SameSite=Strict; Path=/; Max-Age='+str(SESSION_TTL)})
            return
        principal=self._principal()
        if not principal or self.headers.get('X-SG-CSRF')!=principal['csrf'] or data:
            self._json(403,{'ok':False,'error':'Session denied'});return
        with self.server.session_lock:self.server.sessions.pop(principal['session_id'],None)
        self._json(200,{'ok':True},{'Set-Cookie':COOKIE_NAME+'=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0'})

    def _shipment_rows(self,principal):
        bridge=self.server.shipping_bridge
        if not bridge:return {'ok':True,'shipments':[]}
        if not self.server.runtime.db.is_file():raise RuntimeError('World is not initialized')
        proc=subprocess.run([str(bridge),str(self.server.runtime.db)],
            input='LIST|'+str(principal['playerId'])+'\n',text=True,capture_output=True,timeout=8)
        if proc.returncode or not proc.stdout.strip():raise RuntimeError('Native shipping read failed')
        answer=json.loads(proc.stdout.splitlines()[0])
        if answer.get('ok') is not True or not isinstance(answer.get('shipments'),list):
            raise RuntimeError('Native shipment verification refused')
        return answer


    def do_GET(self):
        if not self._host_ok():
            self._json(403,{'error':'Local host required'});return
        url=urllib.parse.urlsplit(self.path)
        if url.path=='/healthz':
            self._send(200,b'B4 interactive relay alive\n','text/plain');return
        principal=self._principal()
        if self.server.accounts and url.path=='/api/session':
            self._json(200,self._session_payload(principal)) if principal else self._json(401,{'ok':False,'error':'Login required'})
            return
        if self.server.accounts and url.path=='/api/shipments':
            if not principal:self._json(401,{'ok':False,'error':'Login required'});return
            try:self._json(200,self._shipment_rows(principal))
            except (RuntimeError,ValueError,subprocess.TimeoutExpired,json.JSONDecodeError):
                self._json(503,{'ok':False,'error':'Shipment authority unavailable'})
            return
        if url.path=='/session':
            if self.server.accounts and (not principal or principal['playerId']!=7):
                self._json(403,{'error':'Colony owner session required'});return
            self._json(200,{'token':principal['csrf'] if self.server.accounts else self.server.runtime.token,'qa_mode':self.server.qa_mode,'colony_mode':self.server.colony_mode,'tick_ms':self.server.tick_ms,
                'max_ticks':self.server.max_ticks,'commands':(['thrust','yaw','colonyFireTraining','colonySalvageWreck','colonyDockBase','colonyUndockBase','colonyUnloadBase','colonyLoadBase','colonyScan','colonyConfigureScanner','colonyProbeDeploy','colonyProbeScan','colonyProbeRecover','colonySignatureScan','colonyCelestialScan','colonyPlanetSurvey','colonyPlanetBuild','colonyPlanetEquip','colonyPlanetSupply','colonyPlanetGrow','colonyPlanetExtract','colonyPlanetRetrieve','colonyEnvironmentScan','colonyProbeFocus','colonyProbeCancel','colonyDroneLaunch','colonyMine','colonyRefine','colonyFabricate','colonyBuild','colonyMineTarget','colonyProcess','colonyFabricateMaterial','colonyPlant','colonyHarvest','colonyRecycleWater','colonyRecycleCompost','colonyOxygen','colonyLifeWater','colonyFarmBuild','colonyFarmStaff','colonyFarmStock','colonyHabBuild','colonyHabResidents','colonyHabStock','colonyUtilityBuild','colonyUtilityRepair','colonyUtilitySpares','colonyUtilityStaff','colonyUtilityAuto','colonyCommunityBuild','colonyCommunityStaff','colonyCommunityStock','colonyClinicBuild','colonyClinicStaff','colonyClinicStock','colonyReclaimerBuild','colonyWaterworksBuild','colonyWaterworksFeed','colonyPolicySet','colonyPolicyStaff','colonySafetyAssess','colonySafetyPreflight'] if self.server.colony_mode else ['thrust','mine','stop','undock','dock','unload','load','sell','travelA','travelB','travelGate','travelHop','jump','jumpHop','directional','probeDeploy','probeScan','probeRecover','surveyArchive','surveyHistory','surveyPublish','planRoute'])});return
        if url.path in STATIC:
            file=ROOT/STATIC[url.path]
            self._send(200,file.read_bytes(),MIME[file.suffix]);return
        if url.path!='/events':
            self._json(404,{'error':'Not found'});return
        if self.server.accounts and (not principal or principal['playerId']!=7):
            self._json(403,{'error':'Colony owner session required'});return
        self.send_response(200)
        self.send_header('Content-Type','text/event-stream; charset=utf-8')
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Connection','close')
        self.end_headers()
        rt=self.server.runtime
        pos=0
        survey_pos=0
        colony_pos=0
        try:
            while True:
                # SSE subscriptions must not outlive logout/session expiry.
                if self.server.accounts:
                    current=self._principal()
                    if not current or current['session_id']!=principal['session_id']:
                        self.wfile.write(b'event: failed\ndata: Session expired\n\n');self.wfile.flush();return
                with rt.cv:
                    rt.cv.wait_for(lambda:pos<len(rt.frames) or rt.finished,timeout=3)
                    available=rt.frames[pos:]
                    surveys=rt.survey_events[survey_pos:]
                    colonies=rt.colony_events[colony_pos:]
                    done=rt.finished
                    error=rt.error
                for data in available:
                    self.wfile.write(('event: frame\ndata: '+data+'\n\n').encode('ascii'))
                    pos+=1
                for data in surveys:
                    self.wfile.write(('event: survey\ndata: '+data+'\n\n').encode('ascii'))
                    survey_pos+=1
                for data in colonies:
                    self.wfile.write(('event: colony\ndata: '+data+'\n\n').encode('ascii'))
                    colony_pos+=1
                if available or surveys or colonies:self.wfile.flush()
                if done and pos==len(rt.frames) and survey_pos==len(rt.survey_events) and colony_pos==len(rt.colony_events):
                    event=('event: failed\ndata: '+str(error or 'Host failed').replace('\n',' ')[:150]+'\n\n') if error else 'event: complete\ndata: finished\n\n'
                    self.wfile.write(event.encode('ascii'));self.wfile.flush();return
                if not available and not surveys and not colonies:
                    self.wfile.write(b': keepalive\n\n');self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError,TimeoutError,OSError):pass
        finally:self.close_connection=True

    def do_POST(self):
        if not self._host_ok():
            self._json(403,{'error':'Local host required'});return
        if self.server.accounts and self.path in ('/api/login','/api/logout'):
            self._credentials_post(self.path);return
        if self.path!='/command':
            self._json(404,{'error':'Not found'});return
        principal=self._principal()
        if self.server.accounts and (not principal or principal['playerId']!=7):
            self._json(403,{'error':'Colony owner session required'});return
        origin='http://127.0.0.1:'+str(self.server.server_port)
        if self.headers.get('Origin')!=origin or self.headers.get('Content-Type')!='application/json':
            self._json(403,{'error':'Same-origin JSON requests only'});return
        try:
            length=int(self.headers.get('Content-Length','-1'))
            if length<2 or length>512:raise ValueError('Invalid request size')
            raw=self.rfile.read(length)
            doc=json.loads(raw.decode('utf-8'))
            if not isinstance(doc,dict) or not isinstance(doc.get('token'),str) or not hmac.compare_digest(doc['token'],principal['csrf'] if self.server.accounts else self.server.runtime.token):
                self._json(403,{'error':'Invalid session token'});return
            action=doc.get('action')
            if self.server.colony_mode:
                if action not in ('thrust','yaw','colonyDockBase','colonyUndockBase','colonyUnloadBase','colonyLoadBase','colonyScan','colonyMine','colonyRefine','colonyFabricate','colonyBuild','colonyMineTarget','colonyProcess','colonyFabricateMaterial','colonyPlant','colonyHarvest','colonyRecycleWater','colonyRecycleCompost','colonyOxygen','colonyLifeWater','colonyFarmBuild','colonyFarmStaff','colonyFarmStock','colonyHabBuild','colonyHabResidents','colonyHabStock','colonyUtilityBuild','colonyUtilityRepair','colonyUtilitySpares','colonyUtilityStaff','colonyUtilityAuto','colonyCommunityBuild','colonyCommunityStaff','colonyCommunityStock','colonyClinicBuild','colonyClinicStaff','colonyClinicStock','colonyReclaimerBuild','colonyWaterworksBuild','colonyWaterworksFeed','colonyPolicySet','colonyPolicyStaff','colonySafetyAssess','colonySafetyPreflight'):
                    self._json(403,{'error':'Unavailable in colony-first mode'});return
            elif action in ('colonyScan','colonyMine','colonyRefine','colonyFabricate','colonyBuild','colonyMineTarget','colonyProcess','colonyFabricateMaterial','colonyPlant','colonyHarvest','colonyRecycleWater','colonyRecycleCompost','colonyOxygen','colonyLifeWater','colonyFarmBuild','colonyFarmStaff','colonyFarmStock','colonyHabBuild','colonyHabResidents','colonyHabStock','colonyUtilityBuild','colonyUtilityRepair','colonyUtilitySpares','colonyUtilityStaff','colonyUtilityAuto','colonyCommunityBuild','colonyCommunityStaff','colonyCommunityStock','colonyClinicBuild','colonyClinicStaff','colonyClinicStock','colonyReclaimerBuild','colonyWaterworksBuild','colonyWaterworksFeed','colonyPolicySet','colonyPolicyStaff','colonySafetyAssess','colonySafetyPreflight'):
                self._json(403,{'error':'Colony mode disabled'});return
            if action in ('qaRoute','qaStop','qaGate') and not self.server.qa_mode:
                # An unauthorized attempt is explicitly rejected and logged,
                # not covertly reported as proof of cheating.
                print('QA AUDIT: rejected testing command while QA mode disabled',flush=True)
                self._json(403,{'error':'QA testing mode disabled'});return
            if action in ('colonySafetyAssess','colonySafetyPreflight') and set(doc)=={'token','action','site'}:
                site=doc['site']
                if type(site)!=int or not 1<=site<=100000000:raise ValueError('Invalid safety site')
                command=f'SAFETY|{site}|'+('ASSESS' if action=='colonySafetyAssess' else 'PRODUCTION_PREFLIGHT')
            elif action=='colonyDroneLaunch' and set(doc)=={'token','action','target'}:
                target=doc['target']
                if type(target)!=int or not 1<=target<=100000000:
                    raise ValueError('Invalid geological target')
                command=f'DRONE_LAUNCH|{target}'
            elif action in ('colonyPlanetBuild','colonyPlanetEquip','colonyPlanetSupply',
                            'colonyPlanetGrow','colonyPlanetExtract','colonyPlanetRetrieve'):
                base={'token','action','body','latitude','longitude'}
                expected=(base if action=='colonyPlanetBuild' else
                          base|({'equipment'} if action=='colonyPlanetEquip' else
                                {'batches'} if action=='colonyPlanetGrow' else
                                {'resource','kg'}))
                if set(doc)!=expected:raise ValueError('Unexpected planetary operation fields')
                body,latitude,longitude=doc['body'],doc['latitude'],doc['longitude']
                if (type(body)!=int or not 1<=body<=9223372036854775807 or
                    type(latitude)!=int or not 0<=latitude<36 or
                    type(longitude)!=int or not 0<=longitude<72):
                    raise ValueError('Invalid planetary operation coordinates')
                prefix=f'{body}|{latitude}|{longitude}'
                if action=='colonyPlanetBuild':
                    command=f'PLANET_BUILD|{prefix}'
                elif action=='colonyPlanetEquip':
                    equipment=doc['equipment']
                    if type(equipment)!=str or equipment not in ('FARM','EXTRACT'):
                        raise ValueError('Invalid planetary equipment')
                    command=f'PLANET_EQUIP|{prefix}|{equipment}'
                elif action=='colonyPlanetGrow':
                    batches=doc['batches']
                    if type(batches)!=int or not 1<=batches<=100:
                        raise ValueError('Invalid planetary harvest batches')
                    command=f'PLANET_GROW|{prefix}|{batches}'
                else:
                    resource,kg=doc['resource'],doc['kg']
                    allowed=(('FOOD','WATER') if action=='colonyPlanetRetrieve'
                             else ('WATER','NUTRIENTS'))
                    if type(resource)!=str or resource not in allowed or
                       type(kg)!=int or not 1<=kg<=10000 or
                       (action=='colonyPlanetExtract' and (kg<1000 or kg%1000))):
                        raise ValueError('Invalid planetary resource request')
                    operation={
                        'colonyPlanetSupply':'PLANET_SUPPLY',
                        'colonyPlanetExtract':'PLANET_EXTRACT',
                        'colonyPlanetRetrieve':'PLANET_RETRIEVE'}[action]
                    command=f'{operation}|{prefix}|{resource}|{kg}'
            elif action=='colonyPlanetSurvey' and set(doc)=={'token','action','body','latitude','longitude'}:
                body,latitude,longitude=doc['body'],doc['latitude'],doc['longitude']
                if (type(body)!=int or not 1<=body<=9223372036854775807 or
                    type(latitude)!=int or not 0<=latitude<36 or
                    type(longitude)!=int or not 0<=longitude<72):
                    raise ValueError('Invalid planetary geographic coordinates')
                command=f'PLANET_SURVEY|{body}|{latitude}|{longitude}'
            elif action=='colonyFrameTravel' and set(doc)=={'token','action','body'}:
                body=doc['body']
                if type(body)!=int or not 1<=body<=9223372036854775807:
                    raise ValueError('Invalid celestial body ID')
                command=f'FRAME_TRAVEL|{body}'
            elif action in ('colonyCelestialScan','colonyEnvironmentScan') and set(doc)=={'token','action'}:
                command='SYSTEM_CELESTIAL' if action=='colonyCelestialScan' else 'SYSTEM_ENVIRONMENT'
            elif action=='colonySignatureScan' and set(doc)=={'token','action','rangeMm'}:
                radius=doc['rangeMm']
                if type(radius)!=int or not 5000000<=radius<=500000000:
                    raise ValueError('Invalid signature search radius')
                command=f'SIGNATURES|{radius}'
            elif action=='colonyProbeFocus' and set(doc)=={'token','action','probe','signature'}:
                probe,signature=doc['probe'],doc['signature']
                if (type(probe)!=int or type(signature)!=int or
                    not 1<=probe<=1000000000000000 or not 1<=signature<=100000000):
                    raise ValueError('Invalid probe focus request')
                command=f'PROBE_FOCUS|{probe}|{signature}'
            elif action=='colonyProbeCancel' and set(doc)=={'token','action','probe'}:
                probe=doc['probe']
                if type(probe)!=int or not 1<=probe<=1000000000000000:
                    raise ValueError('Invalid probe cancellation request')
                command=f'PROBE_CANCEL|{probe}'
            elif action=='colonyProbeDeploy' and set(doc)=={'token','action'}:
                command='PROBE_DEPLOY'
            elif action in ('colonyProbeScan','colonyProbeRecover') and set(doc)=={'token','action','probe'}:
                probe=doc['probe']
                if type(probe)!=int or not 1<=probe<=1000000000000000:
                    raise ValueError('Invalid physical survey probe ID')
                command=('PROBE_SCAN' if action=='colonyProbeScan' else 'PROBE_RECOVER')+'|'+str(probe)
            elif action=='colonyConfigureScanner' and set(doc)=={'token','action','mode'}:
                mode=doc['mode']
                if type(mode)!=int or mode not in (1,-1):
                    raise ValueError('Invalid exploration scanner fitting operation')
                command=f'FIT_SCANNER|{mode}'
            elif action in ('colonyScan','colonyBuild') and set(doc)=={'token','action'}:
                command='SCAN' if action=='colonyScan' else 'BUILD'
            elif action in ('colonyMine','colonyRefine','colonyFabricate') and set(doc)=={'token','action','amount'}:
                n=doc['amount']
                if type(n)!=int or not 1<=n<=(100 if action=='colonyMine' else 1000):
                    raise ValueError('Invalid colony material count')
                command={'colonyMine':'MINE','colonyRefine':'REFINE','colonyFabricate':'FABRICATE'}[action]+'|'+str(n)
            elif action=='colonyMineTarget' and set(doc)=={'token','action','target','amount'}:
                target,n=doc['target'],doc['amount']
                if type(target)!=int or type(n)!=int or not 1<=target<=100000000 or not 1<=n<=100:
                    raise ValueError('Invalid geological target or amount')
                command=f'MINE_TARGET|{target}|{n}'
            elif action=='colonyProcess' and set(doc)=={'token','action','mineral','kg'}:
                phase,kg=doc['mineral'],doc['kg']
                if type(phase)!=str or phase not in ('serpentine','saponite','magnetite','pyrrhotite','carbonates','olivine','pyroxene','nickel_iron','plagioclase','troilite') or type(kg)!=int or not 1<=kg<=100000:
                    raise ValueError('Invalid mineral processing request')
                command=f'PROCESS|{phase}|{kg}'
            elif action=='colonyFabricateMaterial' and set(doc)=={'token','action','material','amount'}:
                material,n=doc['material'],doc['amount']
                if type(material)!=str or material not in ('alloy','aluminum') or type(n)!=int or not 1<=n<=1000:
                    raise ValueError('Invalid component fabrication request')
                command=f'FABRICATE_MATERIAL|{material}|{n}'
            elif action in ('colonyPlant','colonyHarvest','colonyRecycleWater','colonyRecycleCompost','colonyOxygen','colonyLifeWater','colonyFarmBuild','colonyFarmStaff','colonyFarmStock','colonyHabBuild','colonyHabResidents','colonyHabStock') and set(doc)=={'token','action','amount'}:
                n=doc['amount']
                if type(n)!=int or not 1<=n<=(100 if action=='colonyLifeWater' else 10000):
                    raise ValueError('Invalid life-support batch')
                prefix={'colonyPlant':'PLANT','colonyHarvest':'HARVEST','colonyRecycleWater':'RECYCLE_WATER',
                   'colonyRecycleCompost':'RECYCLE_COMPOST','colonyOxygen':'OXYGEN','colonyLifeWater':'LIFE_WATER'}[action]
                command=f'{prefix}|{n}'
            elif action=='colonyFarmBuild' and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:raise ValueError('Invalid farm site')
                command=f'FARM_BUILD|{dest}'
            elif action=='colonyFarmStaff' and set(doc)=={'token','action','outpost','amount'}:
                dest,n=doc['outpost'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(n)!=int or n==0 or not -8<=n<=8:
                    raise ValueError('Invalid farm crew transfer')
                command=f'FARM_STAFF|{dest}|{n}'
            elif action=='colonyFarmStock' and set(doc)=={'token','action','outpost','resource','amount'}:
                dest,res,n=doc['outpost'],doc['resource'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(res)!=str or res not in ('food','water','oxygen','nutrients','co2') or type(n)!=int or n==0 or not -10000<=n<=10000:
                    raise ValueError('Invalid farm supply transfer')
                command=f'FARM_STOCK|{dest}|{res}|{n}'
            elif action in ('colonyPolicySet','colonyPolicyStaff') and set(doc)=={'token','action','outpost','domain','amount'}:
                dest,domain,n=doc['outpost'],doc['domain'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(domain)!=int or not 0<=domain<=3 or type(n)!=int or not 0<=n<=3:
                    raise ValueError('Invalid policy assurance request')
                command=f'{"POLICY_SET" if action=="colonyPolicySet" else "POLICY_STAFF"}|{dest}|{domain}|{n}'
            elif action=='colonyWaterworksBuild' and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:raise ValueError('Invalid waterworks site')
                command=f'WATERWORKS_BUILD|{dest}'
            elif action=='colonyWaterworksFeed' and set(doc)=={'token','action','outpost','amount'}:
                dest,n=doc['outpost'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(n)!=int or not n or n%1000 or not -10000<=n<=10000:
                    raise ValueError('Invalid sealed water delivery')
                command=f'WATERWORKS_FEED|{dest}|{n}'
            elif action=='colonyReclaimerBuild' and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:raise ValueError('Invalid reclaimer site')
                command=f'RECLAIMER_BUILD|{dest}'
            elif action=='colonyClinicBuild' and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:raise ValueError('Invalid clinic site')
                command=f'CLINIC_BUILD|{dest}'
            elif action=='colonyClinicStaff' and set(doc)=={'token','action','outpost','amount'}:
                dest,n=doc['outpost'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(n)!=int or not 0<=n<=4:
                    raise ValueError('Invalid clinic medic count')
                command=f'CLINIC_STAFF|{dest}|{n}'
            elif action=='colonyClinicStock' and set(doc)=={'token','action','outpost','resource','amount'}:
                dest,res,n=doc['outpost'],doc['resource'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(res)!=str or res not in ('water','substrate') or type(n)!=int or not n or not -10000<=n<=10000:
                    raise ValueError('Invalid clinic supply transfer')
                command=f'CLINIC_STOCK|{dest}|{res}|{n}'
            elif action=='colonyCommunityBuild' and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:raise ValueError('Invalid community site')
                command=f'COMMUNITY_BUILD|{dest}'
            elif action=='colonyCommunityStaff' and set(doc)=={'token','action','outpost','amount'}:
                dest,n=doc['outpost'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(n)!=int or not 0<=n<=4:
                    raise ValueError('Invalid community attendant count')
                command=f'COMMUNITY_STAFF|{dest}|{n}'
            elif action=='colonyCommunityStock' and set(doc)=={'token','action','outpost','resource','amount'}:
                dest,res,n=doc['outpost'],doc['resource'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or res not in ('food','water') or type(n)!=int or n==0 or not -10000<=n<=10000:
                    raise ValueError('Invalid community provisions')
                command=f'COMMUNITY_STOCK|{dest}|{res}|{n}'
            elif action in ('colonyUtilityBuild','colonyUtilityRepair') and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:
                    raise ValueError('Invalid utility site')
                command=f'{"UTILITY_BUILD" if action=="colonyUtilityBuild" else "UTILITY_REPAIR"}|{dest}'
            elif action in ('colonyUtilitySpares','colonyUtilityStaff','colonyUtilityAuto') and set(doc)=={'token','action','outpost','amount'}:
                dest,n=doc['outpost'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(n)!=int or (
                   action=='colonyUtilitySpares' and (n==0 or not -32<=n<=32)) or (
                   action=='colonyUtilityStaff' and not 0<=n<=8) or (
                   action=='colonyUtilityAuto' and n not in (0,1)):
                    raise ValueError('Invalid delegated maintenance request')
                prefix={'colonyUtilitySpares':'UTILITY_SPARES',
                        'colonyUtilityStaff':'UTILITY_STAFF','colonyUtilityAuto':'UTILITY_AUTO'}[action]
                command=f'{prefix}|{dest}|{n}'
            elif action=='colonyHabBuild' and set(doc)=={'token','action','outpost'}:
                dest=doc['outpost']
                if type(dest)!=int or not 1<=dest<=100000000:raise ValueError('Invalid habitat site')
                command=f'HAB_BUILD|{dest}'
            elif action=='colonyHabResidents' and set(doc)=={'token','action','outpost','amount'}:
                dest,n=doc['outpost'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(n)!=int or n==0 or not -8<=n<=8:
                    raise ValueError('Invalid habitat crew transfer')
                command=f'HAB_RESIDENTS|{dest}|{n}'
            elif action=='colonyHabStock' and set(doc)=={'token','action','outpost','resource','amount'}:
                dest,res,n=doc['outpost'],doc['resource'],doc['amount']
                if type(dest)!=int or not 1<=dest<=100000000 or type(res)!=str or res not in ('food','water','oxygen') or type(n)!=int or n==0 or not -10000<=n<=10000:
                    raise ValueError('Invalid habitat supply transfer')
                command=f'HAB_STOCK|{dest}|{res}|{n}'
            elif action=='qaStop' and set(doc)=={'token','action'}:
                command='QA_STOP'
            elif action=='qaRoute' and set(doc)=={'token','action','destination','policy'}:
                dest,policy=doc['destination'],doc['policy']
                if type(dest)!=int or not 1<=dest<=7 or policy not in ('S','Q'):
                    raise ValueError('Invalid QA route')
                command=f'QA_ROUTE|{dest}|{policy}'
            elif action=='qaGate' and set(doc)=={'token','action','from','to','online'}:
                a,b,online=doc['from'],doc['to'],doc['online']
                if type(a)!=int or type(b)!=int or not 1<=a<=7 or not 1<=b<=7 or a==b or type(online)!=bool:
                    raise ValueError('Invalid QA gate link')
                command=f'QA_GATE|{a}|{b}|{1 if online else 0}'
            elif action in ('colonyDockBase','colonyUndockBase','colonyUnloadBase','colonyLoadBase') and set(doc)=={'token','action'}:
                command={'colonyDockBase':'DOCK_BASE','colonyUndockBase':'UNDOCK_BASE',
                         'colonyUnloadBase':'UNLOAD_BASE','colonyLoadBase':'LOAD_BASE'}[action]
            elif action=='colonySalvageWreck' and set(doc)=={'token','action','target','amount'}:
                target,amount=doc['target'],doc['amount']
                if type(target)!=int or not 1<=target<=100000000 or type(amount)!=int or not 1<=amount<=10:
                    raise ValueError('Invalid salvage request')
                command=f'SALVAGE_WRECK|{target}|{amount}'
            elif action=='colonyFireTraining' and set(doc)=={'token','action','target'}:
                target=doc['target']
                if type(target)!=int or not 1<=target<=100000000:
                    raise ValueError('Invalid training weapon target')
                command=f'FIRE_TRAINING|{target}'
            elif action=='yaw' and set(doc)=={'token','action','delta'}:
                delta=doc['delta']
                if type(delta)!=int or delta not in (-15,15):raise ValueError('Invalid yaw step')
                command=f'YAW|{delta}'
            elif action=='thrust' and set(doc)=={'token','action','x','y'}:
                x,y=doc['x'],doc['y']
                if type(x)!=int or type(y)!=int or x not in (-1,0,1) or y not in (-1,0,1):raise ValueError('Invalid thrust axes')
                command=f'THRUST|{x}|{y}'
            elif action=='mine' and set(doc)=={'token','action','amount'}:
                n=doc['amount']
                if type(n)!=int or not 1<=n<=10:raise ValueError('Invalid extraction rate')
                command=f'MINE|{n}'
            elif action=='stop' and set(doc)=={'token','action'}:
                command='STOP'
            elif action=='undock' and set(doc)=={'token','action'}:
                command='UNDOCK'
            elif action=='dock' and set(doc)=={'token','action'}:
                command='DOCK'
            elif action=='unload' and set(doc)=={'token','action'}:
                command='UNLOAD|1000000000'
            elif action in ('load','sell','travelA','travelB','travelGate','travelHop','jump','jumpHop','probeDeploy','probeScan',
                            'probeRecover','surveyArchive','surveyHistory','surveyPublish') and set(doc)=={'token','action'}:
                command={'load':'LOAD','sell':'SELL','travelA':'TRAVEL_A','travelB':'TRAVEL_B','travelGate':'TRAVEL_GATE','travelHop':'TRAVEL_HOP','jump':'JUMP','jumpHop':'JUMP_HOP',
                         'probeDeploy':'PROBE_DEPLOY','probeScan':'PROBE_SCAN',
                         'probeRecover':'PROBE_RECOVER','surveyArchive':'SURVEY_ARCHIVE','surveyHistory':'SURVEY_HISTORY',
                         'surveyPublish':'SURVEY_PUBLISH'}[action]
            elif action=='planRoute' and set(doc)=={'token','action','destination','policy'}:
                dest,policy=doc['destination'],doc['policy']
                if type(dest)!=int or dest not in range(1,8) or policy not in ('S','Q'):
                    raise ValueError('Invalid route destination/policy')
                command=f'ROUTE|{dest}|{policy}'
            elif action=='directional' and set(doc)=={'token','action','bearing','cone'}:
                bearing,cone=doc['bearing'],doc['cone']
                if type(bearing)!=int or not 0<=bearing<360 or type(cone)!=int or cone not in (30,90,180,360):
                    raise ValueError('Invalid scan bearing/cone')
                command=f'DSCAN|{bearing}|{cone}' 
            else:raise ValueError('Unknown or invalid command schema')
            self.server.runtime.send(command,principal=principal['playerId'] if self.server.accounts else None)
            self._json(202,{'status':'queued_not_committed'})
        except (ValueError,UnicodeDecodeError,json.JSONDecodeError) as exc:
            self._json(400,{'error':str(exc)[:120]})
        except (BrokenPipeError,RuntimeError) as exc:
            self._json(409,{'error':str(exc)[:120]})

class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    allow_reuse_address=True

def run(args=None):
    p=argparse.ArgumentParser()
    p.add_argument('--host-exe',required=True,type=pathlib.Path)
    p.add_argument('--port',type=int,default=8766)
    p.add_argument('--tick-ms',type=int,default=100)
    p.add_argument('--max-ticks',type=int,default=600)
    p.add_argument('--colony',action='store_true',help='Colony-first local client mode, no seeded infrastructure')
    p.add_argument('--save',type=pathlib.Path,help='Optional persistent SQLite save for colony mode; developer-controlled path')
    p.add_argument('--accounts',type=pathlib.Path,help='Host-provisioned v63 credentials JSON (requires --colony)')
    p.add_argument('--shipping-bridge',type=pathlib.Path,help='Read-only canonical shipment-status executable')
    p.add_argument('--qa-test',action='store_true',help='EXPLICIT local testing mode for QA route automation and gate outage injection; never for production')
    opt=p.parse_args(args)
    if not opt.host_exe.is_file():p.error('Host binary does not exist')
    if opt.qa_test and opt.colony:p.error('QA legacy-gate test mode and colony game mode are separate')
    if opt.accounts and not opt.colony:p.error('--accounts requires --colony')
    if opt.shipping_bridge and not opt.accounts:p.error('--shipping-bridge requires --accounts')
    if opt.accounts and not opt.shipping_bridge:p.error('--accounts requires a native --shipping-bridge (fail closed)')
    if opt.shipping_bridge and not opt.shipping_bridge.is_file():p.error('Native shipping bridge missing')
    accounts=load_accounts(opt.accounts) if opt.accounts else None
    if opt.save and not opt.colony:p.error('--save requires --colony')
    if not 1024<=opt.port<=65535:p.error('Bad port')
    if not 20<=opt.tick_ms<=5000:p.error('Bad tick interval')
    if not 5<=opt.max_ticks<=1200:p.error('Bad max ticks')
    with tempfile.TemporaryDirectory(prefix='galaxy-interactive-') as tmp:
        save=opt.save.resolve() if opt.save else pathlib.Path(tmp)/'world.sqlite'
        save.parent.mkdir(parents=True,exist_ok=True)
        rt=SharedRuntime(opt.host_exe.resolve(),save,opt.tick_ms,opt.max_ticks,opt.qa_test,authenticated=bool(accounts))
        rt.db=save
        try:
            with Server(('127.0.0.1',opt.port),Handler) as server:
                server.runtime=rt;server.tick_ms=opt.tick_ms;server.max_ticks=opt.max_ticks;server.qa_mode=opt.qa_test;server.colony_mode=opt.colony
                server.accounts=accounts;server.shipping_bridge=opt.shipping_bridge.resolve() if opt.shipping_bridge else None
                server.sessions={};server.failures=[];server.session_lock=threading.RLock()
                print('Interactive demo: http://127.0.0.1:%d/viewer/interactive.html'%opt.port,flush=True)
                server.serve_forever(poll_interval=.1)
        except KeyboardInterrupt:pass
        finally:rt.stop()

if __name__=='__main__':run()
