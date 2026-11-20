// The Commit pane: the message of the next commit, written while reviewing the local changes, and
// the button that commits what is staged (optionally amending HEAD).
#pragma once

#include "git/Ops.h"
#include "git/Staging.h"

#include <QPlainTextEdit>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;

namespace gitq {

class Repository;

// Message editor with a guide at the conventional line length.
class CommitMessageEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    static constexpr int kGuideColumn = 72;
    explicit CommitMessageEdit(QWidget *parent = nullptr);
    void applyFonts();

protected:
    void paintEvent(QPaintEvent *e) override;
    bool event(QEvent *e) override;
};

class CommitPane : public QWidget {
    Q_OBJECT
public:
    // `repo` must outlive the pane (it belongs to the RepoTab that owns the pane).
    explicit CommitPane(const Repository *repo, QWidget *parent = nullptr);
    ~CommitPane() override;

    // State of the repository, from the tab: the checked-out branch (empty: detached HEAD), whether
    // HEAD exists, whether it is already on the branch's upstream (amending rewrites published
    // history), and what is staged.
    void setHead(const QString &branch, bool unborn, bool published);
    void setStaged(const StagedSummary &s);

    QString message() const;
    void setMessage(const QString &text);
    bool amend() const;
    void setAmend(bool on);
    bool isBusy() const { return m_busy; }

    void applyFonts();
    // Commits now (Ctrl+Return); does nothing if the button is disabled.
    void commit();

signals:
    // A commit finished (or failed: `r.ok` false, git's output in `r.message`).
    void committed(const gitq::OpResult &r);
    void statusMessage(const QString &text, int timeout);

private:
    void updateState();
    void onAmendToggled(bool on);
    void saveDraftNow();

    const Repository *m_repo;
    QLabel *m_branchLabel;
    CommitMessageEdit *m_edit;
    QLabel *m_info;
    QCheckBox *m_amend, *m_signOff;
    QPushButton *m_commitButton;
    QTimer *m_draftTimer;

    QString m_branch;
    bool m_unborn = false;
    bool m_published = false;
    StagedSummary m_staged;
    QString m_stashedDraft;   // the draft while the amend box shows HEAD's message
    bool m_loading = false;   // setting the text programmatically
    bool m_busy = false;
};

} // namespace gitq
