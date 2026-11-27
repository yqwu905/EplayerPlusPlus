#include "ImageInformation.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QtEndian>

namespace
{
constexpr int maxExifBytes = 1024 * 1024;

QByteArray readExif(QFile &file)
{
    const QByteArray signature = file.read(12);
    if (signature.startsWith("II\x2a") || signature.startsWith(QByteArray("MM\0\x2a", 4))) {
        file.seek(0);
        return file.read(maxExifBytes);
    }

    if (signature.startsWith("\xff\xd8")) {
        file.seek(2);
        while (!file.atEnd()) {
            QByteArray marker = file.read(2);
            if (marker.size() != 2 || uchar(marker[0]) != 0xff) {
                break;
            }
            while (uchar(marker[1]) == 0xff) {
                const QByteArray next = file.read(1);
                if (next.isEmpty()) {
                    return {};
                }
                marker[1] = next[0];
            }
            const auto code = uchar(marker[1]);
            if (code == 0xda || code == 0xd9 || code == 0) {
                break;
            }
            if (code == 0x01 || (code >= 0xd0 && code <= 0xd7)) {
                continue;
            }
            const QByteArray sizeBytes = file.read(2);
            if (sizeBytes.size() != 2) {
                break;
            }
            const int size = qFromBigEndian<quint16>(sizeBytes.constData());
            if (size < 2) {
                break;
            }
            if (code == 0xe1) {
                const QByteArray segment = file.read(size - 2);
                if (segment.startsWith(QByteArray("Exif\0\0", 6))) {
                    return segment.mid(6);
                }
            } else if (!file.seek(file.pos() + size - 2)) {
                break;
            }
        }
        return {};
    }

    const bool png = signature.startsWith("\x89PNG\r\n\x1a\n");
    const bool webp = signature.startsWith("RIFF") && signature.mid(8) == "WEBP";
    if (!png && !webp) {
        return {};
    }
    file.seek(png ? 8 : 12);
    while (file.size() - file.pos() >= 8) {
        const QByteArray header = file.read(8);
        if (header.size() != 8) {
            break;
        }
        const auto size = png ? qFromBigEndian<quint32>(header.constData())
                              : qFromLittleEndian<quint32>(header.constData() + 4);
        const QByteArray type = png ? header.mid(4) : header.left(4);
        const qint64 span = qint64(size) + (png ? 4 : (size % 2));
        if (span > file.size() - file.pos() || type == "IEND") {
            break;
        }
        if ((type == "eXIf" || type == "EXIF") && size <= maxExifBytes) {
            QByteArray payload = file.read(size);
            if (payload.startsWith(QByteArray("Exif\0\0", 6))) {
                payload.remove(0, 6);
            }
            return payload;
        }
        if (!file.seek(file.pos() + span)) {
            break;
        }
    }
    return {};
}

class ExifReader
{
public:
    explicit ExifReader(const QByteArray &data) : m_data(data), m_little(data.startsWith("II")) {}

    QStringList lines() const
    {
        if ((!m_little && !m_data.startsWith("MM")) || read<quint16>(2) != 42) {
            return {};
        }

        const auto primary = directory(read<quint32>(4));
        const auto exif = directory(read<quint32>(primary.value(0x8769, -1)));
        QStringList result;
        const QString make = ascii(primary.value(0x010f));
        const QString model = ascii(primary.value(0x0110));
        const QString camera = model.startsWith(make, Qt::CaseInsensitive)
            ? model : (make + QLatin1Char(' ') + model).trimmed();
        if (!camera.isEmpty()) {
            result << camera;
        }
        QString date = ascii(exif.value(0x9003));
        if (date.isEmpty()) {
            date = ascii(primary.value(0x0132));
        }
        if (!date.isEmpty()) {
            result << date;
        }
        QStringList exposure;
        const qreal time = rational(exif.value(0x829a));
        if (time > 0) {
            exposure << (time < 1 ? QStringLiteral("1/%1 s").arg(1 / time, 0, 'g', 3)
                                  : QStringLiteral("%1 s").arg(time, 0, 'g', 3));
        }
        const qreal aperture = rational(exif.value(0x829d));
        if (aperture > 0) {
            exposure << QStringLiteral("f/%1").arg(aperture, 0, 'g', 3);
        }
        const auto iso = read<quint16>(exif.value(0x8827, -1));
        if (iso > 0) {
            exposure << QStringLiteral("ISO %1").arg(iso);
        }
        if (!exposure.isEmpty()) {
            result << exposure.join(QStringLiteral(" · "));
        }
        const qreal focal = rational(exif.value(0x920a));
        if (focal > 0) {
            result << QCoreApplication::translate("ImageInformation", "%1 毫米").arg(focal, 0, 'g', 3);
        }
        return result;
    }

private:
    template<class T> T read(qint64 offset) const
    {
        if (offset < 0 || offset + qint64(sizeof(T)) > m_data.size()) {
            return 0;
        }
        const auto *bytes = m_data.constData() + offset;
        return m_little ? qFromLittleEndian<T>(bytes) : qFromBigEndian<T>(bytes);
    }

    QHash<quint16, qint64> directory(quint32 offset) const
    {
        QHash<quint16, qint64> tags;
        if (offset < 8 || qint64(offset) + 2 > m_data.size()) {
            return tags;
        }
        const int count = read<quint16>(offset);
        for (int i = 0; i < count; ++i) {
            const qint64 entry = qint64(offset) + 2 + 12 * i;
            if (entry + 12 > m_data.size()) {
                break;
            }
            tags.insert(read<quint16>(entry), entry + 8);
        }
        return tags;
    }

    QString ascii(qint64 value) const
    {
        if (value < 8 || read<quint16>(value - 6) != 2) {
            return {};
        }
        const quint32 length = read<quint32>(value - 4);
        const qint64 start = length <= 4 ? value : read<quint32>(value);
        if (!length || start + length > m_data.size()) {
            return {};
        }
        const QByteArray bytes = m_data.mid(start, qMin(length, quint32(160)));
        return QString::fromLatin1(bytes.constData()).simplified();
    }

    qreal rational(qint64 value) const
    {
        if (value < 8 || read<quint16>(value - 6) != 5) {
            return 0;
        }
        const qint64 offset = read<quint32>(value);
        const auto denominator = read<quint32>(offset + 4);
        return denominator ? qreal(read<quint32>(offset)) / denominator : 0;
    }

    const QByteArray &m_data;
    bool m_little;
};
} // namespace

ImageInformation readImageInformation(const QString &path, const QImage &image)
{
    ImageInformation result;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray exif = readExif(file);
        result.exifLines = ExifReader(exif).lines();
    }

    const QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < rgba.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
        for (int x = 0; x < rgba.width(); ++x) {
            const QRgb pixel = row[x];
            if (qAlpha(pixel)) {
                ++result.histogram[0][qRed(pixel)];
                ++result.histogram[1][qGreen(pixel)];
                ++result.histogram[2][qBlue(pixel)];
            }
        }
    }
    return result;
}
