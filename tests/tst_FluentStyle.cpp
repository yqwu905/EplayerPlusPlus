#include <QtTest>

#include "styles/FluentStyle.h"

class tst_FluentStyle : public QObject
{
    Q_OBJECT

private slots:
    void testWindowsFontPrefersReadableChineseUiFont();
    void testWindowsFontRenderingHints();
    void testMacFontUsesSystemFont();
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

QTEST_MAIN(tst_FluentStyle)
#include "tst_FluentStyle.moc"
