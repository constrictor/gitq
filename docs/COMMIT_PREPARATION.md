# Commit preparation

gitq can prepare and make a commit without leaving the history view: stage, unstage and discard
whole files or single hunks, delete new files, and write the commit message while reviewing the
diff. This document describes how it works for users and how it is built.

## Using it

### The two local changes rows

When the work tree differs from `HEAD`, the commit list starts with up to two rows, as in gitk:

| Row | Shows | Diff | Buttons |
|---|---|---|---|
| **Local uncommitted changes, not checked in to index** | unstaged changes | index → work tree | Stage, Discard / Delete |
| **Local changes checked in to index but not committed** | staged changes | `HEAD` → index | Unstage |

Only the staged changes are committed. The rows are kept apart on purpose: the staged row shows
exactly what the next commit will contain.

Untracked files (not ignored by `.gitignore`) are listed in the unstaged row with a blue `?`
badge, and on their own make the row appear. Preferences ▸ *Show untracked files in local changes*
turns this off (gitk's behaviour).

### Staging and unstaging

- **File header bars** in the diff have buttons at their right end: **Stage** and **Discard** (or
  **Delete** for an untracked file) in the unstaged row, **Unstage** in the staged row.
- **Hunks**: each `@@` line has **Stage hunk** and **Discard hunk** (unstaged row) or **Unstage
  hunk** (staged row).
- The **diff context menu** offers the same for the hunk and the file under the mouse.
- **Files pane**: the row under the mouse has a **Stage** / **Unstage** button. Several files (or a
  folder row, when grouped by folder) can be selected and staged, unstaged, discarded or deleted
  together from the context menu. **Stage all** / **Unstage all** in the pane's tool bar act on
  every file listed.

After each operation the rows, the diff and the file list are refreshed in place. The scroll
position and the file selection are kept. If the row shown becomes empty (everything was staged,
say), the other row is shown.

Hunk buttons are offered only where git can apply the hunk as shown. They are hidden when:

- whitespace changes are ignored (the hunk on screen isn't the real one);
- the old or new version of the files is shown instead of the diff;
- the file is binary, a submodule, renamed or copied, unmerged, untracked, or a file inside a
  submodule. Use the file buttons for these.

Any number of context lines works, including 0.

### Discarding and deleting

Discarding reverts a file, or a hunk, in the work tree to its staged version. Deleting removes an
untracked file. Both ask for confirmation for whole files.

Nothing is lost for good. Before a file is changed or removed, its work tree content is saved in
the repository's object database (`git hash-object -w --no-filters`) and logged in
`.git/gitq/discarded`. **Edit ▸ Undo last discard** (also in the context menus) writes the last
discarded files back. Older backups can be recovered by hand from the log with
`git cat-file blob <id>`.

### The Commit pane

The Commit pane is docked under the Files pane by default. Like the others, it can be moved, tabbed
or floated, and View ▸ Commit brings it back.

- **Message**: a monospace editor with a guide at column 72. Under it, the summary line's length
  (a hint past 50 characters, a warning past 72), a reminder to leave line 2 blank, and what is
  staged ("2 files staged +12 −4").
- **Draft**: the message is saved as you type in `.git/gitq/COMMIT_DRAFT`. It survives switching
  commits, tabs, and restarting gitq. While a merge is in progress and there is no draft, the merge's
  prepared message (`.git/MERGE_MSG`) is filled in.
- **Amend**: replaces `HEAD` with a commit of its changes plus the staged ones. Checking it puts
  `HEAD`'s message in the editor and puts the draft aside; unchecking brings the draft back. If
  `HEAD` is already on the branch's upstream, the pane warns and asks before amending.
- **Sign off** adds a `Signed-off-by` trailer. The choice is remembered.
- **Commit** (Ctrl+Return in the pane) runs `git commit`. It is enabled when there is a message
  and something is staged (or when amending, or during a merge). While it runs (hooks can take a
  while), the pane is read-only. If git refuses, for example because a `pre-commit` or `commit-msg`
  hook rejects the commit, its full output is shown and the message is kept. On success the draft
  is cleared and the new commit is selected.

Typing in the message editor never triggers gitk's single-key shortcuts (`n`, `p`, `j`, ...).

### Changes made outside gitq

gitq watches the git directory (`index`, `HEAD`, and the directory itself) and re-reads the local
changes shortly after git changes them, for example after `git add` in a terminal. Edits to work tree
files aren't watched: they are picked up when gitq becomes the active application again, after any
staging operation, and on File ▸ Update (F5). A commit, checkout or reset made elsewhere reloads the
history.

## How it is built

### Reading with libgit2, writing with git

Diffs, status and the staged summary are read with libgit2, like the rest of gitq. Everything that
changes the index, the work tree or the history runs the `git` program (`src/git/GitCli`). As a
result:

- `pre-commit`, `prepare-commit-msg`, `commit-msg` and `post-commit` hooks run;
- `commit.gpgsign` and SSH signing work;
- clean/smudge filters (git-lfs, `.gitattributes` `filter=`) are applied when staging;
- index locking, sparse checkouts and `core.fsmonitor` behave as with git itself.

`git` is therefore a runtime dependency (the `.deb` depends on it). git runs in the work tree with
`GIT_TERMINAL_PROMPT=0`, `GIT_EDITOR=true` and `LC_ALL=C`, without `GIT_DIR` / `GIT_WORK_TREE` /
`GIT_INDEX_FILE` from gitq's environment. Paths are passed as `:(literal)` pathspecs on standard
input (`--pathspec-from-file=- --pathspec-file-nul`), so any file name works and there is no limit
on how many are passed.

The diff job opens its own repository handle. libgit2 caches the index in memory, so it is re-read
before every diff, because git may have changed it.

### Operations (`src/git/Staging.{h,cpp}`)

| Operation | git command |
|---|---|
| Stage files | `git add -A` (new, modified and deleted files) |
| Unstage files | `git restore --staged` (`git rm --cached -r` before the first commit); renames pass both paths |
| Discard files | back up, then `git restore --worktree` |
| Delete untracked files | back up, then remove; folders left empty are removed too |
| Stage hunk | `git diff` → the hunk → `git apply --cached` |
| Unstage hunk | `git diff --cached` → the hunk → `git apply --cached -R` |
| Discard hunk | back up, `git diff` → the hunk → `git apply -R` |
| Commit | `git commit -F - [--amend] [--signoff]`, the message on standard input |

**Hunks.** The patch given to `git apply` is cut from git's own diff bytes, not from the text shown
on screen. That text was decoded as UTF-8, so CRLF line endings or other encodings could otherwise
corrupt the patch. The diff is made with the options libgit2 used for the display: Myers algorithm,
no indent heuristic, no renames, `a/` `b/` prefixes, no inter-hunk context, the same number of
context lines, and `diff.suppressBlankEmpty=false`. `--unidiff-zero` is added for 0 lines of
context.

**Stale-diff guard.** The hunk to apply is looked up in that fresh diff by its `@@ -a,b +c,d @@`
range *and* its lines. If the file changed since it was shown, the hunk isn't found. Nothing is
applied, the user is told, and the diff refreshes.

### UI

- `DiffView` tags every body line with the index of its hunk in the file (`DiffLineInfo::hunk`). It
  paints the buttons itself: on file header bars (including the sticky one) and at the right end of
  hunk lines. The same layout function serves painting and hit-testing. It emits
  `fileActionRequested` / `hunkActionRequested`. `setActions()` chooses the buttons for the row
  shown.
- `FileListDelegate` paints the row button and handles its clicks in `editorEvent`.
- `CommitPane` is the Commit pane. It commits with `GitCli::runAsync` so that slow hooks don't block
  the UI.
- `RepoTab` connects these to `Ops`, keeps the discard backups for undo, and refreshes the local
  changes:
  - `refreshLocalChanges()` reads the status in the background.
  - If the same rows still apply, only the diff is redone, and it is redisplayed only if it
    changed.
  - If a row appears or disappears, or `HEAD` moved, the history is reloaded.
- `MainWindow` hosts the Commit pane as a fourth dock pane (the saved layout version is 2), has
  Edit ▸ Undo last discard, and refreshes the current tab when the application becomes active.

### Tests

- `tests/tst_staging.cpp` checks each operation against the git command line. It covers:
  - modified, new, deleted and renamed files;
  - an unborn branch;
  - literal pathspecs;
  - discard and delete with restore;
  - hunks with 0 context lines, CRLF line endings and no final newline;
  - the stale-hunk guard;
  - commit, amend and sign-off;
  - a rejecting `commit-msg` hook;
  - drafts and merge messages;
  - untracked files.
- `tests/tst_mainwindow.cpp` (`commitPreparation`, `commitPaneDocked`) drives the window:
  - clicks a Stage hunk button;
  - stages a new file;
  - checks the staged row's buttons;
  - types a message (checking that list shortcuts don't fire), commits with Ctrl+Return, and
    amends.

## Limitations and possible follow-ups

- No staging of single lines within a hunk. This would be an edited hunk applied with
  `git apply --recount`.
- Hunk actions aren't offered for untracked files (stage the file instead), renames, binary files
  or submodules.
- With an unborn branch (no commit yet), the local changes rows aren't shown, as in gitk. Stage with
  git, then commit from the Commit pane.
- No stash, no conflict resolution, no partial commits of unstaged changes.
- Work tree edits are noticed on activation and after operations, not while gitq stays in the
  foreground.
