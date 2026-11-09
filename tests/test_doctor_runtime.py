"""Diagnose both current runtime SQLite formats without modifying their files."""
import hashlib,json,os,subprocess,sys,tempfile,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
runtime=Path(sys.argv[1] if len(sys.argv)>1 else './runtime').resolve()

with tempfile.TemporaryDirectory(prefix='doctor-runtime-') as temporary:
    root=Path(temporary)
    for option in ('--database','--database-structured'):
        stage=root/option[2:];data=stage/'var/lib/liner-notes';data.mkdir(parents=True)
        database=data/'liner.sqlite';log=stage/'startup.log'
        with log.open('wb') as output:
            process=subprocess.Popen([str(runtime),option,str(database),'0'],cwd=data,stdout=output,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+60
                while time.monotonic()<deadline:
                    if b'LISTEN ' in log.read_bytes():break
                    if process.poll() is not None:raise AssertionError(log.read_text(errors='replace'))
                    time.sleep(.05)
                else:raise AssertionError('runtime startup timeout')
            finally:
                process.terminate()
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
        config=stage/'etc/liner-notes/liner-notes.env';config.parent.mkdir(parents=True)
        config.write_text('LINER_NOTES_OPTIONS="'+option+' /var/lib/liner-notes/liner.sqlite 2222"\n',encoding='utf-8')
        def state():return {str(p.relative_to(stage)):(hashlib.sha256(p.read_bytes()).hexdigest(),p.stat().st_mode,p.stat().st_mtime_ns) for p in stage.rglob('*') if p.is_file()}
        before=state()
        result=subprocess.run([sys.executable,'-B',str(ROOT/'scripts/doctor.py'),'--root',str(stage),'--os','Linux','--check-database','--json','--lang','en'],capture_output=True,text=True,timeout=60)
        assert result.returncode==0,(result.stdout,result.stderr)
        assert json.loads(result.stdout)['read_only']
        assert before==state(),'diagnosis changed runtime data/configuration'
        print('PASS doctor against runtime '+option+'; content/metadata/file inventory unchanged')
