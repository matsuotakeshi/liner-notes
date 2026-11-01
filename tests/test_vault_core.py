import pathlib, subprocess, sys, tempfile
with tempfile.TemporaryDirectory() as d:
    p=pathlib.Path(d); (p/"source").write_bytes(b"independent core")
    subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()),str(p/"vault"),str(p/"source")],check=True)
