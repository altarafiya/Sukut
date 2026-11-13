"""Isolated tray bridge/lifetime check. Never starts filtering or changes a real app."""
import os,pathlib,subprocess,sys,tempfile,time,shlex
if len(sys.argv)<3 or sys.argv[-1]!='inside':
 p=subprocess.run(['dbus-run-session','--',sys.executable,__file__,sys.argv[1],'inside']);sys.exit(p.returncode)
root=pathlib.Path(tempfile.mkdtemp(prefix='sukut-tray-test-'));(root/'runtime').mkdir(mode=0o700)
record=root/'popup.args';fail=root/'fail';helper=root/'window.sh'
helper.write_text('#!/bin/sh\nprintf \'%s\\n\' "$*" >> '+shlex.quote(str(record))+'\n[ -e '+shlex.quote(str(fail))+' ] && exit 77\nexit 0\n');helper.chmod(0o700)
env=os.environ.copy();env.update(XDG_RUNTIME_DIR=str(root/'runtime'),XDG_CONFIG_HOME=str(root/'config'),WAYLAND_DISPLAY='sukut-test-display',XDG_CURRENT_DESKTOP='KDE')
p=subprocess.Popen([sys.argv[1],'--tray','--window-command',str(helper)],env=env,stdout=(root/'host.log').open('w'),stderr=subprocess.STDOUT)
name=f'org.kde.StatusNotifierItem-{p.pid}-1'
def call(method,*args):
 q=subprocess.run(['gdbus','call','--session','--dest',name,'--object-path','/StatusNotifierItem','--method',method,*map(str,args)],capture_output=True,text=True,timeout=10)
 if q.returncode:raise RuntimeError(q.stderr)
 return q.stdout
try:
 for _ in range(100):
  try:
   menu=call('org.freedesktop.DBus.Properties.Get','org.kde.StatusNotifierItem','Menu');break
  except RuntimeError:time.sleep(.05)
 else:raise RuntimeError('tray did not export: '+(root/'host.log').read_text())
 assert '/MenuBar' in menu,menu
 def root_items():
  out=subprocess.run(['gdbus','call','--session','--dest',name,'--object-path','/MenuBar','--method','com.canonical.dbusmenu.GetLayout','0','1','[]'],capture_output=True,text=True,timeout=10).stdout
  return out.count("'label'")
 assert root_items()==2,'right click must be the short Open/Exit menu while the flyout works'
 call('org.kde.StatusNotifierItem.Activate',1880,1040)
 for _ in range(50):
  if record.exists() and '--tray-popup 1880 1040' in record.read_text():break
  time.sleep(.05)
 else:raise AssertionError('left click did not launch the popup')
 maps=pathlib.Path(f'/proc/{p.pid}/maps').read_text();assert 'libgtk-4' not in maps and 'libgtk4-layer-shell' not in maps
 print('PASS: left click launches the on-demand popup; right click is Open/Exit; host does not load GTK',flush=True)
 time.sleep(.15);fail.touch();call('org.kde.StatusNotifierItem.Activate',1800,1030)
 for _ in range(50):
  if root_items()>2:break
  time.sleep(.05)
 else:raise AssertionError('unsupported compositor did not restore the full right-click menu')
 print('PASS: popup child reaped and unsupported compositor restores the full menu',flush=True)
finally:
 p.terminate()
 try:p.wait(timeout=8)
 except subprocess.TimeoutExpired:p.kill();p.wait()
 print('evidence:',root)
