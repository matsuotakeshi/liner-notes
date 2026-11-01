import pathlib
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as d:
    subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()),str(pathlib.Path(d)/'vault')],check=True)
