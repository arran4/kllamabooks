#pragma once

#include <QList>
#include <QString>

#include "../db/Database.h"
#include "ArtifactStore.h"

namespace store {

enum class ActivitySeverity { Info, Warning, Error, ActionRequired };

inline QString activitySeverityToString(ActivitySeverity sev) {
    switch (sev) {
        case ActivitySeverity::Info:
            return QStringLiteral("info");
        case ActivitySeverity::Warning:
            return QStringLiteral("warning");
        case ActivitySeverity::Error:
            return QStringLiteral("error");
        case ActivitySeverity::ActionRequired:
            return QStringLiteral("action_required");
    }
    return QStringLiteral("info");
}

struct ActivityItem {
    int id = 0;
    QString source;  // "run" or "notification"
    int runId = 0;
    QString targetType;
    int targetId = 0;
    QString type;
    QString severity;
    QString title;
    QString message;
    bool isDismissed = false;
    QString createdAt;
};

class ActivityStore {
   public:
    explicit ActivityStore(db::Database& db);

    Result<int> addNotification(int targetId, const QString& targetType, const QString& type,
                                ActivitySeverity severity = ActivitySeverity::Info, const QString& title = QString(),
                                const QString& message = QString(), int runId = 0);

    Result<bool> dismissNotification(int id);
    Result<bool> dismissAll();

    QList<ActivityItem> getActivityFeed(bool includeDismissed = false) const;
    int unreadCount() const;

   private:
    db::Database& m_db;
};

}  // namespace store
