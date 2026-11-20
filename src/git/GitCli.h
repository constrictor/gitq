// Running the git command line. Operations that change the index, the work tree or create commits go
// through git itself, so hooks, signing, clean/smudge filters (git-lfs) and index locking behave
// exactly as on the command line. Reading stays on libgit2.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class QObject;

namespace gitq {

struct GitRun {
    bool started = false;   // false: git could not be run (not installed, timed out, ...)
    int exitCode = -1;
    QByteArray out, err;

    bool ok() const { return started && exitCode == 0; }
    // What to tell the user when the command failed: git's own output, or why it didn't run.
    QString errorText() const;
};

namespace GitCli {

// Path of the git executable, empty if none is found in PATH.
QString executable();

// Runs `git args...` in `workdir`, writing `input` to its standard input, and waits for it.
GitRun run(const QString &workdir, const QStringList &args, const QByteArray &input = {}, int timeoutMs = 120000);

// Runs git without waiting; `done` is called (in the thread of `context`) when it has finished.
// Used for commands that may run slow hooks. Nothing is called if `context` is destroyed first.
void runAsync(const QString &workdir, const QStringList &args, const QByteArray &input, QObject *context,
              std::function<void(const GitRun &)> done);

// A repository-relative path as a literal pathspec (no glob or magic interpretation).
QString literal(const QString &path);

} // namespace GitCli
} // namespace gitq
