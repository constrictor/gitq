// Repairs for incomplete platform palettes.
//
// Without a Qt 6 build of KDE's platform integration (Kubuntu 24.04, Plasma 5), Qt 6 falls back to
// its built-in KDE theme. That theme reads only a few colours from kdeglobals: it leaves the
// placeholder text at black (invisible on a dark Base) and derives Light / Dark / Shadow the
// light-theme way, so in a dark scheme "Dark" is a light grey that stylesheets then paint as a
// background. repaired() fixes such roles from the ones the platform did set.
#pragma once

#include <QPalette>

namespace gitq::Theme {

// `pal` with the roles that a dark palette got wrong derived from its Base / Text / Button; a light
// or consistent palette is returned unchanged.
QPalette repaired(const QPalette &pal);

// Applies repaired() to the application palette when it changes anything. Call it once the
// QApplication exists and again when the application palette changes (the system theme was
// switched): it goes back to the platform's palette first, so earlier repairs don't stick.
void repairApplicationPalette();

// With the Fusion style (what Qt 6 uses when no desktop style plugin is available, e.g. on
// Kubuntu 24.04), outlines check boxes, line edits, spin boxes and the commit message editor in
// dark palettes, where Fusion draws their borders barely darker than the window.
void installStyleFixes();

} // namespace gitq::Theme
