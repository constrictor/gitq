#include "Settings.h"

#include <QFontDatabase>
#include <QSettings>

namespace gitq {

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
    limitDiffs = q.value(QStringLiteral("limitDiffs"), limitDiffs).toBool();
    showNearTags = q.value(QStringLiteral("showNearTags"), showNearTags).toBool();
    wrapDiff = q.value(QStringLiteral("wrapDiff"), wrapDiff).toBool();
    diffContext = q.value(QStringLiteral("diffContext"), diffContext).toInt();
    ignoreSpace = q.value(QStringLiteral("ignoreSpace"), ignoreSpace).toBool();
    wordDiff = q.value(QStringLiteral("wordDiff"), wordDiff).toInt();
    dateFormat = q.value(QStringLiteral("dateFormat"), dateFormat).toString();
    extDiffTool = q.value(QStringLiteral("extDiffTool"), extDiffTool).toString();
    maxRefs = q.value(QStringLiteral("maxRefs"), maxRefs).toInt();
    q.endGroup();

    q.beginGroup(QStringLiteral("colors"));
    auto color = [&](const char *key, QColor &c) {
        QVariant v = q.value(QLatin1String(key));
        if (v.isValid()) c = QColor(v.toString());
    };
    color("bg", bgColor);
    color("fg", fgColor);
    color("selectBg", selectBgColor);
    color("diffOld", diffOldColor);
    color("diffNew", diffNewColor);
    color("diffHunk", diffHunkColor);
    color("diffOldBg", diffOldBg);
    color("diffNewBg", diffNewBg);
    color("markBg", markBgColor);
    color("headBg", headBgColor);
    color("remoteBg", remoteBgColor);
    color("tagBg", tagBgColor);
    color("fileSepBg", fileSepBgColor);
    color("foundBg", foundBgColor);
    color("currentSearchBg", currentSearchBgColor);
    color("link", linkColor);
    QStringList gc = q.value(QStringLiteral("graph")).toStringList();
    if (!gc.isEmpty()) {
        graphColors.clear();
        for (const QString &s : gc) graphColors << QColor(s);
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
    q.setValue(QStringLiteral("limitDiffs"), limitDiffs);
    q.setValue(QStringLiteral("showNearTags"), showNearTags);
    q.setValue(QStringLiteral("wrapDiff"), wrapDiff);
    q.setValue(QStringLiteral("diffContext"), diffContext);
    q.setValue(QStringLiteral("ignoreSpace"), ignoreSpace);
    q.setValue(QStringLiteral("wordDiff"), wordDiff);
    q.setValue(QStringLiteral("dateFormat"), dateFormat);
    q.setValue(QStringLiteral("extDiffTool"), extDiffTool);
    q.setValue(QStringLiteral("maxRefs"), maxRefs);
    q.endGroup();

    q.beginGroup(QStringLiteral("colors"));
    q.setValue(QStringLiteral("bg"), bgColor.name());
    q.setValue(QStringLiteral("fg"), fgColor.name());
    q.setValue(QStringLiteral("selectBg"), selectBgColor.name());
    q.setValue(QStringLiteral("diffOld"), diffOldColor.name());
    q.setValue(QStringLiteral("diffNew"), diffNewColor.name());
    q.setValue(QStringLiteral("diffHunk"), diffHunkColor.name());
    q.setValue(QStringLiteral("diffOldBg"), diffOldBg.name());
    q.setValue(QStringLiteral("diffNewBg"), diffNewBg.name());
    q.setValue(QStringLiteral("markBg"), markBgColor.name());
    q.setValue(QStringLiteral("headBg"), headBgColor.name());
    q.setValue(QStringLiteral("remoteBg"), remoteBgColor.name());
    q.setValue(QStringLiteral("tagBg"), tagBgColor.name());
    q.setValue(QStringLiteral("fileSepBg"), fileSepBgColor.name());
    q.setValue(QStringLiteral("foundBg"), foundBgColor.name());
    q.setValue(QStringLiteral("currentSearchBg"), currentSearchBgColor.name());
    q.setValue(QStringLiteral("link"), linkColor.name());
    QStringList gc;
    for (const QColor &c : graphColors) gc << c.name();
    q.setValue(QStringLiteral("graph"), gc);
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
