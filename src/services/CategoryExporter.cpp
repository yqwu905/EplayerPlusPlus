#include "services/CategoryExporter.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>

#include <algorithm>

namespace {

// Sorted (label, category) rows reduced from the relative-path keyed marks.
// Each key is shown by its base filename for readability — but only when that
// base name is unique. When two different relative paths share a base name
// (e.g. "sub/a.png" and "a.png"), the colliding entries fall back to their full
// relative path. Without this, JSON (which keys an object by the label) would
// silently drop all but the last colliding entry, while CSV/TXT would emit
// rows with identical, ambiguous filenames — the same input yielding a
// different record count per format. Relative-path keys are unique, so the
// fallback keeps every classification. Collision-free exports are unchanged.
QList<QPair<QString, QString>> sortedRows(const QHash<QString, QString> &marks)
{
    QHash<QString, int> baseNameCounts;
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        baseNameCounts[QFileInfo(it.key()).fileName()] += 1;
    }

    QList<QPair<QString, QString>> rows;
    rows.reserve(marks.size());
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        const QString baseName = QFileInfo(it.key()).fileName();
        const QString label = baseNameCounts.value(baseName) > 1 ? it.key() : baseName;
        rows.append({label, it.value()});
    }
    std::sort(rows.begin(), rows.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });
    return rows;
}

// RFC-4180: quote a field that contains a comma, double-quote, CR or LF,
// doubling any embedded double-quotes.
QString csvEscape(const QString &field)
{
    if (field.contains(QLatin1Char(',')) || field.contains(QLatin1Char('"'))
        || field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))) {
        QString escaped = field;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return field;
}

} // namespace

namespace CategoryExporter {

Format formatForSuffix(const QString &suffix)
{
    QString normalized = suffix.trimmed().toLower();
    if (normalized.startsWith(QLatin1Char('.'))) {
        normalized.remove(0, 1);
    }
    if (normalized == QLatin1String("txt")) {
        return Format::Txt;
    }
    if (normalized == QLatin1String("json")) {
        return Format::Json;
    }
    return Format::Csv;
}

QString serialize(const QHash<QString, QString> &marks, Format format)
{
    const QList<QPair<QString, QString>> rows = sortedRows(marks);

    if (format == Format::Json) {
        QJsonObject object;
        for (const auto &row : rows) {
            // Labels are unique here (sortedRows disambiguates base-name
            // collisions to full relative paths), so no entry is overwritten.
            object.insert(row.first, row.second);
        }
        return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
    }

    QStringList lines;
    lines.reserve(rows.size());
    for (const auto &row : rows) {
        if (format == Format::Csv) {
            lines.append(csvEscape(row.first) + QLatin1Char(',') + csvEscape(row.second));
        } else {
            lines.append(row.first + QLatin1Char(' ') + row.second);
        }
    }
    QString out = lines.join(QLatin1Char('\n'));
    if (!out.isEmpty()) {
        out.append(QLatin1Char('\n'));
    }
    return out;
}

} // namespace CategoryExporter
