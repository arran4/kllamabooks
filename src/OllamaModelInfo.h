#ifndef OLLAMAMODELINFO_H
#define OLLAMAMODELINFO_H

#include <QString>

struct OllamaModelInfo {
    QString name;
    QString size;
    QString family;
    QString parameterSize;
    QString quantizationLevel;
};

#endif // OLLAMAMODELINFO_H
