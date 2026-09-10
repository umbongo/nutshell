# Protect-Main.ps1 — make "the integration tests passed" the guardrail for
# main, as a repository ruleset (the legacy branch-protection API is disabled
# on repositories that use rulesets, which this one does).
#
# The ruleset makes main accept changes only through a pull request whose
# required status checks -- "Integration tests" (the job in
# .github/workflows/integration.yml) and "Version bump" (the job in
# .github/workflows/checks.yml) -- have succeeded on the pull request's latest
# commit, with the branch up to date with main so the checks ran against what
# will actually land. Force pushes and deletion of main are blocked. No
# reviewer approval is required (a one-person project); raise
# required_approving_review_count if that changes. No bypass actors are
# configured, so the rule binds administrators too.
#
# The integration tests run once per pull request, when it is marked ready for
# review -- so a draft pull request is expected to sit with that check absent,
# and `gh pr ready` is what opens the gate. strict_required_status_checks_policy
# stays on, which means a pull request that has already gone green has to run
# again if main moves under it: that re-run is testing a genuinely different
# tree, which is the point.
#
# Prerequisites: GitHub CLI logged in as a repository administrator.
# Usage:  .\tests\integration\Protect-Main.ps1          (create or update)
#         .\tests\integration\Protect-Main.ps1 -Show    (list rulesets)
#         .\tests\integration\Protect-Main.ps1 -Remove  (delete the ruleset)

param(
    [string] $Repo = "umbongo/nutshell",
    [string] $Branch = "main",
    [string[]] $CheckNames = @("Integration tests", "Version bump"),
    [string] $RulesetName = "main: pull requests with green integration tests",
    # Pre-rename ruleset name. Matched as a fallback so that re-running this
    # updates the existing ruleset in place rather than creating a second one
    # beside it -- two active rulesets both apply, and the stale one would go
    # on requiring a "BVT" check that no workflow reports any more, blocking
    # every merge into main.
    [string] $LegacyRulesetName = "main: pull requests with green BVT",
    [switch] $Show,
    [switch] $Remove
)

$ErrorActionPreference = "Stop"
$gh = (Get-Command gh -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
if (-not $gh) { $gh = "C:\Program Files\GitHub CLI\gh.exe" }
if (-not (Test-Path $gh)) { throw "GitHub CLI (gh) not found. Install it with: winget install GitHub.cli" }
& $gh auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "gh is not logged in. Run: gh auth login" }

$all = (& $gh api "repos/$Repo/rulesets" | ConvertFrom-Json)
$existing = $all | Where-Object { $_.name -eq $RulesetName }
if (-not $existing) {
    $existing = $all | Where-Object { $_.name -eq $LegacyRulesetName }
    if ($existing) { Write-Host "Found the pre-rename ruleset '$LegacyRulesetName' (id $($existing.id)); updating it in place." }
}

if ($Show) {
    & $gh api "repos/$Repo/rulesets" --jq '.[] | [.id, .name, .enforcement] | @tsv'
    exit $LASTEXITCODE
}

if ($Remove) {
    if (-not $existing) { Write-Host "No ruleset named '$RulesetName'."; exit 0 }
    & $gh api -X DELETE "repos/$Repo/rulesets/$($existing.id)" | Out-Null
    Write-Host "Ruleset '$RulesetName' removed; main is unprotected."
    exit 0
}

$body = @{
    name        = $RulesetName
    target      = "branch"
    enforcement = "active"
    conditions  = @{ ref_name = @{ include = @("refs/heads/$Branch"); exclude = @() } }
    rules       = @(
        @{ type = "deletion" },
        @{ type = "non_fast_forward" },
        @{ type = "pull_request"; parameters = @{
            required_approving_review_count   = 0
            dismiss_stale_reviews_on_push     = $false
            require_code_owner_review         = $false
            require_last_push_approval        = $false
            required_review_thread_resolution = $false } },
        @{ type = "required_status_checks"; parameters = @{
            strict_required_status_checks_policy = $true
            required_status_checks = @($CheckNames | ForEach-Object { @{ context = $_ } }) } }
    )
} | ConvertTo-Json -Depth 8

$tmp = [IO.Path]::GetTempFileName()
try {
    [IO.File]::WriteAllText($tmp, $body, (New-Object Text.UTF8Encoding $false))
    if ($existing) {
        & $gh api -X PUT "repos/$Repo/rulesets/$($existing.id)" --input $tmp | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "gh api returned $LASTEXITCODE" }
        Write-Host "Ruleset '$RulesetName' updated (id $($existing.id))."
    } else {
        $created = (& $gh api -X POST "repos/$Repo/rulesets" --input $tmp | ConvertFrom-Json)
        if ($LASTEXITCODE -ne 0) { throw "gh api returned $LASTEXITCODE" }
        Write-Host "Ruleset '$RulesetName' created (id $($created.id))."
    }
} finally { Remove-Item $tmp -ErrorAction SilentlyContinue }

Write-Host "main: pull requests only, $(($CheckNames | ForEach-Object { "'$_'" }) -join ' and ') must pass on the latest commit, branch up to date, no force pushes, no deletion."
