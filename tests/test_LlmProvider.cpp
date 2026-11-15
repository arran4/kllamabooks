#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QtTest>

#include "../src/AIOperationsManager.h"
#include "../src/BookDatabase.h"
#include "../src/DocumentTemplatesManager.h"
#include "../src/OllamaClient.h"
#include "../src/TemplateModel.h"
#include "../src/llm/LlmProvider.h"

namespace {

class MockLlmProvider : public llm::LlmProvider {
   public:
    struct RecordedRequest {
        bool isChat = false;
        llm::LlmRequestOptions options;
        QString prompt;
        QJsonArray messages;
    };

    QList<RecordedRequest> requests;
    bool shouldFail = false;

    QString providerName() const override { return QStringLiteral("mock"); }

    QNetworkReply* generate(const llm::LlmRequestOptions& options, const QString& prompt,
                            const llm::LlmCallbacks& callbacks) override {
        requests.append({false, options, prompt, {}});
        if (shouldFail) {
            if (callbacks.onError) {
                callbacks.onError(QNetworkReply::ConnectionRefusedError, QStringLiteral("mock failure"));
            }
            return nullptr;
        }
        if (callbacks.onChunk) callbacks.onChunk(QStringLiteral("chunk1-"));
        if (callbacks.onChunk) callbacks.onChunk(QStringLiteral("chunk2"));
        if (callbacks.onComplete) callbacks.onComplete(QStringLiteral("chunk1-chunk2"));
        return nullptr;
    }

    QNetworkReply* generateChat(const llm::LlmRequestOptions& options, const QJsonArray& messages,
                                const llm::LlmCallbacks& callbacks) override {
        requests.append({true, options, QString(), messages});
        if (shouldFail) {
            if (callbacks.onError) {
                callbacks.onError(QNetworkReply::TimeoutError, QStringLiteral("mock chat timeout"));
            }
            return nullptr;
        }
        if (callbacks.onComplete) callbacks.onComplete(QStringLiteral("chat-ok"));
        return nullptr;
    }

    void abortGenerations() override { aborted = true; }
    bool aborted = false;
};

}  // namespace

class TestLlmProvider : public QObject {
    Q_OBJECT

   private slots:
    void testConcurrentPayloadsDoNotLeakState();
    void testMockLlmProviderAndOllamaConformance();
    void testTemplateSourceAndPurposeSeparation();
};

void TestLlmProvider::testConcurrentPayloadsDoNotLeakState() {
    llm::LlmRequestOptions reqA;
    reqA.model = QStringLiteral("llama3:8b");
    reqA.systemPrompt = QStringLiteral("System A");
    reqA.optionsJson["temperature"] = 0.2;

    llm::LlmRequestOptions reqB;
    reqB.model = QStringLiteral("mistral:7b");
    reqB.systemPrompt = QStringLiteral("System B");
    reqB.optionsJson["temperature"] = 0.9;

    const QJsonObject payloadA = llm::buildOllamaGeneratePayload(reqA, QStringLiteral("Prompt A"));
    const QJsonObject payloadB = llm::buildOllamaGeneratePayload(reqB, QStringLiteral("Prompt B"));

    QCOMPARE(payloadA.value("model").toString(), QStringLiteral("llama3:8b"));
    QCOMPARE(payloadA.value("system").toString(), QStringLiteral("System A"));
    QCOMPARE(payloadA.value("prompt").toString(), QStringLiteral("Prompt A"));
    QCOMPARE(payloadA.value("options").toObject().value("temperature").toDouble(), 0.2);

    QCOMPARE(payloadB.value("model").toString(), QStringLiteral("mistral:7b"));
    QCOMPARE(payloadB.value("system").toString(), QStringLiteral("System B"));
    QCOMPARE(payloadB.value("prompt").toString(), QStringLiteral("Prompt B"));
    QCOMPARE(payloadB.value("options").toObject().value("temperature").toDouble(), 0.9);

    QJsonArray msgs;
    QJsonObject userMsg;
    userMsg["role"] = QStringLiteral("user");
    userMsg["content"] = QStringLiteral("Hello");
    msgs.append(userMsg);

    const QJsonObject chatPayload = llm::buildOllamaChatPayload(reqA, msgs);
    const QJsonArray chatMsgs = chatPayload.value("messages").toArray();
    QCOMPARE(chatMsgs.size(), 2);
    QCOMPARE(chatMsgs.at(0).toObject().value("role").toString(), QStringLiteral("system"));
    QCOMPARE(chatMsgs.at(0).toObject().value("content").toString(), QStringLiteral("System A"));
    QCOMPARE(chatMsgs.at(1).toObject().value("content").toString(), QStringLiteral("Hello"));
}

void TestLlmProvider::testMockLlmProviderAndOllamaConformance() {
    OllamaClient ollama;
    llm::LlmProvider* basePtr = &ollama;
    QCOMPARE(basePtr->providerName(), QStringLiteral("ollama"));

    MockLlmProvider mock;
    QString completed;
    QString streamed;
    mock.generate(
        {QStringLiteral("m1"), QStringLiteral("sys1"), {}}, QStringLiteral("hello"),
        {[&](const QString& chunk) { streamed += chunk; }, [&](const QString& full) { completed = full; }, nullptr});
    QCOMPARE(streamed, QStringLiteral("chunk1-chunk2"));
    QCOMPARE(completed, QStringLiteral("chunk1-chunk2"));
    QCOMPARE(mock.requests.size(), 1);
    QCOMPARE(mock.requests.at(0).options.systemPrompt, QStringLiteral("sys1"));

    mock.shouldFail = true;
    QString errMsg;
    mock.generateChat({QStringLiteral("m2"), QStringLiteral("sys2"), {}}, QJsonArray{},
                      {nullptr, nullptr, [&](QNetworkReply::NetworkError, const QString& err) { errMsg = err; }});
    QCOMPARE(errMsg, QStringLiteral("mock chat timeout"));
}

void TestLlmProvider::testTemplateSourceAndPurposeSeparation() {
    QCoreApplication::setOrganizationName(QStringLiteral("KLlamaBooksTest"));
    QCoreApplication::setApplicationName(QStringLiteral("TestLlmProvider"));
    QSettings settings;
    settings.clear();

    QCOMPARE(templateSourceToString(TemplateSource::BuiltIn), QStringLiteral("built-in"));
    QCOMPARE(templateSourceToString(TemplateSource::Global), QStringLiteral("global"));
    QCOMPARE(templateSourceToString(TemplateSource::Book), QStringLiteral("database"));
    QCOMPARE(templateSourceToString(TemplateSource::ExternalCache), QStringLiteral("external-cache"));

    QVERIFY(isTemplateSourceReadOnly(TemplateSource::BuiltIn));
    QVERIFY(isTemplateSourceReadOnly(TemplateSource::ExternalCache));
    QVERIFY(!isTemplateSourceReadOnly(TemplateSource::Global));
    QVERIFY(!isTemplateSourceReadOnly(TemplateSource::Book));

    DocumentTemplate extTpl;
    extTpl.id = QStringLiteral("ext:1");
    extTpl.name = QStringLiteral("Cached External");
    extTpl.content = QStringLiteral("# External");
    extTpl.sourceKind = TemplateSource::ExternalCache;
    extTpl.purpose = TemplatePurpose::DocumentCreation;
    extTpl.externalUrl = QStringLiteral("https://example.invalid/tpl.md");
    DocumentTemplatesManager::setExternalCacheTemplates({extTpl});

    const auto docCreationTpls =
        DocumentTemplatesManager::getTemplatesByPurpose(nullptr, TemplatePurpose::DocumentCreation);
    QVERIFY(docCreationTpls.size() >= 2);
    bool foundExt = false;
    for (const auto& t : docCreationTpls) {
        if (t.id == QLatin1String("ext:1")) {
            foundExt = true;
            QCOMPARE(t.sourceKind, TemplateSource::ExternalCache);
            QVERIFY(t.isReadOnly());
            QCOMPARE(t.externalUrl, QStringLiteral("https://example.invalid/tpl.md"));
        }
    }
    QVERIFY(foundExt);

    const auto singleOps = AIOperationsManager::getOperationsByPurpose(nullptr, TemplatePurpose::SingleDocTransform);
    const auto mergeOps = AIOperationsManager::getOperationsByPurpose(nullptr, TemplatePurpose::MultiDocMerge);
    QVERIFY(singleOps.size() >= 3);
    QCOMPARE(mergeOps.size(), 1);
    QCOMPARE(mergeOps.first().id, QStringLiteral("merge"));
    QVERIFY(mergeOps.first().isReadOnly());

    settings.clear();
}

QTEST_MAIN(TestLlmProvider)
#include "test_LlmProvider.moc"
