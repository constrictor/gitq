#include "ui/Theme.h"

#include "Settings.h"

#include <QApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <cmath>

namespace gitq::Theme {

namespace {

double luminance(const QColor &c) {
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

// `c` as it shows over `bg` (colours with alpha).
QColor over(const QColor &c, const QColor &bg) { return Settings::mix(bg, QColor(c.rgb()), c.alphaF()); }

} // namespace

QPalette repaired(const QPalette &pal) {
    const bool dark = pal.color(QPalette::Base).lightness() < pal.color(QPalette::Text).lightness();
    if (!dark) return pal;
    QPalette p = pal;
    for (auto g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const QColor base = p.color(g, QPalette::Base), text = p.color(g, QPalette::Text);
        // Placeholder text: readable but dimmer than the text.
        if (contrast(over(p.color(g, QPalette::PlaceholderText), base), base) < 3.0)
            p.setColor(g, QPalette::PlaceholderText, Settings::mix(base, text, 0.55));
        // Shades: lighter above the button colour, darker below it.
        const QColor button = p.color(g, QPalette::Button);
        if (p.color(g, QPalette::Dark).lightness() > button.lightness() ||
            p.color(g, QPalette::Light).lightness() < button.lightness()) {
            p.setColor(g, QPalette::Light, button.lighter(140));
            p.setColor(g, QPalette::Midlight, button.lighter(120));
            p.setColor(g, QPalette::Mid, button.darker(130));
            p.setColor(g, QPalette::Dark, button.darker(160));
            p.setColor(g, QPalette::Shadow, button.darker(300));
        }
    }
    return p;
}

namespace {

class FusionFixes : public QProxyStyle {
public:
    FusionFixes() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const override {
        QProxyStyle::drawPrimitive(pe, opt, p, w);
        switch (pe) {
        case PE_IndicatorCheckBox:
        case PE_IndicatorRadioButton:
        case PE_FrameLineEdit:
            outline(opt, p, opt->rect, pe == PE_IndicatorRadioButton);
            break;
        case PE_Frame:
            if (w && w->objectName() == QLatin1String("commitMessage")) outline(opt, p, opt->rect, false);
            break;
        default:
            break;
        }
    }

    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p, const QWidget *w) const override {
        QProxyStyle::drawComplexControl(cc, opt, p, w);
        if (cc == CC_SpinBox) outline(opt, p, opt->rect, false);
    }

private:
    static void outline(const QStyleOption *opt, QPainter *p, const QRect &rect, bool round) {
        const QPalette &pal = opt->palette;
        if (pal.color(QPalette::Base).lightness() >= pal.color(QPalette::Text).lightness()) return;   // light
        const bool focus = opt->state & State_HasFocus;
        QColor c = focus ? pal.color(QPalette::Highlight)
                         : Settings::mix(pal.color(QPalette::Window), pal.color(QPalette::WindowText), 0.35);
        if (!(opt->state & State_Enabled)) c = Settings::mix(pal.color(QPalette::Window), c, 0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(QPen(c, 1));
        p->setBrush(Qt::NoBrush);
        const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
        if (round) p->drawEllipse(r);
        else p->drawRoundedRect(r, 2, 2);
        p->restore();
    }
};

} // namespace

void installStyleFixes() {
    if (QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0) return;
    QApplication::setStyle(new FusionFixes);
}

void repairApplicationPalette() {
    static bool busy = false, overridden = false;
    if (busy) return;   // our own setPalette() calls come back here
    busy = true;
    if (overridden) {   // back to the platform's palette, then repair that
        QApplication::setPalette(QPalette());
        overridden = false;
    }
    const QPalette current = QApplication::palette();
    const QPalette fixed = repaired(current);
    if (fixed != current) {
        QApplication::setPalette(fixed);
        overridden = true;
    }
    busy = false;
}

} // namespace gitq::Theme
