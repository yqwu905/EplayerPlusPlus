#ifndef FLUENTSTYLE_H
#define FLUENTSTYLE_H

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

/**
 * @brief Centralized Fluent 2 Design System tokens and global stylesheet.
 *
 * Provides color palette, typography scale, spacing constants, and a
 * global QSS stylesheet that applies Fluent 2 visual language across
 * all standard Qt widgets.
 */
namespace FluentStyle
{

// ============================================================
//  Color Tokens — Neutral Palette
// ============================================================
inline constexpr QColor neutralBackground1()   { return QColor(0xFA, 0xFA, 0xFA); } // #FAFAFA  — app background
inline constexpr QColor neutralBackground2()   { return QColor(0xF5, 0xF5, 0xF5); } // #F5F5F5  — surface / panel bg
inline constexpr QColor neutralBackground3()   { return QColor(0xEF, 0xEF, 0xEF); } // #EFEFEF  — subtle surface
inline constexpr QColor neutralBackground4()   { return QColor(0xE0, 0xE0, 0xE0); } // #E0E0E0  — card hover
inline constexpr QColor neutralBackgroundCard() { return QColor(0xFF, 0xFF, 0xFF); } // #FFFFFF  — card surface
inline constexpr QColor neutralStroke1()       { return QColor(0xE0, 0xE0, 0xE0); } // #E0E0E0  — border
inline constexpr QColor neutralStroke2()       { return QColor(0xD1, 0xD1, 0xD1); } // #D1D1D1  — subtle border
inline constexpr QColor neutralForeground1()   { return QColor(0x1A, 0x1A, 0x1A); } // #1A1A1A  — primary text
inline constexpr QColor neutralForeground2()   { return QColor(0x61, 0x61, 0x61); } // #616161  — secondary text
inline constexpr QColor neutralForeground3()   { return QColor(0x9E, 0x9E, 0x9E); } // #9E9E9E  — tertiary text
inline constexpr QColor neutralForegroundDisabled() { return QColor(0xBD, 0xBD, 0xBD); }

// ============================================================
//  Color Tokens — Brand Palette
// ============================================================
inline constexpr QColor brandPrimary()         { return QColor(0x00, 0x78, 0xD4); } // #0078D4
inline constexpr QColor brandHover()           { return QColor(0x10, 0x6E, 0xBE); } // #106EBE
inline constexpr QColor brandPressed()         { return QColor(0x00, 0x5A, 0x9E); } // #005A9E
inline constexpr QColor brandSelected()        { return QColor(0xE5, 0xF1, 0xFB); } // #E5F1FB  — selected row bg
inline constexpr QColor brandSelectedBorder()  { return QColor(0x00, 0x78, 0xD4); } // same as primary

// ============================================================
//  Color Tokens — Semantic
// ============================================================
inline constexpr QColor dangerPrimary()        { return QColor(0xD1, 0x34, 0x38); } // #D13438
inline constexpr QColor successPrimary()       { return QColor(0x10, 0x7C, 0x10); } // #107C10
inline constexpr QColor warningPrimary()       { return QColor(0xFF, 0xB9, 0x00); } // #FFB900

// ============================================================
//  Spacing (4px base grid)
// ============================================================
inline constexpr int spacingXS()  { return 4;  }
inline constexpr int spacingS()   { return 8;  }
inline constexpr int spacingM()   { return 12; }
inline constexpr int spacingL()   { return 16; }
inline constexpr int spacingXL()  { return 20; }
inline constexpr int spacingXXL() { return 24; }

// ============================================================
//  Border Radius
// ============================================================
inline constexpr int radiusSmall()  { return 4; }
inline constexpr int radiusMedium() { return 8; }
inline constexpr int radiusLarge()  { return 12; }

// ============================================================
//  Elevation / Shadow
// ============================================================
inline constexpr int elevationNone()  { return 0; }
inline constexpr int elevationCard()  { return 2; }
inline constexpr int elevationFloat() { return 8; }

// ============================================================
//  Typography — Font sizes (pt)
// ============================================================
inline constexpr int fontSizeCaption()  { return 10; }
inline constexpr int fontSizeBody()     { return 12; }
inline constexpr int fontSizeSubtitle() { return 13; }
inline constexpr int fontSizeTitle()    { return 16; }
inline constexpr int fontSizeDisplay()  { return 20; }

/**
 * @brief Apply the Fluent 2 global stylesheet and font to the application.
 *
 * Should be called once at startup from main().
 */
void applyGlobalStyle(QApplication *app);

/**
 * @brief Get the global Fluent 2 QSS stylesheet string.
 */
QString globalStyleSheet();

} // namespace FluentStyle

#endif // FLUENTSTYLE_H
