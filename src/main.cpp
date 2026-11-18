// gitq: a Qt reimplementation of gitk.
#include "Settings.h"
#include "git/GitPtr.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QTimer>

#include <cstdio>
#include <string>

int main(int argc, char **argv) {
    // --help / --version work without a display
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--") break;
        if (a == "--help" || a == "-h") {
            std::puts("usage: gitq [<git rev-list options>] [<revision range>] [--] [<path>...]\n\n"
                      "A git repository browser compatible with gitk's command line.\n"
                      "Examples: gitq --all, gitq master..topic, gitq -- src/");
            return 0;
        }
        if (a == "--version") {
            std::printf("gitq %s\n", PROJECT_VERSION);
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("gitq"));
    QApplication::setApplicationName(QStringLiteral("gitq"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("gitq"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("gitq")));
    gitq::LibGit2Init libgit2;

    QStringList args = QApplication::arguments().mid(1);

    QString start = QDir::currentPath();
    QString gitDir = qEnvironmentVariable("GIT_DIR");
    if (!gitDir.isEmpty()) start = gitDir;

    gitq::Settings::instance().load();
    try {
        gitq::MainWindow w(start, args);
        // Paths given on the command line are relative to the current directory.
        gitq::Repository repo = gitq::Repository::open(start);
        QString wd = repo.workdir();
        if (!wd.isEmpty()) {
            QString rel = QDir(wd).relativeFilePath(QDir::currentPath());
            if (!rel.startsWith(QLatin1String("..")) && rel != QLatin1String(".")) w.setCommandLinePrefix(rel);
        }
        w.show();
        // Debug aid: GITQ_SCREENSHOT=file.png grabs the window after loading and quits.
        const QString shot = qEnvironmentVariable("GITQ_SCREENSHOT");
        if (!shot.isEmpty()) {
            QTimer::singleShot(qEnvironmentVariableIntValue("GITQ_SCREENSHOT_DELAY") ?: 2000, &w, [&w, shot] {
                w.grab().save(shot);
                QApplication::quit();
            });
        }
        return app.exec();
    } catch (const gitq::GitError &e) {
        QMessageBox::critical(nullptr, QStringLiteral("gitq"),
                              QStringLiteral("Cannot find a git repository here.\n\n%1").arg(e.message()));
        return 1;
    }
}
