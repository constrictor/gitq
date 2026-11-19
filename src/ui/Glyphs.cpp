#include "ui/Glyphs.h"

#include "Settings.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace gitq {

void drawGlyph(QPainter *p, Glyph g, const QRectF &box, const QColor &color) {
    // Work in a centred square so that glyphs keep their proportions.
    const double side = qMin(box.width(), box.height());
    const QRectF r(box.center().x() - side / 2, box.center().y() - side / 2, side, side);
    auto P = [&](double x, double y) { return QPointF(r.left() + x * side, r.top() + y * side); };
    const double w = qMax(1.0, side * 0.11);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    auto dot = [&](double x, double y, double rad) { p->drawEllipse(P(x, y), rad * side, rad * side); };
    auto text = [&](const QString &s, bool bold, double scale) {
        QFont f = p->font();
        f.setPixelSize(qMax(6, int(side * scale)));
        f.setBold(bold);
        p->setFont(f);
        p->drawText(r, Qt::AlignCenter, s);
    };

    switch (g) {
    case Glyph::Branch: {
        dot(0.3, 0.2, 0.11);
        dot(0.3, 0.8, 0.11);
        dot(0.72, 0.3, 0.11);
        p->drawLine(P(0.3, 0.31), P(0.3, 0.69));
        QPainterPath path(P(0.72, 0.41));
        path.cubicTo(P(0.72, 0.62), P(0.3, 0.5), P(0.3, 0.68));
        p->drawPath(path);
        break;
    }
    case Glyph::Cloud: {
        QPainterPath path;
        path.addEllipse(P(0.1, 0.42).x(), P(0.1, 0.42).y(), side * 0.36, side * 0.34);
        path.addEllipse(P(0.26, 0.24).x(), P(0.26, 0.24).y(), side * 0.42, side * 0.44);
        path.addEllipse(P(0.52, 0.38).x(), P(0.52, 0.38).y(), side * 0.38, side * 0.38);
        path.addRoundedRect(QRectF(P(0.2, 0.55), P(0.8, 0.76)), side * 0.08, side * 0.08);
        path.setFillRule(Qt::WindingFill);
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawPath(path.simplified());
        break;
    }
    case Glyph::Tag: {
        QPainterPath path(P(0.12, 0.14));
        path.lineTo(P(0.5, 0.14));
        path.lineTo(P(0.88, 0.52));
        path.lineTo(P(0.52, 0.88));
        path.lineTo(P(0.12, 0.5));
        path.closeSubpath();
        p->drawPath(path);
        p->setBrush(color);
        p->setPen(Qt::NoPen);
        dot(0.33, 0.35, 0.07);
        break;
    }
    case Glyph::Ref:
        dot(0.5, 0.5, 0.3);
        p->setBrush(color);
        dot(0.5, 0.5, 0.1);
        break;
    case Glyph::Search:
        dot(0.42, 0.42, 0.25);
        p->drawLine(P(0.61, 0.61), P(0.86, 0.86));
        break;
    case Glyph::Copy:
        p->drawRoundedRect(QRectF(P(0.32, 0.3), P(0.84, 0.86)), side * 0.08, side * 0.08);
        p->drawPolyline(QPolygonF{P(0.18, 0.66), P(0.18, 0.16), P(0.66, 0.16)});
        break;
    case Glyph::CaseSensitive: text(QStringLiteral("Aa"), true, 0.62); break;
    case Glyph::Regex: text(QStringLiteral(".*"), true, 0.8); break;
    case Glyph::Whitespace:
        p->drawPolyline(QPolygonF{P(0.12, 0.5), P(0.12, 0.72), P(0.88, 0.72), P(0.88, 0.5)});
        p->setBrush(color);
        p->setPen(Qt::NoPen);
        dot(0.5, 0.42, 0.07);
        break;
    case Glyph::Unified:
        for (double y : {0.25, 0.5, 0.75}) p->drawLine(P(0.15, y), P(0.85, y));
        break;
    case Glyph::Split:
        p->drawRoundedRect(QRectF(P(0.12, 0.18), P(0.88, 0.82)), side * 0.06, side * 0.06);
        p->drawLine(P(0.5, 0.18), P(0.5, 0.82));
        break;
    case Glyph::More:
        p->setBrush(color);
        p->setPen(Qt::NoPen);
        for (double x : {0.2, 0.5, 0.8}) dot(x, 0.5, 0.08);
        break;
    case Glyph::Folder: {
        QPainterPath path(P(0.1, 0.8));
        path.lineTo(P(0.1, 0.22));
        path.lineTo(P(0.38, 0.22));
        path.lineTo(P(0.48, 0.34));
        path.lineTo(P(0.9, 0.34));
        path.lineTo(P(0.9, 0.8));
        path.closeSubpath();
        p->drawPath(path);
        break;
    }
    case Glyph::ArrowUp:
        p->drawLine(P(0.5, 0.15), P(0.5, 0.85));
        p->drawPolyline(QPolygonF{P(0.22, 0.43), P(0.5, 0.15), P(0.78, 0.43)});
        break;
    case Glyph::ArrowDown:
        p->drawLine(P(0.5, 0.15), P(0.5, 0.85));
        p->drawPolyline(QPolygonF{P(0.22, 0.57), P(0.5, 0.85), P(0.78, 0.57)});
        break;
    }
    p->restore();
}

namespace {

class GlyphEngine : public QIconEngine {
public:
    GlyphEngine(Glyph g, const QColor &c) : m_glyph(g), m_color(c) {}

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        QColor c = m_color;
        const QPalette pal = QGuiApplication::palette();
        if (!c.isValid()) c = pal.color(QPalette::ButtonText);
        if (mode == QIcon::Disabled) c = Settings::mix(c, pal.color(QPalette::Button), 0.6);
        // A little inset: tool buttons draw icons edge to edge.
        const double inset = rect.width() * 0.08;
        drawGlyph(p, m_glyph, QRectF(rect).adjusted(inset, inset, -inset, -inset), c);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new GlyphEngine(m_glyph, m_color); }

private:
    Glyph m_glyph;
    QColor m_color;
};

} // namespace

QIcon glyphIcon(Glyph g, const QColor &color) { return QIcon(new GlyphEngine(g, color)); }

} // namespace gitq
