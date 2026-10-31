#!/usr/bin/env python3
"""Development-only, loopback-only SSE adapter for the real C++ simulation stream.

No external dependencies. Not an authenticated multiplayer transport.
"""
import argparse
import base64
import http.server
import pathlib
import subprocess
import tempfile
import urllib.parse

ROOT=pathlib.Path(__file__).resolve().parents[1]
STATIC={
    '/viewer/live.html': ('viewer/live.html','text/html; charset=utf-8'),
    '/viewer/contract.js': ('viewer/contract.js','text/javascript; charset=utf-8'),
    '/viewer/recordings.js': ('viewer/recordings.js','text/javascript; charset=utf-8'),
    '/viewer/viewer.js': ('viewer/viewer.js','text/javascript; charset=utf-8'),
    '/viewer/live.js': ('viewer/live.js','text/javascript; charset=utf-8'),
}
MAX_FRAME=4*1024*1024

class Relay(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self, fmt, *args):
        print('relay:', fmt%args, flush=True)

    def do_GET(self):
        url=urllib.parse.urlsplit(self.path)
        if url.path=='/healthz':
            data=b'B4 loopback relay ready\n'
            self.send_response(200)
            self.send_header('Content-Type','text/plain')
            self.send_header('Content-Length',str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if url.path in STATIC:
            rel,mime=STATIC[url.path]
            data=(ROOT/rel).read_bytes()
            self.send_response(200)
            self.send_header('Content-Type',mime)
            self.send_header('Content-Length',str(len(data)))
            self.send_header('Cache-Control','no-store')
            self.end_headers()
            self.wfile.write(data)
            return
        if url.path!='/events':
            self.send_error(404,'Unknown path')
            return
        args=urllib.parse.parse_qs(url.query)
        scenario=args.get('scenario',['hold'])[0]
        if scenario not in ('ten','hold','motion'):
            self.send_error(400,'Invalid scenario')
            return
        exe=self.server.executable  # absolute executable path, never user-supplied
        self.send_response(200)
        self.send_header('Content-Type','text/event-stream; charset=utf-8')
        self.send_header('Cache-Control','no-store, no-cache')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Connection','close')
        self.end_headers()
        proc=None
        try:
            with tempfile.TemporaryDirectory(prefix='galaxy-live-') as tmp:
                db=str(pathlib.Path(tmp)/'runtime.sqlite')
                proc=subprocess.Popen([str(exe),scenario,db,str(self.server.tick_ms)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
                frame=bytearray()
                count=0
                while True:
                    raw=proc.stdout.readline()
                    if not raw: break
                    if b'\r' in raw or len(raw)>MAX_FRAME or len(frame)+len(raw)>MAX_FRAME:
                        raise ValueError('Oversized or noncanonical SGVIEW frame')
                    frame.extend(raw)
                    if raw==b'END\n':
                        if not frame.startswith(b'SGVIEW|1|'): raise ValueError('Invalid frame header')
                        payload=base64.b64encode(frame).decode('ascii')
                        self.wfile.write(('event: frame\ndata: '+payload+'\n\n').encode('ascii'))
                        self.wfile.flush()
                        frame.clear()
                        count+=1
                        if count>32: raise ValueError('Unexpected frame count')
                if frame: raise ValueError('Incomplete terminal frame')
                code=proc.wait(timeout=5)
                if code: raise RuntimeError('simulation process failed with code '+str(code))
                self.wfile.write(b'event: complete\ndata: finished\n\n')
                self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError):
            pass
        except Exception as exc:
            try:
                message=str(exc).replace('\n',' ')[:160]
                self.wfile.write(('event: failed\ndata: '+message+'\n\n').encode('utf-8'))
                self.wfile.flush()
            except OSError:
                pass
        finally:
            if proc and proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=2)
                except subprocess.TimeoutExpired: proc.kill();proc.wait()
            self.close_connection=True

class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    allow_reuse_address=True

def run(argv=None):
    parser=argparse.ArgumentParser(description='Local Single Galaxy B4 development visual relay')
    parser.add_argument('--host-exe',required=True,type=pathlib.Path)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--tick-ms',type=int,default=100)
    opts=parser.parse_args(argv)
    if not opts.host_exe.is_file(): parser.error('--host-exe does not exist')
    if not 10<=opts.tick_ms<=5000: parser.error('tick-ms must be 10..5000')
    if not 1024<=opts.port<=65535: parser.error('port must be 1024..65535')
    with Server(('127.0.0.1',opts.port),Relay) as server:
        server.executable=opts.host_exe.resolve()
        server.tick_ms=opts.tick_ms
        print('Open http://127.0.0.1:%d/viewer/live.html'%opts.port,flush=True)
        try: server.serve_forever(poll_interval=0.2)
        except KeyboardInterrupt: pass

if __name__=='__main__':run()
