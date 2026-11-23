// Palette repairs for incomplete platform palettes (Qt's built-in KDE theme on Plasma 5).
#include "ui/Theme.h"

#include <QTest>
#include <cmath>

using namespace gitq;

namespace {

double luminance(const QColor &c) {
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

// What Qt 6.4's built-in KDE theme makes of Breeze Dark's kdeglobals.
QPalette kdeFallbackDark() {
    QPalette p(QColor(0x31, 0x36, 0x3b), QColor(0x2a, 0x2e, 0x32));
    for (auto g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::Base, QColor(0x1b, 0x1e, 0x20));
        p.setColor(g, QPalette::Text, QColor(0xfc, 0xfc, 0xfc));
        p.setColor(g, QPalette::WindowText, QColor(0xfc, 0xfc, 0xfc));
        p.setColor(g, QPalette::PlaceholderText, QColor(0, 0, 0, 128));
        p.setColor(g, QPalette::Light, QColor(0x18, 0x1b, 0x1d));
        p.setColor(g, QPalette::Dark, QColor(0x62, 0x6c, 0x76));
        p.setColor(g, QPalette::Shadow, QColor(0x76, 0x76, 0x76));
    }
    return p;
}

} // namespace

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void repairsKdeFallbackDark() {
        const QPalette p = Theme::repaired(kdeFallbackDark());
        for (auto g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            const QColor base = p.color(g, QPalette::Base), button = p.color(g, QPalette::Button);
            QCOMPARE(p.color(g, QPalette::PlaceholderText).alpha(), 255);
            QVERIFY(contrast(p.color(g, QPalette::PlaceholderText), base) >= 3.0);
            QVERIFY(contrast(p.color(g, QPalette::PlaceholderText), base) < contrast(p.color(g, QPalette::Text), base));
            QVERIFY(p.color(g, QPalette::Dark).lightness() < button.lightness());
            QVERIFY(p.color(g, QPalette::Shadow).lightness() < button.lightness());
            QVERIFY(p.color(g, QPalette::Light).lightness() > button.lightness());
        }
        // Roles the platform set are kept.
        QCOMPARE(p.color(QPalette::Base), QColor(0x1b, 0x1e, 0x20));
        QCOMPARE(p.color(QPalette::Window), QColor(0x2a, 0x2e, 0x32));
    }

    void leavesConsistentPalettesAlone() {
        const QPalette fixed = Theme::repaired(kdeFallbackDark());
        QCOMPARE(Theme::repaired(fixed), fixed);   // nothing left to repair

        // A light palette, even with the black placeholder (normal there).
        QPalette light(QColor(0xfc, 0xfc, 0xfc), QColor(0xef, 0xf0, 0xf1));
        light.setColor(QPalette::Base, Qt::white);
        light.setColor(QPalette::Text, QColor(0x23, 0x26, 0x29));
        light.setColor(QPalette::PlaceholderText, QColor(0, 0, 0, 128));
        QCOMPARE(Theme::repaired(light), light);
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
