#include "TemplateParser.h"

#include <QRegularExpression>

QString TemplateParser::parseMergeTemplate(QString prompt, const QList<QString>& documentContents) {
    // Process {foreach contexts} block
    // Syntax: {foreach contexts} ... {context} ... [{between} ... ] {end}
    // Note: Use a non-greedy match for the block
    QRegularExpression foreachRe("\\{foreach contexts\\}(.*?)\\{end\\}",
                                 QRegularExpression::DotMatchesEverythingOption);

    int offset = 0;
    while (true) {
        QRegularExpressionMatch match = foreachRe.match(prompt, offset);
        if (!match.hasMatch()) {
            break;
        }

        QString innerBlock = match.captured(1);
        QString mergedContexts;

        // Split by {between}
        int betweenIndex = innerBlock.indexOf("{between}");
        QString mainPart = innerBlock;
        QString betweenPart = "";

        if (betweenIndex != -1) {
            mainPart = innerBlock.left(betweenIndex);
            betweenPart = innerBlock.mid(betweenIndex + 9);  // length of "{between}"
        }

        for (int k = 0; k < documentContents.size(); ++k) {
            QString docPart = mainPart;
            docPart.replace("{context}", documentContents[k]);
            mergedContexts += docPart;
            if (k < documentContents.size() - 1) {
                mergedContexts += betweenPart;
            }
        }

        prompt.replace(match.capturedStart(0), match.capturedLength(0), mergedContexts);
        offset = match.capturedStart(0) + mergedContexts.length();
    }

    // Also support a simple {context} for backwards compatibility if needed?
    // Just replace it with all joined by newlines just in case
    if (prompt.contains("{context}")) {
        prompt.replace("{context}", documentContents.join("\n\n---\n\n"));
    }

    return prompt;
}
