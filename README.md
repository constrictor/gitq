# gitq

A reimplementation of **gitk**, the git history browser, in C++ with Qt 6 Widgets and libgit2.
The goal is gitk's behaviour and layout on a codebase that is easy to extend.

## Build

Requirements: CMake ≥ 3.28, a C++20 compiler, Qt 6 (Widgets, Concurrent, Test, private GUI headers),
libxcb, libgit2 ≥ 1.7; at run time, `git` (≥ 2.26) for staging and committing. The [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)
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
- Visual Studio style dockable panes: drag the Commits, Diff, Files or Commit title bar (or its tab, shown at the bottom when panes share an area) to dock it beside another
  pane, tab it with one, or float it in its own window. To dock a floating pane again, drag its title bar onto one of the drop markers
  that appear over the main window. All repository tabs share the layout, which is remembered; View ▸ Reset layout
  restores the gitk arrangement and the View menu reopens closed panes.
- Commit list with graph (one colour per lane, curved lane changes, hollow merge commits, a ring around
  HEAD), ref labels with icons (heads, remotes, tags; the checked-out branch in bold; a branch and its
  upstream at the same commit share a label, otherwise the branch shows ↑ahead ↓behind), author, date
  (relative, "3 hours ago", with the exact time in the tooltip; Preferences switch to absolute dates);
  the row under the mouse is highlighted; streaming load; long edges elided with arrows like gitk; local
  uncommitted / staged changes rows.
- Multi-line commit list (View menu): author and date under the headline, the commit id at the top
  right (shortened when the row is narrow).
- Find bar above the list: back/forward history buttons, one search field with match-case (Aa) and
  regex (.*) toggles and previous / next arrows, and a chip choosing what to search: text in all fields /
  headline / message / author / committer, *touching paths*, *adding/removing string* (`-S`), *changing
  lines matching* (`-G`); matches are bold, Return / Shift-Up / Shift-Down / `?` move between them.
  The chip's *commit id* mode (`g`) goes to a commit by id prefix, ref name or any revision.
- Details pane: a header card with the author's initials in a coloured circle, the subject as a heading,
  author and committer with relative and exact dates, the commit id (click to copy), the commit's ref
  labels, clickable Parent/Child links and Branches / Follows / Precedes (computed in the background);
  then the message and the diff. Merges show a combined (`--cc`) diff. Each file starts with a header bar
  (status badge, directory and file name, +added / −removed lines and a five-block bar); the header of
  the file being read sticks to the top of the pane, and clicking it goes back to the file's start.
  Removed and added lines have tinted backgrounds (the text keeps its colour, the +/- marks take the
  line's); in a removed line and the most similar added line the changed words get a stronger tint.
  Code is syntax highlighted by a small built-in highlighter (keywords, types, strings, comments,
  numbers, functions, preprocessor lines, markup tags) for C/C++, Java/Kotlin/C#, JavaScript/TypeScript,
  Go, Rust, Swift, PHP, Python, shell, Ruby, Perl, Tcl, CMake, Make, SQL, Lua, JSON, XML/HTML, CSS and
  config files; old and new sides are followed separately through block comments and multi-line
  strings. Preferences can turn it off.
  Side by side, the old version is on the left and the new on the right, similar lines facing each
  other, missing lines hatched and long lines wrapped to the column width (laid out again when the
  pane is resized); the new side's line numbers are between the columns.
  A strip in the diff's scroll bar marks where the removed and added lines, the files and the search
  hits are.
  A gutter shows old and new line numbers (the result's for merges; one column in Tree mode); clicking
  it selects the line, right-clicking it opens the line's menu.
- Diff tool bar in one compact row: Diff / Old / New version, unified or side-by-side diff, lines of
  context, ignore space change,
  line / markup words / color words, diff search (finds as you type; arrows or Ctrl-R / Ctrl-S for the
  previous / next match); the ⋯ menu wraps long lines and chooses how submodule changes are shown.
- File list: Patch mode (scrolls to the file, tracks the file on screen) and Tree mode (file contents);
  each file has a status badge and its added / removed line counts (with a five-block bar when the
  pane is wide enough); the folder button groups the files under a row per folder.
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

## Commit preparation

Beyond gitk, gitq prepares and makes commits (details in [docs/COMMIT_PREPARATION.md](docs/COMMIT_PREPARATION.md)):

- The *Local uncommitted changes* row (unstaged, now including untracked files) and the *Local changes
  checked in to index* row (staged, what gets committed) stay separate, as in gitk.
- Stage / discard (delete, for new files) or unstage whole files from buttons on the diff's file
  header bars, the file list (a button on the row under the mouse, multi-selection, Stage all /
  Unstage all, context menu) or the diff's context menu.
- Stage / discard / unstage single hunks from buttons on each `@@` line. A hunk is looked up again
  before it is applied, so a file changed in the meantime is never patched wrongly.
- Discarded and deleted content is backed up in the object database; Edit ▸ Undo last discard
  restores it.
- The Commit pane: message editor with a 72-column guide and summary length hints, a draft kept
  across restarts, amend (with a warning for pushed commits), sign-off, and Commit (Ctrl+Return).
  It runs `git commit`, so hooks and commit signing work.
- The local changes refresh on their own after git changes the repository and when gitq becomes the
  active application.

## Differences from gitk

- Blame opens a built-in blame window instead of launching `git gui blame`.
- History operations go through libgit2, so **git hooks are not run** for cherry-pick/revert
  commits. Conflicted cherry-picks/reverts are left in the work tree for resolution with git.
  Staging and committing (see above) run the `git` program instead, so hooks, signing and filters
  such as git-lfs apply there.
- Commit preparation (staging hunks and files, the Commit pane) is gitq's own; gitk leaves it to
  `git gui`.
- `--first-parent` views show only the followed parent (git prints all parents).
- Settings live in `~/.config/gitq/gitq.conf`, not `~/.config/git/gitk`.
- The commit list, diff, file list and commit message are dockable panes rather than a fixed layout.

## Layout

```
src/git/     git layer (no widgets): Repository, RevArgs, LogLoader, DiffEngine, Ops, Search,
             Blame, NearbyRefs (libgit2); GitCli (runs git), Staging (stage / unstage / discard
             files and hunks, commit)
src/graph/   GraphLayout: incremental lane assignment with long-edge elision
src/model/   CommitModel (list + graph), FileListModel
src/ui/      MainWindow (tabs, menus, panes), RepoTab (one repository's views and commands), WelcomePage,
             GraphDelegate, DiffView, CommitPane, dialogs, BlameWindow
tests/       Qt Test suites; fixtures are built with the git CLI and compared with git's output.
             bench_load / bench_diff are manual benchmarks.
```

Debug aid: `GITQ_SCREENSHOT=out.png gitq ...` saves a screenshot after loading and exits;
`GITQ_TIMING=1` prints load times.

## License

GPL v2 (see `COPYING`), like gitk.
