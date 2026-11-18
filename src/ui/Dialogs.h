// Small dialogs used by gitk's commands.
#pragma once

#include "git/CommitInfo.h"
#include "git/Ops.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QListWidget;

namespace gitq {

// "Create tag" / "Create new branch" style dialog: shows the commit, asks for a name (+ message).
class NameDialog : public QDialog {
    Q_OBJECT
public:
    NameDialog(const QString &title, const QString &idText, const QString &nameLabel, bool withMessage,
               QWidget *parent = nullptr);
    QString name() const;
    QString message() const;

private:
    QLineEdit *m_name;
    QPlainTextEdit *m_message = nullptr;
};

// "Make patch" and "Write commit to file".
class PatchDialog : public QDialog {
    Q_OBJECT
public:
    PatchDialog(const QString &title, const QString &from, const QString &to, const QString &file,
                bool withFrom, QWidget *parent = nullptr);
    QString from() const;
    QString to() const;
    QString file() const;

private:
    QLineEdit *m_from = nullptr;
    QLineEdit *m_to;
    QLineEdit *m_file;
};

class ResetDialog : public QDialog {
    Q_OBJECT
public:
    ResetDialog(const QString &branch, const QString &target, QWidget *parent = nullptr);
    Ops::ResetType type() const;

private:
    QRadioButton *m_soft, *m_mixed, *m_hard;
};

// "List references" (F2).
class RefsDialog : public QDialog {
    Q_OBJECT
public:
    RefsDialog(const QList<RefInfo> &refs, QWidget *parent = nullptr);

signals:
    void refChosen(const gitq::Oid &commit);

private:
    void refilter();
    QList<RefInfo> m_refs;
    QLineEdit *m_filter;
    QListWidget *m_list;
};

QString keyBindingsText();

} // namespace gitq
