#include "ClassLabels.h"

#include <QFile>
#include <QTextStream>

bool ClassLabels::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream input(&file);
    input.setEncoding(QStringConverter::Utf8);
    QStringList loaded;
    while (!input.atEnd()) loaded.push_back(input.readLine().trimmed());
    if (loaded.isEmpty()) {
        if (error) *error = "Class names file is empty";
        return false;
    }
    names = std::move(loaded);
    return true;
}

QString ClassLabels::label(qint32 classId) const
{
    if (classId < 0 || classId == 255) return "Unknown";
    if (classId < names.size() && !names[classId].isEmpty()) return names[classId];
    return QString("Class %1").arg(classId);
}
