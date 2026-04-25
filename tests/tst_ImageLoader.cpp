#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QSet>

#include "services/ImageLoader.h"

class tst_ImageLoader : public QObject
{
    Q_OBJECT

private slots:
    void diskCacheHit_afterFirstDecode();
    void cancelThumbnailRequestsExcept_filtersQueue();
    void requestImageBatch_usesMemoryCacheOnRepeatedLoad();
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

QTEST_MAIN(tst_ImageLoader)
#include "tst_ImageLoader.moc"
