#include "FluentStyle.h"

#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QtMath>

namespace FluentStyle
{

namespace
{

class FluentVectorIconEngine final : public QIconEngine
{
public:
    FluentVectorIconEngine(Icon kind, const QColor &baseColor)
        : m_kind(kind)
        , m_baseColor(baseColor)
    {
    }

    void paint(QPainter *painter,
               const QRect &rect,
               QIcon::Mode mode,
               QIcon::State state) override
    {
        if (!painter || rect.isEmpty()) {
            return;
        }

        const QColor ink = resolvedColor(mode, state);
        const QColor accent = (mode == QIcon::Disabled)
            ? neutralForegroundDisabled()
            : brandPrimary();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        // Normalize into a centered 16x16 logical coordinate system so every
        // icon scales crisply at 1x, 1.25x, 1.5x, 2x, and 3x DPI.
        const qreal extent = qMin(rect.width(), rect.height());
        const qreal offsetX = rect.x() + (rect.width() - extent) / 2.0;
        const qreal offsetY = rect.y() + (rect.height() - extent) / 2.0;
        painter->translate(offsetX, offsetY);
        painter->scale(extent / 16.0, extent / 16.0);

        QPen strokePen(ink, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(strokePen);
        painter->setBrush(Qt::NoBrush);

        switch (m_kind) {
        case Icon::FolderAdd: {
            // Fluent folder outline with a crisp brand-accent '+' badge.
            QPainterPath folder;
            folder.moveTo(2.0, 4.2);
            folder.quadTo(2.0, 3.0, 3.2, 3.0);
            folder.lineTo(6.1, 3.0);
            folder.lineTo(7.5, 4.6);
            folder.lineTo(12.8, 4.6);
            folder.quadTo(14.0, 4.6, 14.0, 5.8);
            folder.lineTo(14.0, 8.2);
            painter->drawPath(folder);

            QPainterPath bodyBottom;
            bodyBottom.moveTo(2.0, 4.2);
            bodyBottom.lineTo(2.0, 11.8);
            bodyBottom.quadTo(2.0, 13.0, 3.2, 13.0);
            bodyBottom.lineTo(8.4, 13.0);
            painter->drawPath(bodyBottom);

            QPen plusPen(accent, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(plusPen);
            painter->drawLine(QPointF(11.8, 9.4), QPointF(11.8, 14.0));
            painter->drawLine(QPointF(9.5, 11.7), QPointF(14.1, 11.7));
            break;
        }
        case Icon::Refresh: {
            // Clean 285-degree circular arc with balanced open arrowhead.
            const QRectF arcRect(2.7, 2.7, 10.6, 10.6);
            painter->drawArc(arcRect, 35 * 16, 285 * 16);

            QPainterPath arrow;
            arrow.moveTo(10.8, 2.3);
            arrow.lineTo(13.2, 4.6);
            arrow.lineTo(10.2, 5.3);
            painter->drawPath(arrow);
            break;
        }
        case Icon::Clear: {
            // Minimalist Fluent trash bin: handle, lid, tapered body, two flutes.
            painter->drawLine(QPointF(2.4, 4.4), QPointF(13.6, 4.4));
            QPainterPath handle;
            handle.moveTo(6.1, 4.4);
            handle.lineTo(6.1, 3.1);
            handle.quadTo(6.1, 2.3, 6.9, 2.3);
            handle.lineTo(9.1, 2.3);
            handle.quadTo(9.9, 2.3, 9.9, 3.1);
            handle.lineTo(9.9, 4.4);
            painter->drawPath(handle);

            QPainterPath bin;
            bin.moveTo(3.8, 4.4);
            bin.lineTo(4.4, 12.6);
            bin.quadTo(4.5, 13.7, 5.6, 13.7);
            bin.lineTo(10.4, 13.7);
            bin.quadTo(11.5, 13.7, 11.6, 12.6);
            bin.lineTo(12.2, 4.4);
            painter->drawPath(bin);

            QPen flutePen(ink, 1.25, Qt::SolidLine, Qt::RoundCap);
            painter->setPen(flutePen);
            painter->drawLine(QPointF(6.7, 7.0), QPointF(6.9, 11.3));
            painter->drawLine(QPointF(9.3, 7.0), QPointF(9.1, 11.3));
            break;
        }
        case Icon::ArrowUp: {
            painter->drawLine(QPointF(8.0, 12.8), QPointF(8.0, 3.4));
            QPainterPath head;
            head.moveTo(4.2, 7.0);
            head.lineTo(8.0, 3.2);
            head.lineTo(11.8, 7.0);
            painter->drawPath(head);
            break;
        }
        case Icon::ArrowDown: {
            painter->drawLine(QPointF(8.0, 3.2), QPointF(8.0, 12.6));
            QPainterPath head;
            head.moveTo(4.2, 9.0);
            head.lineTo(8.0, 12.8);
            head.lineTo(11.8, 9.0);
            painter->drawPath(head);
            break;
        }
        case Icon::Swap: {
            // Opposing horizontal arrows.
            painter->drawLine(QPointF(3.0, 5.5), QPointF(12.8, 5.5));
            QPainterPath topHead;
            topHead.moveTo(10.1, 3.0);
            topHead.lineTo(12.8, 5.5);
            topHead.lineTo(10.1, 8.0);
            painter->drawPath(topHead);

            painter->drawLine(QPointF(13.0, 10.5), QPointF(3.2, 10.5));
            QPainterPath bottomHead;
            bottomHead.moveTo(5.9, 8.0);
            bottomHead.lineTo(3.2, 10.5);
            bottomHead.lineTo(5.9, 13.0);
            painter->drawPath(bottomHead);
            break;
        }
        case Icon::Tolerance: {
            // Split comparison frame with subtle diff fill on the right half.
            const QRectF frame(2.4, 2.8, 11.2, 10.4);
            painter->drawRoundedRect(frame, 2.0, 2.0);
            painter->drawLine(QPointF(8.0, 2.8), QPointF(8.0, 13.2));

            QColor tint = ink;
            tint.setAlpha(45);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tint);
            painter->drawRoundedRect(QRectF(8.6, 3.6, 4.2, 8.8), 1.0, 1.0);
            break;
        }
        case Icon::Settings: {
            // Clean 6-tooth Fluent gear.
            painter->drawEllipse(QPointF(8.0, 8.0), 2.3, 2.3);
            painter->drawEllipse(QPointF(8.0, 8.0), 5.0, 5.0);
            for (int i = 0; i < 6; ++i) {
                const qreal rad = qDegreesToRadians(i * 60.0);
                const QPointF inner(8.0 + 5.1 * qCos(rad), 8.0 + 5.1 * qSin(rad));
                const QPointF outer(8.0 + 6.6 * qCos(rad), 8.0 + 6.6 * qSin(rad));
                painter->drawLine(inner, outer);
            }
            break;
        }
        case Icon::Sparkle: {
            // Four-point AI sparkle + small secondary accent star.
            QPainterPath star;
            star.moveTo(7.2, 2.2);
            star.quadTo(7.2, 7.2, 12.2, 7.2);
            star.quadTo(7.2, 7.2, 7.2, 12.2);
            star.quadTo(7.2, 7.2, 2.2, 7.2);
            star.quadTo(7.2, 7.2, 7.2, 2.2);
            star.closeSubpath();
            painter->setPen(QPen(ink, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(star);

            painter->setPen(QPen(accent, 1.35, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(12.4, 10.3), QPointF(12.4, 13.7));
            painter->drawLine(QPointF(10.7, 12.0), QPointF(14.1, 12.0));
            break;
        }
        case Icon::Edit: {
            // Clean diagonal pencil with subtle baseline.
            QPainterPath pencil;
            pencil.moveTo(3.2, 12.8);
            pencil.lineTo(4.0, 9.9);
            pencil.lineTo(10.6, 3.3);
            pencil.quadTo(11.4, 2.5, 12.2, 3.3);
            pencil.lineTo(12.7, 3.8);
            pencil.quadTo(13.5, 4.6, 12.7, 5.4);
            pencil.lineTo(6.1, 12.0);
            pencil.closeSubpath();
            painter->drawPath(pencil);
            painter->drawLine(QPointF(9.4, 4.5), QPointF(11.5, 6.6));
            break;
        }
        case Icon::More: {
            painter->setPen(Qt::NoPen);
            painter->setBrush(ink);
            painter->drawEllipse(QPointF(3.4, 8.0), 1.35, 1.35);
            painter->drawEllipse(QPointF(8.0, 8.0), 1.35, 1.35);
            painter->drawEllipse(QPointF(12.6, 8.0), 1.35, 1.35);
            break;
        }
        }

        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size,
                         QIcon::Mode mode,
                         QIcon::State state,
                         qreal scale) override
    {
        const qreal effectiveScale = (scale > 0.0) ? scale : 1.0;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        // Qt 6.8+ passes logical (device-independent) size to scaledPixmap().
        const QSize logicalSize = size.isValid() ? size : QSize(16, 16);
        const QSize pixelSize(qMax(1, qRound(logicalSize.width() * effectiveScale)),
                              qMax(1, qRound(logicalSize.height() * effectiveScale)));
#else
        // Qt 6.7 and earlier pass physical (pre-scaled) size to scaledPixmap().
        const QSize pixelSize = size.isValid() ? size : QSize(16, 16);
        const QSize logicalSize(qMax(1, qRound(pixelSize.width() / effectiveScale)),
                                qMax(1, qRound(pixelSize.height() / effectiveScale)));
#endif
        QPixmap pm(pixelSize);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(effectiveScale);
        QPainter painter(&pm);
        paint(&painter, QRect(QPoint(0, 0), logicalSize), mode, state);
        return pm;
    }

    QIconEngine *clone() const override
    {
        return new FluentVectorIconEngine(m_kind, m_baseColor);
    }

private:
    QColor resolvedColor(QIcon::Mode mode, QIcon::State state) const
    {
        if (mode == QIcon::Disabled) {
            return qApp->palette().color(QPalette::Disabled, QPalette::WindowText);
        }
        if (state == QIcon::On || mode == QIcon::Selected) {
            return qApp->palette().color(QPalette::Link);
        }
        if (m_baseColor.isValid()) {
            return m_baseColor;
        }
        return qApp->palette().color(QPalette::WindowText);
    }

    Icon m_kind;
    QColor m_baseColor;
};

} // namespace

QIcon icon(Icon kind, const QColor &color)
{
    return QIcon(new FluentVectorIconEngine(kind, color));
}

void applyGlobalStyle(QApplication *app, bool dark)
{
#if defined(Q_OS_MACOS)
    const Platform platform = Platform::MacOS;
#elif defined(Q_OS_WIN)
    const Platform platform = Platform::Windows;
#else
    const Platform platform = Platform::Other;
#endif

    QPalette palette;
    const auto setColor = [&palette, dark](QPalette::ColorRole role, QRgb light, QRgb darkColor) {
        palette.setColor(role, QColor::fromRgb(dark ? darkColor : light));
    };
    setColor(QPalette::Window, 0xFAFAFA, 0x181B20);
    setColor(QPalette::Base, 0xFFFFFF, 0x23272E);
    setColor(QPalette::AlternateBase, 0xF5F5F5, 0x1C2026);
    setColor(QPalette::Button, 0xFFFFFF, 0x2D333D);
    setColor(QPalette::Light, 0xEFEFEF, 0x363D49);
    setColor(QPalette::Midlight, 0xE5F1FB, 0x233E59);
    setColor(QPalette::Mid, 0xE0E0E0, 0x424B58);
    setColor(QPalette::Dark, 0xC7C7C7, 0x606C7C);
    setColor(QPalette::Shadow, 0x808080, 0x8795A7);
    setColor(QPalette::WindowText, 0x1A1A1A, 0xF1F4F8);
    setColor(QPalette::Text, 0x1A1A1A, 0xF1F4F8);
    setColor(QPalette::ButtonText, 0x616161, 0xC2CCD9);
    setColor(QPalette::PlaceholderText, 0x777777, 0xA5B1C0);
    setColor(QPalette::Link, 0x006BBE, 0x8BC6FF);
    setColor(QPalette::Highlight, 0x0078D4, 0x006BBE);
    setColor(QPalette::HighlightedText, 0xFFFFFF, 0xFFFFFF);
    setColor(QPalette::ToolTipBase, 0xFFFFFF, 0x2D333D);
    setColor(QPalette::ToolTipText, 0x1A1A1A, 0xF1F4F8);
    const QColor disabledText = dark ? QColor(0x80, 0x8B, 0x99) : neutralForegroundDisabled();
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);

    app->setPalette(palette);
    app->setFont(applicationFont(platform));
    app->setStyleSheet(globalStyleSheet());
}

QStringList preferredFontFamilies(Platform platform)
{
    switch (platform) {
    case Platform::Windows:
        return {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Segoe UI Variable Text"),
            QStringLiteral("Segoe UI")
        };
    case Platform::MacOS:
        return {QStringLiteral(".AppleSystemUIFont")};
    case Platform::Other:
        return {};
    }

    return {};
}

QFont applicationFont(Platform platform)
{
    QFont appFont;
    appFont.setFamilies(preferredFontFamilies(platform));
    appFont.setPointSize(fontSizeBody());
    appFont.setWeight(QFont::Normal);
    appFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));

    if (platform == Platform::Windows) {
        appFont.setHintingPreference(QFont::PreferNoHinting);
    }

    return appFont;
}

QString globalStyleSheet()
{
    return QStringLiteral(R"(

/* ============================================================
   GLOBAL RESET
   ============================================================ */
* {
    outline: none;
}

/* ============================================================
   QMainWindow / top-level background
   ============================================================ */
QMainWindow {
    background-color: palette(window);
}

/* ============================================================
   QMenuBar
   ============================================================ */
QMenuBar {
    background-color: palette(window);
    border-bottom: 1px solid palette(mid);
    padding: 2px 0px;
    spacing: 0px;
}
QMenuBar::item {
    padding: 6px 12px;
    border-radius: 4px;
    color: palette(window-text);
}
QMenuBar::item:selected {
    background-color: palette(light);
}
QMenuBar::item:pressed {
    background-color: palette(mid);
}

/* ============================================================
   QMenu (context menu, dropdown)
   ============================================================ */
QMenu {
    background-color: palette(base);
    border: 1px solid palette(mid);
    border-radius: 8px;
    padding: 4px;
}
QMenu::item {
    padding: 8px 32px 8px 12px;
    border-radius: 4px;
    color: palette(window-text);
}
QMenu::item:selected {
    background-color: palette(light);
}
QMenu::item:disabled {
    color: palette(button-text);
}
QMenu::separator {
    height: 1px;
    background-color: palette(mid);
    margin: 4px 8px;
}

/* ============================================================
   QToolBar
   ============================================================ */
QToolBar {
    background-color: palette(window);
    border: none;
    border-bottom: 1px solid palette(mid);
    padding: 4px 8px;
    spacing: 4px;
}
QToolBar::separator {
    width: 1px;
    background-color: palette(mid);
    margin: 4px 4px;
}

/* ============================================================
   QToolButton (toolbar buttons)
   ============================================================ */
QToolButton {
    background-color: transparent;
    border: none;
    border-radius: 4px;
    padding: 6px 12px;
    color: palette(window-text);
    font-size: 12px;
}
QToolButton:hover {
    background-color: palette(light);
}
QToolButton:pressed {
    background-color: palette(mid);
}
QToolButton:checked {
    background-color: palette(midlight);
    color: palette(link);
}

/* ============================================================
   QPushButton
   ============================================================ */
QPushButton {
    background-color: palette(base);
    border: 1px solid palette(dark);
    border-radius: 4px;
    padding: 6px 16px;
    color: palette(window-text);
    font-size: 12px;
    min-height: 20px;
}
QPushButton:hover {
    background-color: palette(alternate-base);
    border-color: palette(dark);
}
QPushButton:pressed {
    background-color: palette(light);
    border-color: palette(dark);
}
QPushButton:disabled {
    background-color: palette(alternate-base);
    border-color: palette(mid);
    color: palette(button-text);
}
/* Primary (accent) button: set objectName = "primaryButton" */
QPushButton#primaryButton {
    background-color: #0078D4;
    border: 1px solid #0078D4;
    color: #FFFFFF;
}
QPushButton#primaryButton:hover {
    background-color: #106EBE;
    border-color: #106EBE;
}
QPushButton#primaryButton:pressed {
    background-color: #005A9E;
    border-color: #005A9E;
}

/* ============================================================
   QTreeView
   ============================================================ */
QTreeView {
    background-color: palette(window);
    border: none;
    outline: none;
    font-size: 12px;
    color: palette(window-text);
}
QTreeView::item {
    padding: 4px 8px;
    border-radius: 4px;
    min-height: 24px;
}
QTreeView::item:hover {
    background-color: palette(light);
}
QTreeView::item:selected {
    background-color: palette(midlight);
    color: palette(window-text);
}
QTreeView::branch {
    background-color: transparent;
}
QTreeView::branch:has-children:!has-siblings:closed,
QTreeView::branch:closed:has-children:has-siblings {
    image: url(:/icons/branch_closed.svg);
    padding: 5px;
}
QTreeView::branch:open:has-children:!has-siblings,
QTreeView::branch:open:has-children:has-siblings {
    image: url(:/icons/branch_open.svg);
    padding: 5px;
}

/* ============================================================
   QScrollArea
   ============================================================ */
QScrollArea {
    background-color: transparent;
    border: none;
}

/* ============================================================
   QScrollBar — Vertical (thin Fluent-style)
   ============================================================ */
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0px;
    border: none;
}
QScrollBar::handle:vertical {
    background-color: palette(dark);
    border-radius: 4px;
    min-height: 32px;
}
QScrollBar::handle:vertical:hover {
    background-color: palette(shadow);
}
QScrollBar::handle:vertical:pressed {
    background-color: palette(shadow);
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0px;
}
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: transparent;
}

/* ============================================================
   QScrollBar — Horizontal (thin Fluent-style)
   ============================================================ */
QScrollBar:horizontal {
    background: transparent;
    height: 8px;
    margin: 0px;
    border: none;
}
QScrollBar::handle:horizontal {
    background-color: palette(dark);
    border-radius: 4px;
    min-width: 32px;
}
QScrollBar::handle:horizontal:hover {
    background-color: palette(shadow);
}
QScrollBar::handle:horizontal:pressed {
    background-color: palette(shadow);
}
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
    width: 0px;
}
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: transparent;
}

/* ============================================================
   QSlider — Horizontal (Fluent-style)
   ============================================================ */
QSlider::groove:horizontal {
    background-color: palette(mid);
    height: 4px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background-color: palette(base);
    border: 2px solid #0078D4;
    width: 16px;
    height: 16px;
    margin: -7px 0;
    border-radius: 9px;
}
QSlider::handle:horizontal:hover {
    background-color: palette(midlight);
}
QSlider::handle:horizontal:pressed {
    background-color: #0078D4;
    border-color: #005A9E;
}
QSlider::sub-page:horizontal {
    background-color: #0078D4;
    border-radius: 2px;
}

/* ============================================================
   QSplitter
   ============================================================ */
QSplitter::handle {
    background-color: palette(mid);
}
QSplitter::handle:horizontal {
    width: 1px;
}
QSplitter::handle:vertical {
    height: 1px;
}
QSplitter::handle:hover {
    background-color: #0078D4;
}

/* ============================================================
   QLabel
   ============================================================ */
QLabel {
    color: palette(window-text);
    background-color: transparent;
}

/* ============================================================
   QMessageBox
   ============================================================ */
QMessageBox {
    background-color: palette(base);
}
QMessageBox QLabel {
    color: palette(window-text);
    font-size: 13px;
}

/* ============================================================
   QHeaderView (for any tables/headers)
   ============================================================ */
QHeaderView::section {
    background-color: palette(window);
    border: none;
    border-bottom: 1px solid palette(mid);
    padding: 6px 12px;
    color: palette(button-text);
    font-size: 12px;
}

)");
}

} // namespace FluentStyle
