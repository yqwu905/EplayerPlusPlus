#include <QTest>
#include <QTemporaryDir>
#include <QImage>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>

#include <type_traits>
#include <utility>

#include "services/ImageLoader.h"
#include "services/ImageComparer.h"
#include "services/ImageMarkManager.h"
#include "models/ImageListModel.h"
#include "models/CompareSession.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ThumbnailWidget.h"
#include "utils/ImageUtils.h"

namespace
{
template <typename Model, typename = void>
struct HasAtomicSetFilters : std::false_type {};

template <typename Model>
struct HasAtomicSetFilters<Model,
    std::void_t<decltype(std::declval<Model &>().setFilters(
        std::declval<QString>(), std::declval<QString>()))>> : std::true_type {};

template <typename Model>
void applyFiltersCompat(Model &model,
                        const QString &fileName,
                        const QString &category)
{
    if constexpr (HasAtomicSetFilters<Model>::value) {
        model.setFilters(fileName, category);
    } else {
        model.setFileNameFilter(fileName);
        model.setCategoryFilter(category);
    }
}
}

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
    QTemporaryDir m_batchTempDir;
    QTemporaryDir m_largeTempDir;
    QStringList m_imagePaths;
    QStringList m_batchImagePaths;
    static constexpr int kImageCount = 500;
    static constexpr int kImageSize = 400; // 400x400 source images
    static constexpr int kLargeFileCount = 20000;

private slots:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());
        QVERIFY(m_batchTempDir.isValid());

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

            const QString batchPath = m_batchTempDir.filePath(
                QStringLiteral("img_%1.png").arg(i, 4, 10, QLatin1Char('0')));
            QVERIFY(QFile::copy(path, batchPath));
            m_batchImagePaths.append(batchPath);
        }
        qDebug() << "Generated" << kImageCount << "test images in" << gen.elapsed() << "ms";

        // A large metadata-only folder exercises scanning, filtering and model
        // roles without spending the benchmark setup time encoding 20k images.
        // FileUtils intentionally discovers images by extension; decoding is not
        // part of the large-list measurements below.
        QVERIFY(m_largeTempDir.isValid());
        QJsonObject marks;
        for (int i = 0; i < kLargeFileCount; ++i) {
            const QString fileName = QStringLiteral("img_%1.jpg")
                                         .arg(i, 6, 10, QLatin1Char('0'));
            QFile file(m_largeTempDir.filePath(fileName));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();

            if ((i % 3) == 0) {
                QJsonObject entry;
                entry.insert(QStringLiteral("category"), QStringLiteral("A"));
                entry.insert(QStringLiteral("timestamp"), i + 1);
                marks.insert(fileName, entry);
            }
        }
        QJsonObject root;
        root.insert(QStringLiteral("version"), 2);
        root.insert(QStringLiteral("marks"), marks);
        QFile markFile(m_largeTempDir.filePath(QStringLiteral(".imagecompare_marks.json")));
        QVERIFY(markFile.open(QIODevice::WriteOnly));
        QVERIFY(markFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
        markFile.close();
    }

    /**
     * Benchmark: individual requestThumbnail() calls, one watcher per image.
     */
    void benchSingleRequests()
    {
        QElapsedTimer totalTimer;
        totalTimer.start();
        qint64 readyMs = 0;
        QHash<QString, qint64> metrics;
        {
            ImageLoader loader;
            QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

            for (const QString &path : m_imagePaths) {
                loader.requestThumbnail(path, QSize(200, 200));
            }

            while (spy.count() < kImageCount) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                if (totalTimer.elapsed() > 60000) {
                    QFAIL("Timed out waiting for single requests");
                }
            }

            readyMs = totalTimer.elapsed();
            metrics = loader.thumbnailMetrics();
            QVERIFY(metrics.value(QStringLiteral("pendingDiskWrites")) <= 16);
            QVERIFY(metrics.value(QStringLiteral("pendingDiskWriteBytes")) <=
                    32LL * 1024LL * 1024LL);
        }
        const qint64 totalMs = totalTimer.elapsed();
        QCOMPARE(metrics.value(QStringLiteral("decodes")), qint64(kImageCount));
        QCOMPARE(metrics.value(QStringLiteral("diskHits")), qint64(0));
        qDebug() << "[Single requests]" << kImageCount << "thumbnails in"
                 << readyMs << "ms time-to-ready,"
                 << (totalMs - readyMs) << "ms teardown,"
                 << totalMs << "ms total"
                 << "(" << (double(readyMs) / kImageCount) << "ms/image)";
    }

    /**
     * Benchmark: batch requestThumbnailBatch() calls.
     */
    void benchBatchRequests()
    {
        QElapsedTimer totalTimer;
        totalTimer.start();
        qint64 readyMs = 0;
        QHash<QString, qint64> metrics;
        {
            ImageLoader loader;
            QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

            loader.requestThumbnailBatchVisibleFirst(m_batchImagePaths, QSize(200, 200));

            while (spy.count() < kImageCount) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                if (totalTimer.elapsed() > 60000) {
                    QFAIL("Timed out waiting for batch requests");
                }
            }

            readyMs = totalTimer.elapsed();
            metrics = loader.thumbnailMetrics();
            QVERIFY(metrics.value(QStringLiteral("pendingDiskWrites")) <= 16);
            QVERIFY(metrics.value(QStringLiteral("pendingDiskWriteBytes")) <=
                    32LL * 1024LL * 1024LL);
        }
        const qint64 totalMs = totalTimer.elapsed();
        QCOMPARE(metrics.value(QStringLiteral("decodes")), qint64(kImageCount));
        QCOMPARE(metrics.value(QStringLiteral("diskHits")), qint64(0));
        qDebug() << "[Batch requests] " << kImageCount << "thumbnails in"
                 << readyMs << "ms time-to-ready,"
                 << (totalMs - readyMs) << "ms teardown,"
                 << totalMs << "ms total"
                 << "(" << (double(readyMs) / kImageCount) << "ms/image)";
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
        qint64 indexOfNs = timer.nsecsElapsed();

        // O(1) hash lookup
        timer.restart();
        for (int i = 0; i < kLookupIters; ++i) {
            const QString &target = paths.at(i % paths.size());
            dummy += pathToIndex.value(target, -1);
        }
        qint64 hashNs = timer.nsecsElapsed();

        qDebug() << "[indexOf]     " << kLookupIters << "lookups in" << indexOfNs << "ns";
        qDebug() << "[hash lookup] " << kLookupIters << "lookups in" << hashNs << "ns";
        qDebug() << "Speedup:" << (indexOfNs > 0 ? double(indexOfNs) / qMax(1LL, hashNs) : 0) << "x";
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
        QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
        while (model.hasMoreToLoad()) {
            model.loadNextThumbnailBatch(32);
        }

        // Wait for all thumbnails
        int received = 0;
        while (received < imageCount) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            received = dataSpy.count();
            if (thumbTimer.elapsed() > 60000) {
                qDebug() << "Got" << received << "/" << imageCount << "thumbnails before timeout";
                break;
            }
        }

        QCOMPARE(received, imageCount);

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
        qint64 rowsReadyMs = -1;
        qint64 firstViewportMs = -1;
        while (rowsReadyMs < 0 || firstViewportMs < 0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (firstViewportMs < 0 && thumbnailSpy.count() >= 4) {
                firstViewportMs = timer.elapsed();
            }
            if (rowsReadyMs < 0) {
                views = panel.findChildren<QListView *>(
                    QStringLiteral("compareColumnListView"));
                if (views.size() == 1 &&
                    views.first()->model()->rowCount() == kImageCount) {
                    rowsReadyMs = timer.elapsed();
                }
            }
            if (timer.elapsed() > 10000) {
                QFAIL("Timed out waiting for browse rows/first viewport thumbnails");
            }
        }

        qDebug() << "[BrowsePanel virtualized] rows ready:" << rowsReadyMs << "ms,"
                 << "first thumbnails:" << firstViewportMs << "ms,"
                 << "thumbnail widget count:" << panel.findChildren<ThumbnailWidget *>().size();
    }

    void benchLargeFolderScanFilterAndRoles()
    {
        ImageMarkManager marks;
        ImageListModel model;
        model.setImageMarkManager(&marks);
        QSignalSpy readySpy(&model, &ImageListModel::folderReady);

        QElapsedTimer timer;
        timer.start();
        model.setFolder(m_largeTempDir.path());
        while (readySpy.count() < 1) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (timer.elapsed() > 30000) {
                QFAIL("Timed out waiting for large folder scan");
            }
        }
        const qint64 scanMs = timer.elapsed();
        QCOMPARE(model.unfilteredImageCount(), kLargeFileCount);

        timer.restart();
        applyFiltersCompat(model, QStringLiteral("1"), QStringLiteral("A"));
        const qint64 combinedFilterNs = timer.nsecsElapsed();
        const int filteredCount = model.imageCount();
        QCOMPARE(filteredCount, 4483);

        applyFiltersCompat(model, QString(), QString());
        timer.restart();
        qint64 roleChecksum = 0;
        for (int row = 0; row < model.imageCount(); ++row) {
            const QModelIndex idx = model.index(row);
            roleChecksum += model.data(idx, ImageListModel::MarkRole).toString().size();
            roleChecksum += model.data(idx, ImageListModel::MarkSourceRole).toString().size();
            roleChecksum += model.data(idx, Qt::ToolTipRole).toString().size();
        }
        const qint64 rolesNs = timer.nsecsElapsed();
        QCOMPARE(roleChecksum, qint64(6667));

        qDebug() << "[Large folder]" << kLargeFileCount << "rows scan:" << scanMs << "ms,"
                 << "combined filename/category filter:" << combinedFilterNs << "ns ("
                 << filteredCount << "rows), roles:" << rolesNs << "ns, checksum:"
                 << roleChecksum;
    }

    void benchToleranceMap4Mpx()
    {
        constexpr int side = 2048;
        QImage first(side, side, QImage::Format_ARGB32);
        QImage second(side, side, QImage::Format_ARGB32);
        for (int y = 0; y < side; ++y) {
            auto *a = reinterpret_cast<QRgb *>(first.scanLine(y));
            auto *b = reinterpret_cast<QRgb *>(second.scanLine(y));
            for (int x = 0; x < side; ++x) {
                a[x] = qRgb((x + y) & 255, (x * 3) & 255, (y * 5) & 255);
                b[x] = qRgb((x + y + 7) & 255, (x * 3 + 2) & 255,
                            (y * 5 + 1) & 255);
            }
        }

        QElapsedTimer timer;
        timer.start();
        const QImage result = ImageComparer::generateToleranceMap(first, second, 10);
        const qint64 elapsed = timer.elapsed();
        QVERIFY(!result.isNull());
        qDebug() << "[Tolerance map]" << side << "x" << side << "in" << elapsed << "ms";
    }

    void benchFullImageSwitchingColdAndWarm()
    {
        const QStringList paths = m_imagePaths.mid(0, 64);
        ImageLoader loader;
        QSignalSpy readySpy(&loader, &ImageLoader::imageReady);

        QElapsedTimer timer;
        timer.start();
        loader.requestImageBatch(paths);
        while (readySpy.count() < paths.size()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (timer.elapsed() > 30000) {
                QFAIL("Timed out waiting for cold full-image loads");
            }
        }
        const qint64 coldMs = timer.elapsed();

        constexpr int warmPasses = 100;
        timer.restart();
        qint64 byteChecksum = 0;
        for (int pass = 0; pass < warmPasses; ++pass) {
            for (const QString &path : paths) {
                const QImage image = loader.getCachedImage(path);
                QVERIFY(!image.isNull());
                byteChecksum += image.sizeInBytes();
            }
        }
        const qint64 warmNs = timer.nsecsElapsed();
        qDebug() << "[Full image switching]" << paths.size()
                 << "cold loads:" << coldMs << "ms, warm LRU reads:"
                 << (double(warmNs) / warmPasses / paths.size()) << "ns/read, bytes:"
                 << byteChecksum;
    }
};

QTEST_MAIN(BenchThumbnailPerf)
#include "bench_ThumbnailPerf.moc"
