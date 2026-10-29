# Branch protection

Recommended settings for `main`: pull request required, required status checks, linear history, squash merge only, no force-push or deletion.

## Repository merge settings

```
gh repo edit ivanerricis/pentagrit \
  --enable-squash-merge --enable-merge-commit=false --enable-rebase-merge=false \
  --delete-branch-on-merge --enable-auto-merge
```

## Ruleset via `gh api`

Adjust the check names to those emitted by the CI workflows (see the check names on a recent PR).

```
gh api -X POST repos/ivanerricis/pentagrit/rulesets --input - <<'JSON'
{
  "name": "protect-main",
  "target": "branch",
  "enforcement": "active",
  "conditions": { "ref_name": { "include": ["~DEFAULT_BRANCH"], "exclude": [] } },
  "rules": [
    { "type": "deletion" },
    { "type": "non_fast_forward" },
    { "type": "required_linear_history" },
    { "type": "pull_request", "parameters": {
        "required_approving_review_count": 0,
        "dismiss_stale_reviews_on_push": true,
        "require_code_owner_review": false,
        "require_last_push_approval": false,
        "required_review_thread_resolution": false,
        "allowed_merge_methods": ["squash"] } },
    { "type": "required_status_checks", "parameters": {
        "strict_required_status_checks_policy": true,
        "required_status_checks": [
          { "context": "lint" },
          { "context": "build / windows" },
          { "context": "build / linux" }
        ] } }
  ]
}
JSON
```

List or remove rulesets: `gh api repos/ivanerricis/pentagrit/rulesets`, `gh api -X DELETE repos/ivanerricis/pentagrit/rulesets/<id>`.

macOS is only built on `main` or with the `ci:macos` label, so do not make it a required check.

## Private repository limitations

On the GitHub Free plan, rulesets and branch protection rules are **not enforced on private repositories** (they need GitHub Pro, Team or Enterprise); the API call may be rejected or the rules saved but inactive. Alternatives:

- Make the repository public (rules are enforced for free; see the AGPL note in `RELEASING.md`).
- Upgrade to GitHub Pro.
- Rely on discipline: the squash-only merge settings above (`gh repo edit`) still work on Free, together with the PR template and CODEOWNERS. Never push directly to `main`.
