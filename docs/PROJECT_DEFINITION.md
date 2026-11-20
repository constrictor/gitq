# gitq — Project Definition

## 1. Summary

gitq is a desktop application for browsing and inspecting the history of git repositories. It
presents the commit graph, commit details and diffs in a single window, lets the user search and
filter history, offers a small set of everyday history operations (branching, tagging,
cherry-pick, revert, reset) directly from the history view, and prepares commits: staging and
discarding files and hunks and writing the commit message while reviewing the diff.

It is written in C++20 with Qt 6 Widgets and libgit2, targets Linux desktops, and is distributed
under the GPL v2.

## 2. Problem statement

Developers regularly need to answer questions about a repository's past: what changed, when, by
whom, on which branch, and why. Command-line tools answer these one query at a time and do not show
the shape of history. Existing graphical browsers tend to be either heavyweight IDE-style clients
focused on committing and syncing, or older tools that are hard to extend and do not integrate with
modern desktops (themes, high-DPI, flexible layouts).

gitq fills the gap with a fast, focused history browser that:

- opens instantly on large repositories and streams history as it loads;
- accepts the same revision and filtering syntax developers already use with git;
- is built on a clean, layered codebase that is straightforward to test and extend.

## 3. Goals

1. **History browsing.** Show the commit graph with branch/tag/remote labels, and let the user
   inspect any commit's metadata, message and diff.
2. **git-native selection.** Accept `git rev-list` style arguments (revisions, ranges, ref globs,
   date/author/message filters, path limiting) and produce the same commit set and parent
   relationships git does.
3. **Search.** Find commits by text in any field, by touched paths, by added/removed strings and by
   changed lines matching a pattern; search within diffs.
4. **Everyday operations.** Create branches and tags, check out, cherry-pick, revert, reset, write
   patches, and compare arbitrary commits, all from context menus.
5. **Commit preparation.** Stage, unstage and discard whole files or single hunks, delete new
   files, and commit (or amend) the staged changes with a message written while reviewing them,
   without leaving the history view.
6. **Performance.** Remain responsive on repositories with hundreds of thousands of commits: load
   incrementally, lay out the graph incrementally, and run expensive work (reference reachability,
   searches) in the background.
7. **Desktop integration.** Follow the system light/dark theme, support configurable fonts and
   colours, remember window and pane layout, and install as a regular desktop application.
8. **Maintainability.** Keep git access, graph layout, models and UI in separate layers with
   automated tests that compare results against the git CLI.

## 4. Non-goals

- Line-level (partial hunk) staging, stashing, and editing files in the work tree.
- Network operations: fetch, pull, push, remote management.
- Merge/rebase conflict resolution UI; conflicted operations are left for the user to finish with
  git.
- Hosting-service integration (pull requests, issues, CI status).
- Platforms other than Linux, for now.

## 5. Target users

- Developers who live on the command line and want a visual view of history that understands the
  revision syntax they already know.
- Reviewers and maintainers who need to trace where a change came from, which releases contain it,
  and how branches relate.

## 6. Functional scope

### 6.1 Launch and repository selection

- `gitq [rev-list options] [revisions] [--] [paths]`, run inside a repository (or with `GIT_DIR`).
- Each repository opens in its own tab; open tabs are restored on the next start. Started inside a
  repository, gitq adds (or switches to) that repository's tab and applies the command line to it.
- With no tab open, a welcome page lists recently opened repositories; further repositories can be
  opened from the File menu.
- Paths given on the command line are relative to the current directory.
- `--help` and `--version` work without a display.

### 6.2 Commit list

- Graph column with lane assignment, merge/fork edges, and elision of long edges (drawn as arrows).
- Reference labels for local branches, remote branches, tags, with the checked-out branch emphasised.
- Author and date columns (relative or absolute dates); rows for uncommitted and staged local changes.
- Streaming load with a row counter; selection by SHA prefix, ref name or any revision expression;
  back/forward navigation history.

### 6.3 Commit details and diff

- Author, committer, tags, clickable parent/child links, and the branches containing the commit and
  the nearest tags before/after it (computed in the background).
- Unified or side-by-side diff with configurable context and whitespace handling, word-level diff
  modes, syntax highlighting, and views of the old or new file version. Merge commits show a combined diff.
- Search within the diff.
- Submodule changes: the submodule's commits between the two recorded commits, and optionally its
  file changes shown inline as files under the submodule's path; opening the submodule in its own tab.

### 6.4 File list

- Patch mode: lists files changed by the commit with their status and line counts, and keeps the
  selection in sync with the diff position. Files can be grouped by folder.
- Tree mode: browse the commit's tree and view file contents.

### 6.5 Search and views

- Find bar: text in all fields or a specific one (headline, message, author, committer), touched
  paths, string added/removed (pickaxe), lines changed matching a regex; exact, case-insensitive or
  regex matching; next/previous navigation with highlighted matches.
- Named views: saved combinations of revisions, filters and paths that can be created, edited,
  deleted and remembered between sessions.

### 6.6 Operations

- Commit context menu: diff against selected/marked commit, make patch, create tag, create branch,
  copy commit reference, write commit to file, cherry-pick, revert, reset (soft/mixed/hard),
  mark/return to mark/compare with mark, find common descendant.
- Branch label menu: checkout, rename, remove, copy name.
- File menu: highlight commits touching the file, external diff, blame at the parent or at this
  commit, copy path.
- Diff menu: show the origin of a line, blame a line.
- Built-in blame window.

### 6.7 Commit preparation

Specified in detail in [COMMIT_PREPARATION.md](COMMIT_PREPARATION.md).

- Two local changes rows, kept separate: unstaged (index → work tree, including untracked files
  unless turned off) and staged (HEAD → index, what the next commit contains).
- File level: stage, unstage, discard, delete untracked files; from the diff's file header bars,
  the file list (row button, multi-selection, stage all / unstage all, context menu) and the diff's
  context menu.
- Hunk level: stage, unstage and discard single hunks from buttons on each hunk. A hunk is matched
  again against a fresh diff before it is applied; a file changed in the meantime is refused.
- Discarded and deleted content is backed up in the object database and can be restored (undo
  last discard).
- Commit pane: message editor with line-length guidance and a persistent draft, amend (warning
  when HEAD is already pushed), sign-off, commit through `git commit` so that hooks and signing
  apply; hook output is shown when a commit is refused.
- The local changes refresh by themselves when git changes the repository and when the
  application becomes active.

### 6.8 Layout and preferences

- Commit list, diff, file list and commit message are dockable panes: they can be docked side by side, tabbed
  together, or floated, with the layout shared by all repository tabs, remembered and resettable.
- Preferences for general behaviour, colours and fonts; a reference list window; a key-binding
  reference.
- Settings are stored in `~/.config/gitq/gitq.conf`.

## 7. Non-functional requirements

| Area            | Requirement                                                                                 |
|-----------------|---------------------------------------------------------------------------------------------|
| Correctness     | Commit selection, ordering and parent rewriting match `git rev-list --parents` for the supported options. |
| Responsiveness  | First rows appear while history is still loading; background work never blocks the UI thread. |
| Safety          | Destructive operations (hard reset, branch removal, discarding or deleting files) require confirmation; discarded content is backed up and can be restored. History operations run through libgit2, so git hooks are not executed for them; this is documented. Staging and committing run the `git` program, so hooks, signing and filters apply. |
| Portability     | Builds with CMake ≥ 3.28 and a C++20 compiler on current Ubuntu LTS and interim releases.   |
| Packaging       | Each release ships a `.deb` and an AppImage per supported Ubuntu version.                    |
| Quality         | Every change passes the unit and offscreen GUI test suites in CI.                           |
| Licensing       | GPL v2.                                                                                     |

## 8. Architecture

```
src/git/     git layer, no widgets: repository access, revision-argument parsing, streaming log
             loader, diff engine, operations, search, blame, nearby-reference computation (libgit2);
             the git program runner and commit preparation (staging, discarding, committing)
src/graph/   incremental graph lane layout with long-edge elision
src/model/   Qt item models for the commit list (with graph) and the file list
src/ui/      main window, panes, delegates, diff view, commit pane, dialogs, blame window
tests/       Qt Test suites; fixtures are created with the git CLI and results compared with git
```

Dependencies: Qt 6 (Widgets, Concurrent, Test), libgit2 ≥ 1.7, and the Qt Advanced Docking System
(fetched and linked statically at build time). At run time, the `git` program (≥ 2.26) for staging
and committing.

Layering rules:

- `git/`, `graph/` and `model/` do not depend on widgets and are tested without a UI.
- The UI talks to git only through the `git/` layer. Reading uses libgit2; changes to the index, the
  work tree and history made for commit preparation run the `git` program (`GitCli`).
- Long-running work runs on worker threads and reports back through signals.

## 9. Delivery

- **Build:** `cmake -B build -G Ninja && cmake --build build`.
- **Test:** `ctest --test-dir build` (unit tests plus GUI smoke tests run offscreen).
- **CI:** GitHub Actions builds, tests and packages on Ubuntu 24.04 and 26.04 for every push to the
  default branch and every pull request; `v*` tags publish the packages as a GitHub release.

## 10. Success criteria

- Opening a large repository (100k+ commits) shows the first screen of history within a second and
  stays responsive while the rest loads.
- For every supported rev-list option, the displayed commits and parents match git's output in the
  test suite.
- The common review workflow — find a commit, read its diff, see which branches and tags contain
  it, blame a line, compare with another commit — is possible without leaving the application.
- The package installs and runs on each supported Ubuntu release from CI artifacts alone.

## 11. Future directions

- Additional platforms (macOS, Windows).
- Optional running of git hooks for operations that create commits.
