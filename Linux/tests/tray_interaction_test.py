"""Native Wayland regression: stalled host, focused mode click, and tray placement.
Uses a fake host only; never changes Sukut configuration or starts audio.
"""
from pathlib import Path
import json,os,socket,subprocess,tempfile,threading,time,sys
binary=sys.argv[1];anchor_x=int(sys.argv[2]) if len(sys.argv)>2 else 960
active='active' in sys.argv[3:]
top='top' in sys.argv[3:]
arabic='ar' in sys.argv[3:]
base=Path(tempfile.mkdtemp(prefix='sukut-tray-interaction-'));runtime=base/'runtime';(runtime/'sukut').mkdir(parents=True)
original_runtime=os.environ.get('XDG_RUNTIME_DIR','/run/user/'+str(os.getuid()));display=os.environ.get('WAYLAND_DISPLAY','wayland-0');wayland=display if display.startswith('/') else original_runtime+'/'+display
server=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET);server.bind(str(runtime/'sukut/host.socket'));server.listen(1)
state={'mode':0,'state':0,'preferredMode':2,'buffering':1,'optimizedBackgroundDb':-10,'theme':'dark','language':'en','setupCompleted':True,'closeToTray':True}
if active:state.update(mode=2,state=1)
if arabic:state['language']='ar'
snapshot=json.dumps(state);commands=[];first_reply=[];first_frame=[];bounds=[];output=[]
started=time.perf_counter()
def serve():
 c,_=server.accept()
 try:
  while True:
   msg=c.recv(1<<20)
   if not msg:break
   fields=msg.decode().split('\x1f');verb=fields[0];commands.append(fields)
   if verb=='read':
    if not first_reply:time.sleep(1.0);first_reply.append(time.perf_counter()-started)
    payload=state
   elif verb in ('tray-catalog','apps'):payload={'outputs':[{'name':'test','description':'Preview output'}],'apps':[],'defaultOutput':'test'}
   elif verb=='select':
    time.sleep(.35);state['mode']=int(fields[1]);state['preferredMode']=int(fields[1]);payload={}
   elif verb=='settings':
    time.sleep(.35)
    if 'PreferredMode=3' in fields:state['preferredMode']=3
    payload={}
   else:payload={}
   c.send(b'1'+json.dumps(payload).encode())
 except (BrokenPipeError,ConnectionResetError):pass
 finally:c.close()
t=threading.Thread(target=serve,daemon=True);t.start()
env=os.environ.copy();env.update(XDG_RUNTIME_DIR=str(runtime),WAYLAND_DISPLAY=wayland,GDK_BACKEND='wayland');env.pop('GSK_RENDERER',None)
p=subprocess.Popen([binary,'--tray-popup',str(anchor_x),'30' if top else '1060','--tray-state',snapshot,'--tray-exercise'],env=env,stdout=subprocess.PIPE,stderr=(base/'stderr.log').open('w'),text=True,bufsize=1)
def read():
 for line in p.stdout:
  output.append(line.strip())
  if line.startswith('tray_first_frame'):first_frame.append(time.perf_counter()-started)
  if line.startswith('tray_bounds '):bounds.append(list(map(float,line.split()[1:])))
r=threading.Thread(target=read,daemon=True);r.start()
time.sleep(2.5);alive=p.poll() is None
memory={}
if alive:
 for line in Path('/proc',str(p.pid),'status').read_text().splitlines():
  if line.startswith(('VmRSS:','VmHWM:')):memory[line.split(':')[0]]=int(line.split()[1])
 for line in Path('/proc',str(p.pid),'smaps_rollup').read_text().splitlines():
  if line.startswith('Pss:'):memory['Pss']=int(line.split()[1])
print('resident_memory_kib=',memory)
if alive:p.terminate()
p.wait(timeout=8);t.join(timeout=2);r.join(timeout=2);server.close()
print('first_frame_seconds=',first_frame);print('first_host_reply_seconds=',first_reply);print('stayed_open_after_mode_click=',alive);print('commands=',commands);print('\n'.join(output));print('evidence=',base)
assert alive,'Menu closed while a focused mode button was being disabled/re-enabled'
assert first_frame and first_reply and first_frame[0]<first_reply[0],'First frame waited for slow host'
assert any((c[:2]==['select','3'] if active else c==['settings','PreferredMode=3']) for c in commands),'Mode action was not delivered'
assert state['preferredMode']==3
assert not any(c[0] in (('hello','apps') if active else ('select','hello','apps')) for c in commands),'Unexpected audio, main-window or eager discovery request'
assert bounds,'No native geometry'
x,y,width,height,screen_w,screen_h=bounds[-1]
expected=min(max(anchor_x-width/2,8),max(8,screen_w-width-8))
assert abs(x-expected)<=2,(x,expected)
assert (abs(y-8)<=2 if top else abs(y+height-(screen_h-8))<=2),(y,height,screen_h)
print('PASS: fast first frame despite stalled host; mode keeps menu open; centred/clamped at panel edge')
