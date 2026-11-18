# gitq

A reimplementation of **gitk**, the git history browser, in C++ with Qt 6 Widgets and libgit2.
The goal is gitk's behaviour and layout on a codebase that is easy to extend.

## Build

Requirements: CMake ≥ 3.21, a C++20 compiler, Qt 6 (Widgets, Concurrent, Test), libgit2 ≥ 1.7.

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build          # unit + GUI smoke tests (run offscreen)
./build/gitq --all              # run inside any git repository
```

## Usage

The command line is gitk's: `gitq [rev-list options] [revisions] [--] [paths]`, e.g.
`gitq --all`, `gitq master..topic`, `gitq v1.0...v2.0`, `gitq --author=alice -- src/`.

Supported rev-list options: revisions, `^rev`, `a..b`, `a...b`, `rev^!`, `rev^@`, `--not`, `--all`,
`--branches[=glob]`, `--tags[=glob]`, `--remotes[=glob]`, `--glob=`, `--date-order`, `--topo-order`,
`--first-parent`, `--merges`, `--no-merges`, `--min-parents`, `--max-parents`, `-n`/`--max-count`,
`--skip`, `--since`/`--after`, `--until`/`--before`, `--author`, `--committer`, `--grep`,
`--all-match`, `-i`, `-F`, `-E`, `--full-history`. Path limiting uses git's default history
simplification and parent rewriting (tested against `git rev-list --parents`).

## Features (gitk parity)

- Commit list with graph, ref labels (heads, remotes, tags, HEAD in bold), author, date;
  streaming load; long edges elided with arrows like gitk; local uncommitted / staged changes rows.
- SHA1 field (goto by id prefix, ref name or any revision), back/forward history, row counter.
- Find bar: *containing* (all fields / headline / comments / author / committer), *touching paths*,
  *adding/removing string* (`-S`), *changing lines matching* (`-G`); Exact / IgnCase / Regexp;
  matches are bold, Return / Shift-Up / Shift-Down / `?` move between them.
- Details pane: author/committer, tags, clickable Parent/Child links, Branches / Follows / Precedes
  (computed in the background), message, diff. Merges show a combined (`--cc`) diff.
- Diff options: lines of context, ignore space change, line / markup words / color words,
  Diff / Old version / New version, diff search (Search field, Ctrl-S / Ctrl-R).
- File list: Patch mode (scrolls to the file, tracks the file on screen) and Tree mode (file contents).
- Views: new / edit / delete, remembered views, gitk's view dialog fields.
- Context menus: diff this ↔ selected / marked, make patch, create tag, copy commit reference,
  write commit to file, create branch, cherry-pick, revert, reset (soft/mixed/hard), mark / return to
  mark / find common descendant / compare with marked; branch label menu (checkout, rename, remove,
  copy); file menu (highlight this too/only, external diff, blame parent/this commit, copy path);
  diff menu (show origin of this line, blame this line).
- Preferences (general, colours, fonts), List references (F2), gitk key bindings (Help ▸ Key bindings).

## Differences from gitk

- Blame opens a built-in blame window instead of launching `git gui blame`.
- Operations go through libgit2, so **git hooks are not run** for cherry-pick/revert commits.
  Conflicted cherry-picks/reverts are left in the work tree for resolution with git.
- `--first-parent` views show only the followed parent (git prints all parents).
- Settings live in `~/.config/gitq/gitq.conf`, not `~/.config/git/gitk`.

## Layout

```
src/git/     libgit2 layer (no widgets): Repository, RevArgs, LogLoader, DiffEngine, Ops, Search,
             Blame, NearbyRefs
src/graph/   GraphLayout: incremental lane assignment with long-edge elision
src/model/   CommitModel (list + graph), FileListModel
src/ui/      MainWindow, GraphDelegate, DiffView, dialogs, BlameWindow
tests/       Qt Test suites; fixtures are built with the git CLI and compared with git's output.
             bench_load / bench_diff are manual benchmarks.
```

Debug aid: `GITQ_SCREENSHOT=out.png gitq ...` saves a screenshot after loading and exits;
`GITQ_TIMING=1` prints load times.

## License

GPL v2 (see `COPYING`), like gitk.
