#!/usr/bin/env python3
"""Match staged manual, HELP, scripts and services against pkg-plist."""
from pathlib import Path
import re
import sys,tempfile

ROOT = Path(__file__).resolve().parents[1]
PLIST = ROOT / "ports/comms/liner-notes/pkg-plist"


def check_help_samples(content):
    makefile = PLIST.with_name("Makefile").read_text(encoding="utf-8")
    languages = re.search(r"^\.for lang in +(.+)$", makefile, re.MULTILINE)
    topics = re.search(r"^\.for topic in +(.+)$", makefile, re.MULTILINE)
    assert languages and topics, "FreeBSD HELP install loops missing"
    prefix = "@sample %%DATADIR%%/help/"
    recipes = {
        f"{prefix}{lang}/{topic}.txt.sample"
        for lang in languages[1].split() for topic in topics[1].split()
    }
    sources = {
        f"{prefix}{lang}/{path.name}.sample"
        for lang in ("ja", "en") for path in (ROOT / "help" / lang).glob("*.txt")
    }
    listed = {line for line in content if line.startswith(prefix)}
    for label, entries in (("install recipe", recipes), ("pkg-plist", listed)):
        assert entries == sources, (f"FreeBSD HELP {label} mismatch",
                                    sorted(sources - entries), sorted(entries - sources))
    return len(sources)


def main():
    content = PLIST.read_text(encoding="utf-8").splitlines()
    help_count = check_help_samples(content)
    sys.path.insert(0,str(ROOT/'scripts/manual'))
    from build import build
    with tempfile.TemporaryDirectory(prefix='manual-plist-') as tmp:
        book=Path(tmp)/'book';build(book)
        expected = {f"%%PORTDOCS%%%%DOCSDIR%%/{p.relative_to(book).as_posix()}"
                    for p in book.rglob('*') if p.is_file()}
    expected.update({
        f"%%DATADIR%%/scripts/{p.name}"
        for p in (ROOT / "scripts").glob("*.py") if p.is_file()
    })
    for role in ('broker','isolation'):
        name='liner_notes_door_'+role
        assert (ROOT/'ports/comms/liner-notes/files'/(name+'.in')).is_file()
        expected.add('%%DATADIR%%/door-services/freebsd/'+name)
        assert 'etc/rc.d/'+name not in content,'Candidate must not activate automatically'
    actual = set(content)
    missing = sorted(expected - actual)
    extra=sorted(x for x in actual-expected if x.startswith('%%PORTDOCS%%'))
    if extra:raise AssertionError(('Unexpected published documentation',extra))
    if missing:
        for entry in missing:
            print(f"UNLISTED FreeBSD stage file: {entry}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS: pkg-plist covers {len(expected)} staged manual/script/service files and {help_count} HELP samples")


if __name__ == "__main__":
    main()
