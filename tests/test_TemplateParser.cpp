#include <QtTest>
#include "../src/TemplateParser.h"

class TestTemplateParser : public QObject {
    Q_OBJECT
private slots:
    void testBasicForeach();
    void testBetweenSeparator();
    void testNoMatchMissingEnd();
    void testFallbackContext();
};

void TestTemplateParser::testBasicForeach() {
    QString prompt = "Merge this:\n{foreach contexts}Doc:\n{context}\n{end}";
    QList<QString> contents = {"Hello", "World"};

    QString result = TemplateParser::parseMergeTemplate(prompt, contents);

    QCOMPARE(result, QString("Merge this:\nDoc:\nHello\nDoc:\nWorld\n"));
}

void TestTemplateParser::testBetweenSeparator() {
    QString prompt = "{foreach contexts}{context}{between}---{end}";
    QList<QString> contents = {"A", "B", "C"};

    QString result = TemplateParser::parseMergeTemplate(prompt, contents);

    QCOMPARE(result, QString("A---B---C"));
}

void TestTemplateParser::testNoMatchMissingEnd() {
    QString prompt = "{foreach contexts}{context}"; // Missing {end}
    QList<QString> contents = {"A", "B"};

    QString result = TemplateParser::parseMergeTemplate(prompt, contents);

    // Without {end}, the regex should fail to match, but {context} should be caught by fallback.
    // The fallback replaces {context} with "A\n\n---\n\nB"
    QCOMPARE(result, QString("{foreach contexts}A\n\n---\n\nB"));
}

void TestTemplateParser::testFallbackContext() {
    QString prompt = "Merge:\n{context}";
    QList<QString> contents = {"Doc1", "Doc2"};

    QString result = TemplateParser::parseMergeTemplate(prompt, contents);

    QCOMPARE(result, QString("Merge:\nDoc1\n\n---\n\nDoc2"));
}

QTEST_MAIN(TestTemplateParser)
#include "test_TemplateParser.moc"
