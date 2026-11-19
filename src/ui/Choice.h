// An exclusive choice among a few options, as checkable actions that can go into any menu (tool
// button menus, overflow menus); a lighter replacement for a combo box in compact tool bars.
#pragma once

#include <QAction>
#include <QActionGroup>
#include <QStringList>

namespace gitq {

class Choice : public QActionGroup {
    Q_OBJECT
public:
    Choice(const QStringList &items, int current, QObject *parent) : QActionGroup(parent) {
        setExclusive(true);
        for (int i = 0; i < items.size(); ++i) {
            QAction *a = addAction(items[i]);
            a->setCheckable(true);
            a->setData(i);
            a->setChecked(i == current);
        }
        connect(this, &QActionGroup::triggered, this, [this](QAction *a) { emit currentIndexChanged(a->data().toInt()); });
    }

    int currentIndex() const {
        QAction *a = checkedAction();
        return a ? a->data().toInt() : -1;
    }
    QString currentText() const {
        QAction *a = checkedAction();
        return a ? a->text() : QString();
    }
    void setCurrentIndex(int i) {
        if (i == currentIndex() || i < 0 || i >= actions().size()) return;
        actions().at(i)->setChecked(true);
        emit currentIndexChanged(i);
    }

signals:
    void currentIndexChanged(int index);
};

} // namespace gitq
