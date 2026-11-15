#include <sqlcipher/sqlite3.h>

#include <QtTest>

#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/store/ActivityStore.h"
#include "../src/store/PromptStore.h"
#include "../src/store/RunStore.h"

class TestActivityStore : public QObject {
    Q_OBJECT

   private slots:
    void testNotificationsAndDismissal();
    void testFeedCombinesRunsAndNotifications();
};

void TestActivityStore::testNotificationsAndDismissal() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::ActivityStore activity(database);
    QCOMPARE(activity.unreadCount(), 0);

    auto n1 = activity.addNotification(10, "document", "info", store::ActivitySeverity::Info, "Saved",
                                       "Document saved cleanly.");
    auto n2 = activity.addNotification(11, "document", "error", store::ActivitySeverity::Error, "Network error",
                                       "Ollama unreachable.");
    QVERIFY(n1.isSuccess() && n2.isSuccess());
    QCOMPARE(activity.unreadCount(), 2);

    QVERIFY(activity.dismissNotification(*n1.value).isSuccess());
    QCOMPARE(activity.unreadCount(), 1);

    const auto activeFeed = activity.getActivityFeed(false);
    QCOMPARE(activeFeed.size(), 1);
    QCOMPARE(activeFeed.first().title, QStringLiteral("Network error"));

    QVERIFY(activity.dismissAll().isSuccess());
    QCOMPARE(activity.unreadCount(), 0);
    QCOMPARE(activity.getActivityFeed(true).size(), 2);

    sqlite3_close(rawDb);
}

void TestActivityStore::testFeedCombinesRunsAndNotifications() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::PromptStore promptStore(database);
    auto rev = promptStore.createRevision("user", "", "document", 5, "", "Prompt", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(database);
    auto runRes = runStore.enqueueRun(rev.value->id, "document", 5, 0, "replace", "llama3");
    QVERIFY(runRes.isSuccess());
    QVERIFY(runStore.claimRun(runRes.value->id, "w1").isSuccess());
    QVERIFY(runStore.failRun(runRes.value->id, "Model timed out").isSuccess());

    store::ActivityStore activity(database);
    QVERIFY(activity
                .addNotification(5, "document", "warning", store::ActivitySeverity::Warning, "Conflict",
                                 "Base version changed")
                .isSuccess());

    const auto feed = activity.getActivityFeed(false);
    QCOMPARE(feed.size(), 2);

    bool hasRunItem = false;
    bool hasNotifItem = false;
    for (const auto& item : feed) {
        if (item.source == QLatin1String("run") && item.severity == QLatin1String("error")) {
            hasRunItem = true;
            QCOMPARE(item.message, QStringLiteral("Model timed out"));
        }
        if (item.source == QLatin1String("notification") && item.severity == QLatin1String("warning")) {
            hasNotifItem = true;
        }
    }
    QVERIFY(hasRunItem);
    QVERIFY(hasNotifItem);

    sqlite3_close(rawDb);
}

QTEST_MAIN(TestActivityStore)
#include "test_ActivityStore.moc"
