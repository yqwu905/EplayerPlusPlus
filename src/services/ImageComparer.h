#ifndef IMAGECOMPARER_H
#define IMAGECOMPARER_H

#include <QFuture>
#include <QImage>

#include <atomic>
#include <memory>

/**
 * @brief Image comparison algorithm service.
 *
 * Generates tolerance maps by computing per-pixel differences between two images.
 * Pixels are colored based on difference magnitude:
 *   - Difference > threshold: red overlay blended onto imageB's pixel
 *   - Difference <= threshold (but > 0): blue overlay blended onto imageB's pixel
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
     *                  above this value get red overlay, otherwise blue overlay.
     * @return The tolerance map image. Returns null QImage if inputs are invalid.
     *
     * If the images have different sizes, they are aligned at the top-left corner.
     * Pixels in imageB that fall outside imageA's bounds are treated as having
     * difference greater than threshold (red overlay).
     *
     * The work is parallelized across rows using QtConcurrent::blockingMap so the
     * call returns once every worker thread has finished. The call still runs
     * synchronously on whichever thread invokes it — see ::generateToleranceMapAsync
     * for a non-blocking variant suitable for use from the GUI thread.
     */
    static QImage generateToleranceMap(const QImage &imageA,
                                       const QImage &imageB,
                                       int threshold = 10);

    /**
     * @brief Async variant of ::generateToleranceMap.
     *
     * Dispatches the synchronous implementation onto a dedicated, serialized
     * outer-job pool, returning immediately. The returned QFuture<QImage> will
     * be completed with the same image that ::generateToleranceMap would have
     * returned. Use a QFutureWatcher to observe completion without blocking the
     * GUI thread. The underlying task is not cancellable; callers that need to
     * discard stale results should track a generation token and check it inside
     * the watcher slot. Serializing outer jobs bounds full-resolution temporary
     * buffers; each active job still parallelizes its rows internally.
     */
    static QFuture<QImage> generateToleranceMapAsync(const QImage &imageA,
                                                     const QImage &imageB,
                                                     int threshold = 10);
    static QFuture<QImage> generateToleranceMapAsync(
        const QImage &imageA,
        const QImage &imageB,
        int threshold,
        const std::shared_ptr<std::atomic_bool> &cancelFlag);

    /**
     * @brief Compute the per-pixel difference value between two colors.
     * @param colorA First color.
     * @param colorB Second color.
     * @return Maximum absolute difference across R, G, B channels (0-255).
     */
    static int pixelDifference(QRgb colorA, QRgb colorB);

private:
    static QImage generateToleranceMapCancellable(
        const QImage &imageA,
        const QImage &imageB,
        int threshold,
        const std::shared_ptr<std::atomic_bool> &cancelFlag);
};

#endif // IMAGECOMPARER_H
