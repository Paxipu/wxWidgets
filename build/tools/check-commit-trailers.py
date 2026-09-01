#!/usr/bin/env python3
"""Check that the commits of a branch carry the attribution this fork agreed on.

docs/gtk/gtk4-upstream-summary.md settles two things about every commit made
here, and #177 exists because neither was being followed:

  * exactly one `Co-authored-by: Claude Opus 5 <noreply@anthropic.com>`,
    spelt the way git and GitHub spell it -- lowercase `authored-by`, which is
    also how the commits this fork took from upstream spell it;

  * no `Claude-Session:` trailer, because those URLs point at private sessions
    and resolve for nobody else, so in a permanent public history they are
    dead links.

Neither is something to remember. A convention that is written down and not
checked is how 150 commits came to carry `Co-Authored-By` and 109 a session
URL.

Checks the commits a branch adds, not the whole history: the history before
the convention was settled is #174's problem, and failing on it here would
make this check useless.

Usage:
    check-commit-trailers.py [<base>]

<base> defaults to the upstream branch this one forked from.
"""

import re
import subprocess
import sys

TRAILER = 'Co-authored-by: Claude Opus 5 <noreply@anthropic.com>'

# The same trailer said any other way. Git's own trailer matching is
# case-insensitive, so these are the same trailer to git and different strings
# to a reader -- which is exactly the inconsistency being removed.
WRONG_CASE = re.compile(r'^co-authored-by:\s*Claude', re.IGNORECASE | re.MULTILINE)
SESSION = re.compile(r'^Claude-Session:', re.IGNORECASE | re.MULTILINE)


def run(*args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def commits(base):
    out = run('git', 'rev-list', '%s..HEAD' % base)
    return [c for c in out.split('\n') if c]


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else 'origin/master'

    if not run('git', 'rev-parse', '--verify', base).strip():
        print('check-commit-trailers: no such base "%s", nothing to check' % base)
        return 0

    problems = []

    for sha in commits(base):
        body = run('git', 'log', '-1', '--format=%B', sha)
        subject = run('git', 'log', '-1', '--format=%s', sha).strip()

        # Merge commits carry no work of their own.
        if len(run('git', 'rev-list', '--parents', '-n', '1', sha).split()) > 2:
            continue

        exact = body.count(TRAILER)
        anycase = len(WRONG_CASE.findall(body))

        if anycase and not exact:
            problems.append((sha, subject,
                             'attribution trailer is spelt differently; it has '
                             'to read exactly "%s"' % TRAILER))
        elif exact > 1:
            problems.append((sha, subject, 'attribution trailer appears %d times'
                             % exact))

        if SESSION.search(body):
            problems.append((sha, subject,
                             'carries a Claude-Session: trailer, which is a '
                             'private URL and a dead link to everyone else'))

    if not problems:
        print('check-commit-trailers: %d commits, all consistent'
              % len(commits(base)))
        return 0

    print('check-commit-trailers: %d problem(s)\n' % len(problems))
    for sha, subject, what in problems:
        print('  %s  %s' % (sha[:12], subject))
        print('      %s\n' % what)

    print('See "How this work is attributed" in '
          'docs/gtk/gtk4-upstream-summary.md.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
