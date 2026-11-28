"""FreeBSD rc-process adapter for the isolated integration fixture."""
from pathlib import Path
import os,signal,subprocess,time

class RcProcess:
    """Installed rc sample, restricted to the already-admitted disposable jail."""
    def __init__(self,role,config,base):
        self.name='liner_notes_door_'+role
        self.rc=Path('/usr/local/etc/rc.d')/self.name
        sample=Path('/usr/local/share/liner-notes/door-services/freebsd')/self.name
        assert self.rc.is_file() and os.access(self.rc,os.X_OK),'installed rc service missing'
        assert self.rc.read_bytes()==sample.read_bytes(),'installed rc differs from packaged service'
        self.pidfile=base/(role+'.pid');self.logfile=base/(role+'.log')
        self.env=dict(os.environ,**{self.name+'_enable':'NO',self.name+'_config':str(config),
            self.name+'_pidfile':str(self.pidfile),self.name+'_logfile':str(self.logfile),
            self.name+'_run_user':'ln_broker_ci' if role=='broker' else 'root'})
        self.pid=None;self.returncode=None;self.stopper=None;self.wait_verified=False
        self.logfile.touch(mode=0o600);self.stderr=self.logfile.open('rb')

    def command(self,action):return ['/bin/sh',str(self.rc),action]

    def start(self):
        # rc.conf defaults remain disabled. A normal start must not create a
        # pidfile; only this explicit test's onestart authorizes activation.
        subprocess.run(self.command('start'),env=self.env,capture_output=True,check=True,timeout=10)
        assert not self.pidfile.exists(),'disabled service started'
        result=subprocess.run(self.command('onestart'),env=self.env,capture_output=True,timeout=20)
        if self.pidfile.exists():self.pid=int(self.pidfile.read_text().strip())
        assert result.returncode==0,('rc start failed',result.stdout,result.stderr,self.logfile.read_bytes()[-2048:])
        assert self.pid is not None
        subprocess.run(self.command('onestatus'),env=self.env,check=True,timeout=10)
        duplicate=subprocess.run(self.command('onestart'),env=self.env,capture_output=True,timeout=10)
        assert duplicate.returncode!=0 and int(self.pidfile.read_text())==self.pid,'duplicate rc launch'

    def restart(self):
        old_pid = self.pid
        result = subprocess.run(self.command('onerestart'), env=self.env,
                                capture_output=True, timeout=90)
        if self.pidfile.exists():
            self.pid = int(self.pidfile.read_text().strip())
        assert result.returncode == 0, ('rc restart failed', result.stdout, result.stderr)
        assert self.pid is not None and self.pid != old_pid
        subprocess.run(self.command('onestatus'), env=self.env, check=True, timeout=10)

    def poll(self):
        if self.returncode is not None:return self.returncode
        if self.pid is None:return 1
        try:os.kill(self.pid,0)
        except ProcessLookupError:
            # daemon owns/removes the pidfile; wait for its lock release too.
            if not self.pidfile.exists():self.returncode=0
        return self.returncode

    def terminate(self):
        self.stopper=subprocess.Popen(self.command('onestop'),env=self.env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)

    def kill(self):os.kill(self.pid,signal.SIGKILL)

    def wait(self,timeout):
        # Like Popen.wait(), repeated waits refer to this completed generation.
        # A later rc start can already own the same service name and PID path.
        if self.wait_verified:return self.returncode
        until=time.monotonic()+timeout
        while self.poll() is None:
            if time.monotonic()>=until:raise subprocess.TimeoutExpired(self.name,timeout)
            time.sleep(.05)
        if self.stopper:
            _,errors=self.stopper.communicate(timeout=max(.1,until-time.monotonic()))
            assert self.stopper.returncode==0,('rc stop failed',errors)
        status=subprocess.run(self.command('onestatus'),env=self.env,capture_output=True,timeout=10)
        assert status.returncode!=0,'stopped rc service still ready'
        self.wait_verified=True
        return self.returncode
