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

// An icon painting `g` at any size. With an invalid `color` it uses the application palette's
// text colour (button text for active, a dimmed variant for disabled icons) at paint time.
QIcon glyphIcon(Glyph g, const QColor &color = QColor());

} // namespace gitq
