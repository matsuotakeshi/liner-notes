"""Run real journal checks in a unique directory, independent of working tree."""
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='activity-store-') as directory:
    subprocess.run([str(Path(sys.argv[1]).resolve()),str(Path(directory)/'notes')],check=True)
