// gitk's "New view" / "Edit view" dialog.
#pragma once

#include "Settings.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace gitq {

class ViewDialog : public QDialog {
    Q_OBJECT
public:
    explicit ViewDialog(const ViewDef &view, bool isNew, QWidget *parent = nullptr);
    ViewDef view() const;

private:
    void load(const ViewDef &v);

    QLineEdit *m_name;
    QCheckBox *m_remember;
    QLineEdit *m_refs;
    QCheckBox *m_allRefs, *m_branches, *m_tags, *m_remotes;
    QLineEdit *m_author, *m_committer, *m_grep;
    QCheckBox *m_allMatch;
    QLineEdit *m_since, *m_until;
    QSpinBox *m_maxCount, *m_skip;
    QCheckBox *m_dateOrder, *m_firstParent, *m_noMerges, *m_fullHistory;
    QLineEdit *m_extraArgs;
    QPlainTextEdit *m_paths;
};

} // namespace gitq
