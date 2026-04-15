#include "CompareSession.h"

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

    if (m_folders.contains(folderPath)) {
        return false;
    }

    int index = m_folders.size();
    m_folders.append(folderPath);

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

    emit cleared();
    emit sessionChanged();
}

QStringList CompareSession::folders() const
{
    return m_folders;
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
