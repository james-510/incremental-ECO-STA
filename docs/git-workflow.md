# Local timing branch and team branches

The same directory can be checked out on different Git branches. Creating a
branch does not require copying or renaming the project folder. The current
feature branch is `timing-engine`; it was created from the existing local
`main`. Main's commit was not changed.

```sh
git branch --show-current
git status --short
git diff --cached --stat
git diff --cached
```

Sources, data, build scripts, docs and the portable VS Code CMake source setting
are the intended commit contents. Archives, `.DS_Store`, generated binaries,
results and external dependencies are ignored. The local ZIP is preserved under
`archives/`. Use explicit paths rather than accidentally adding downloaded trees.

To stage the reorganized project, if it is not already staged:

```sh
git add .gitignore .vscode/settings.json CMakeLists.txt Makefile README.md \
  include src tests data scripts docs bench
```

After reviewing the staged diff:

```sh
git commit -m "Organize timing starter and ECO integration scaffold"
git push -u origin timing-engine
```

The branch was created locally during reorganization. No commit or push is
performed automatically. Committing saves a local snapshot; pushing publishes
the branch to the existing team remote. Open a PR into main for team review.
Do not switch branches with unfinished changes unless you intend those changes
to follow; commit first or use a separate worktree.

## Inspect teammate branches without ZIPs

```sh
git fetch origin
git branch -r
# If the actual remote branch is named eco:
git worktree add --detach ../incremental-ECO-STA-eco-review origin/eco
```

This creates a sibling review checkout; your timing folder stays on its branch.
To update a clean detached review checkout later:

```sh
git fetch origin
git -C ../incremental-ECO-STA-eco-review switch --detach origin/eco
```

`origin/eco` is an example until verified by fetch. Fetch updates remote-tracking
branches without merging into your work. A detached checkout is suitable for
inspection; create a named branch if you want to retain commits made there.

The existing ECO files came from an extracted snapshot, not a merge of James's
branch. Ask him to review/co-ordinate the import and future moves before merging
the timing branch; a later merge may require reconciling duplicate paths. Once
the layout is agreed, exchange commits/PRs rather than copying ZIP contents.
