#include "ComparisonImageComposer.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ComparisonImageComposer
{
namespace
{
const std::array<QColor, 6> badgePalette{
    QColor(0x186FD7), QColor(0x228A46), QColor(0xF77313),
    QColor(0x7455C8), QColor(0x0F7B93), QColor(0xC50F1F),
};
const QString noImageText = QStringLiteral("未选择图片");

QRectF effectiveSourceRect(const CellItem &cell)
{
    const QRectF fullImage(cell.image.rect());
    if (!cell.visibleSourceRect.isValid()) {
        return fullImage;
    }
    const QRectF crop = fullImage.intersected(cell.visibleSourceRect);
    return crop.width() >= 1e-3 && crop.height() >= 1e-3 ? crop : fullImage;
}

void drawLabel(QPainter &painter, const QRectF &rect, const QFont &font, const QColor &color,
               const QString &text, Qt::TextElideMode elideMode, Qt::Alignment alignment)
{
    if (rect.isEmpty()) {
        return;
    }

    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.setFont(font);
    painter.setPen(color);
    const QFontMetricsF metrics(font, painter.device());
    painter.drawText(rect, alignment | Qt::TextSingleLine,
                     metrics.elidedText(text, elideMode, rect.width()));
    painter.restore();
}
} // namespace

GridDimensions gridDimensions(int cellCount)
{
    if (cellCount <= 0) {
        return {};
    }
    if (cellCount <= 3) {
        return {1, cellCount};
    }

    const int columns = static_cast<int>(std::ceil(std::sqrt(cellCount)));
    return {1 + (cellCount - 1) / columns, columns};
}

QImage compose(const QList<CellItem> &cells, const Options &options)
{
    const int maxInt = std::numeric_limits<int>::max();
    if (cells.isEmpty() || cells.size() > maxInt) {
        return {};
    }
    const GridDimensions grid = gridDimensions(static_cast<int>(cells.size()));
    QList<QRectF> sources;
    sources.reserve(cells.size());
    QSizeF imageSlot(0, 0);
    for (const CellItem &cell : cells) {
        const QRectF source = effectiveSourceRect(cell);
        sources.append(source);
        imageSlot = imageSlot.expandedTo(source.size());
    }
    if (imageSlot.isEmpty()) {
        return {};
    }

    imageSlot = imageSlot.expandedTo(QSizeF(1, 1));
    const qreal longestEdge = std::max(imageSlot.width(), imageSlot.height());
    if (options.maxCellImageEdge > 0 && longestEdge > options.maxCellImageEdge) {
        imageSlot *= options.maxCellImageEdge / longestEdge;
    }
    const int minWidth = std::max(1, options.minCellWidth);
    const int minHeight = std::max(1, options.minCellImageHeight);
    const int effectiveMinWidth = options.maxCellImageEdge > 0
        ? std::min(minWidth, options.maxCellImageEdge)
        : minWidth;
    const int effectiveMinHeight = options.maxCellImageEdge > 0
        ? std::min(minHeight, options.maxCellImageEdge)
        : minHeight;
    imageSlot = imageSlot.expandedTo(QSizeF(effectiveMinWidth, effectiveMinHeight));

    // Keep annotation sizes independent of the zoomed image crop.
    constexpr qreal padding = 12;
    constexpr qreal gap = 12;
    constexpr qreal radius = 8;
    constexpr qreal lineGap = 2;
    constexpr qreal badgeHeight = 30;

    QFont secondaryFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    secondaryFont.setPixelSize(13);
    QFont titleFont = secondaryFont;
    titleFont.setPixelSize(16);
    titleFont.setBold(true);
    const QFontMetricsF titleMetrics(titleFont);
    const qreal titleHeight = std::ceil(titleMetrics.height());
    const qreal secondaryHeight = std::ceil(QFontMetricsF(secondaryFont).height());
    const qreal headerHeight = std::max(badgeHeight, titleHeight + lineGap + secondaryHeight);
    if (options.maxCanvasEdge > 0) {
        // Reserve the fixed headers and spacing, then fit the image slots into
        // the remaining canvas. Only very small limits shrink the full layout.
        const qreal maxSlotWidth = (options.maxCanvasEdge - gap) / grid.columns - 2 * padding - gap;
        const qreal maxSlotHeight =
            (options.maxCanvasEdge - gap) / grid.rows - headerHeight - 3 * padding - gap;
        if (maxSlotWidth >= 1.0 && maxSlotHeight >= 1.0) {
            imageSlot *= std::min({1.0, maxSlotWidth / imageSlot.width(),
                                   maxSlotHeight / imageSlot.height()});
        }
    }
    const QSizeF cardSize(imageSlot.width() + 2 * padding,
                          imageSlot.height() + headerHeight + 3 * padding);
    const QSizeF logicalCanvas(grid.columns * (cardSize.width() + gap) + gap,
                               grid.rows * (cardSize.height() + gap) + gap);
    qreal scale = 1;
    if (options.maxCanvasEdge > 0) {
        scale = std::min(scale, options.maxCanvasEdge
                                    / std::max(logicalCanvas.width(), logicalCanvas.height()));
    }
    // Reject canvas limits so small that cells cannot render both a readable
    // header band and a non-degenerate image slot.
    if (imageSlot.width() * scale < 1.0 || imageSlot.height() * scale < 1.0 ||
        headerHeight * scale < 4.0) {
        return {};
    }
    const qreal width = std::ceil(logicalCanvas.width() * scale);
    const qreal height = std::ceil(logicalCanvas.height() * scale);
    const int edgeLimit = options.maxCanvasEdge > 0 ? options.maxCanvasEdge : maxInt;
    if (options.maxCanvasEdge <= 0 && std::max(width, height) > edgeLimit) {
        return {};
    }
    QImage result(static_cast<int>(std::min<qreal>(width, edgeLimit)),
                  static_cast<int>(std::min<qreal>(height, edgeLimit)), QImage::Format_ARGB32);
    if (result.isNull()) {
        return {};
    }
    result.fill(QColor(0xF3F6FA));

    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.scale(scale, scale);
    for (qsizetype position = 0; position < cells.size(); ++position) {
        const CellItem &cell = cells.at(position);
        const QRectF source = sources.at(position);
        const QPointF origin(gap + (position % grid.columns) * (cardSize.width() + gap),
                             gap + (position / grid.columns) * (cardSize.height() + gap));
        const QRectF card(origin, cardSize);
        painter.setPen(QColor(0xDCE3EC));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(card, radius, radius);

        const int index = cell.index > 0 ? cell.index : static_cast<int>(position + 1);
        const QString badgeText = QString::number(index);
        const QColor badgeColor = cell.badgeColor.isValid()
            ? cell.badgeColor : badgePalette[(index - 1) % badgePalette.size()];
        const qreal left = card.left() + padding;
        const qreal headerTop = card.top() + padding;
        const qreal badgeWidth = std::min(imageSlot.width(),
            std::max(badgeHeight, titleMetrics.horizontalAdvance(badgeText) + padding));
        const QRectF badge(left, headerTop + (headerHeight - badgeHeight) / 2,
                           badgeWidth, badgeHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(badgeColor);
        painter.drawRoundedRect(badge, 6, 6);
        drawLabel(painter, badge, titleFont, Qt::white, badgeText, Qt::ElideRight, Qt::AlignCenter);

        const qreal labelLeft = badge.right() + padding;
        const qreal labelWidth = card.right() - padding - labelLeft;
        const qreal titleTop = headerTop + (headerHeight - titleHeight - lineGap - secondaryHeight) / 2;
        const QString gridName = cell.gridName.isEmpty()
            ? QStringLiteral("#") + badgeText : cell.gridName;
        drawLabel(painter, QRectF(labelLeft, titleTop, labelWidth, titleHeight), titleFont,
                  QColor(0x1F2937), gridName, Qt::ElideRight, Qt::AlignLeft | Qt::AlignVCenter);

        const bool hasFileName = !cell.fileName.isEmpty();
        drawLabel(painter,
                  QRectF(labelLeft, titleTop + titleHeight + lineGap, labelWidth, secondaryHeight),
                  secondaryFont, QColor(hasFileName ? 0x4B5563 : 0x9CA3AF),
                  hasFileName ? cell.fileName : noImageText, Qt::ElideMiddle,
                  Qt::AlignLeft | Qt::AlignVCenter);

        const qreal divider = headerTop + headerHeight + padding / 2;
        painter.setPen(QColor(0xEEF1F5));
        painter.drawLine(QPointF(left, divider), QPointF(card.right() - padding, divider));

        const QRectF imageArea(QPointF(left, headerTop + headerHeight + padding), imageSlot);
        painter.setBrush(QColor(0xF8FAFC));
        painter.drawRect(imageArea);
        if (source.isEmpty()) {
            const QString &placeholder = cell.placeholderText.isEmpty()
                ? noImageText : cell.placeholderText;
            drawLabel(painter, imageArea, secondaryFont, QColor(0x9CA3AF), placeholder,
                      Qt::ElideRight, Qt::AlignCenter);
            continue;
        }

        const qreal imageScale = std::min(imageSlot.width() / source.width(),
                                          imageSlot.height() / source.height());
        QRectF target(QPointF(), source.size() * imageScale);
        target.moveCenter(imageArea.center());
        painter.drawImage(target, cell.image, source);
    }
    painter.end();
    return result;
}
} // namespace ComparisonImageComposer
