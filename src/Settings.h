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
    bool showUntracked = true;   // list untracked files in the local changes (they can be staged)
    bool commitSignOff = false;  // commit pane: add a Signed-off-by trailer
    bool autoSelect = true;      // auto-select SHA1 in the id field
    int autoSelLen = 40;
    bool hideRemotes = false;
    bool commitListMultiline = false;   // commit list: subject over author/date instead of columns
    bool limitDiffs = true;      // limit diffs to the view's paths
    bool showNearTags = true;
    bool wrapDiff = false;
    bool syntaxHighlighting = true;   // colour code in the diff pane
    bool sideBySide = false;          // diff pane: old and new side by side
    bool groupFiles = false;          // file list: grouped by folder
    int diffContext = 3;
    bool ignoreSpace = false;
    bool onlySelectedFiles = false;   // the diff shows only the files selected in the file list
    int wordDiff = 0;            // 0 line diff, 1 markup words, 2 color words
    int submoduleDiff = 1;       // DiffOptions::Submodules: 0 summary, 1 commits, 2 full diff
    QString dateFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    bool relativeDates = true;   // commit list: "2 hours ago", the exact time in the tooltip
    QString extDiffTool = QStringLiteral("meld");
    int maxRefs = 20;            // max refs to list in "Branches/Follows/Precedes"

    // Colours. The initialisers are the light-theme (gitk) defaults; with a dark application
    // palette, themeDefaults() supplies dark ones. Only colours that differ from the defaults
    // of the current theme are persisted, so the rest follow the theme.
    QColor bgColor = Qt::white;
    QColor fgColor = Qt::black;
    QColor selectBgColor = QColor(0xd9, 0xd9, 0xd9);
    // Lane colours of the commit graph: distinct hues of similar weight, readable on the background.
    QList<QColor> graphColors = {QColor(0x1f, 0x78, 0xd1), QColor(0xd9, 0x3f, 0x3f), QColor(0x2e, 0x9d, 0x4f),
                                 QColor(0x8e, 0x5c, 0xd9), QColor(0xe0, 0x82, 0x1e), QColor(0x13, 0x9c, 0x9c),
                                 QColor(0xc8, 0x3e, 0x8f), QColor(0x8a, 0x6a, 0x3c)};
    QColor diffOldColor = QColor(0xcf, 0x22, 0x2e);   // +/- marks, word diffs
    QColor diffNewColor = QColor(0x1a, 0x7f, 0x37);
    QColor diffHunkColor = QColor(0x09, 0x69, 0xda);
    QColor diffOldBg = QColor(0xff, 0xeb, 0xe9);      // removed / added lines
    QColor diffNewBg = QColor(0xe6, 0xff, 0xec);
    QColor markBgColor = QColor(0xe0, 0xe0, 0xff);
    QColor headBgColor = QColor(0xcf, 0xf0, 0xd8);       // ref labels: local branches
    QColor remoteBgColor = QColor(0xff, 0xe3, 0xc4);     // remote branches
    QColor tagBgColor = QColor(0xff, 0xf0, 0xa8);        // tags
    QColor otherRefBgColor = QColor(0xdf, 0xe2, 0xff);   // other refs
    QColor fileSepBgColor = QColor(0xee, 0xf0, 0xf3);   // file header bars in the diff
    QColor foundBgColor = Qt::yellow;
    QColor currentSearchBgColor = QColor(0xff, 0xa5, 0x00);
    QColor linkColor = Qt::blue;
    QColor workdirCircleColor = Qt::red;
    QColor indexCircleColor = QColor(0x00, 0xff, 0x00);

    // Saved views ("remember this view")
    QList<ViewDef> views;

    static Settings &instance();
    void load();
    void save() const;

    // Default colours for the current application palette.
    static Settings themeDefaults();
    // Defaults of earlier versions (gitk's colours): a stored colour equal to one of them counts as
    // not customised, so it follows the current defaults.
    static Settings legacyDefaults();
    static bool darkTheme();
    // Linear blend of two colours: t = 0 gives a, t = 1 gives b.
    static QColor mix(const QColor &a, const QColor &b, double t);
    // Call after the application palette changed: colours still at the old theme's defaults
    // switch to the new theme's defaults; customised colours are kept.
    void updateThemeColors();
};

} // namespace gitq
