#pragma once
#include <QString>
#include <QVector>

struct ModelOption {
    quint32 id = 0;
    QString name;
};
using ModelOptions = QVector<ModelOption>;
