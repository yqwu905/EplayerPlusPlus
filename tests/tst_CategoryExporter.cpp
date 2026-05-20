#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "services/CategoryExporter.h"

class tst_CategoryExporter : public QObject
{
    Q_OBJECT

private slots:
    void formatForSuffix_mapsExtensions();
    void csv_sortsByFilenameAndUsesBaseName();
    void csv_escapesSpecialCharacters();
    void txt_spaceSeparated();
    void json_objectShape();
    void empty_producesEmptyOrBraces();
};

void tst_CategoryExporter::formatForSuffix_mapsExtensions()
{
    using F = CategoryExporter::Format;
    QCOMPARE(CategoryExporter::formatForSuffix(QStringLiteral("csv")), F::Csv);
    QCOMPARE(CategoryExporter::formatForSuffix(QStringLiteral(".CSV")), F::Csv);
    QCOMPARE(CategoryExporter::formatForSuffix(QStringLiteral("txt")), F::Txt);
    QCOMPARE(CategoryExporter::formatForSuffix(QStringLiteral(".Txt")), F::Txt);
    QCOMPARE(CategoryExporter::formatForSuffix(QStringLiteral("json")), F::Json);
    QCOMPARE(CategoryExporter::formatForSuffix(QStringLiteral("bogus")), F::Csv);
}

void tst_CategoryExporter::csv_sortsByFilenameAndUsesBaseName()
{
    QHash<QString, QString> marks;
    marks.insert(QStringLiteral("sub/b.png"), QStringLiteral("B"));
    marks.insert(QStringLiteral("a.png"), QStringLiteral("A"));
    marks.insert(QStringLiteral("c.png"), QStringLiteral("C"));

    const QString csv = CategoryExporter::serialize(marks, CategoryExporter::Format::Csv);
    QCOMPARE(csv, QStringLiteral("a.png,A\nb.png,B\nc.png,C\n"));
}

void tst_CategoryExporter::csv_escapesSpecialCharacters()
{
    QHash<QString, QString> marks;
    marks.insert(QStringLiteral("comma,name.png"), QStringLiteral("A"));
    marks.insert(QStringLiteral("quote\"name.png"), QStringLiteral("B"));

    const QString csv = CategoryExporter::serialize(marks, CategoryExporter::Format::Csv);
    // Sorted by filename: comma... before quote...
    QCOMPARE(csv,
             QStringLiteral("\"comma,name.png\",A\n\"quote\"\"name.png\",B\n"));
}

void tst_CategoryExporter::txt_spaceSeparated()
{
    QHash<QString, QString> marks;
    marks.insert(QStringLiteral("img_02.png"), QStringLiteral("B"));
    marks.insert(QStringLiteral("img_01.png"), QStringLiteral("A"));

    const QString txt = CategoryExporter::serialize(marks, CategoryExporter::Format::Txt);
    QCOMPARE(txt, QStringLiteral("img_01.png A\nimg_02.png B\n"));
}

void tst_CategoryExporter::json_objectShape()
{
    QHash<QString, QString> marks;
    marks.insert(QStringLiteral("sub/x.png"), QStringLiteral("D"));
    marks.insert(QStringLiteral("y.png"), QStringLiteral("A"));

    const QString json = CategoryExporter::serialize(marks, CategoryExporter::Format::Json);
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    QCOMPARE(obj.size(), 2);
    QCOMPARE(obj.value(QStringLiteral("x.png")).toString(), QStringLiteral("D"));
    QCOMPARE(obj.value(QStringLiteral("y.png")).toString(), QStringLiteral("A"));
}

void tst_CategoryExporter::empty_producesEmptyOrBraces()
{
    const QHash<QString, QString> marks;
    QCOMPARE(CategoryExporter::serialize(marks, CategoryExporter::Format::Csv), QString());
    QCOMPARE(CategoryExporter::serialize(marks, CategoryExporter::Format::Txt), QString());

    const QString json = CategoryExporter::serialize(marks, CategoryExporter::Format::Json);
    QCOMPARE(QJsonDocument::fromJson(json.toUtf8()).object().size(), 0);
}

QTEST_MAIN(tst_CategoryExporter)
#include "tst_CategoryExporter.moc"
