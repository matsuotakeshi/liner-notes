"""Isolated real-store API exercise; no sockets, UI, VM or production contacts."""
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='liner-session-api-') as directory:
    subprocess.run([str(Path(sys.argv[1]).resolve()),directory],check=True,timeout=120)
