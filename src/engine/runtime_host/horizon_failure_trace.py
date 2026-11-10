"""Bounded failure context, without variable dumps or credential files."""
from pathlib import Path,PureWindowsPath
import datetime as dt
import json,os,re,traceback,subprocess
from pgn_paths import state_path

def safe_text(value,limit=600):
    text=str(value)
    text=re.sub(r'(?i)(authorization|cookie|password|secret|private[ _-]?key|api[ _-]?key)\s*[:=][^\r\n]*','<redacted>',text)
    text=re.sub(r'(?i)\bprv[0-9a-f]{64}\b','<redacted>',text)
    text=re.sub(r'(?i)[a-z]:[\\/]+Users[\\/]+[^\\/\s]+','<user>',text)
    text=re.sub(r'/(?:home|Users)/[^/\s]+','<user>',text)
    return text[:limit]

def resource_name(value,bundle=None,state_root=None,cookie=None):
    if not value:return None
    path=str(value).replace('\\','/')
    for prefix,root in [('bundle',bundle),('state',state_root)]:
        if root is not None:
            base=str(root).replace('\\','/').rstrip('/')
            if path.lower().startswith(base.lower()+'/'):
                return safe_text(prefix+'/'+path[len(base)+1:],240)
    if cookie is not None and path.lower()==str(cookie).replace('\\','/').lower():return 'core_state/'+PureWindowsPath(path).name
    return safe_text(PureWindowsPath(path).name,240)

def failed_executable(error):
    # Windows CreateProcess errors omit OSError.filename. Inspect only the
    # known stdlib launch parameter; never retain arguments or other locals.
    current=error.__traceback__
    while current is not None:
        frame=current.tb_frame
        if frame.f_globals.get('__name__')=='subprocess'and frame.f_code.co_name=='_execute_child':
            value=frame.f_locals.get('executable')
            if not value:
                args=frame.f_locals.get('args')
                if isinstance(args,(list,tuple))and args:value=args[0]
                elif isinstance(args,str):
                    first=args.lstrip();value=first.split('"',2)[1]if first.startswith('"')and'"'in first[1:]else first.split(' ',1)[0]
            if isinstance(value,(str,os.PathLike)):return str(value)[:2048]
        current=current.tb_next
    return None

def failure_context(error,*,bundle=None,state_root=None,cookie=None,step='controller_startup'):
    if state_root is not None:
        try:state_root=state_path(state_root)
        except OSError:pass
    if state_root is not None:
        phase=Path(state_root)/'runtime-startup.json'
        try:
            if not phase.is_symlink()and phase.stat().st_size<=4096:
                value=json.loads(phase.read_bytes())
                if value.get('schema')=='PGN_CONTROLLER_STARTUP_V1'and value.get('pid')==os.getpid():step=value.get('phase',step)
        except(OSError,ValueError,TypeError):pass
    causes=[];seen=set();current=error
    while current is not None and id(current)not in seen and len(causes)<3:
        seen.add(id(current))
        frames=[dict(file=PureWindowsPath(frame.filename).name[:120],line=frame.lineno,function=frame.name[:80])
                for frame in traceback.extract_tb(current.__traceback__,limit=-6)]
        resource=getattr(current,'filename',None)or getattr(current,'path',None)or failed_executable(current)
        # subprocess exception strings contain the complete command. Keep its
        # executable and return status, never its arguments (wallet/RPC context).
        summary=safe_text(current)
        if isinstance(current,(subprocess.CalledProcessError,subprocess.TimeoutExpired)):
            cmd=current.cmd
            if isinstance(cmd,(list,tuple))and cmd:resource=cmd[0]
            summary=('Process exited with code '+str(current.returncode))if isinstance(current,subprocess.CalledProcessError)else('Process exceeded timeout '+str(current.timeout)+' seconds')
        causes.append(dict(error_type=type(current).__name__,os_code=getattr(current,'errno',None),
            os_win_code=getattr(current,'winerror',None),resource=resource_name(resource,bundle,state_root,cookie),
            module=safe_text(getattr(current,'name',''),120),summary=summary,frames=frames))
        current=current.__cause__ or (None if current.__suppress_context__ else current.__context__)
    result=dict(schema='PGN_RUNTIME_FAILURE_V1',at_utc=dt.datetime.now(dt.timezone.utc).isoformat(),pid=os.getpid(),step=safe_text(step,80),causes=causes)
    if state_root is not None:
        pending=Path(state_root)/'runtime-failure.pending';target=Path(state_root)/'runtime-failure.json'
        try:
            raw=(json.dumps(result,separators=(',',':'))+'\n').encode('utf-8');assert len(raw)<16384
            with pending.open('wb')as stream:stream.write(raw);stream.flush();os.fsync(stream.fileno())
            os.replace(pending,target)
        except(OSError,AssertionError):pass
    return result

def failure_message(context):
    cause=next((c for c in context['causes']if c.get('resource')),context['causes'][0])
    prefix=cause['error_type']+((' · file: '+cause['resource'])if cause.get('resource')else'')
    return (prefix+' · step: '+context['step']+' · '+cause['summary'])[:600]
