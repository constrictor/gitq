#include "ui/FileListDelegate.h"

#include "Settings.h"
#include "model/FileListModel.h"
#include "ui/Glyphs.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>

namespace gitq {

QSize FileListDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    QSize s = QStyledItemDelegate::sizeHint(opt, idx);
    s.setHeight(qMax(s.height(), opt.fontMetrics.height() + 6));
    return s;
}

QString FileListDelegate::buttonText() const { return m_action == StageRow ? tr("Stage") : tr("Unstage"); }

QRectF FileListDelegate::buttonRect(const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    if (m_action == NoRowAction || idx.data(FileListModel::PathRole).toString().isEmpty() ||
        idx.data(FileListModel::IsFolderRole).toBool())
        return {};
    QFont small = opt.font;
    if (small.pointSizeF() > 0) small.setPointSizeF(small.pointSizeF() * 0.85);
    const QFontMetricsF sm(small);
    const QRectF r = QRectF(opt.rect).adjusted(4, 0, -6, 0);
    const double w = sm.horizontalAdvance(buttonText()) + 14, h = qMin(r.height() - 4, sm.height() + 4);
    return QRectF(r.right() - w, r.center().y() + 0.5 - h / 2, w, h);
}

bool FileListDelegate::editorEvent(QEvent *e, QAbstractItemModel *model, const QStyleOptionViewItem &opt,
                                   const QModelIndex &idx) {
    if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonRelease ||
        e->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(e);
        const QRectF b = buttonRect(opt, idx);
        if (me->button() == Qt::LeftButton && !b.isNull() && b.contains(me->position())) {
            if (e->type() == QEvent::MouseButtonRelease) emit rowActionClicked(idx.data(FileListModel::PathRole).toString());
            return true;   // the click doesn't select the row
        }
    }
    return QStyledItemDelegate::editorEvent(e, model, opt, idx);
}

void FileListDelegate::paint(QPainter *p, const QStyleOptionViewItem &optIn, const QModelIndex &idx) const {
    QStyleOptionViewItem opt = optIn;
    initStyleOption(&opt, idx);
    // Background, selection and hover from the style; the content is ours.
    const QString text = opt.text;
    opt.text.clear();
    opt.icon = QIcon();
    const QWidget *w = opt.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor fg = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor bg = opt.palette.color(selected ? QPalette::Highlight : QPalette::Base);
    const QColor dim = Settings::mix(fg, bg, 0.45);
    const QRect r = opt.rect.adjusted(4, 0, -6, 0);
    const QFontMetricsF fm(opt.font);
    const double cy = r.center().y() + 0.5;
    double x = r.left();
    double right = r.right();

    p->save();
    p->setFont(opt.font);
    if (idx.data(FileListModel::IsCommentsRole).toBool()) {
        p->setPen(fg);
        p->drawText(QRectF(x, r.top(), r.width(), r.height()), Qt::AlignVCenter, text);
        p->restore();
        return;
    }
    const QString name = idx.data(FileListModel::NameRole).toString();
    if (idx.data(FileListModel::IsFolderRole).toBool()) {
        const double s = fm.height() * 0.9;
        drawGlyph(p, Glyph::Folder, QRectF(x, cy - s / 2, s, s), dim);
        x += s + 6;
        QFont bold = opt.font;
        bold.setBold(true);
        p->setFont(bold);
        p->setPen(dim);
        p->drawText(QRectF(x, r.top(), right - x, r.height()), Qt::AlignVCenter,
                    QFontMetricsF(bold).elidedText(name, Qt::ElideLeft, right - x));
        p->restore();
        return;
    }

    const QString path = idx.data(FileListModel::PathRole).toString();
    const auto *model = qobject_cast<const FileListModel *>(idx.model());
    const bool grouped = model && model->grouped();
    if (grouped && path.contains(QLatin1Char('/'))) x += fm.height();   // under its folder row

    // Status badge (patch mode).
    const QString status = idx.data(FileListModel::StatusRole).toString();
    if (!status.isEmpty()) {
        const double s = fm.height() - 1;
        drawStatusBadge(p, QRectF(x, cy - s / 2, s, s), status.at(0));
        x += s + 6;
        p->setFont(opt.font);
    }

    // The row button, on the row under the mouse.
    if (opt.state & QStyle::State_MouseOver) {
        const QRectF b = buttonRect(opt, idx);
        if (!b.isNull()) {
            QFont small = opt.font;
            if (small.pointSizeF() > 0) small.setPointSizeF(small.pointSizeF() * 0.85);
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setPen(Settings::mix(bg, fg, 0.3));
            p->setBrush(Settings::mix(bg, fg, 0.08));
            const QRectF br = b.adjusted(0.5, 0.5, -0.5, -0.5);
            p->drawRoundedRect(br, br.height() * 0.3, br.height() * 0.3);
            p->setFont(small);
            p->setPen(fg);
            p->drawText(b, Qt::AlignCenter, buttonText());
            p->setFont(opt.font);
            right = b.left() - 8;
        }
    }

    // Line counts on the right.
    const int added = idx.data(FileListModel::AddedRole).toInt(), removed = idx.data(FileListModel::RemovedRole).toInt();
    QFont small = opt.font;
    if (small.pointSizeF() > 0) small.setPointSizeF(small.pointSizeF() * 0.85);
    const QFontMetricsF sm(small);
    if (idx.data(FileListModel::BinaryRole).toBool()) {
        const QString t = tr("bin");
        right -= sm.horizontalAdvance(t);
        p->setFont(small);
        p->setPen(dim);
        p->drawText(QRectF(right, r.top(), sm.horizontalAdvance(t) + 1, r.height()), Qt::AlignVCenter, t);
        right -= 8;
    } else if (added >= 0 && removed >= 0 && r.width() > 140) {
        // The bar only when there is room to spare for it.
        if (r.width() > 260) right = drawChangeBar(p, right, cy, qMax(4.0, fm.height() * 0.4), added, removed) - 6;
        p->setFont(small);
        const QString m = QStringLiteral("−%1").arg(removed), a = QStringLiteral("+%1").arg(added);
        right -= sm.horizontalAdvance(m);
        p->setPen(selected ? fg : removedColor());
        p->drawText(QRectF(right, r.top(), sm.horizontalAdvance(m) + 1, r.height()), Qt::AlignVCenter, m);
        right -= sm.horizontalAdvance(a) + 4;
        p->setPen(selected ? fg : addedColor());
        p->drawText(QRectF(right, r.top(), sm.horizontalAdvance(a) + 1, r.height()), Qt::AlignVCenter, a);
        right -= 8;
    }

    // The name: grouped, the file name; otherwise the directory dimmed and the file name.
    p->setFont(opt.font);
    const double avail = right - x;
    if (avail > 4) {
        const int slash = int(name.lastIndexOf(QLatin1Char('/')));
        QString dir = slash < 0 ? QString() : name.left(slash + 1);
        QString file = name.mid(slash + 1);
        const double fw = fm.horizontalAdvance(file);
        if (fw >= avail) {
            file = fm.elidedText(file, Qt::ElideMiddle, avail);
            dir.clear();
        } else {
            dir = fm.elidedText(dir, Qt::ElideLeft, avail - fw);
        }
        const double dw = fm.horizontalAdvance(dir);
        p->setPen(dim);
        p->drawText(QRectF(x, r.top(), dw + 1, r.height()), Qt::AlignVCenter, dir);
        p->setPen(fg);
        p->drawText(QRectF(x + dw, r.top(), avail - dw, r.height()), Qt::AlignVCenter, file);
    }
    p->restore();
}

} // namespace gitq
