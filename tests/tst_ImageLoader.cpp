#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QColorSpace>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QHash>
#include <QDateTime>
#include <QFileInfo>

#include <algorithm>

#include "services/ImageLoader.h"

class tst_ImageLoader : public QObject
{
    Q_OBJECT

private slots:
    void diskCacheHit_afterFirstDecode();
    void cancelThumbnailRequestsExcept_filtersQueue();
    void visibleRequest_promotesQueuedThumbnail();
    void visibleBatch_emitsSingleFastPreview();
    void requestImageBatch_usesMemoryCacheOnRepeatedLoad();
    void fastSwitchActivePathSet_cancelsInflightDecodes();
    void cacheEviction_bounded_lruPreservesRecent();
    void getCachedThumbnailNoSize_returnsLargestForPath();
    void providedMtime_hitsDiskCacheWithoutStat();
    void invalidProvidedMtime_fallsBackToStat();
    void concurrencyPoolSanity();
    void destructorDuringInflight_noCrash();
    void ignoreColorProfile_stripsLoadedThumbnail();
    void ignoreColorProfile_offKeepsTagOnThumbnail();
    void ignoreColorProfile_stripsLoadedFullImage();
    void setIgnoreColorProfile_toggleInvalidatesMemoryCache();
};

void tst_ImageLoader::diskCacheHit_afterFirstDecode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("cache_test.png");
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    QVERIFY(img.save(imagePath));

    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

        loader.requestThumbnail(imagePath, QSize(120, 120));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        const auto metrics = loader.thumbnailMetrics();
        QVERIFY(metrics.value(QStringLiteral("decodes")) >= 1);
    }

    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

        loader.requestThumbnail(imagePath, QSize(120, 120));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        const auto metrics = loader.thumbnailMetrics();
        QVERIFY(metrics.value(QStringLiteral("diskHits")) >= 1);
    }
}

void tst_ImageLoader::cancelThumbnailRequestsExcept_filtersQueue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList paths;
    for (int i = 0; i < 6; ++i) {
        const QString imagePath = dir.filePath(QString("img_%1.png").arg(i));
        QImage img(128, 128, QImage::Format_ARGB32);
        img.fill(i % 2 == 0 ? Qt::red : Qt::green);
        QVERIFY(img.save(imagePath));
        paths << imagePath;
    }

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    loader.requestThumbnailBatch(paths, QSize(180, 180));
    QSet<QString> keep;
    keep.insert(paths.first());
    loader.cancelThumbnailRequestsExcept(keep);

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    QVERIFY(spy.count() <= 2);

    bool hasKeptPath = false;
    for (const auto &call : spy) {
        if (call.at(0).toString() == paths.first()) {
            hasKeptPath = true;
            break;
        }
    }
    QVERIFY(hasKeptPath);
}

void tst_ImageLoader::visibleRequest_promotesQueuedThumbnail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList paths;
    for (int i = 0; i < 6; ++i) {
        const QString imagePath = dir.filePath(QString("img_%1.png").arg(i));
        QImage img(512, 512, QImage::Format_ARGB32);
        img.fill(QColor::fromHsv((i * 37) % 360, 255, 220));
        QVERIFY(img.save(imagePath));
        paths << imagePath;
    }

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    loader.requestThumbnailBatch(paths, QSize(180, 180));
    loader.requestThumbnailBatchVisibleFirst({paths.last()}, QSize(180, 180));

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 2, 5000);
    QCOMPARE(spy.at(1).at(0).toString(), paths.last());
}

void tst_ImageLoader::visibleBatch_emitsSingleFastPreview()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("two_stage.png");
    QImage img(512, 512, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QVERIFY(img.save(imagePath));

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    loader.requestThumbnailBatchVisibleFirst({imagePath}, QSize(180, 180));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QCOMPARE(spy.at(0).at(0).toString(), imagePath);
}

void tst_ImageLoader::requestImageBatch_usesMemoryCacheOnRepeatedLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("full_image_cache.png");
    QImage img(96, 96, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    QVERIFY(img.save(imagePath));

    ImageLoader loader;
    QSignalSpy spy(&loader, &ImageLoader::imageReady);

    loader.requestImageBatch({imagePath});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);

    const QImage cached = loader.getCachedImage(imagePath);
    QVERIFY(!cached.isNull());

    spy.clear();
    loader.requestImage(imagePath);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), imagePath);
    const QImage secondLoad = args.at(1).value<QImage>();
    QVERIFY(!secondLoad.isNull());
    QCOMPARE(secondLoad.size(), QSize(96, 96));
}

void tst_ImageLoader::fastSwitchActivePathSet_cancelsInflightDecodes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Build a moderate batch of images — large enough that with concurrency 1
    // many will still be queued (and some likely mid-decode) when we cancel,
    // small enough to keep the test fast.
    QStringList paths;
    const int kCount = 20;
    paths.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const QString imagePath = dir.filePath(QString("stress_%1.png").arg(i));
        QImage img(256, 256, QImage::Format_ARGB32);
        img.fill(QColor::fromHsv((i * 23) % 360, 200, 200));
        QVERIFY(img.save(imagePath));
        paths << imagePath;
    }

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    loader.requestThumbnailBatch(paths, QSize(180, 180));

    // Fast-switch the keep set several times. Final keep set is just the
    // last image. Anything still queued or being decoded for the prior
    // sets must not emit thumbnailReady.
    QSet<QString> keep1;
    keep1.insert(paths.at(5));
    loader.cancelThumbnailRequestsExcept(keep1);

    QSet<QString> keep2;
    keep2.insert(paths.at(10));
    loader.cancelThumbnailRequestsExcept(keep2);

    QSet<QString> keep3;
    keep3.insert(paths.last());
    loader.cancelThumbnailRequestsExcept(keep3);

    // cancelThumbnailRequestsExcept filters the queue but never re-adds; the
    // final keep path was already dropped by the first cancel call. Re-request
    // it so a legitimate emission can occur and we can assert "no cancelled
    // path emits" with at least one expected emission in flight.
    loader.requestThumbnail(paths.last(), QSize(180, 180));

    // Wait long enough for any in-flight worker to finish and for the
    // surviving request to make it through the queue.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    QTest::qWait(200);

    QSet<QString> emitted;
    for (const auto &call : spy) {
        emitted.insert(call.at(0).toString());
    }

    // The only path that may legitimately be emitted is the surviving
    // keep path. Anything else that emits means an in-flight decode wasn't
    // cancelled.
    for (const QString &p : emitted) {
        QVERIFY2(p == paths.last(),
                 qPrintable(QString("Unexpected emission for cancelled path: %1").arg(p)));
    }

    QVERIFY(emitted.contains(paths.last()));
}

void tst_ImageLoader::cacheEviction_bounded_lruPreservesRecent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Create more images than the cap and verify the cache stays at cap
    // while the most recent insertion is preserved.
    const int kCap = 8;
    const int kExtra = 12;
    QStringList paths;
    paths.reserve(kCap + kExtra);
    for (int i = 0; i < kCap + kExtra; ++i) {
        const QString imagePath = dir.filePath(QString("evict_%1.png").arg(i));
        QImage img(64, 64, QImage::Format_ARGB32);
        img.fill(QColor::fromHsv((i * 17) % 360, 200, 200));
        QVERIFY(img.save(imagePath));
        paths << imagePath;
    }

    ImageLoader loader;
    // This test verifies the LRU eviction policy, so keep completion order
    // deterministic instead of depending on platform thread scheduling.
    loader.setMaxConcurrentLoads(1);
    loader.setMaxCacheSize(kCap);

    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    for (const QString &p : paths) {
        loader.requestThumbnail(p, QSize(80, 80));
    }

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), static_cast<int>(paths.size()), 20000);
    QTest::qWait(50);

    const int total = static_cast<int>(paths.size());
    // Most-recently-inserted thumbnails should be present.
    for (int i = total - kCap; i < total; ++i) {
        QVERIFY2(!loader.getCachedThumbnail(paths.at(i), QSize(80, 80)).isNull(),
                 qPrintable(QString("Expected recent thumbnail still cached: %1").arg(paths.at(i))));
    }

    // The first few should have been evicted.
    bool sawEviction = false;
    for (int i = 0; i < total - kCap; ++i) {
        if (loader.getCachedThumbnail(paths.at(i), QSize(80, 80)).isNull()) {
            sawEviction = true;
            break;
        }
    }
    QVERIFY(sawEviction);
}

void tst_ImageLoader::getCachedThumbnailNoSize_returnsLargestForPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("multisize.png");
    QImage img(512, 512, QImage::Format_ARGB32);
    img.fill(Qt::yellow);
    QVERIFY(img.save(imagePath));

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);

    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    loader.requestThumbnail(imagePath, QSize(64, 64));
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    loader.requestThumbnail(imagePath, QSize(256, 256));
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 2, 5000);

    const QImage best = loader.getCachedThumbnail(imagePath);
    QVERIFY(!best.isNull());
    // It should prefer the larger of the two cached sizes for this path.
    const QImage small = loader.getCachedThumbnail(imagePath, QSize(64, 64));
    const QImage large = loader.getCachedThumbnail(imagePath, QSize(256, 256));
    QVERIFY(!small.isNull());
    QVERIFY(!large.isNull());
    QVERIFY(best.width() >= small.width());
    QVERIFY(best.height() >= small.height());

    // And an unknown path returns null without scanning everything.
    const QImage missing = loader.getCachedThumbnail(dir.filePath("not_present.png"));
    QVERIFY(missing.isNull());
}

void tst_ImageLoader::providedMtime_hitsDiskCacheWithoutStat()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("provided_mtime.png");
    QImage img(96, 96, QImage::Format_ARGB32);
    img.fill(Qt::darkGreen);
    QVERIFY(img.save(imagePath));

    const QSize size(150, 150);

    // Phase 1: populate the on-disk cache by decoding. No mtime is provided, so
    // the worker stats the file to key the cache (high-quality batch path).
    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
        loader.requestThumbnailBatch({imagePath}, size);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QVERIFY(loader.thumbnailMetrics().value(QStringLiteral("decodes")) >= 1);
    }

    // The mtime a scan would have captured — identical to what stat returns.
    const QDateTime realMtime = QFileInfo(imagePath).lastModified().toUTC();

    // Phase 2: a fresh loader given the correct mtime must hit the on-disk
    // cache WITHOUT decoding. That is only possible if the *provided* mtime
    // (not a re-stat) keyed the lookup.
    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
        QHash<QString, QDateTime> mtimes;
        mtimes.insert(imagePath, realMtime);
        loader.requestThumbnailBatch({imagePath}, size, mtimes);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        const auto metrics = loader.thumbnailMetrics();
        QVERIFY(metrics.value(QStringLiteral("diskHits")) >= 1);
        QCOMPARE(metrics.value(QStringLiteral("decodes")), qint64(0));
    }

    // Control: a deliberately wrong mtime must MISS the disk cache (different
    // key) and force a decode — confirming the provided value drives the key.
    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
        QHash<QString, QDateTime> mtimes;
        mtimes.insert(imagePath, realMtime.addSecs(-100000)); // valid but wrong
        loader.requestThumbnailBatch({imagePath}, size, mtimes);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        const auto metrics = loader.thumbnailMetrics();
        QCOMPARE(metrics.value(QStringLiteral("diskHits")), qint64(0));
        QVERIFY(metrics.value(QStringLiteral("decodes")) >= 1);
    }
}

void tst_ImageLoader::invalidProvidedMtime_fallsBackToStat()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("invalid_mtime.png");
    QImage img(80, 80, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QVERIFY(img.save(imagePath));

    const QSize size(110, 110);
    QHash<QString, QDateTime> mtimes;
    mtimes.insert(imagePath, QDateTime()); // invalid -> worker must stat

    // First load: invalid provided mtime, so the worker stats, decodes, persists.
    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
        loader.requestThumbnailBatch({imagePath}, size, mtimes);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QVERIFY(loader.thumbnailMetrics().value(QStringLiteral("decodes")) >= 1);
    }

    // Second load, still invalid: the fallback stat reproduces the same cache
    // key as the first persist, so this is a disk hit — proving the fallback
    // path is exercised and correct.
    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(1);
        QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
        loader.requestThumbnailBatch({imagePath}, size, mtimes);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QVERIFY(loader.thumbnailMetrics().value(QStringLiteral("diskHits")) >= 1);
    }
}

void tst_ImageLoader::concurrencyPoolSanity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList paths;
    const int kCount = 16;
    paths.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const QString p = dir.filePath(QString("pool_%1.png").arg(i));
        QImage img(64, 64, QImage::Format_ARGB32);
        img.fill(QColor::fromHsv((i * 19) % 360, 200, 200));
        QVERIFY(img.save(p));
        paths << p;
    }

    ImageLoader loader;
    loader.setMaxConcurrentLoads(8);
    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);

    loader.requestThumbnailBatchVisibleFirst(paths, QSize(80, 80));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), kCount, 20000);

    // Every unique path is serviced exactly once, by decode or disk hit.
    const auto metrics = loader.thumbnailMetrics();
    QCOMPARE(metrics.value(QStringLiteral("decodes")) + metrics.value(QStringLiteral("diskHits")),
             static_cast<qint64>(kCount));
}

void tst_ImageLoader::destructorDuringInflight_noCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList paths;
    const int kCount = 24;
    paths.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const QString p = dir.filePath(QString("inflight_%1.png").arg(i));
        QImage img(256, 256, QImage::Format_ARGB32);
        img.fill(QColor::fromHsv((i * 29) % 360, 200, 200));
        QVERIFY(img.save(p));
        paths << p;
    }

    {
        ImageLoader loader;
        loader.setMaxConcurrentLoads(8);
        loader.requestThumbnailBatch(paths, QSize(200, 200));
        // Let workers start, then let `loader` go out of scope mid-flight. The
        // destructor must drain the pool and drop its watchers without crashing
        // or delivering callbacks on a destroyed object.
        QTest::qWait(20);
    }

    // Pump the event loop to surface any stray queued callbacks (there should
    // be none). Reaching here without a crash means teardown is sound.
    QTest::qWait(100);
    QVERIFY(true);
}

void tst_ImageLoader::ignoreColorProfile_stripsLoadedThumbnail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Tag the source with an explicit sRGB profile so the saved PNG carries
    // an embedded iCCP/sRGB chunk; on the read path, QImageReader restores
    // the tag — unless the loader strips it as we configured.
    const QString imagePath = dir.filePath("strip_on.png");
    QImage tagged(96, 96, QImage::Format_ARGB32);
    tagged.fill(Qt::magenta);
    tagged.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QVERIFY(tagged.save(imagePath));

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    loader.setIgnoreColorProfile(true);
    QCOMPARE(loader.ignoreColorProfile(), true);

    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
    loader.requestThumbnail(imagePath, QSize(48, 48));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    const QImage emitted = spy.takeFirst().at(1).value<QImage>();
    QVERIFY(!emitted.isNull());
    QVERIFY2(!emitted.colorSpace().isValid(),
             "ICC profile should be stripped when ignoreColorProfile is enabled");
}

void tst_ImageLoader::ignoreColorProfile_offKeepsTagOnThumbnail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("strip_off.png");
    QImage tagged(96, 96, QImage::Format_ARGB32);
    tagged.fill(Qt::cyan);
    tagged.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QVERIFY(tagged.save(imagePath));

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    loader.setIgnoreColorProfile(false);

    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
    loader.requestThumbnail(imagePath, QSize(48, 48));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    const QImage emitted = spy.takeFirst().at(1).value<QImage>();
    QVERIFY(!emitted.isNull());
    QVERIFY2(emitted.colorSpace().isValid(),
             "ICC profile should survive when ignoreColorProfile is disabled");
}

void tst_ImageLoader::ignoreColorProfile_stripsLoadedFullImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("full_strip.png");
    QImage tagged(64, 64, QImage::Format_ARGB32);
    tagged.fill(Qt::yellow);
    tagged.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QVERIFY(tagged.save(imagePath));

    ImageLoader loader;
    loader.setIgnoreColorProfile(true);

    QSignalSpy spy(&loader, &ImageLoader::imageReady);
    loader.requestImage(imagePath);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    const QImage emitted = spy.takeFirst().at(1).value<QImage>();
    QVERIFY(!emitted.isNull());
    QVERIFY2(!emitted.colorSpace().isValid(),
             "Full-image load should also strip the color profile when enabled");
}

void tst_ImageLoader::setIgnoreColorProfile_toggleInvalidatesMemoryCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("toggle_invalidate.png");
    QImage img(96, 96, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QVERIFY(img.save(imagePath));

    ImageLoader loader;
    loader.setMaxConcurrentLoads(1);
    loader.setIgnoreColorProfile(false);

    QSignalSpy spy(&loader, &ImageLoader::thumbnailReady);
    loader.requestThumbnail(imagePath, QSize(48, 48));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QVERIFY(loader.thumbnailMetrics().value(QStringLiteral("decodes")) >= 1);

    // Same policy: the second request should be a pure in-memory hit.
    spy.clear();
    loader.requestThumbnail(imagePath, QSize(48, 48));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    const qint64 memoryHitsBeforeToggle =
        loader.thumbnailMetrics().value(QStringLiteral("memoryHits"));
    QCOMPARE(memoryHitsBeforeToggle, qint64(1));

    // Toggling the policy clears the in-memory cache and bumps cancel
    // generations. The follow-up request must NOT count as a memoryHit;
    // the prior entry was discarded.
    loader.setIgnoreColorProfile(true);

    spy.clear();
    loader.requestThumbnail(imagePath, QSize(48, 48));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QCOMPARE(loader.thumbnailMetrics().value(QStringLiteral("memoryHits")),
             memoryHitsBeforeToggle);
}

QTEST_MAIN(tst_ImageLoader)
#include "tst_ImageLoader.moc"
