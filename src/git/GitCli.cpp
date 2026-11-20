#include "git/GitCli.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>

namespace gitq {

QString GitRun::errorText() const {
    if (!started) return err.isEmpty() ? QStringLiteral("git could not be run.") : QString::fromUtf8(err);
    QString text = QString::fromUtf8(err).trimmed();
    QString o = QString::fromUtf8(out).trimmed();
    if (!o.isEmpty()) text = text.isEmpty() ? o : o + QLatin1Char('\n') + text;
    if (text.isEmpty()) text = QStringLiteral("git exited with code %1.").arg(exitCode);
    return text;
}

namespace GitCli {

namespace {

QProcessEnvironment environment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));   // never wait for a password
    env.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral("true"));         // never open an editor
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.remove(QStringLiteral("GIT_DIR"));   // the work tree decides the repository
    env.remove(QStringLiteral("GIT_WORK_TREE"));
    env.remove(QStringLiteral("GIT_INDEX_FILE"));
    return env;
}

void setUp(QProcess &p, const QString &workdir) {
    p.setWorkingDirectory(workdir);
    p.setProcessEnvironment(environment());
}

GitRun notFound() {
    GitRun r;
    r.err = "The git program was not found. gitq needs git to change the index and to commit.";
    return r;
}

} // namespace

QString executable() {
    static const QString exe = QStandardPaths::findExecutable(QStringLiteral("git"));
    return exe;
}

GitRun run(const QString &workdir, const QStringList &args, const QByteArray &input, int timeoutMs) {
    if (executable().isEmpty()) return notFound();
    QProcess p;
    setUp(p, workdir);
    p.start(executable(), args);
    GitRun r;
    if (!p.waitForStarted()) {
        r.err = p.errorString().toUtf8();
        return r;
    }
    if (!input.isEmpty()) p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished();
        r.err = "git did not finish in time.";
        return r;
    }
    r.started = p.exitStatus() == QProcess::NormalExit;
    r.exitCode = p.exitCode();
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    if (!r.started && r.err.isEmpty()) r.err = "git crashed.";
    return r;
}

void runAsync(const QString &workdir, const QStringList &args, const QByteArray &input, QObject *context,
              std::function<void(const GitRun &)> done) {
    if (executable().isEmpty()) {
        GitRun r = notFound();
        QMetaObject::invokeMethod(context, [done, r] { done(r); }, Qt::QueuedConnection);
        return;
    }
    auto *p = new QProcess(context);
    setUp(*p, workdir);
    QObject::connect(p, &QProcess::finished, context, [p, done](int code, QProcess::ExitStatus status) {
        GitRun r;
        r.started = status == QProcess::NormalExit;
        r.exitCode = code;
        r.out = p->readAllStandardOutput();
        r.err = p->readAllStandardError();
        if (!r.started && r.err.isEmpty()) r.err = "git crashed.";
        p->deleteLater();
        done(r);
    });
    QObject::connect(p, &QProcess::errorOccurred, context, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;   // other errors are followed by finished()
        GitRun r;
        r.err = p->errorString().toUtf8();
        p->deleteLater();
        done(r);
    });
    p->start(executable(), args);
    if (!input.isEmpty()) p->write(input);
    p->closeWriteChannel();
}

QString literal(const QString &path) { return QStringLiteral(":(literal)") + path; }

} // namespace GitCli
} // namespace gitq
