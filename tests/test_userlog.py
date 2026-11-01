"""Controlled-clock connection-ledger checks without real-time sleeps."""
import pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory(prefix="userlog-unit-") as directory:
 subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()),str(pathlib.Path(directory)/"ledger")],check=True)
