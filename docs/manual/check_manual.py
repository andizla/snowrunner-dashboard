"""check_manual.py: which manual paragraphs a change touches.

From the lines a change adds or removes (outside docs/manual and test/) it takes the names a player meets: ini keys
("BackToA", RequireClutch=1, `IgnoreShifter`), the tab's labels (Checkbox, Button and PageLabel texts) and the log's
lines (the literal start of an "area: what happened" string). It then lists every paragraph of the manual sources
(docs/manual/*.py, English and Russian, and README.md) that names one of them, and the names no paragraph has.

The commit then changes those paragraphs or says "manual: checked, unchanged". A name with no paragraph is either new
to the manual or never meant for it; the list says which names those are.

Run from the repo root:
    python docs/manual/check_manual.py             the working tree and the index against HEAD
    python docs/manual/check_manual.py <commit>    what that commit changed
    python docs/manual/check_manual.py <a>..<b>    what changed from a to b
It always exits 0: it lists, it does not judge.
"""
import html
import io
import os
import re
import subprocess
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SKIP = ('docs/manual/', 'test/', 'out/', 'deps/')
# The names of the hidden extra, which is in no manual on purpose (BRIEF_checkers.md): its ini key, its binding, its row and its one
# log line. They are left out of the lists. Nothing else is: a name not here is listed as before.
HIDDEN_NAMES = {('Checkers', 'key'), ('BindCheckers', 'key'), ('Checkers', 'tab'),
                # the tuning tool's Gear ratio pull (BRIEF_jobs_1114.md, 161, must 6): in no tab and in no manual until its default is set
                ('GearRatioPull', 'key'),
                # the drawn winch wire's sag, a probe (BRIEF_jobs_1114.md, 164, must 7): in no tab and in no manual
                ('WinchSag', 'key'),
                # the tuning tool's Engine inertia (BRIEF_drive_1114_findings.md, 180): in no tab and in no manual until its default is set
                ('EngineInertia', 'key'),
                # the tuning tool's Driveline inertia (BRIEF_drive_1114_findings.md, 185): the same, until its default is set
                ('DrivelineInertia', 'key')}
HIDDEN_LOG = ('checkers: ', 'gear ratio pull:', 'probe: gear ratio pull:', 'winch sag:', 'WINCH SAG:', 'engine inertia:', 'probe: engine inertia:', 'driveline inertia:', 'probe: driveline inertia:')


def hidden(name, kind):
    return (name, kind) in HIDDEN_NAMES or (kind == 'log' and name.startswith(HIDDEN_LOG))

# an ini key: two or more capitalised parts ("BackToA", "Gear16"), quoted, followed by = or in backticks
KEY = r'[A-Z][a-z]+(?:[A-Z][a-z0-9]*|[0-9]+)+'
SECTIONS = {'TirePressure', 'NextGen'} # the ini files' section names, on every line that writes a key
# a key of one word ("Creep", "Spotter") only where it is surely one: before = and a value, in backticks, or named in
# a call that reads or writes the ini
WORD = r'[A-Z][a-z0-9]+'
KEY_PATTERNS = [re.compile(r'L?"(' + KEY + r')"'), re.compile(r'\b(' + KEY + '|' + WORD + r')=(?=[-0-9A-Za-z])'),
                re.compile(r'`(' + KEY + '|' + WORD + r')`'),
                re.compile(r'\b(?:Ini[A-Za-z]*|whole|num|has|put)\([^"]*"(' + KEY + '|' + WORD + r')"')]
# the tab's labels
LABEL = re.compile(r'\b(?:Checkbox|Button|PageLabel)\(\s*"([^"#%\\]{3,60})')
# a log line: "area: what happened", its literal start up to the first format or escape
LOG = re.compile(r'L"([a-z][a-z ]{1,20}: [^"%\\]{6,})')
# the paragraphs of the manual sources: HTML blocks, and outside them each line (the tables are Python rows)
BLOCK = re.compile(r'<(p|li|h[1-6]|pre|dt|dd|td|th|caption|figcaption)\b[^>]*>.*?</\1>', re.S)


def changed_lines(spec):
    if spec is None:
        args = ['git', 'diff', 'HEAD', '-U0']
    elif '..' in spec:
        args = ['git', 'diff', spec, '-U0']
    else:
        args = ['git', 'show', spec, '-U0', '--format=']
    out = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace', check=True).stdout
    added, removed, keep, old = [], [], False, ''
    for line in out.splitlines():
        if line.startswith('--- '):
            old = line[6:] if line.startswith('--- a/') else ''
            continue
        if line.startswith('+++ '):
            # a deleted file has only its old path
            path = line[6:] if line.startswith('+++ b/') else old
            keep = bool(path) and not path.startswith(SKIP)
            continue
        if keep and line[:1] == '+': added.append(line[1:])
        elif keep and line[:1] == '-': removed.append(line[1:])
    return added, removed


def found_in(lines):
    names = {}
    def count(name, kind):
        names[(name, kind)] = names.get((name, kind), 0) + 1
    for line in lines:
        # a function looked up by name is no key a player meets
        if 'GetProcAddress' in line: continue
        for p in KEY_PATTERNS:
            for m in p.findall(line):
                if m not in SECTIONS: count(m, 'key')
        for m in LABEL.findall(line): count(m.strip(), 'tab')
        for m in LOG.findall(line):
            start = m[:50]
            if len(m) > 50 and ' ' in start: start = start[:start.rindex(' ')]
            count(start.strip(), 'log')
    return names


# The names on the changed lines. A key or a tab label counts whenever it is there: a rewritten help line changes what
# its key does. A log line's start as often on the removed lines as on the added ones only stands beside what changed
# (a log line that gained a field), and is told apart.
def names_of(added, removed):
    plus, minus = found_in(added), found_in(removed)
    every = [n for n in set(plus) | set(minus) if not hidden(*n)]
    beside = sorted(n for n in every if n[1] == 'log' and plus.get(n, 0) == minus.get(n, 0))
    changed = sorted(n for n in every if n not in beside)
    return changed, beside


def plain(text):
    text = re.sub(r'<[^>]+>', ' ', text)
    return re.sub(r'\s+', ' ', html.unescape(text)).strip()


def paragraphs():
    # the README too, a line a paragraph: it names the same keys and lags when nothing lists it (review N2)
    readme = os.path.join(ROOT, 'README.md')
    if os.path.exists(readme):
        for number, line in enumerate(open(readme, encoding='utf-8').read().split('\n'), 1):
            if line.strip():
                yield 'README.md', number, plain(line)
    for name in sorted(os.listdir(HERE)):
        if not name.endswith('.py') or name == os.path.basename(__file__):
            continue
        text = open(os.path.join(HERE, name), encoding='utf-8').read()
        covered = []
        for m in BLOCK.finditer(text):
            covered.append((m.start(), m.end()))
            yield name, text.count('\n', 0, m.start()) + 1, plain(m.group(0))
        at = 0
        for number, line in enumerate(text.split('\n'), 1):
            start, end = at, at + len(line)
            at = end + 1
            if not any(a <= start < b or a < end <= b for a, b in covered) and line.strip():
                yield name, number, plain(line)


# The list of hidden names leaves out its three and no fourth: made-up lines with the three and one more key.
def selftest():
    lines = ['    if (s.checkers) whole(L"Checkers", 1);', '            PageLabel("Checkers", labelX);', '            !wcscmp(boardKey, L"BindCheckers")',
             '            GaugeLog(L"checkers: the board was opened for the first time");', '    whole(L"CheckersTwo", 1);',
             '            PageLabel("Checkers board", labelX);', '            Log(L"chess: the board was opened for the first time");']
    names, _ = names_of(lines, [])
    want = [('CheckersTwo', 'key'), ('Checkers board', 'tab'), ('chess: the board was opened for the first time', 'log')]
    ok = sorted(names) == sorted(want)
    print('%s manual check: the hidden extra is left out by its four names, and a fourth key, another label and another log line are listed' % ('ok  ' if ok else 'FAIL'))
    if not ok: print('     listed: %r' % (names,))
    sys.exit(0 if ok else 1)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest': selftest()
    spec = sys.argv[1] if len(sys.argv) > 1 else None
    names, beside = names_of(*changed_lines(spec))
    order = {'key': 0, 'tab': 1, 'log': 2}
    names.sort(key=lambda n: (order[n[1]], n[0]))
    kinds = [sum(1 for n in names if n[1] == k) for k in ('key', 'tab', 'log')]
    print('manual check of %s: %d keys, %d tab labels, %d log lines' % (spec or 'the working tree', *kinds))
    if beside:
        print('only beside the change, not checked: ' + ', '.join(n for n, _ in beside))
    if not names:
        print('nothing a player meets changed: manual: checked, unchanged')
        return
    paras = list(paragraphs())
    seen, missing = set(), []
    # each name with its paragraphs under it
    for name, kind in names:
        found = [(file, line, text) for file, line, text in paras
                 if (re.search(r'(?<![A-Za-z0-9])' + re.escape(name) + r'(?![A-Za-z0-9])', text) if kind == 'key' else name.lower() in text.lower())]
        if not found:
            missing.append('%s (%s)' % (name, kind))
            continue
        print('%s (%s), %d paragraph%s:' % (name, kind, len(found), '' if len(found) == 1 else 's'))
        for file, line, text in found:
            print('  %s:%d %s' % (file, line, text[:150] + ('...' if len(text) > 150 else '')))
            seen.add((file, line))
    print('%d paragraphs to check' % len(seen))
    if missing:
        print('in no paragraph: ' + ', '.join(missing))


if __name__ == '__main__':
    main()
