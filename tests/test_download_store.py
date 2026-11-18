"""Real download journal tests; SQLite interception is a native POSIX path."""
from pathlib import Path
import subprocess
import sys
import tempfile
binary=str(Path(sys.argv[1]).resolve())
for sqlite in ((False,) if sys.platform=='win32' else (False,True)):
    with tempfile.TemporaryDirectory(prefix='download-store-') as directory:
        args=[binary,directory]
        if sqlite: args.append(str(Path(directory)/'authoritative.sqlite'))
        subprocess.run(args,check=True,timeout=60)
