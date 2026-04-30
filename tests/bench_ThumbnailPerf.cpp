#include <QTest>
#include <QTemporaryDir>
#include <QImage>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QListView>

#include "services/ImageLoader.h"
#include "models/ImageListModel.h"
#include "models/CompareSession.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ThumbnailWidget.h"
#include "utils/ImageUtils.h"

/**
 * @brief Benchmark for thumbnail generation pipeline.
 *
 * Measures:
 *  1. Single requestThumbnail() vs batch requestThumbnailBatch()
 *  2. onThumbnailReady O(1) hash lookup vs O(n) indexOf
 *  3. End-to-end folder add → all thumbnails ready
 */
class BenchThumbnailPerf : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QStringList m_imagePaths;
    static constexpr int kImageCount = 500;
    static constexpr int kImageSize = 400; // 400x400 source images

private slots:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());

        // Generate test images
        QElapsedTimer gen;
        gen.start();
        for (int i = 0; i < kImageCount; ++i) {
            QImage img(kImageSize, kImageSize, QImage::Format_RGB32);
            // Fill with a gradient pattern so encoding isn't trivially fast
            for (int y = 0; y < kImageSize; ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < kImageSize; ++x) {
                    line[x] = qRgb((x + i) % 256, (y + i) % 256, (x * y + i) % 256);
                }
            }
            QString path = m_tempDir.filePath(
                QStringLiteral("img_%1.png").arg(i, 4, 10, QLatin1Char('0')));
            QVERIFY(img.save(path, "JPEG", 85));
            m_imagePaths.append(path);
        }
        qDebug() << "Generated" << kImageCount << "test images in" << gen.elapsed() << "ms";
    }

    /**
     * Benchmark: individual requestThumbnail() calls, one watcher per image.
     */
    void benchSingleRequests()
    {
        ImageLoader loader;
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

        QElapsedTimer timer;
        timer.start();

        for (const QString &path : m_imagePaths) {
            loader.requestThumbnail(path, QSize(200, 200));
        }

        // Wait for all thumbnails to complete
        while (spy.count() < kImageCount) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (timer.elapsed() > 60000) {
                QFAIL("Timed out waiting for single requests");
            }
        }

        qint64 elapsed = timer.elapsed();
        qDebug() << "[Single requests]" << kImageCount << "thumbnails in"
                 << elapsed << "ms"
                 << "(" << (double(elapsed) / kImageCount) << "ms/image)";
    }

    /**
     * Benchmark: batch requestThumbnailBatch() calls.
     */
    void benchBatchRequests()
    {
        ImageLoader loader;
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

        QElapsedTimer timer;
        timer.start();

        loader.requestThumbnailBatch(m_imagePaths, QSize(200, 200));

        // Wait for all thumbnails to complete
        while (spy.count() < kImageCount) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (timer.elapsed() > 60000) {
                QFAIL("Timed out waiting for batch requests");
            }
        }

        qint64 elapsed = timer.elapsed();
        qDebug() << "[Batch requests] " << kImageCount << "thumbnails in"
                 << elapsed << "ms"
                 << "(" << (double(elapsed) / kImageCount) << "ms/image)";
    }

    /**
     * Benchmark: O(n) indexOf lookup vs O(1) hash lookup.
     */
    void benchLookup()
    {
        // Build a list similar to what ImageListModel holds
        QStringList paths = m_imagePaths;
        QHash<QString, int> pathToIndex;
        pathToIndex.reserve(paths.size());
        for (int i = 0; i < paths.size(); ++i) {
            pathToIndex.insert(paths.at(i), i);
        }

        constexpr int kLookupIters = 10000;

        // O(n) indexOf
        QElapsedTimer timer;
        timer.start();
        volatile int dummy = 0;
        for (int i = 0; i < kLookupIters; ++i) {
            const QString &target = paths.at(i % paths.size());
            dummy += paths.indexOf(target);
        }
        qint64 indexOfTime = timer.elapsed();

        // O(1) hash lookup
        timer.restart();
        for (int i = 0; i < kLookupIters; ++i) {
            const QString &target = paths.at(i % paths.size());
            dummy += pathToIndex.value(target, -1);
        }
        qint64 hashTime = timer.elapsed();

        qDebug() << "[indexOf]     " << kLookupIters << "lookups in" << indexOfTime << "ms";
        qDebug() << "[hash lookup] " << kLookupIters << "lookups in" << hashTime << "ms";
        qDebug() << "Speedup:" << (indexOfTime > 0 ? double(indexOfTime) / qMax(1LL, hashTime) : 0) << "x";
    }

    /**
     * Benchmark: end-to-end ImageListModel folder load + all thumbnails.
     */
    void benchEndToEnd()
    {
        ImageLoader loader;
        ImageListModel model;
        model.setImageLoader(&loader);

        QSignalSpy readySpy(&model, &ImageListModel::folderReady);

        QElapsedTimer timer;
        timer.start();

        model.setFolder(m_tempDir.path());

        // Wait for scan to finish
        while (readySpy.count() < 1) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (timer.elapsed() > 10000) {
                QFAIL("Timed out waiting for folder scan");
            }
        }
        qint64 scanTime = timer.elapsed();
        int imageCount = model.imageCount();
        qDebug() << "[End-to-end] Scan:" << scanTime << "ms," << imageCount << "images found";

        // Now load all thumbnails via batch
        QElapsedTimer thumbTimer;
        thumbTimer.start();

        // Simulate what BrowsePanel does
        while (model.hasMoreToLoad()) {
            model.loadNextThumbnailBatch(32);
        }

        // Wait for all thumbnails
        QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
        int received = 0;
        while (received < imageCount) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            received = dataSpy.count();
            if (thumbTimer.elapsed() > 60000) {
                qDebug() << "Got" << received << "/" << imageCount << "thumbnails before timeout";
                break;
            }
        }

        qint64 thumbTime = thumbTimer.elapsed();
        qDebug() << "[End-to-end] Thumbnails:" << thumbTime << "ms for" << received << "images"
                 << "(" << (double(thumbTime) / qMax(1, received)) << "ms/image)";
        qDebug() << "[End-to-end] Total:" << (scanTime + thumbTime) << "ms";
    }

    void benchBrowsePanelVirtualizedFirstViewport()
    {
        CompareSession session;
        ImageLoader loader;
        BrowsePanel panel(&session, &loader);
        panel.resize(360, 640);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));

        QElapsedTimer timer;
        timer.start();

        QSignalSpy thumbnailSpy(&loader, &ImageLoader::thumbnailReady);
        QVERIFY(session.addFolder(m_tempDir.path()));

        QList<QListView *> views;
        QTRY_VERIFY_WITH_TIMEOUT((views = panel.findChildren<QListView *>(
                                      QStringLiteral("compareColumnListView")),
                                  views.size() == 1),
                                 5000);
        QTRY_COMPARE_WITH_TIMEOUT(views.first()->model()->rowCount(), kImageCount, 10000);
        const qint64 rowsReadyMs = timer.elapsed();

        QTRY_VERIFY_WITH_TIMEOUT(thumbnailSpy.count() >= 4, 10000);
        const qint64 firstViewportMs = timer.elapsed();

        qDebug() << "[BrowsePanel virtualized] rows ready:" << rowsReadyMs << "ms,"
                 << "first thumbnails:" << firstViewportMs << "ms,"
                 << "thumbnail widget count:" << panel.findChildren<ThumbnailWidget *>().size();
    }
};

QTEST_MAIN(BenchThumbnailPerf)
#include "bench_ThumbnailPerf.moc"
