# Token use of the development loop

Each retrospective measures the token use of the Claude Code sessions that
worked on Nutshell since the last measurement here and compares the two (the
`checkpoint` skill, step 1). The tool is
`.claude/skills/checkpoint/token-usage.py`; it reads the local transcripts on
`automaton`, so it runs only there. Each `*.json` file is one window, with
totals by role and by model; no transcript content is kept. Windows never
overlap: each starts at the previous file's `until`.

**Cost index**: fresh input 1, 5-minute cache write 1.25, 1-hour cache write 2,
cache read 0.1, output 5, Anthropic's API price ratios within one model. It does
not weight Opus against Sonnet or Fable; read the by-model shares for that. A
subscription's usage limits may weigh tokens differently.

**Roles** come from each sub-agent's description, except that any agent that
dispatched agents of its own counts as an orchestrator. "Controller" is the
main session plus the orchestrators: the work the 2026-11-26 change moves.

## Baseline: 2026-11-21T00:00 to 2026-11-26T00:45 UTC (`2026-11-26-baseline.json`)

The loop before the change: a long-lived Opus orchestrator per pull request
ran the plan's tasks. 17 merges (#97 and #101 to #117, no #114; the AI switch and
the voice work among them), 271 agents, 8,144 model requests. Four of those
merges (#97, #101-#103) were cheap and partly worked before the window; voice
PR 3 was mostly built in it (about 55M of the index, the critique's figure) and merges after it.

| Measure | Value |
|---|---|
| Cost index | 242.5M (14.3M per merge) |
| Cache reads / fresh input / output | 1.26B / 60.9M / 7.5M tokens |
| Controller (main session + orchestrators) | 40% of the index, 1.25M per implementer agent (77) |
| Cache rewrites after a wait over 5 minutes | 106, 28.0M tokens, 15% of the index; 68 in orchestrators |
| Compactions | 4 (all in main sessions) |
| Sub-agent start-up context, median | 56k tokens (266 agents) |
| By model | Opus 56%, Sonnet 38%, Fable 6%, Haiku 0% |

| Role | Agents | Share of the index | Peak context |
|---|---|---|---|
| Orchestrator | 12 | 32% | 596k |
| Implement | 77 | 26% | 363k |
| Review | 83 | 14% | 519k |
| Fix | 61 | 10% | 277k |
| Main session | 5 | 8% | 291k |
| Plan or spec writing | 2 | 4% | 554k |
| Survey, critique, other | 31 | 6% | |

The 56k start-up context was, roughly: system prompt and built-in tool
definitions ~28k, `CLAUDE.md` and project memory ~13k, the list of available
skills ~8.5k (mostly plugins and connectors this repository does not use), the
list of loadable tools ~4k; the brief itself ~1.5k.

## Changes since the baseline

- 2026-11-26: the main session runs a plan itself; no sub-agent spans two jobs
  (`feature-workflow`, "Running a plan"). The orchestrator role mostly goes, so
  its share falls by construction; the test is the controller's cost per
  implementer agent, the idle rewrites and the compactions. Expected: a net
  saving of roughly 14-17% of the index (the critique's estimate), with the
  main session's share and compactions rising. The reviewer rule of
  2026-11-25 (Opus or Fable, never Sonnet) moves review tokens to dearer
  models at the same time, which the index does not show.
- 2026-11-26: the start-up context is trimmed for nutshell sessions on
  `automaton`, in `~/nutshell/.claude/settings.local.json` (not in the
  repository): the skills and connectors synced from claude.ai, the
  playwright plugin and the synced engineering, design and cowork plugins are
  off; desktop-commander stays. A headless session's first request fell from
  54.3k to 45.6k tokens (81 skills to 45, 141 tools to 53). Expected: the
  sub-agent start-up median falls by a similar 8-9k.
- 2026-11-26: before a merge the main session no longer reads the whole diff
  of a branch that had the two blind final reviews; it reads the diff stat,
  any file no task named and the hunks the findings cite (`feature-workflow`
  step 5). Expected: one compaction fewer on a large pull request.
