#include "CompareSession.h"

#include <QDir>
#include <QFileInfo>

CompareSession::CompareSession(QObject *parent)
    : QObject(parent)
{
}

CompareSession::~CompareSession() = default;

bool CompareSession::addFolder(const QString &folderPath)
{
    if (m_folders.size() >= MaxFolders) {
        return false;
    }

    int index = m_folders.size();
    m_folders.append(folderPath);
    m_displayNames.append(defaultDisplayNameForFolder(folderPath));

    emit folderAdded(folderPath, index);
    emit sessionChanged();
    return true;
}

bool CompareSession::removeFolder(const QString &folderPath)
{
    int index = m_folders.indexOf(folderPath);
    if (index < 0) {
        return false;
    }

    m_folders.removeAt(index);
    m_displayNames.removeAt(index);

    emit folderRemoved(folderPath, index);
    emit sessionChanged();
    return true;
}

bool CompareSession::removeFolderAt(int index)
{
    if (index < 0 || index >= m_folders.size()) {
        return false;
    }

    QString path = m_folders.at(index);
    m_folders.removeAt(index);
    m_displayNames.removeAt(index);

    emit folderRemoved(path, index);
    emit sessionChanged();
    return true;
}

void CompareSession::clear()
{
    if (m_folders.isEmpty()) {
        return;
    }

    m_folders.clear();
    m_displayNames.clear();

    emit cleared();
    emit sessionChanged();
}

QStringList CompareSession::folders() const
{
    return m_folders;
}

QString CompareSession::folderDisplayNameAt(int index) const
{
    if (index < 0 || index >= m_folders.size()) {
        return QString();
    }

    if (index < m_displayNames.size() && !m_displayNames.at(index).trimmed().isEmpty()) {
        return m_displayNames.at(index);
    }

    return defaultDisplayNameForFolder(m_folders.at(index));
}

bool CompareSession::setFolderDisplayNameAt(int index, const QString &displayName)
{
    if (index < 0 || index >= m_folders.size()) {
        return false;
    }

    while (m_displayNames.size() < m_folders.size()) {
        m_displayNames.append(defaultDisplayNameForFolder(m_folders.at(m_displayNames.size())));
    }

    const QString effectiveName = displayName.trimmed().isEmpty()
        ? defaultDisplayNameForFolder(m_folders.at(index))
        : displayName.trimmed();

    if (m_displayNames.at(index) == effectiveName) {
        return true;
    }

    m_displayNames[index] = effectiveName;
    emit folderDisplayNameChanged(m_folders.at(index), index, effectiveName);
    emit sessionChanged();
    return true;
}

int CompareSession::folderCount() const
{
    return m_folders.size();
}

bool CompareSession::containsFolder(const QString &folderPath) const
{
    return m_folders.contains(folderPath);
}

bool CompareSession::isFull() const
{
    return m_folders.size() >= MaxFolders;
}

int CompareSession::indexOf(const QString &folderPath) const
{
    return m_folders.indexOf(folderPath);
}

QString CompareSession::defaultDisplayNameForFolder(const QString &folderPath) const
{
    const QFileInfo info(folderPath);
    QString name = QDir(folderPath).dirName();
    if (name.isEmpty()) {
        name = info.fileName();
    }
    if (name.isEmpty()) {
        name = info.absoluteFilePath();
    }
    if (name.isEmpty()) {
        name = folderPath;
    }
    return name;
}
