#ifndef TEMPLATEPARSER_H
#define TEMPLATEPARSER_H

#include <QList>
#include <QString>

class TemplateParser {
   public:
    static QString parseMergeTemplate(QString prompt, const QList<QString>& documentContents);
};

#endif  // TEMPLATEPARSER_H
