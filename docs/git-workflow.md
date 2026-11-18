# Git Workflow — Solo Developer

- Work on the existing branch, normally `main`; do not impose pull requests or feature branches.
- Check `git status` before modifying files and preserve existing uncommitted changes.
- Do not automatically stage, commit, push, create/switch branches, merge, rebase, or cherry-pick without explicit permission.
- Never discard changes or rewrite history without explicit approval; this includes `git reset --hard`, `git clean -fd`, force push, and destructive branch deletion.
- Stage only files relevant to the approved change; avoid credentials, secrets, build outputs, and machine-specific configuration.
- After editing, inspect the diff, run applicable verification, and summarize changed files.
- Keep feature changes and unrelated refactoring separate when practical.
- The developer ordinarily reviews changes, commits to `main`, and pushes to GitHub through VS Code Source Control.
