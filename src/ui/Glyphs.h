// Small vector glyphs drawn with QPainter (ref labels, tool buttons), so that they look the same on
// every desktop, follow the theme's colours and stay sharp at any scale.
#pragma once

#include <QColor>
#include <QIcon>
#include <QRectF>

class QPainter;

namespace gitq {

enum class Glyph {
    Branch,       // local branch
    Cloud,        // remote branch
    Tag,
    Ref,          // other refs
    Search,
    Copy,
    CaseSensitive,
    Regex,
    Whitespace,
    Unified,      // unified diff
    Split,        // side-by-side diff
    More,         // overflow menu
    Folder,
    ArrowUp,
    ArrowDown,
};

// Draws `g` into `r` (a square works best) in `color`.
void drawGlyph(QPainter *p, Glyph g, const QRectF &r, const QColor &color);

// A file's change status (A, M, D, R, C, T) as a coloured rounded square with the letter.
QColor statusColor(QChar status);
void drawStatusBadge(QPainter *p, const QRectF &r, QChar status);
// Colours of added / removed line counts.
QColor addedColor();
QColor removedColor();
// Five small squares, green and red in the proportion of added to removed lines, ending at
// `right` and centred on `cy`; returns the left edge.
// On a selected row, pass the selection's text colour as `onSelection`: added blocks are drawn solid
// in it and removed ones as outlines, as the usual green / red would vanish on the highlight.
double drawChangeBar(QPainter *p, double right, double cy, double size, int added, int removed,
                     const QColor &onSelection = QColor());

// An icon painting `g` at any size. With an invalid `color` it uses the application palette's
// text colour (button text for active, a dimmed variant for disabled icons) at paint time.
QIcon glyphIcon(Glyph g, const QColor &color = QColor());

} // namespace gitq
