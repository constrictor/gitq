// gitk's preferences dialog.
#pragma once

#include "Settings.h"

#include <QDialog>

namespace gitq {

class PrefsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrefsDialog(QWidget *parent = nullptr);
    // Settings as edited in the dialog.
    Settings result() const { return m_s; }

private:
    QWidget *generalPage();
    QWidget *colorsPage();
    QWidget *fontsPage();

    Settings m_s;
};

} // namespace gitq
