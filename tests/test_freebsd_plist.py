#!/usr/bin/env python3
"""Match the generated canonical book and installed scripts against pkg-plist."""
from pathlib import Path
import sys,tempfile

ROOT = Path(__file__).resolve().parents[1]
PLIST = ROOT / "ports/comms/liner-notes/pkg-plist"


def main():
    content = PLIST.read_text(encoding="utf-8").splitlines()
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
    actual = set(content)
    missing = sorted(expected - actual)
    extra=sorted(x for x in actual-expected if x.startswith('%%PORTDOCS%%'))
    if extra:raise AssertionError(('Unexpected published documentation',extra))
    if missing:
        for entry in missing:
            print(f"UNLISTED FreeBSD stage file: {entry}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS: pkg-plist covers {len(expected)} staged Markdown/Python files")


if __name__ == "__main__":
    main()
