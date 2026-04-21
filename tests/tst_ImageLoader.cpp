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

QTEST_MAIN(tst_ImageLoader)
#include "tst_ImageLoader.moc"
