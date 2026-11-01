"""Test-only resource-compatible journal write limit.
SQLite indexes use pwrite and remain usable while authoritative append writes
are cut short. The original real RLIMIT_FSIZE test is in test_index_fault.py.
"""
import os,resource,subprocess,tempfile,shlex
from pathlib import Path
RLIMIT_FSIZE=resource.RLIMIT_FSIZE
_build=tempfile.TemporaryDirectory(prefix='liner-fault-helper-')
_library=Path(_build.name)/'journal-limit.so'
subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-shared','-fPIC','-Wall','-Wextra','-Werror',str(Path(__file__).with_name('fault_write.c')),'-ldl','-o',str(_library)],check=True)
def setrlimit(kind,limits):
 if kind!=RLIMIT_FSIZE:return resource.setrlimit(kind,limits)
 os.environ['LINER_TEST_JOURNAL_LIMIT']=str(limits[0])
 os.environ['LD_PRELOAD']=str(_library)
def __getattr__(name):return getattr(resource,name)
