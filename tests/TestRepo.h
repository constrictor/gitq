// Helper to build throwaway git repositories with the git CLI for tests.
#pragma once

#include "git/GitPtr.h"
#include "git/Repository.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

namespace gitq::test {

class TestRepo {
public:
    TestRepo() {
        git({"init", "-q", "-b", "master"});
        git({"config", "user.name", "Tester"});
        git({"config", "user.email", "tester@example.com"});
        git({"config", "commit.gpgsign", "false"});
    }

    QString path() const { return m_dir.path(); }

    QString git(const QStringList &args, bool mustSucceed = true) {
        QProcess p;
        p.setWorkingDirectory(m_dir.path());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString date = QStringLiteral("%1 +0000").arg(1700000000 + m_tick * 60);
        env.insert(QStringLiteral("GIT_AUTHOR_DATE"), date);
        env.insert(QStringLiteral("GIT_COMMITTER_DATE"), date);
        env.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
        env.insert(QStringLiteral("HOME"), m_dir.path());
        p.setProcessEnvironment(env);
        p.start(QStringLiteral("git"), args);
        p.waitForFinished(30000);
        QString out = QString::fromUtf8(p.readAllStandardOutput());
        if (mustSucceed && (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0))
            qFatal("git %s failed: %s", qPrintable(args.join(' ')), p.readAllStandardError().constData());
        return out;
    }

    void write(const QString &file, const QByteArray &content) {
        QFileInfo fi(QDir(m_dir.path()).filePath(file));
        QDir().mkpath(fi.absolutePath());
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::WriteOnly)) qFatal("cannot write %s", qPrintable(file));
        f.write(content);
    }

    // Writes files, stages everything and commits; returns the new commit id.
    QString commit(const QString &message, const QList<QPair<QString, QByteArray>> &files = {}) {
        for (const auto &f : files) write(f.first, f.second);
        git({"add", "-A"});
        ++m_tick;
        git({"commit", "-q", "--allow-empty", "-m", message});
        return rev("HEAD");
    }

    QString rev(const QString &spec) { return git({"rev-parse", spec}).trimmed(); }

    // Adds the repository at `url` as a submodule at `path` (staged, not committed).
    void addSubmodule(const QString &url, const QString &path) {
        git({"-c", "protocol.file.allow=always", "submodule", "add", "-q", url, path});
    }

    // Checks out `commit` (fetched from its origin first) in the submodule at `path` (not staged).
    void moveSubmodule(const QString &path, const QString &commit) {
        git({"-C", path, "fetch", "-q", "origin"});
        git({"-C", path, "checkout", "-q", commit});
    }

    Repository open() const { return Repository::open(m_dir.path()); }

    int m_tick = 0;

private:
    QTemporaryDir m_dir;
};

} // namespace gitq::test
