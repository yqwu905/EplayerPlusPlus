#ifndef CATEGORYEXPORTER_H
#define CATEGORYEXPORTER_H

#include <QHash>
#include <QString>

/**
 * @brief Pure serialization of folder classification marks into a text payload.
 *
 * Input is the map returned by ImageMarkManager::marksForFolder — keys are
 * folder-relative image paths, values are A/B/C/D categories. The exporter
 * reduces each key to its base filename and produces deterministic output
 * sorted by filename, in one of three formats.
 */
namespace CategoryExporter {

enum class Format {
    Csv,  // each line: 文件名,类别 (RFC-4180 escaped)
    Txt,  // each line: 文件名 类别 (single space)
    Json  // object: { "文件名": "类别" }
};

// Maps a file suffix (with or without leading dot, case-insensitive) to a
// format. Unknown suffixes default to Csv.
Format formatForSuffix(const QString &suffix);

// Serializes marks (imageKey -> category) to a text payload in the given format.
QString serialize(const QHash<QString, QString> &marks, Format format);

} // namespace CategoryExporter

#endif // CATEGORYEXPORTER_H
