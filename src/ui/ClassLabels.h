#pragma once

#include <QStringList>

class ClassLabels
{
public:
    bool load(const QString &path, QString *error = nullptr);
    QString label(qint32 classId) const;

private:
    QStringList names;
};
