#include "ImageComparer.h"

#include <algorithm>
#include <cmath>

QImage ImageComparer::generateToleranceMap(const QImage &imageA,
                                           const QImage &imageB,
                                           int threshold)
{
    if (imageA.isNull() || imageB.isNull()) {
        return QImage();
    }

    // Use imageB's size as the output size
    const QSize outputSize = imageB.size();

    // Scale imageA to match imageB's size if needed
    QImage scaledA = imageA;
    if (imageA.size() != outputSize) {
        scaledA = imageA.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    // Convert both images to ARGB32 for consistent pixel access
    QImage srcA = scaledA.convertToFormat(QImage::Format_ARGB32);
    QImage srcB = imageB.convertToFormat(QImage::Format_ARGB32);

    // Start with a copy of imageB — this ensures imageB is fully preserved
    QImage result = srcB.copy();

    threshold = std::clamp(threshold, 0, 255);

    for (int y = 0; y < outputSize.height(); ++y) {
        const QRgb *lineA = reinterpret_cast<const QRgb *>(srcA.constScanLine(y));
        const QRgb *lineB = reinterpret_cast<const QRgb *>(srcB.constScanLine(y));
        QRgb *lineOut = reinterpret_cast<QRgb *>(result.scanLine(y));

        for (int x = 0; x < outputSize.width(); ++x) {
            int diff = pixelDifference(lineA[x], lineB[x]);

            if (diff == 0) {
                // No difference — keep imageB pixel as-is (no modification)
                // lineOut[x] is already srcB's pixel from the copy
            } else if (diff > threshold) {
                // Above threshold — apply red tint overlay on imageB's pixel
                int r = qRed(lineB[x]);
                int g = qGreen(lineB[x]);
                int b = qBlue(lineB[x]);

                // Overlay intensity scales with difference
                double t = std::min(1.0, static_cast<double>(diff) / 255.0 * 1.5);
                double alpha = 0.25 + 0.35 * t; // 0.25 ~ 0.6

                r = static_cast<int>(r * (1.0 - alpha) + 255 * alpha);
                g = static_cast<int>(g * (1.0 - alpha) + 50 * alpha);
                b = static_cast<int>(b * (1.0 - alpha) + 50 * alpha);
                lineOut[x] = qRgba(std::min(r, 255), std::min(g, 255),
                                   std::min(b, 255), qAlpha(lineB[x]));
            } else {
                // Below threshold — apply subtle blue tint overlay on imageB's pixel
                int r = qRed(lineB[x]);
                int g = qGreen(lineB[x]);
                int b = qBlue(lineB[x]);

                double t = static_cast<double>(diff) / std::max(threshold, 1);
                double alpha = 0.10 + 0.20 * t; // 0.10 ~ 0.30

                r = static_cast<int>(r * (1.0 - alpha) + 60 * alpha);
                g = static_cast<int>(g * (1.0 - alpha) + 100 * alpha);
                b = static_cast<int>(b * (1.0 - alpha) + 255 * alpha);
                lineOut[x] = qRgba(std::min(r, 255), std::min(g, 255),
                                   std::min(b, 255), qAlpha(lineB[x]));
            }
        }
    }

    return result;
}

int ImageComparer::pixelDifference(QRgb colorA, QRgb colorB)
{
    int dr = std::abs(qRed(colorA) - qRed(colorB));
    int dg = std::abs(qGreen(colorA) - qGreen(colorB));
    int db = std::abs(qBlue(colorA) - qBlue(colorB));
    return std::max({dr, dg, db});
}
