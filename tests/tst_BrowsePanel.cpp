#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QMap>
#include <algorithm>

#include "models/CompareSession.h"
#include "services/ImageLoader.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ThumbnailWidget.h"

using SelectedImages = QList<QPair<QString, QString>>;
Q_DECLARE_METATYPE(SelectedImages)

class tst_BrowsePanel : public QObject
{
    Q_OBJECT

private slots:
    void duplicateFolder_plainClickSelectionsAreIndependent();
};

void tst_BrowsePanel::duplicateFolder_plainClickSelectionsAreIndependent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString imageAPath = tempDir.filePath("a.png");
    const QString imageBPath = tempDir.filePath("b.png");

    QImage imageA(16, 16, QImage::Format_ARGB32);
    imageA.fill(Qt::red);
    QVERIFY(imageA.save(imageAPath));

    QImage imageB(16, 16, QImage::Format_ARGB32);
    imageB.fill(Qt::green);
    QVERIFY(imageB.save(imageBPath));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    qRegisterMetaType<SelectedImages>("QList<QPair<QString,QString>>");
    panel.resize(900, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QSignalSpy selectionSpy(&panel, &BrowsePanel::selectionChanged);

    QVERIFY(session.addFolder(tempDir.path()));
    QVERIFY(session.addFolder(tempDir.path()));

    QList<ThumbnailWidget *> thumbnails;
    QTRY_VERIFY_WITH_TIMEOUT((thumbnails = panel.findChildren<ThumbnailWidget *>(), thumbnails.size() == 4), 5000);

    QMap<int, QList<ThumbnailWidget *>> columnMap;
    for (auto *thumb : thumbnails) {
        const int x = thumb->mapTo(&panel, QPoint(0, 0)).x();
        columnMap[x].append(thumb);
    }
    QCOMPARE(columnMap.size(), 2);

    auto columns = columnMap.values();
    for (auto &columnThumbs : columns) {
        std::sort(columnThumbs.begin(), columnThumbs.end(), [&panel](ThumbnailWidget *lhs, ThumbnailWidget *rhs) {
            return lhs->mapTo(&panel, QPoint(0, 0)).y() < rhs->mapTo(&panel, QPoint(0, 0)).y();
        });
        QCOMPARE(columnThumbs.size(), 2);
    }

    // Click first image in first column.
    QTest::mouseClick(columns[0][0], Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(selectionSpy.count() >= 1, 2000);

    // Click second image in second column. Plain-click should not clear first column selection.
    QTest::mouseClick(columns[1][1], Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(selectionSpy.count() >= 2, 2000);

    const auto latestSelection = qvariant_cast<SelectedImages>(selectionSpy.last().at(0));
    QCOMPARE(latestSelection.size(), 2);
    QVERIFY(latestSelection.contains(qMakePair(tempDir.path(), imageAPath)));
    QVERIFY(latestSelection.contains(qMakePair(tempDir.path(), imageBPath)));
}

QTEST_MAIN(tst_BrowsePanel)
#include "tst_BrowsePanel.moc"
