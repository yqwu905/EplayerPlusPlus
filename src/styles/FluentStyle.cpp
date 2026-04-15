#include "FluentStyle.h"

#include <QApplication>
#include <QFont>
#include <QString>

namespace FluentStyle
{

void applyGlobalStyle(QApplication *app)
{
    // ---- System font, Fluent-appropriate weight ----
#if defined(Q_OS_MACOS)
    QFont appFont(".AppleSystemUIFont", fontSizeBody());
#elif defined(Q_OS_WIN)
    QFont appFont("Segoe UI", fontSizeBody());
#else
    QFont appFont("", fontSizeBody()); // system default
#endif
    appFont.setWeight(QFont::Normal);
    app->setFont(appFont);

    // ---- Global stylesheet ----
    app->setStyleSheet(globalStyleSheet());
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
    background-color: #FAFAFA;
}

/* ============================================================
   QMenuBar
   ============================================================ */
QMenuBar {
    background-color: #FAFAFA;
    border-bottom: 1px solid #E0E0E0;
    padding: 2px 0px;
    spacing: 0px;
}
QMenuBar::item {
    padding: 6px 12px;
    border-radius: 4px;
    color: #1A1A1A;
}
QMenuBar::item:selected {
    background-color: #EFEFEF;
}
QMenuBar::item:pressed {
    background-color: #E0E0E0;
}

/* ============================================================
   QMenu (context menu, dropdown)
   ============================================================ */
QMenu {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item {
    padding: 8px 32px 8px 12px;
    border-radius: 4px;
    color: #1A1A1A;
}
QMenu::item:selected {
    background-color: #EFEFEF;
}
QMenu::item:disabled {
    color: #BDBDBD;
}
QMenu::separator {
    height: 1px;
    background-color: #E0E0E0;
    margin: 4px 8px;
}

/* ============================================================
   QToolBar
   ============================================================ */
QToolBar {
    background-color: #FAFAFA;
    border: none;
    border-bottom: 1px solid #E0E0E0;
    padding: 4px 8px;
    spacing: 4px;
}
QToolBar::separator {
    width: 1px;
    background-color: #E0E0E0;
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
    color: #1A1A1A;
    font-size: 12px;
}
QToolButton:hover {
    background-color: #EFEFEF;
}
QToolButton:pressed {
    background-color: #E0E0E0;
}
QToolButton:checked {
    background-color: #E5F1FB;
    color: #0078D4;
}

/* ============================================================
   QPushButton
   ============================================================ */
QPushButton {
    background-color: #FFFFFF;
    border: 1px solid #D1D1D1;
    border-radius: 4px;
    padding: 6px 16px;
    color: #1A1A1A;
    font-size: 12px;
    min-height: 20px;
}
QPushButton:hover {
    background-color: #F5F5F5;
    border-color: #C7C7C7;
}
QPushButton:pressed {
    background-color: #EFEFEF;
    border-color: #BBBBBB;
}
QPushButton:disabled {
    background-color: #F5F5F5;
    border-color: #E0E0E0;
    color: #BDBDBD;
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
    background-color: #FAFAFA;
    border: none;
    outline: none;
    font-size: 12px;
    color: #1A1A1A;
}
QTreeView::item {
    padding: 4px 8px;
    border-radius: 4px;
    min-height: 24px;
}
QTreeView::item:hover {
    background-color: #F0F0F0;
}
QTreeView::item:selected {
    background-color: #E5F1FB;
    color: #1A1A1A;
}
QTreeView::branch {
    background-color: transparent;
}
QTreeView::branch:has-children:!has-siblings:closed,
QTreeView::branch:closed:has-children:has-siblings {
    image: none;
    border-image: none;
}
QTreeView::branch:open:has-children:!has-siblings,
QTreeView::branch:open:has-children:has-siblings {
    image: none;
    border-image: none;
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
    background-color: #C7C7C7;
    border-radius: 4px;
    min-height: 32px;
}
QScrollBar::handle:vertical:hover {
    background-color: #A0A0A0;
}
QScrollBar::handle:vertical:pressed {
    background-color: #808080;
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
    background-color: #C7C7C7;
    border-radius: 4px;
    min-width: 32px;
}
QScrollBar::handle:horizontal:hover {
    background-color: #A0A0A0;
}
QScrollBar::handle:horizontal:pressed {
    background-color: #808080;
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
    background-color: #E0E0E0;
    height: 4px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background-color: #FFFFFF;
    border: 2px solid #0078D4;
    width: 16px;
    height: 16px;
    margin: -7px 0;
    border-radius: 9px;
}
QSlider::handle:horizontal:hover {
    background-color: #E5F1FB;
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
    background-color: #E0E0E0;
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
    color: #1A1A1A;
    background-color: transparent;
}

/* ============================================================
   QMessageBox
   ============================================================ */
QMessageBox {
    background-color: #FFFFFF;
}
QMessageBox QLabel {
    color: #1A1A1A;
    font-size: 13px;
}

/* ============================================================
   QHeaderView (for any tables/headers)
   ============================================================ */
QHeaderView::section {
    background-color: #FAFAFA;
    border: none;
    border-bottom: 1px solid #E0E0E0;
    padding: 6px 12px;
    color: #616161;
    font-size: 12px;
}

)");
}

} // namespace FluentStyle
