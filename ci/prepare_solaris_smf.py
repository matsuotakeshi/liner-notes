#!/usr/bin/env python3
"""Generate a reviewable, fixed-host SMF fixture bundle; does not install it."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
HOME = "/opt/liner-notes-ci"


def manifest():
    root = ET.parse(ROOT / "solaris/liner-notes.xml").getroot()
    root.set("name", "liner-notes-ci")
    service = root.find("service")
    service.set("name", "application/liner-notes-ci")
    context = service.find("method_context")
    context.set("working_directory", "/export/home/ci-test/ci")
    context.find("method_credential").set("user", "ci-test")
    context.find("method_credential").set("group", "staff")
    service.find("exec_method[@name='start']").set(
        "exec", "/usr/bin/python3.13 -I " + HOME + "/solaris_smf_method.py")
    return ("<?xml version='1.0'?>\n<!DOCTYPE service_bundle SYSTEM "
            "'/usr/share/lib/xml/dtd/service_bundle.dtd.1'>\n" +
            ET.tostring(root, encoding="unicode") + "\n").encode()


def prepare(output):
    output = Path(output)
    output.mkdir(mode=0o700)  # refuse to overwrite a previous approved bundle
    files = {"liner-notes-ci.xml": manifest(),
             "solaris_smf_control.py": (ROOT / "ci/solaris_smf_control.py").read_bytes(),
             "solaris_smf_method.py": (ROOT / "ci/solaris_smf_method.py").read_bytes(),
             "session.lock": b"",
             "liner-notes-smf-ci.sudoers":
             b"ci-test ALL=(root) NOPASSWD: /opt/liner-notes-ci/solaris_smf_control.py session\n"}
    for name, content in files.items():
        (output / name).write_bytes(content)
    (output / "SHA256.json").write_text(json.dumps({name: hashlib.sha256(data).hexdigest()
                                                 for name, data in files.items()}, indent=2) + "\n")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    args = parser.parse_args()
    print(prepare(args.output))
