#!/usr/bin/env python3
"""Conservative documentation-only CI classification, without API path limits."""
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess

DOC_METADATA = {
    'docs/manual/manifest.json', 'docs/manual/legacy-urls.json',
    'docs/manual/LICENSE',
    'docs/development/manual-consolidation/source-lock.json',
}


def documentation(path):
    p = PurePosixPath(path)
    if p.is_absolute() or '..' in p.parts:
        return False
    if path in DOC_METADATA or path in {'LICENSE', 'COPYING', 'NOTICE'}:
        return True
    if len(p.parts) == 1 and p.suffix == '.md':
        return True
    if p.parts[0] == 'docs' and p.suffix in {'.md', '.txt', '.rst', '.png', '.jpg', '.jpeg', '.webp'}:
        return True
    return len(p.parts) == 3 and p.parts[:2] in {('help', 'ja'), ('help', 'en')} and p.suffix == '.txt'


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], stderr=subprocess.PIPE)


def changes(repo, base, head):
    # Disable rename detection: both old and new paths must be documentation.
    data = git(repo, 'diff', '--raw', '--no-abbrev', '--no-renames', '-z', base, head, '--')
    parts = data.split(b'\0')
    rows = []
    for i in range(0, len(parts) - 1, 2):
        header = parts[i].decode('ascii').split()
        path = parts[i + 1].decode('utf-8', errors='strict')
        rows.append((path, header[0][1:], header[1]))
    return rows


def classify(repo, event, payload, ref):
    result = dict(schema=1, event=event, source_sha=git(repo, 'rev-parse', 'HEAD').decode().strip(),
                  build=True, reason='manual, release or unknown event: full verification', files=[])
    if event not in {'pull_request', 'push'} or ref.startswith(('refs/tags/', 'refs/heads/release/')):
        return result
    try:
        if event == 'pull_request':
            pr = payload['pull_request']
            base, head = pr['base']['sha'], pr['head']['sha']
        else:
            base, head = payload['before'], payload['after']
            if head != result['source_sha']:
                raise ValueError('push checkout does not match event')
        for sha in (base, head):
            if not isinstance(sha, str) or not re.fullmatch('[0-9a-f]{40}', sha) or sha == '0' * 40:
                raise ValueError('missing or invalid comparison commit')
        if event == 'pull_request':
            base = git(repo, 'merge-base', base, head).decode().strip()
        rows = changes(repo, base, head)
        result.update(base=base, head=head, files=[r[0] for r in rows])
        docs_only = bool(rows) and all(documentation(p) and old in {'000000', '100644'}
                                      and new in {'000000', '100644'} for p, old, new in rows)
        result.update(build=not docs_only, reason='documentation-only' if docs_only else 'code, configuration, unknown path, mode change or empty comparison')
    except (KeyError, ValueError, UnicodeError, subprocess.CalledProcessError) as exc:
        result['reason'] = 'comparison unavailable: full verification (' + type(exc).__name__ + ')'
    return result


def main():
    payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    result = classify(Path.cwd(), os.environ['GITHUB_EVENT_NAME'], payload, os.environ['GITHUB_REF'])
    Path('ci-change-scope.json').write_text(json.dumps(result, ensure_ascii=True, indent=2) + '\n', encoding='utf-8')
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as out:
        out.write('build=' + str(result['build']).lower() + '\n')
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as out:
        out.write('### CI scope\n\n' + result['reason'] + '\n\n')
        out.write('Build/tests/packages: **' + ('required' if result['build'] else 'not applicable (documentation only)') + '**. Canonical manual remains enabled.\n')
    print(json.dumps(result, ensure_ascii=True))


if __name__ == '__main__':
    main()
