#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QDateTime>
#include <QMap>
#include <QList>
#include <QPair>
#include "shared/plugin_interface.h"

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool initialize(const QString& dbPath);
    void close();

    // セッション管理
    int startNewSession();
    void closeCurrentSession(int sessionId);
    
    // データ保存
    bool saveComment(const TwitchComment& comment, int sessionId);
    bool saveRedemption(const TwitchRewardRedemption& redemption);
    
    // CSVエクスポート
    bool exportSessionToCsv(int sessionId, const QString& filePath);

    // 解析・集計用API
    int getTotalCommentCount(int sessionId) const;
    QList<QPair<QString, int>> getCommentRanking(int sessionId, int limit = 10) const;
    
    // トレンドデータ（時系列累積発言数推移。X軸用QDateTimeと、Y軸用累積コメント数のリスト）
    QList<QPair<QDateTime, int>> getCommentTrendTotal(int sessionId, int intervalSeconds = 60) const;
    QList<QPair<QString, QList<QPair<QDateTime, int>>>> getCommentTrendByUser(int sessionId, int limitUsers = 5, int intervalSeconds = 60) const;

    // 視聴者リスト用カテゴリ別取得
    QMap<QString, QStringList> getCategorizedActiveUsers(int sessionId) const;

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    
    bool createTables();
};
