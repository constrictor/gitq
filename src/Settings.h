// User preferences and saved views, persisted with QSettings.
#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QStringList>

namespace gitq {

struct ViewDef {
    QString name;
    QStringList args;   // rev-list style arguments, including "--" and paths
    bool remember = false;
    bool operator==(const ViewDef &) const = default;
};

struct Settings {
    // Fonts
    QFont mainFont;     // commit list
    QFont textFont;     // diff / details
    QFont uiFont;

    // Behaviour
    int tabStop = 8;
    int maxGraphWidth = 16;      // columns
    int maxGraphPct = 50;        // percentage of the column width
    bool showLocalChanges = true;
    bool autoSelect = true;      // auto-select SHA1 in the id field
    int autoSelLen = 40;
    bool hideRemotes = false;
    bool limitDiffs = true;      // limit diffs to the view's paths
    bool showNearTags = true;
    bool wrapDiff = false;
    int diffContext = 3;
    bool ignoreSpace = false;
    int wordDiff = 0;            // 0 line diff, 1 markup words, 2 color words
    QString dateFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    QString extDiffTool = QStringLiteral("meld");
    int maxRefs = 20;            // max refs to list in "Branches/Follows/Precedes"

    // Colours. The initialisers are the light-theme (gitk) defaults; with a dark application
    // palette, themeDefaults() supplies dark ones. Only colours that differ from the defaults
    // of the current theme are persisted, so the rest follow the theme.
    QColor bgColor = Qt::white;
    QColor fgColor = Qt::black;
    QColor selectBgColor = QColor(0xd9, 0xd9, 0xd9);
    QList<QColor> graphColors = {QColor(0x00, 0xff, 0x00), Qt::red, Qt::blue, Qt::magenta,
                                 Qt::darkGray, QColor(0xa5, 0x2a, 0x2a), QColor(0xff, 0xa5, 0x00)};
    QColor diffOldColor = QColor(0xc3, 0x00, 0x00);
    QColor diffNewColor = QColor(0x00, 0x98, 0x00);
    QColor diffHunkColor = Qt::blue;
    QColor diffOldBg = QColor(0xff, 0xf3, 0xf3);
    QColor diffNewBg = QColor(0xf0, 0xff, 0xf0);
    QColor markBgColor = QColor(0xe0, 0xe0, 0xff);
    QColor headBgColor = QColor(0x00, 0xff, 0x00);
    QColor remoteBgColor = QColor(0xff, 0xdd, 0xaa);
    QColor tagBgColor = Qt::yellow;
    QColor otherRefBgColor = QColor(0xdd, 0xdd, 0xff);
    QColor fileSepBgColor = QColor(0xaa, 0xaa, 0xaa);
    QColor foundBgColor = Qt::yellow;
    QColor currentSearchBgColor = QColor(0xff, 0xa5, 0x00);
    QColor linkColor = Qt::blue;
    QColor workdirCircleColor = Qt::red;
    QColor indexCircleColor = QColor(0x00, 0xff, 0x00);
    QColor mainHeadCircleColor = Qt::yellow;

    // Saved views ("remember this view")
    QList<ViewDef> views;

    static Settings &instance();
    void load();
    void save() const;

    // Default colours for the current application palette.
    static Settings themeDefaults();
    static bool darkTheme();
    // Call after the application palette changed: colours still at the old theme's defaults
    // switch to the new theme's defaults; customised colours are kept.
    void updateThemeColors();
};

} // namespace gitq
