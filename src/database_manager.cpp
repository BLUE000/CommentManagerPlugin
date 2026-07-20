#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>

DatabaseManager::DatabaseManager() {
    m_connectionName = QString("CommentManagerDB_%1").arg(QDateTime::currentMSecsSinceEpoch());
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::initialize(const QString& dbPath) {
    // データベースファイルの保存先ディレクトリを作成
    QFileInfo fileInfo(dbPath);
    QDir().mkpath(fileInfo.absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "DatabaseManager: Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // 外部キー制約を有効化
    QSqlQuery pragmaQuery(m_db);
    pragmaQuery.exec("PRAGMA foreign_keys = ON;");

    return createTables();
}

void DatabaseManager::close() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    // QSqlDatabase のクリーンアップ
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DatabaseManager::createTables() {
    QSqlQuery query(m_db);

    // sessions テーブル
    QString createSessions = 
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  session_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  started_at TEXT NOT NULL,"
        "  ended_at TEXT"
        ");";
    if (!query.exec(createSessions)) {
        qWarning() << "DatabaseManager: Failed to create sessions table:" << query.lastError().text();
        return false;
    }

    // users テーブル
    QString createUsers = 
        "CREATE TABLE IF NOT EXISTS users ("
        "  user_id TEXT PRIMARY KEY,"
        "  username TEXT NOT NULL,"
        "  display_name TEXT,"
        "  comment_count INTEGER DEFAULT 0,"
        "  last_active_at TEXT,"
        "  category TEXT"
        ");";
    if (!query.exec(createUsers)) {
        qWarning() << "DatabaseManager: Failed to create users table:" << query.lastError().text();
        return false;
    }

    // comments テーブル
    QString createComments = 
        "CREATE TABLE IF NOT EXISTS comments ("
        "  comment_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id INTEGER,"
        "  user_id TEXT,"
        "  badge_info TEXT,"
        "  message TEXT NOT NULL,"
        "  received_at TEXT NOT NULL,"
        "  FOREIGN KEY(session_id) REFERENCES sessions(session_id),"
        "  FOREIGN KEY(user_id) REFERENCES users(user_id)"
        ");";
    if (!query.exec(createComments)) {
        qWarning() << "DatabaseManager: Failed to create comments table:" << query.lastError().text();
        return false;
    }

    // points_redemptions テーブル
    QString createRedemptions = 
        "CREATE TABLE IF NOT EXISTS points_redemptions ("
        "  redemption_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id TEXT,"
        "  reward_id TEXT NOT NULL,"
        "  reward_title TEXT NOT NULL,"
        "  input_text TEXT,"
        "  redeemed_at TEXT NOT NULL,"
        "  FOREIGN KEY(user_id) REFERENCES users(user_id)"
        ");";
    if (!query.exec(createRedemptions)) {
        qWarning() << "DatabaseManager: Failed to create points_redemptions table:" << query.lastError().text();
        return false;
    }

    // インデックス作成
    query.exec("CREATE INDEX IF NOT EXISTS idx_comments_session ON comments(session_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_comments_user ON comments(user_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_comments_received ON comments(received_at);");

    return true;
}

int DatabaseManager::startNewSession() {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO sessions (started_at) VALUES (:started_at);");
    query.bindValue(":started_at", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    
    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to start new session:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toInt();
}

void DatabaseManager::closeCurrentSession(int sessionId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET ended_at = :ended_at WHERE session_id = :session_id;");
    query.bindValue(":ended_at", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    query.bindValue(":session_id", sessionId);
    
    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to close session:" << query.lastError().text();
    }
}

bool DatabaseManager::saveComment(const TwitchComment& comment, int sessionId) {
    if (!m_db.isOpen()) return false;

    // トランザクション処理
    m_db.transaction();

    // 1. ユーザーマスタの UPSERT
    // TwitchComment 内の badges からカテゴリを推測
    QString category = "regular";
    QStringList badgeNames;
    for (int i = 0; i < comment.badges.size(); ++i) {
        QString name;
        QJsonValue val = comment.badges.at(i);
        if (val.isObject()) {
            name = val.toObject().value("name").toString();
        } else if (val.isString()) {
            name = val.toString();
        }

        if (!name.isEmpty()) {
            badgeNames.append(name);
            QString type = name.split('/').first().toLower();
            if (type == "broadcaster") category = "streamer";
            else if (type == "moderator" && category == "regular") category = "moderator";
            else if (type == "vip" && (category == "regular" || category == "moderator")) category = "vip";
            else if (type == "artist" && (category == "regular" || category == "moderator" || category == "vip")) category = "artist";
        }
    }

    QSqlQuery userQuery(m_db);
    userQuery.prepare(
        "INSERT INTO users (user_id, username, display_name, comment_count, last_active_at, category)"
        "VALUES (:user_id, :username, :display_name, 1, :last_active_at, :category)"
        "ON CONFLICT(user_id) DO UPDATE SET"
        "  username = excluded.username,"
        "  display_name = excluded.display_name,"
        "  comment_count = comment_count + 1,"
        "  last_active_at = excluded.last_active_at,"
        "  category = excluded.category;"
    );
    userQuery.bindValue(":user_id", comment.userId);
    userQuery.bindValue(":username", comment.username);
    userQuery.bindValue(":display_name", comment.displayName.isEmpty() ? comment.username : comment.displayName);
    userQuery.bindValue(":last_active_at", QDateTime::fromMSecsSinceEpoch(comment.timestamp).toString("yyyy-MM-dd HH:mm:ss"));
    userQuery.bindValue(":category", category);

    if (!userQuery.exec()) {
        qWarning() << "DatabaseManager: Failed to upsert user:" << userQuery.lastError().text();
        m_db.rollback();
        return false;
    }

    // 2. コメント履歴のインサート
    QSqlQuery commentQuery(m_db);
    commentQuery.prepare(
        "INSERT INTO comments (session_id, user_id, badge_info, message, received_at)"
        "VALUES (:session_id, :user_id, :badge_info, :message, :received_at);"
    );
    commentQuery.bindValue(":session_id", sessionId);
    commentQuery.bindValue(":user_id", comment.userId);
    commentQuery.bindValue(":badge_info", badgeNames.join(","));
    commentQuery.bindValue(":message", comment.comment);
    commentQuery.bindValue(":received_at", QDateTime::fromMSecsSinceEpoch(comment.timestamp).toString("yyyy-MM-dd HH:mm:ss"));

    if (!commentQuery.exec()) {
        qWarning() << "DatabaseManager: Failed to insert comment:" << commentQuery.lastError().text();
        m_db.rollback();
        return false;
    }

    m_db.commit();
    return true;
}

bool DatabaseManager::saveRedemption(const TwitchRewardRedemption& redemption) {
    if (!m_db.isOpen()) return false;

    m_db.transaction();

    // ユーザーがマスタに存在しない場合もあるので UPSERT を実施 (コメント数カウントは増やさない)
    QSqlQuery userQuery(m_db);
    userQuery.prepare(
        "INSERT INTO users (user_id, username, display_name, comment_count, last_active_at, category)"
        "VALUES (:user_id, :username, :display_name, 0, :last_active_at, 'regular')"
        "ON CONFLICT(user_id) DO UPDATE SET"
        "  username = excluded.username,"
        "  display_name = excluded.display_name,"
        "  last_active_at = excluded.last_active_at;"
    );
    userQuery.bindValue(":user_id", redemption.userId);
    userQuery.bindValue(":username", redemption.username);
    userQuery.bindValue(":display_name", redemption.displayName.isEmpty() ? redemption.username : redemption.displayName);
    userQuery.bindValue(":last_active_at", QDateTime::fromMSecsSinceEpoch(redemption.timestamp).toString("yyyy-MM-dd HH:mm:ss"));

    if (!userQuery.exec()) {
        qWarning() << "DatabaseManager: Failed to upsert user for redemption:" << userQuery.lastError().text();
        m_db.rollback();
        return false;
    }

    // ポイント引換履歴の追加
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO points_redemptions (user_id, reward_id, reward_title, input_text, redeemed_at)"
        "VALUES (:user_id, :reward_id, :reward_title, :input_text, :redeemed_at);"
    );
    query.bindValue(":user_id", redemption.userId);
    query.bindValue(":reward_id", redemption.rewardId);
    query.bindValue(":reward_title", redemption.rewardName);
    query.bindValue(":input_text", redemption.userInput);
    query.bindValue(":redeemed_at", QDateTime::fromMSecsSinceEpoch(redemption.timestamp).toString("yyyy-MM-dd HH:mm:ss"));

    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to insert redemption:" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    m_db.commit();
    return true;
}

bool DatabaseManager::exportSessionToCsv(int sessionId, const QString& filePath) {
    if (!m_db.isOpen()) return false;

    // 保存先ディレクトリの作成
    QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DatabaseManager: Failed to open CSV file for write:" << filePath;
        return false;
    }

    // BOMの書き込み (Excelの文字化け防止)
    file.write("\xEF\xBB\xBF", 3);

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    // ヘッダー書き込み
    out << "Timestamp,Username,DisplayName,Badges,Message\n";

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT c.received_at, u.username, u.display_name, c.badge_info, c.message "
        "FROM comments c "
        "JOIN users u ON c.user_id = u.user_id "
        "WHERE c.session_id = :session_id "
        "ORDER BY c.received_at ASC;"
    );
    query.bindValue(":session_id", sessionId);

    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to select comments for CSV:" << query.lastError().text();
        return false;
    }

    while (query.next()) {
        QString timestamp = query.value(0).toString();
        QString username = query.value(1).toString();
        QString displayName = query.value(2).toString();
        QString badges = query.value(3).toString();
        QString message = query.value(4).toString();

        // 改行をスペースに置換
        message.replace("\r\n", " ");
        message.replace("\n", " ");
        message.replace("\r", " ");

        // ダブルクォーテーションのエスケープと囲み
        message.replace("\"", "\"\"");
        
        // CSV出力
        out << "\"" << timestamp << "\","
            << "\"" << username << "\","
            << "\"" << displayName << "\","
            << "\"" << badges << "\","
            << "\"" << message << "\"\n";
    }

    file.close();
    return true;
}

int DatabaseManager::getTotalCommentCount(int sessionId) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM comments WHERE (:session_id <= 0 OR session_id = :session_id);");
    query.bindValue(":session_id", sessionId);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QList<QPair<QString, int>> DatabaseManager::getCommentRanking(int sessionId, int limit) const {
    QList<QPair<QString, int>> ranking;
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT u.display_name, COUNT(c.comment_id) as cnt "
        "FROM comments c "
        "JOIN users u ON c.user_id = u.user_id "
        "WHERE (:session_id <= 0 OR c.session_id = :session_id) "
        "GROUP BY c.user_id "
        "ORDER BY cnt DESC "
        "LIMIT :limit;"
    );
    query.bindValue(":session_id", sessionId);
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            ranking.append({query.value(0).toString(), query.value(1).toInt()});
        }
    }
    return ranking;
}

QList<QPair<QDateTime, int>> DatabaseManager::getCommentTrendTotal(int sessionId, int intervalSeconds) const {
    QList<QPair<QDateTime, int>> trend;
    QSqlQuery query(m_db);
    
    query.prepare(
        "SELECT received_at "
        "FROM comments "
        "WHERE (:session_id <= 0 OR session_id = :session_id) "
        "ORDER BY received_at ASC;"
    );
    query.bindValue(":session_id", sessionId);

    if (!query.exec()) return trend;

    int accumCount = 0;
    QDateTime slotStart;

    while (query.next()) {
        QDateTime dt = QDateTime::fromString(query.value(0).toString(), "yyyy-MM-dd HH:mm:ss");
        if (!slotStart.isValid()) {
            slotStart = dt;
        }

        while (slotStart.addSecs(intervalSeconds) <= dt) {
            trend.append({slotStart, accumCount});
            slotStart = slotStart.addSecs(intervalSeconds);
        }
        accumCount++;
    }
    if (slotStart.isValid()) {
        trend.append({slotStart, accumCount});
    }

    return trend;
}

QList<QPair<QString, QList<QPair<QDateTime, int>>>> DatabaseManager::getCommentTrendByUser(int sessionId, int limitUsers, int intervalSeconds) const {
    QList<QPair<QString, QList<QPair<QDateTime, int>>>> result;

    QSqlQuery topQuery(m_db);
    topQuery.prepare(
        "SELECT c.user_id, u.display_name "
        "FROM comments c "
        "JOIN users u ON c.user_id = u.user_id "
        "WHERE (:session_id <= 0 OR c.session_id = :session_id) "
        "GROUP BY c.user_id "
        "ORDER BY COUNT(c.comment_id) DESC "
        "LIMIT :limit;"
    );
    topQuery.bindValue(":session_id", sessionId);
    topQuery.bindValue(":limit", limitUsers);

    if (!topQuery.exec()) return result;

    struct TopUser {
        QString userId;
        QString displayName;
    };
    QList<TopUser> topUsers;
    while (topQuery.next()) {
        topUsers.append({topQuery.value(0).toString(), topQuery.value(1).toString()});
    }

    for (const auto& tu : topUsers) {
        QSqlQuery cQuery(m_db);
        cQuery.prepare(
            "SELECT received_at "
            "FROM comments "
            "WHERE (:session_id <= 0 OR session_id = :session_id) AND user_id = :user_id "
            "ORDER BY received_at ASC;"
        );
        cQuery.bindValue(":session_id", sessionId);
        cQuery.bindValue(":user_id", tu.userId);

        QList<QPair<QDateTime, int>> userTrend;
        if (cQuery.exec()) {
            int accum = 0;
            QDateTime slotStart;
            while (cQuery.next()) {
                QDateTime dt = QDateTime::fromString(cQuery.value(0).toString(), "yyyy-MM-dd HH:mm:ss");
                if (!slotStart.isValid()) {
                    slotStart = dt;
                }
                while (slotStart.addSecs(intervalSeconds) <= dt) {
                    userTrend.append({slotStart, accum});
                    slotStart = slotStart.addSecs(intervalSeconds);
                }
                accum++;
            }
            if (slotStart.isValid()) {
                userTrend.append({slotStart, accum});
            }
        }
        result.append({tu.displayName, userTrend});
    }

    return result;
}

QMap<QString, QStringList> DatabaseManager::getCategorizedActiveUsers(int sessionId) const {
    QMap<QString, QStringList> categorized;
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT u.username, u.category, GROUP_CONCAT(c.badge_info, ',') AS all_badges "
        "FROM comments c "
        "JOIN users u ON c.user_id = u.user_id "
        "WHERE (:session_id <= 0 OR c.session_id = :session_id) "
        "GROUP BY u.user_id, u.username, u.category "
        "ORDER BY u.username ASC;"
    );
    query.bindValue(":session_id", sessionId);

    if (query.exec()) {
        QStringList allList;
        QStringList streamerList;
        QStringList moderatorList;
        QStringList vipList;
        QStringList artistList;
        QStringList botList;
        QStringList regularList;

        while (query.next()) {
            QString name = query.value(0).toString();
            QString userCat = query.value(1).toString().toLower();
            QString allBadges = query.value(2).toString().toLower();

            allList.append(name);

            // Twitchバッジ情報および登録カテゴリから全該当ロールを判定（重複所属対応）
            bool isStreamer  = userCat == "streamer"  || allBadges.contains("broadcaster");
            bool isModerator = userCat == "moderator" || allBadges.contains("moderator");
            bool isVip       = userCat == "vip"       || allBadges.contains("vip");
            bool isArtist    = userCat == "artist"    || allBadges.contains("artist");
            bool isBot       = userCat == "bot"       || allBadges.contains("bot");

            if (isStreamer)  streamerList.append(name);
            if (isModerator) moderatorList.append(name);
            if (isVip)       vipList.append(name);
            if (isArtist)    artistList.append(name);
            if (isBot)       botList.append(name);

            // いずれの特別ロールにも属さない場合は一般視聴者
            if (!isStreamer && !isModerator && !isVip && !isArtist && !isBot) {
                regularList.append(name);
            }
        }

        categorized["all"] = allList;
        categorized["streamer"] = streamerList;
        categorized["moderator"] = moderatorList;
        categorized["vip"] = vipList;
        categorized["artist"] = artistList;
        categorized["bot"] = botList;
        categorized["regular"] = regularList;
    }

    return categorized;
}
