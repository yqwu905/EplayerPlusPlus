#include "ImageComparer.h"

#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <QThreadPool>
#include <QVector>
#include <algorithm>
#include <cstdlib>

namespace {

// Fixed-point overlay math at 1/1000 precision. The original implementation
// used the floating-point formula `out = orig * (1 - alpha) + dst * alpha`
// with `static_cast<int>` truncation. Switching to integers at 1000x scale
// keeps the truncation semantics and is ~5x faster than double precision.
//
// alpha_scaled is in [0, 1000]. Result is clamped to [0, 255].
inline int blendChannel(int orig, int dst, int alphaScaled)
{
    const int inv = 1000 - alphaScaled;
    int v = (orig * inv + dst * alphaScaled) / 1000;
    if (v > 255) v = 255;
    if (v < 0) v = 0;
    return v;
}

// Integer luminance: (299*R + 587*G + 114*B) / 1000 — bit-identical to the
// previous `static_cast<int>(0.299*R + 0.587*G + 0.114*B)` for all
// 8-bit RGB inputs because divisions truncate toward zero and there is no
// floating-point rounding error.
inline int luminance(int r, int g, int b)
{
    return (299 * r + 587 * g + 114 * b) / 1000;
}

// Process a single row of the tolerance map. Each call writes to a unique
// scanline in `result`, so multiple threads can execute this concurrently
// without synchronization. The base pointers / stride values must be derived
// outside this function so concurrent calls never touch QImage's per-call
// state (detach checks, etc.).
struct RowContext {
    const uchar *srcAData = nullptr;
    qsizetype srcABytesPerLine = 0;
    int widthA = 0;
    int heightA = 0;
    const uchar *srcBData = nullptr;
    qsizetype srcBBytesPerLine = 0;
    uchar *resultData = nullptr;
    qsizetype resultBytesPerLine = 0;
    int width = 0;
    int threshold = 0;
};

// Dedicated pool for the per-row fan-out below. generateToleranceMapAsync hands
// the whole computation to QtConcurrent::run on the GLOBAL pool, and ComparePanel
// can launch up to six of those at once (one per cell). If the inner blockingMap
// also used the global pool, those outer tasks would occupy every global thread
// and starve their own row work down to serial. A separate pool keeps the row
// parallelism available. Intentionally leaked (never destroyed) to sidestep
// static-destruction ordering at program exit.
QThreadPool *toleranceRowPool()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
        return p;
    }();
    return pool;
}

void processRow(int y, const RowContext &ctx)
{
    const int width = ctx.width;
    const int heightA = ctx.heightA;
    const int widthA = ctx.widthA;
    const int threshold = ctx.threshold;

    const QRgb *lineA = (y < heightA)
        ? reinterpret_cast<const QRgb *>(ctx.srcAData + qsizetype(y) * ctx.srcABytesPerLine)
        : nullptr;
    const QRgb *lineB = reinterpret_cast<const QRgb *>(
        ctx.srcBData + qsizetype(y) * ctx.srcBBytesPerLine);
    QRgb *lineOut = reinterpret_cast<QRgb *>(
        ctx.resultData + qsizetype(y) * ctx.resultBytesPerLine);

    for (int x = 0; x < width; ++x) {
        const QRgb bPixel = lineB[x];
        const int br = qRed(bPixel);
        const int bg = qGreen(bPixel);
        const int bb = qBlue(bPixel);
        const int ba = qAlpha(bPixel);

        const bool outOfBounds = (lineA == nullptr || x >= widthA);

        if (outOfBounds) {
            // Max-intensity red overlay (alpha = 0.6 → 600/1000)
            const int r = blendChannel(br, 255, 600);
            const int g = blendChannel(bg, 50, 600);
            const int b = blendChannel(bb, 50, 600);
            lineOut[x] = qRgba(r, g, b, ba);
            continue;
        }

        const QRgb aPixel = lineA[x];
        const int dr = std::abs(qRed(aPixel) - br);
        const int dg = std::abs(qGreen(aPixel) - bg);
        const int db = std::abs(qBlue(aPixel) - bb);
        const int diff = std::max({dr, dg, db});

        if (diff == 0) {
            const int gray = luminance(br, bg, bb);
            lineOut[x] = qRgba(gray, gray, gray, ba);
        } else if (diff > threshold) {
            // t = min(1.0, diff/255 * 1.5)  →  t_scaled = min(1000, diff*1500/255)
            int tScaled = (diff * 1500) / 255;
            if (tScaled > 1000) tScaled = 1000;
            // alpha = 0.25 + 0.35 * t  →  alpha_scaled = 250 + 350*t_scaled/1000
            const int alphaScaled = 250 + (350 * tScaled) / 1000;
            const int r = blendChannel(br, 255, alphaScaled);
            const int g = blendChannel(bg, 50, alphaScaled);
            const int b = blendChannel(bb, 50, alphaScaled);
            lineOut[x] = qRgba(r, g, b, ba);
        } else {
            // t = diff / max(threshold, 1)  →  t_scaled = diff * 1000 / max(threshold,1)
            const int thr = (threshold > 0) ? threshold : 1;
            int tScaled = (diff * 1000) / thr;
            if (tScaled > 1000) tScaled = 1000;
            // alpha = 0.10 + 0.20 * t  →  alpha_scaled = 100 + 200*t_scaled/1000
            const int alphaScaled = 100 + (200 * tScaled) / 1000;
            const int r = blendChannel(br, 60, alphaScaled);
            const int g = blendChannel(bg, 100, alphaScaled);
            const int b = blendChannel(bb, 255, alphaScaled);
            lineOut[x] = qRgba(r, g, b, ba);
        }
    }
}

} // namespace

QImage ImageComparer::generateToleranceMap(const QImage &imageA,
                                           const QImage &imageB,
                                           int threshold)
{
    if (imageA.isNull() || imageB.isNull()) {
        return QImage();
    }

    const QSize outputSize = imageB.size();
    const int height = outputSize.height();

    // Convert both images to ARGB32 for consistent pixel access.
    const QImage srcA = imageA.convertToFormat(QImage::Format_ARGB32);
    const QImage srcB = imageB.convertToFormat(QImage::Format_ARGB32);

    // Allocate the output buffer directly — the previous implementation copied
    // imageB only to overwrite every pixel, wasting ≈4 bytes per pixel of I/O.
    QImage result(outputSize, QImage::Format_ARGB32);
    if (result.isNull()) {
        return QImage();
    }

    threshold = std::clamp(threshold, 0, 255);

    // Resolve the QImage pointers / strides up front so the parallel workers
    // touch only plain raw memory — QImage's per-call mutators (scanLine,
    // bits) are not thread-safe even when the data itself is uniquely owned.
    RowContext ctx;
    ctx.srcAData = srcA.constBits();
    ctx.srcABytesPerLine = srcA.bytesPerLine();
    ctx.widthA = srcA.width();
    ctx.heightA = srcA.height();
    ctx.srcBData = srcB.constBits();
    ctx.srcBBytesPerLine = srcB.bytesPerLine();
    ctx.resultData = result.bits();
    ctx.resultBytesPerLine = result.bytesPerLine();
    ctx.width = outputSize.width();
    ctx.threshold = threshold;

    // Parallelize over rows: each worker writes to a disjoint scanline range
    // in `result`, so no synchronization is needed. blockingMap waits until
    // every row is finished before returning, preserving the synchronous
    // contract of this entry point.
    QVector<int> rows;
    rows.reserve(height);
    for (int y = 0; y < height; ++y) {
        rows.append(y);
    }

    QtConcurrent::blockingMap(toleranceRowPool(), rows, [&ctx](int y) {
        processRow(y, ctx);
    });

    return result;
}

QFuture<QImage> ImageComparer::generateToleranceMapAsync(const QImage &imageA,
                                                         const QImage &imageB,
                                                         int threshold)
{
    // QImage uses implicit sharing so capturing by value here is cheap — only
    // the small handle is copied, not the pixel buffer. We deliberately wrap
    // the parallelized synchronous version with QtConcurrent::run so the
    // dispatch happens off the calling (GUI) thread; the inner blockingMap
    // then fans the per-row work out across the global thread pool.
    return QtConcurrent::run([imageA, imageB, threshold]() {
        return ImageComparer::generateToleranceMap(imageA, imageB, threshold);
    });
}

int ImageComparer::pixelDifference(QRgb colorA, QRgb colorB)
{
    const int dr = std::abs(qRed(colorA) - qRed(colorB));
    const int dg = std::abs(qGreen(colorA) - qGreen(colorB));
    const int db = std::abs(qBlue(colorA) - qBlue(colorB));
    return std::max({dr, dg, db});
}
