#ifndef IMAGECOMPARER_H
#define IMAGECOMPARER_H

#include <QImage>

/**
 * @brief Image comparison algorithm service.
 *
 * Generates tolerance maps by computing per-pixel differences between two images.
 * Pixels are colored based on difference magnitude:
 *   - Difference > threshold: red channel set to 255
 *   - Difference <= threshold (but > 0): blue channel set to 255
 *   - Difference == 0: pixel converted to grayscale
 */
class ImageComparer
{
public:
    /**
     * @brief Generate a tolerance (difference) map between two images.
     * @param imageA The source image (image being compared from).
     * @param imageB The target image (image being compared to).
     * @param threshold Difference threshold (0-255). Pixels with difference
     *                  above this value are marked red, otherwise blue.
     * @return The tolerance map image. Returns null QImage if inputs are invalid.
     *
     * If the images have different sizes, imageA is scaled to match imageB's size.
     */
    static QImage generateToleranceMap(const QImage &imageA,
                                       const QImage &imageB,
                                       int threshold = 10);

    /**
     * @brief Compute the per-pixel difference value between two colors.
     * @param colorA First color.
     * @param colorB Second color.
     * @return Maximum absolute difference across R, G, B channels (0-255).
     */
    static int pixelDifference(QRgb colorA, QRgb colorB);
};

#endif // IMAGECOMPARER_H
