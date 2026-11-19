# gitq

A reimplementation of **gitk**, the git history browser, in C++ with Qt 6 Widgets and libgit2.
The goal is gitk's behaviour and layout on a codebase that is easy to extend.

## Build

Requirements: CMake ≥ 3.28, a C++20 compiler, Qt 6 (Widgets, Concurrent, Test, private GUI headers),
libxcb, libgit2 ≥ 1.7. The [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)
is downloaded at configure time and linked statically.

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build          # unit + GUI smoke tests (run offscreen)
./build/gitq --all              # run inside any git repository
```

Ubuntu packages: `apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-base-private-dev libxcb1-dev libgit2-dev`.

### Packages

CI (`.github/workflows/ci.yml`) builds and tests on Ubuntu 24.04 and 26.04 and produces a `.deb` and an
AppImage for each; pushing a `v*` tag attaches them to a GitHub release. `packaging/ci-build.sh` does
the whole build inside an `ubuntu:<version>` container. To build just the `.deb` locally:
`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && (cd build && cpack -G DEB)`.

## Usage

The command line is gitk's: `gitq [rev-list options] [revisions] [--] [paths]`, e.g.
`gitq --all`, `gitq master..topic`, `gitq v1.0...v2.0`, `gitq --author=alice -- src/`.

Each repository opens in a tab, and the open tabs are remembered. Started inside a repository, gitq
reopens the previous tabs and adds (or switches to) that repository, applying the command line to it;
started elsewhere, it just reopens the previous tabs. With no tab open, a welcome page lists the
recently opened repositories. File ▸ Open repository (Ctrl-O) opens another one; Ctrl-W closes a tab,
Ctrl-Tab / Ctrl-Shift-Tab switch between them.

Supported rev-list options: revisions, `^rev`, `a..b`, `a...b`, `rev^!`, `rev^@`, `--not`, `--all`,
`--branches[=glob]`, `--tags[=glob]`, `--remotes[=glob]`, `--glob=`, `--date-order`, `--topo-order`,
`--first-parent`, `--merges`, `--no-merges`, `--min-parents`, `--max-parents`, `-n`/`--max-count`,
`--skip`, `--since`/`--after`, `--until`/`--before`, `--author`, `--committer`, `--grep`,
`--all-match`, `-i`, `-F`, `-E`, `--full-history`. Path limiting uses git's default history
simplification and parent rewriting (tested against `git rev-list --parents`).

## Features (gitk parity)

- Tabs: several repositories in one window, restored on the next start; welcome page with recent
  repositories.
- Visual Studio style dockable panes: drag the Commits, Diff or Files title bar (or its tab, shown at the bottom when panes share an area) to dock it beside another
  pane, tab it with one, or float it in its own window. To dock a floating pane again, drag its title bar onto one of the drop markers
  that appear over the main window. All repository tabs share the layout, which is remembered; View ▸ Reset layout
  restores the gitk arrangement and the View menu reopens closed panes.
- Commit list with graph (one colour per lane, curved lane changes, hollow merge commits, a ring around
  HEAD), ref labels with icons (heads, remotes, tags; the checked-out branch in bold; a branch and its
  upstream at the same commit share a label, otherwise the branch shows ↑ahead ↓behind), author, date
  (relative, "3 hours ago", with the exact time in the tooltip; Preferences switch to absolute dates);
  the row under the mouse is highlighted; streaming load; long edges elided with arrows like gitk; local
  uncommitted / staged changes rows.
- SHA1 field (goto by id prefix, ref name or any revision), back/forward history, row counter.
- Find bar: one search field with match-case (Aa) and regex (.*) toggles and previous / next arrows, and
  a chip choosing what to search: text in all fields / headline / message / author / committer,
  *touching paths*, *adding/removing string* (`-S`), *changing lines matching* (`-G`); matches are
  bold, Return / Shift-Up / Shift-Down / `?` move between them.
- Details pane: a header card with the author's initials in a coloured circle, the subject as a heading,
  author and committer with relative and exact dates, the commit id (click to copy), the commit's ref
  labels, clickable Parent/Child links and Branches / Follows / Precedes (computed in the background);
  then the message and the diff. Merges show a combined (`--cc`) diff. Each file starts with a header bar
  (status badge, directory and file name, +added / −removed lines and a five-block bar); the header of
  the file being read sticks to the top of the pane, and clicking it goes back to the file's start.
- Diff tool bar in one compact row: Diff / Old / New version, lines of context, ignore space change,
  line / markup words / color words, diff search (finds as you type; arrows or Ctrl-R / Ctrl-S for the
  previous / next match); the ⋯ menu wraps long lines and chooses how submodule changes are shown.
- File list: Patch mode (scrolls to the file, tracks the file on screen) and Tree mode (file contents).
- Submodules: a commit that moves a submodule lists the submodule's commits in between
  (⋯ ▸ Submodule changes: *Commits in between*, the default); *[show files]* adds its file changes to the diff and file list
  as ordinary files under the submodule's path, *Full diff* does that for every submodule, *Summary*
  shows git's `Subproject commit` lines. Works for nested submodules and local changes inside them.
  Commit ids in the list and *[open in tab]* open the submodule in a tab of its own; blame, line
  origin, external diff and Tree mode reach into submodules; a path inside a submodule matches the
  commits that move it.
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
- The commit list, diff and file list are dockable panes rather than a fixed layout.

## Layout

```
src/git/     libgit2 layer (no widgets): Repository, RevArgs, LogLoader, DiffEngine, Ops, Search,
             Blame, NearbyRefs
src/graph/   GraphLayout: incremental lane assignment with long-edge elision
src/model/   CommitModel (list + graph), FileListModel
src/ui/      MainWindow (tabs, menus, panes), RepoTab (one repository's views and commands), WelcomePage,
             GraphDelegate, DiffView, dialogs, BlameWindow
tests/       Qt Test suites; fixtures are built with the git CLI and compared with git's output.
             bench_load / bench_diff are manual benchmarks.
```

Debug aid: `GITQ_SCREENSHOT=out.png gitq ...` saves a screenshot after loading and exits;
`GITQ_TIMING=1` prints load times.

## License

GPL v2 (see `COPYING`), like gitk.
