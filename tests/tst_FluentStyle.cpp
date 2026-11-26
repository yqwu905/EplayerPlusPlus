#include <QtTest>
#include <QImage>
#include <QPixmap>

#include "styles/FluentStyle.h"

class tst_FluentStyle : public QObject
{
    Q_OBJECT

private slots:
    void testWindowsFontPrefersReadableChineseUiFont();
    void testWindowsFontRenderingHints();
    void testMacFontUsesSystemFont();
    void testVectorIconsRenderCrisplyAcrossModes();
};

void tst_FluentStyle::testWindowsFontPrefersReadableChineseUiFont()
{
    const QStringList families = FluentStyle::preferredFontFamilies(FluentStyle::Platform::Windows);

    QVERIFY(families.size() >= 3);
    QCOMPARE(families.at(0), QStringLiteral("Microsoft YaHei UI"));
    QVERIFY(families.contains(QStringLiteral("Segoe UI")));

    const QFont font = FluentStyle::applicationFont(FluentStyle::Platform::Windows);
    QCOMPARE(font.families(), families);
    QCOMPARE(font.pointSize(), FluentStyle::fontSizeBody());
    QCOMPARE(font.weight(), QFont::Normal);
}

void tst_FluentStyle::testWindowsFontRenderingHints()
{
    const QFont font = FluentStyle::applicationFont(FluentStyle::Platform::Windows);
    const int strategy = static_cast<int>(font.styleStrategy());

    QVERIFY((strategy & QFont::PreferAntialias) != 0);
    QVERIFY((strategy & QFont::PreferQuality) != 0);
    QCOMPARE(font.hintingPreference(), QFont::PreferNoHinting);
}

void tst_FluentStyle::testMacFontUsesSystemFont()
{
    const QStringList families = FluentStyle::preferredFontFamilies(FluentStyle::Platform::MacOS);

    QCOMPARE(families, QStringList{QStringLiteral(".AppleSystemUIFont")});
}

void tst_FluentStyle::testVectorIconsRenderCrisplyAcrossModes()
{
    const QList<FluentStyle::Icon> icons = {
        FluentStyle::Icon::FolderAdd,
        FluentStyle::Icon::Refresh,
        FluentStyle::Icon::Clear,
        FluentStyle::Icon::ArrowUp,
        FluentStyle::Icon::ArrowDown,
        FluentStyle::Icon::Swap,
        FluentStyle::Icon::Tolerance,
        FluentStyle::Icon::Settings,
        FluentStyle::Icon::Sparkle,
        FluentStyle::Icon::Edit,
        FluentStyle::Icon::More
    };

    auto hasOpaquePixel = [](const QImage &img) {
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (img.pixelColor(x, y).alpha() > 0) {
                    return true;
                }
            }
        }
        return false;
    };

    const QList<qreal> dprs = {1.0, 1.25, 2.0};
    for (FluentStyle::Icon kind : icons) {
        const QIcon ic = FluentStyle::icon(kind);
        QVERIFY(!ic.isNull());

        for (qreal dpr : dprs) {
            const QPixmap normalPm = ic.pixmap(QSize(16, 16), dpr, QIcon::Normal, QIcon::Off);
            const QPixmap disabledPm = ic.pixmap(QSize(16, 16), dpr, QIcon::Disabled, QIcon::Off);
            const QSize expectedPhysical(qRound(16.0 * dpr), qRound(16.0 * dpr));

            QCOMPARE(normalPm.size(), expectedPhysical);
            QCOMPARE(normalPm.deviceIndependentSize(), QSizeF(16.0, 16.0));
            QCOMPARE(disabledPm.size(), expectedPhysical);

            const QImage normalImg = normalPm.toImage();
            const QImage disabledImg = disabledPm.toImage();
            QVERIFY(hasOpaquePixel(normalImg));
            QVERIFY(hasOpaquePixel(disabledImg));
            QVERIFY(normalImg != disabledImg);
        }
    }
}

QTEST_MAIN(tst_FluentStyle)
#include "tst_FluentStyle.moc"
