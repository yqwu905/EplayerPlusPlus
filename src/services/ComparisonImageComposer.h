#ifndef COMPARISONIMAGECOMPOSER_H
#define COMPARISONIMAGECOMPOSER_H

#include <QColor>
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

namespace ComparisonImageComposer
{

struct CellItem {
    int index = 0;               ///< 1-based badge number (if <= 0, defaults to position + 1)
    QColor badgeColor;           ///< Accent color for index badge (falls back to default palette if invalid)
    QString gridName;            ///< Comparison grid display name (custom name or folder name)
    QString fileName;            ///< Image file name (empty if no image selected)
    QImage image;                ///< Image to render (may be null if cell has no image)
    QRectF visibleSourceRect;    ///< Optional sub-rect in image pixel coords (empty/invalid = full image)
    QString placeholderText;     ///< Placeholder text when image is null (defaults to "未选择图片")
};

struct Options {
    int maxCellImageEdge = 1600;  ///< Hard upper bound on each cell's image width/height (<=0 = unlimited)
    int maxCanvasEdge = 4096;     ///< Hard upper bound on entire canvas width/height (<=0 = unlimited)
    int minCellWidth = 320;       ///< Preferred minimum cell content width (clamped by maxCellImageEdge)
    int minCellImageHeight = 180; ///< Preferred minimum cell image area height (clamped by maxCellImageEdge)
};

struct GridDimensions {
    int rows = 0;
    int columns = 0;
};

GridDimensions gridDimensions(int cellCount);

QImage compose(const QList<CellItem> &cells, const Options &options = Options{});

} // namespace ComparisonImageComposer

#endif // COMPARISONIMAGECOMPOSER_H
