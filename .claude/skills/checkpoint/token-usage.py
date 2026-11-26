#!/usr/bin/env python3
"""Token use of the Claude Code sessions that worked on Nutshell, by role and model.

Reads the local transcripts under ~/.claude/projects/ (the main clone's and the
worktrees' project directories) and sums each model request's usage. Every
request is counted once (streamed entries share a message id) and is filtered
by its own timestamp, so a window can cut through a session.

    token-usage.py --since 2026-11-21 --until 2026-11-26T00:20 [--repo ~/nutshell]
                   [--json out.json] [--compare baseline.json]

Cost index: fresh input 1, 5-minute cache write 1.25, 1-hour cache write 2,
cache read 0.1, output 5 (Anthropic's API price ratios within one model). It
does not weight models against each other; the by-model table is there for
that. Usage limits on a subscription may weigh tokens differently.

Claude Code deletes transcripts older than `cleanupPeriodDays` (30 by
default), so measure a window before it ages out.
"""
import argparse, collections, datetime, glob, json, os, re, statistics, subprocess, sys

ROLES = [  # first match wins, on the sub-agent's description
    ('critique', r'critiqu|critic\b|second opinion|builder check'),
    ('orchestrator', r'orchestrat|resume .*orchestrator'),
    ('plan/spec', r'\b(write|draft|revise)\b.*\b(plan|spec|design)\b'),
    ('fix', r'\bfix'),
    ('review', r'review|audit'),
    ('implement', r'implement|\btask \d|tdd|write tests?'),
    ('survey', r'survey|explore|find|locate|investig|research'),
]
W = dict(inp=1.0, cw5=1.25, cw1h=2.0, cr=0.1, out=5.0)
IDLE = 300  # seconds: the 5-minute cache TTL


def ts(s):
    return datetime.datetime.fromisoformat(s.replace('Z', '+00:00'))


def bound(s):
    d = datetime.datetime.fromisoformat(s)
    return d if d.tzinfo else d.replace(tzinfo=datetime.timezone.utc)


def role(desc, atype, has_children=False):
    if has_children:  # by structure: an agent that dispatched agents controlled them
        return 'orchestrator'
    if atype == 'Explore':
        return 'survey'
    d = (desc or '').lower()
    for name, rx in ROLES:
        if re.search(rx, d):
            return name
    return 'other'


def requests(path, since, until):
    seen, compactions = {}, 0
    for line in open(path, errors='replace'):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if d.get('type') == 'system' and d.get('subtype') == 'compact_boundary' and d.get('timestamp') \
                and since <= ts(d['timestamp']) < until:
            compactions += 1
        if d.get('type') != 'assistant':
            continue
        m = d.get('message') or {}
        u = m.get('usage')
        if not u or not d.get('timestamp'):
            continue
        k = m.get('id') or d.get('requestId') or d.get('uuid')
        seen.setdefault(k, (ts(d['timestamp']), m.get('model') or '?', u))
    rs = [r for r in sorted(seen.values(), key=lambda r: r[0]) if since <= r[0] < until]
    return rs, compactions


def measure(since, until, project_globs):
    agents = []
    for pdir in sorted({p for g in project_globs for p in glob.glob(g)}):
        for main in glob.glob(os.path.join(pdir, '*.jsonl')):
            agents.append((main, 'main', 'main session', 0))
            metas = {}
            for meta in glob.glob(os.path.join(main[:-6], 'subagents', '*.meta.json')):
                jl = meta[:-len('.meta.json')] + '.jsonl'
                if os.path.exists(jl):
                    metas[os.path.basename(jl)[len('agent-'):-len('.jsonl')]] = (jl, json.load(open(meta)))
            parents = {md.get('parentAgentId') for _, md in metas.values()}
            for aid, (jl, md) in metas.items():
                agents.append((jl, role(md.get('description'), md.get('agentType'), aid in parents),
                               md.get('description', ''), md.get('spawnDepth', 1)))
    tot = collections.defaultdict(collections.Counter)
    starts, peaks = [], collections.defaultdict(list)
    for path, r, desc, depth in agents:
        rs, compactions = requests(path, since, until)
        if not rs:
            continue
        for key in (('role', r), ('all', 'all')):
            tot[key]['compactions'] += compactions
        prev = None
        for i, (t, model, u) in enumerate(rs):
            cc = u.get('cache_creation') or {}
            cw = u.get('cache_creation_input_tokens', 0)
            cw1h = cc.get('ephemeral_1h_input_tokens', 0)
            cw5 = cw - cw1h
            cr = u.get('cache_read_input_tokens', 0)
            inp = u.get('input_tokens', 0)
            out = u.get('output_tokens', 0)
            ctx = inp + cw + cr
            cost = inp * W['inp'] + cw5 * W['cw5'] + cw1h * W['cw1h'] + cr * W['cr'] + out * W['out']
            for key in (('role', r), ('model', model.replace('claude-', '')), ('all', 'all')):
                c = tot[key]
                c['requests'] += 1
                c['fresh_input'] += inp + cw
                c['cache_read'] += cr
                c['output'] += out
                c['cost_index'] += cost
                if prev and (t - prev).total_seconds() > IDLE and cw > 0.5 * ctx and ctx > 50000:
                    c['idle_rewrites'] += 1
                    c['idle_rewrite_tokens'] += cw
                    c['idle_rewrite_cost'] += cw5 * W['cw5'] + cw1h * W['cw1h']
            if i == 0 and depth > 0:
                starts.append(ctx)
            peaks[r].append(ctx)
            prev = t
        for key in (('role', r), ('all', 'all')):
            tot[key]['agents'] += 1
    return dict(
        totals={f'{k[0]}:{k[1]}': dict(v) for k, v in tot.items()},
        subagent_start_median=int(statistics.median(starts)) if starts else 0,
        subagent_start_n=len(starts),
        peak_context={r: max(v) for r, v in peaks.items()},
    )


def merges(repo, since, until):
    if not repo:
        return None
    out = subprocess.run(['git', '-C', repo, 'log', '--merges', '--first-parent', '--format=%H',
                          f'--since={since.isoformat()}', f'--until={until.isoformat()}', 'origin/main'],
                         capture_output=True, text=True).stdout.split()
    return len(out)


def fmt(n):
    n = float(n)
    if n != n:
        return 'n/a'
    return f'{n/1e6:.2f}M' if abs(n) >= 1e6 else f'{n/1e3:.0f}k'


def report(res, base=None):
    print(f"Window {res['since']} .. {res['until']}; merges to main: {res['merges']}")
    a = res['totals']['all:all']
    print(f"Cost index {fmt(a['cost_index'])} over {a['requests']} requests, {a['agents']} agents; "
          f"per merge {fmt(a['cost_index'] / res['merges']) if res['merges'] else 'n/a'}")
    print(f"Sub-agent start-up context, median {fmt(res['subagent_start_median'])} (n={res['subagent_start_n']})")
    ctl = sum(v['cost_index'] for k, v in res['totals'].items() if k in ('role:main', 'role:orchestrator'))
    tasks = res['totals'].get('role:implement', {}).get('agents', 0)
    print(f"Controller (main session + orchestrators): {100 * ctl / a['cost_index']:.0f}% of the cost index; "
          f"per implementer agent {fmt(ctl / tasks) if tasks else 'n/a'}; compactions {a.get('compactions', 0)}")
    print(f"Idle cache rewrites: {a.get('idle_rewrites', 0)}, {fmt(a.get('idle_rewrite_tokens', 0))} tokens, "
          f"{100 * a.get('idle_rewrite_cost', 0) / a['cost_index']:.0f}% of the cost index")
    for kind in ('role', 'model'):
        print(f"\n{'by ' + kind:16} {'agents':>6} {'reqs':>6} {'fresh in':>9} {'cache rd':>9} {'output':>8} "
              f"{'cost':>8} {'share':>6} {'rewrites':>8} {'peak ctx':>8}" + ('   base share' if base else ''))
        rows = [(k.split(':', 1)[1], v) for k, v in res['totals'].items() if k.startswith(kind + ':')]
        if base:  # keep a baseline row that has no counterpart now (a role the change removed)
            have = {n for n, _ in rows}
            rows += [(k.split(':', 1)[1], collections.Counter(requests=0, cost_index=0, fresh_input=0,
                                                               cache_read=0, output=0))
                     for k in base['totals'] if k.startswith(kind + ':') and k.split(':', 1)[1] not in have]
        for name, v in sorted(rows, key=lambda kv: -kv[1]['cost_index']):
            share = 100 * v['cost_index'] / a['cost_index']
            line = (f"{name:16} {v.get('agents', 0) or '':>6} {v['requests']:6} {fmt(v['fresh_input']):>9} "
                    f"{fmt(v['cache_read']):>9} {fmt(v['output']):>8} {fmt(v['cost_index']):>8} {share:5.0f}% "
                    f"{v.get('idle_rewrites', 0):8} {fmt(res['peak_context'].get(name, 0)) if kind == 'role' else '':>8}")
            if base:
                b = base['totals'].get(f'{kind}:{name}')
                line += f"   {100 * b['cost_index'] / base['totals']['all:all']['cost_index']:5.0f}%" if b else '      -'
            print(line)
    if base:
        b = base['totals']['all:all']
        print('\nAgainst the baseline', base['since'], '..', base['until'])
        def per(r, x):
            return r['totals']['all:all'].get(x, 0) / r['merges'] if r['merges'] else float('nan')
        for x in ('cost_index', 'cache_read', 'fresh_input', 'output', 'idle_rewrite_cost'):
            pb, pn = per(base, x), per(res, x)
            ch = f'{100 * (pn - pb) / pb:+.0f}%' if pb and pn == pn else 'n/a'
            print(f"  {x:18} per merge: {fmt(pb):>8} -> {fmt(pn):>8}  ({ch})")
        def ctl_per_task(r):
            t = r['totals']
            c = sum(t.get(k, {}).get('cost_index', 0) for k in ('role:main', 'role:orchestrator'))
            n = t.get('role:implement', {}).get('agents', 0)
            return c / n if n else float('nan')
        pb, pn = ctl_per_task(base), ctl_per_task(res)
        ch = f'{100 * (pn - pb) / pb:+.0f}%' if pb == pb and pn == pn and pb else 'n/a'
        print(f"  controller per implementer agent: {fmt(pb):>8} -> {fmt(pn):>8}  ({ch})")
        print(f"  start-up median   : {fmt(base['subagent_start_median']):>8} -> {fmt(res['subagent_start_median']):>8}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--since', required=True)
    ap.add_argument('--until', default=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='minutes'))
    ap.add_argument('--repo', help='clone whose origin/main merges normalise the figures')
    ap.add_argument('--merges', type=int, help='merge count, instead of --repo')
    ap.add_argument('--projects', nargs='*',
                    default=[os.path.expanduser('~/.claude/projects/-home-thomas-nutshell'),
                             os.path.expanduser('~/.claude/projects/-home-thomas-nutshell-wt-*')])
    ap.add_argument('--json', help='write the result here')
    ap.add_argument('--compare', help='a baseline written by --json')
    o = ap.parse_args()
    since, until = bound(o.since), bound(o.until)
    res = measure(since, until, o.projects)
    res.update(since=since.isoformat(), until=until.isoformat(),
               merges=o.merges if o.merges is not None else merges(o.repo, since, until))
    if res['totals'].get('all:all') is None:
        sys.exit('no requests in that window (transcripts older than cleanupPeriodDays are deleted)')
    base = json.load(open(o.compare)) if o.compare else None
    report(res, base)
    if o.json:
        json.dump(res, open(o.json, 'w'), indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
