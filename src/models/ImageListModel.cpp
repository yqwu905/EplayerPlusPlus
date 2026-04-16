#include "ImageListModel.h"
#include "services/ImageLoader.h"
#include "utils/FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

ImageListModel::ImageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

ImageListModel::~ImageListModel()
{
    cancelPendingScan();
}

int ImageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_imagePaths.size();
}

QVariant ImageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_imagePaths.size()) {
        return QVariant();
    }

    const QString &path = m_imagePaths.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case FileNameRole:
        return QFileInfo(path).fileName();
    case FilePathRole:
        return path;
    case Qt::DecorationRole:
    case ThumbnailRole: {
        auto it = m_thumbnails.constFind(path);
        if (it != m_thumbnails.constEnd()) {
            return *it;
        }
        return QVariant();
    }
    case IsSelectedRole:
        return m_selectedIndices.contains(index.row());
    default:
        return QVariant();
    }
}

void ImageListModel::setFolder(const QString &folderPath)
{
    if (m_folderPath == folderPath) {
        return;
    }

    cancelPendingScan();

    beginResetModel();
    m_folderPath = folderPath;
    m_imagePaths.clear();
    m_selectedIndices.clear();
    m_thumbnails.clear();
    endResetModel();

    // Start async scan
    m_loading = true;
    m_scanWatcher = new QFutureWatcher<QStringList>(this);
    connect(m_scanWatcher, &QFutureWatcher<QStringList>::finished,
            this, &ImageListModel::onScanFinished);

    QString path = folderPath;
    m_scanWatcher->setFuture(QtConcurrent::run([path]() {
        return FileUtils::scanForImages(path, false);
    }));
}

QString ImageListModel::folderPath() const
{
    return m_folderPath;
}

QString ImageListModel::folderName() const
{
    return QDir(m_folderPath).dirName();
}

void ImageListModel::refresh()
{
    cancelPendingScan();

    beginResetModel();
    m_imagePaths.clear();
    m_selectedIndices.clear();
    m_thumbnails.clear();
    endResetModel();

    if (m_folderPath.isEmpty()) {
        return;
    }

    // Start async scan
    m_loading = true;
    m_scanWatcher = new QFutureWatcher<QStringList>(this);
    connect(m_scanWatcher, &QFutureWatcher<QStringList>::finished,
            this, &ImageListModel::onScanFinished);

    QString path = m_folderPath;
    m_scanWatcher->setFuture(QtConcurrent::run([path]() {
        return FileUtils::scanForImages(path, false);
    }));
}

bool ImageListModel::isLoading() const
{
    return m_loading;
}

QString ImageListModel::imagePathAt(int index) const
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return QString();
    }
    return m_imagePaths.at(index);
}

QString ImageListModel::fileNameAt(int index) const
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return QString();
    }
    return QFileInfo(m_imagePaths.at(index)).fileName();
}

int ImageListModel::imageCount() const
{
    return m_imagePaths.size();
}

int ImageListModel::indexOfFileName(const QString &fileName) const
{
    for (int i = 0; i < m_imagePaths.size(); ++i) {
        if (QFileInfo(m_imagePaths.at(i)).fileName() == fileName) {
            return i;
        }
    }
    return -1;
}

void ImageListModel::setSelected(int index, bool selected)
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return;
    }

    bool changed = false;
    if (selected && !m_selectedIndices.contains(index)) {
        m_selectedIndices.insert(index);
        changed = true;
    } else if (!selected && m_selectedIndices.contains(index)) {
        m_selectedIndices.remove(index);
        changed = true;
    }

    if (changed) {
        QModelIndex mi = this->index(index);
        emit dataChanged(mi, mi, {IsSelectedRole});
        emit selectionChanged();
    }
}

void ImageListModel::clearSelection()
{
    if (m_selectedIndices.isEmpty()) {
        return;
    }

    QList<int> indices = m_selectedIndices.values();
    m_selectedIndices.clear();

    for (int idx : indices) {
        QModelIndex mi = this->index(idx);
        emit dataChanged(mi, mi, {IsSelectedRole});
    }
    emit selectionChanged();
}

QList<int> ImageListModel::selectedIndices() const
{
    return m_selectedIndices.values();
}

bool ImageListModel::isSelected(int index) const
{
    return m_selectedIndices.contains(index);
}

void ImageListModel::setImageLoader(ImageLoader *loader)
{
    if (m_imageLoader) {
        disconnect(m_imageLoader, nullptr, this, nullptr);
    }

    m_imageLoader = loader;

    if (m_imageLoader) {
        connect(m_imageLoader, &ImageLoader::thumbnailReady,
                this, &ImageListModel::onThumbnailReady);
    }
}

void ImageListModel::loadThumbnailsForRange(int firstVisible, int lastVisible)
{
    if (!m_imageLoader) {
        return;
    }

    firstVisible = qMax(0, firstVisible);
    lastVisible = qMin(m_imagePaths.size() - 1, lastVisible);

    for (int i = firstVisible; i <= lastVisible; ++i) {
        const QString &path = m_imagePaths.at(i);
        if (!m_thumbnails.contains(path)) {
            m_imageLoader->requestThumbnail(path);
        }
    }
}

void ImageListModel::onThumbnailReady(const QString &imagePath, const QImage &thumbnail)
{
    // Find the index of this image in our list
    int idx = m_imagePaths.indexOf(imagePath);
    if (idx < 0) {
        return;
    }

    m_thumbnails.insert(imagePath, thumbnail);

    QModelIndex mi = this->index(idx);
    emit dataChanged(mi, mi, {Qt::DecorationRole, ThumbnailRole});
}

void ImageListModel::onScanFinished()
{
    if (!m_scanWatcher) {
        return;
    }

    QStringList results = m_scanWatcher->result();
    m_scanWatcher->deleteLater();
    m_scanWatcher = nullptr;
    m_loading = false;

    if (!results.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, results.size() - 1);
        m_imagePaths = results;
        endInsertRows();
    }

    emit folderReady();
}

void ImageListModel::cancelPendingScan()
{
    if (m_scanWatcher) {
        m_scanWatcher->cancel();
        m_scanWatcher->deleteLater();
        m_scanWatcher = nullptr;
        m_loading = false;
    }
}
