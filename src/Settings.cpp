#include "Settings.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>

namespace gitq {

namespace {

struct ColorDef {
    const char *key;
    QColor Settings::*member;
};

// Persisted colours.
const ColorDef kColors[] = {
    {"bg", &Settings::bgColor},
    {"fg", &Settings::fgColor},
    {"selectBg", &Settings::selectBgColor},
    {"diffOld", &Settings::diffOldColor},
    {"diffNew", &Settings::diffNewColor},
    {"diffHunk", &Settings::diffHunkColor},
    {"diffOldBg", &Settings::diffOldBg},
    {"diffNewBg", &Settings::diffNewBg},
    {"markBg", &Settings::markBgColor},
    {"headBg", &Settings::headBgColor},
    {"remoteBg", &Settings::remoteBgColor},
    {"tagBg", &Settings::tagBgColor},
    {"otherRefBg", &Settings::otherRefBgColor},
    {"fileSepBg", &Settings::fileSepBgColor},
    {"foundBg", &Settings::foundBgColor},
    {"currentSearchBg", &Settings::currentSearchBgColor},
    {"link", &Settings::linkColor},
};

// Defaults the current colours were derived from (for updateThemeColors()).
Settings &activeDefaults() {
    static Settings d;
    return d;
}

} // namespace

QColor Settings::mix(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

bool Settings::darkTheme() {
    const QPalette pal = QGuiApplication::palette();
    return pal.color(QPalette::Base).lightness() < pal.color(QPalette::Text).lightness();
}

Settings Settings::themeDefaults() {
    Settings d;
    if (!darkTheme()) return d;
    const QPalette pal = QGuiApplication::palette();
    d.bgColor = pal.color(QPalette::Base);
    d.fgColor = pal.color(QPalette::Text);
    d.selectBgColor = mix(d.bgColor, d.fgColor, 0.25);
    d.graphColors = {QColor(0x4e, 0xd8, 0x4e), QColor(0xff, 0x5f, 0x5f), QColor(0x6c, 0xa8, 0xff),
                     QColor(0xff, 0x6b, 0xff), QColor(0xa8, 0xa8, 0xa8), QColor(0xd2, 0x8a, 0x4a),
                     QColor(0xff, 0xb3, 0x40)};
    d.diffOldColor = QColor(0xff, 0x7b, 0x72);
    d.diffNewColor = QColor(0x56, 0xd3, 0x64);
    d.diffHunkColor = QColor(0x6c, 0xa8, 0xff);
    d.diffOldBg = mix(d.bgColor, QColor(0xff, 0x40, 0x40), 0.15);
    d.diffNewBg = mix(d.bgColor, QColor(0x40, 0xff, 0x40), 0.12);
    d.markBgColor = mix(d.bgColor, QColor(0x80, 0x80, 0xff), 0.3);
    d.fileSepBgColor = mix(d.bgColor, d.fgColor, 0.3);
    d.foundBgColor = QColor(0x80, 0x6c, 0x00);
    d.currentSearchBgColor = QColor(0xa8, 0x55, 0x00);
    d.linkColor = QColor(0x6c, 0xa8, 0xff);
    // Ref labels keep light backgrounds with black text, as in gitk.
    return d;
}

void Settings::updateThemeColors() {
    Settings &old = activeDefaults();
    Settings now = themeDefaults();
    for (const ColorDef &c : kColors)
        if (this->*c.member == old.*c.member) this->*c.member = now.*c.member;
    if (graphColors == old.graphColors) graphColors = now.graphColors;
    old = now;
}

Settings &Settings::instance() {
    static Settings s;
    return s;
}

void Settings::load() {
    QSettings q;
    mainFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    textFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    uiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    auto font = [&](const char *key, QFont &f) {
        QString s = q.value(QLatin1String(key)).toString();
        if (!s.isEmpty()) f.fromString(s);
    };
    font("fonts/main", mainFont);
    font("fonts/text", textFont);
    font("fonts/ui", uiFont);

    q.beginGroup(QStringLiteral("prefs"));
    tabStop = q.value(QStringLiteral("tabStop"), tabStop).toInt();
    maxGraphWidth = q.value(QStringLiteral("maxGraphWidth"), maxGraphWidth).toInt();
    maxGraphPct = q.value(QStringLiteral("maxGraphPct"), maxGraphPct).toInt();
    showLocalChanges = q.value(QStringLiteral("showLocalChanges"), showLocalChanges).toBool();
    autoSelect = q.value(QStringLiteral("autoSelect"), autoSelect).toBool();
    autoSelLen = q.value(QStringLiteral("autoSelLen"), autoSelLen).toInt();
    hideRemotes = q.value(QStringLiteral("hideRemotes"), hideRemotes).toBool();
    commitListMultiline = q.value(QStringLiteral("commitListMultiline"), commitListMultiline).toBool();
    limitDiffs = q.value(QStringLiteral("limitDiffs"), limitDiffs).toBool();
    showNearTags = q.value(QStringLiteral("showNearTags"), showNearTags).toBool();
    wrapDiff = q.value(QStringLiteral("wrapDiff"), wrapDiff).toBool();
    diffContext = q.value(QStringLiteral("diffContext"), diffContext).toInt();
    ignoreSpace = q.value(QStringLiteral("ignoreSpace"), ignoreSpace).toBool();
    wordDiff = q.value(QStringLiteral("wordDiff"), wordDiff).toInt();
    submoduleDiff = q.value(QStringLiteral("submoduleDiff"), submoduleDiff).toInt();
    dateFormat = q.value(QStringLiteral("dateFormat"), dateFormat).toString();
    extDiffTool = q.value(QStringLiteral("extDiffTool"), extDiffTool).toString();
    maxRefs = q.value(QStringLiteral("maxRefs"), maxRefs).toInt();
    q.endGroup();

    // A stored colour equal to either theme's default counts as not customised (older versions
    // saved every colour).
    const Settings light;
    const Settings defs = themeDefaults();
    activeDefaults() = defs;
    q.beginGroup(QStringLiteral("colors"));
    for (const ColorDef &c : kColors) {
        this->*c.member = defs.*c.member;
        QVariant v = q.value(QLatin1String(c.key));
        if (!v.isValid()) continue;
        QColor col(v.toString());
        if (col.isValid() && col != light.*c.member && col != defs.*c.member) this->*c.member = col;
    }
    graphColors = defs.graphColors;
    QStringList gc = q.value(QStringLiteral("graph")).toStringList();
    if (!gc.isEmpty()) {
        QList<QColor> cols;
        for (const QString &s : gc) cols << QColor(s);
        if (cols != light.graphColors && cols != defs.graphColors) graphColors = cols;
    }
    q.endGroup();

    views.clear();
    int n = q.beginReadArray(QStringLiteral("views"));
    for (int i = 0; i < n; ++i) {
        q.setArrayIndex(i);
        ViewDef v;
        v.name = q.value(QStringLiteral("name")).toString();
        v.args = q.value(QStringLiteral("args")).toStringList();
        v.remember = true;
        views << v;
    }
    q.endArray();
}

void Settings::save() const {
    QSettings q;
    q.setValue(QStringLiteral("fonts/main"), mainFont.toString());
    q.setValue(QStringLiteral("fonts/text"), textFont.toString());
    q.setValue(QStringLiteral("fonts/ui"), uiFont.toString());

    q.beginGroup(QStringLiteral("prefs"));
    q.setValue(QStringLiteral("tabStop"), tabStop);
    q.setValue(QStringLiteral("maxGraphWidth"), maxGraphWidth);
    q.setValue(QStringLiteral("maxGraphPct"), maxGraphPct);
    q.setValue(QStringLiteral("showLocalChanges"), showLocalChanges);
    q.setValue(QStringLiteral("autoSelect"), autoSelect);
    q.setValue(QStringLiteral("autoSelLen"), autoSelLen);
    q.setValue(QStringLiteral("hideRemotes"), hideRemotes);
    q.setValue(QStringLiteral("commitListMultiline"), commitListMultiline);
    q.setValue(QStringLiteral("limitDiffs"), limitDiffs);
    q.setValue(QStringLiteral("showNearTags"), showNearTags);
    q.setValue(QStringLiteral("wrapDiff"), wrapDiff);
    q.setValue(QStringLiteral("diffContext"), diffContext);
    q.setValue(QStringLiteral("ignoreSpace"), ignoreSpace);
    q.setValue(QStringLiteral("wordDiff"), wordDiff);
    q.setValue(QStringLiteral("submoduleDiff"), submoduleDiff);
    q.setValue(QStringLiteral("dateFormat"), dateFormat);
    q.setValue(QStringLiteral("extDiffTool"), extDiffTool);
    q.setValue(QStringLiteral("maxRefs"), maxRefs);
    q.endGroup();

    const Settings &defs = activeDefaults();
    q.beginGroup(QStringLiteral("colors"));
    for (const ColorDef &c : kColors) {
        if (this->*c.member == defs.*c.member) q.remove(QLatin1String(c.key));
        else q.setValue(QLatin1String(c.key), (this->*c.member).name());
    }
    if (graphColors == defs.graphColors) {
        q.remove(QStringLiteral("graph"));
    } else {
        QStringList gc;
        for (const QColor &c : graphColors) gc << c.name();
        q.setValue(QStringLiteral("graph"), gc);
    }
    q.endGroup();

    q.beginWriteArray(QStringLiteral("views"));
    int i = 0;
    for (const ViewDef &v : views) {
        if (!v.remember) continue;
        q.setArrayIndex(i++);
        q.setValue(QStringLiteral("name"), v.name);
        q.setValue(QStringLiteral("args"), v.args);
    }
    q.endArray();
}

} // namespace gitq
