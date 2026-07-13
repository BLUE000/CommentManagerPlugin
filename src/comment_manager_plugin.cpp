#include "comment_manager_plugin.h"
#include "database_manager.h"
#include "config_manager.h"
#include "comment_manager_widget.h"
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

CommentManagerPlugin::CommentManagerPlugin() {
}

CommentManagerPlugin::~CommentManagerPlugin() {
}

void CommentManagerPlugin::initialize(ICoreContext* context) {
    m_context = context;
    if (!m_context) return;

    m_context->writeLog("Info", "CommentManagerPlugin", "initialize", "CommentManagerPlugin is starting...");

    // ディレクトリパスの解決
    QString baseDir = m_context->getPluginDirectory();
    if (baseDir.isEmpty()) {
        baseDir = ".";
    }

    // データベース初期化
    m_dbManager = new DatabaseManager();
    QString dbPath = baseDir + "/data/users.db";
    m_dbManager->initialize(dbPath);

    // 設定初期化
    m_configManager = new ConfigManager(m_context);
    m_configManager->loadConfig();

    // セッション開始
    m_sessionStartTime = QDateTime::currentDateTime();
    m_currentSessionId = m_dbManager->startNewSession();

    m_context->writeLog("Info", "CommentManagerPlugin", "initialize", 
        QString("CommentManagerPlugin started session ID: %1").arg(m_currentSessionId));
}

void CommentManagerPlugin::shutdown() {
    if (!m_context) return;

    m_context->writeLog("Info", "CommentManagerPlugin", "shutdown", "CommentManagerPlugin is shutting down...");

    // 1. セッションコメントを CSV にエクスポート
    if (m_currentSessionId != -1 && m_dbManager) {
        QString baseDir = m_context->getPluginDirectory();
        if (baseDir.isEmpty()) baseDir = ".";
        
        QString csvPath = baseDir + QString("/logs/session_%1.csv").arg(m_sessionStartTime.toString("yyyyMMdd_HHmmss"));
        m_dbManager->exportSessionToCsv(m_currentSessionId, csvPath);
        
        m_context->writeLog("Info", "CommentManagerPlugin", "shutdown", 
            QString("Session comments exported to: %1").arg(csvPath));

        m_dbManager->closeCurrentSession(m_currentSessionId);
    }

    // 2. 設定の保存
    if (m_configManager) {
        m_configManager->saveConfig();
        delete m_configManager;
        m_configManager = nullptr;
    }

    // 3. データベース切断
    if (m_dbManager) {
        m_dbManager->close();
        delete m_dbManager;
        m_dbManager = nullptr;
    }

    m_mainWidget = nullptr;
    m_context->writeLog("Info", "CommentManagerPlugin", "shutdown", "CommentManagerPlugin shutdown completed.");
}

QString CommentManagerPlugin::pluginId() const {
    return "com.blue000.twitchchannelmanagementtool.CommentManagerPlugin";
}

QString CommentManagerPlugin::pluginName() const {
    return "CommentManagerPlugin";
}

QString CommentManagerPlugin::pluginVersion() const {
    return "0.1.0"; // まだリリースしていないため1.0.0にはしない
}

QString CommentManagerPlugin::pluginDescription() const {
    return "コメントをデータベースに収集・蓄積し、視聴者動向を可視化する管理プラグインです。";
}

QByteArray CommentManagerPlugin::iconPngData() const {
    QFile file("pic/Comment.png");
    if (file.open(QIODevice::ReadOnly)) {
        return file.readAll();
    }
    // テスト環境等のためのパス検索
    QFile file2("../pic/Comment.png");
    if (file2.open(QIODevice::ReadOnly)) {
        return file2.readAll();
    }
    return QByteArray();
}

QMap<QString, QByteArray> CommentManagerPlugin::defaultAssets() const {
    QMap<QString, QByteArray> assets;
    
    // comment.html
    assets["comment.html"] = QByteArray(
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <style>\n"
        "    body { font-family: sans-serif; color: white; background: transparent; overflow: hidden; margin: 0; padding: 10px; }\n"
        "    #comments { display: flex; flex-direction: column; gap: 8px; }\n"
        "    .comment { background: rgba(0, 0, 0, 0.7); padding: 8px 12px; border-radius: 6px; font-size: 18px; animation: slideIn 0.3s ease-out; }\n"
        "    .user { font-weight: bold; color: #a970ff; margin-right: 8px; }\n"
        "    @keyframes slideIn { from { transform: translateX(-50px); opacity: 0; } to { transform: translateX(0); opacity: 1; } }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div id=\"comments\"></div>\n"
        "  <script>\n"
        "    const ws = new WebSocket('ws://localhost:8081/ws');\n"
        "    ws.onmessage = (event) => {\n"
        "      const msg = JSON.parse(event.data);\n"
        "      if (msg.type === 'CM_EVENT_COMMENT') {\n"
        "        const container = document.getElementById('comments');\n"
        "        const div = document.createElement('div');\n"
        "        div.className = 'comment';\n"
        "        div.innerHTML = `<span class=\"user\">${msg.data.displayName || msg.data.username}</span>${msg.data.message}`;\n"
        "        container.appendChild(div);\n"
        "        if (container.children.length > 10) { container.removeChild(container.firstChild); }\n"
        "      }\n"
        "    };\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n"
    );

    // ranking.html
    assets["ranking.html"] = QByteArray(
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <style>\n"
        "    body { font-family: sans-serif; color: white; background: transparent; overflow: hidden; margin: 0; padding: 10px; }\n"
        "    #ranking { display: flex; flex-direction: column; gap: 6px; }\n"
        "    .rank-item { display: flex; justify-content: space-between; background: rgba(0,0,0,0.6); padding: 6px 12px; border-radius: 4px; font-size: 16px; }\n"
        "    .name { font-weight: bold; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <h2>コメント数ランキング</h2>\n"
        "  <div id=\"ranking\"></div>\n"
        "  <script>\n"
        "    const ws = new WebSocket('ws://localhost:8081/ws');\n"
        "    ws.onmessage = (event) => {\n"
        "      const msg = JSON.parse(event.data);\n"
        "      if (msg.type === 'CM_EVENT_RANKING') {\n"
        "        const container = document.getElementById('ranking');\n"
        "        container.innerHTML = '';\n"
        "        msg.data.list.forEach(item => {\n"
        "          const div = document.createElement('div');\n"
        "          div.className = 'rank-item';\n"
        "          div.innerHTML = `<span class=\"name\">${item.rank}. ${item.displayName}</span><span>${item.count} 回</span>`;\n"
        "          container.appendChild(div);\n"
        "        });\n"
        "      }\n"
        "    };\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n"
    );

    return assets;
}

QWidget* CommentManagerPlugin::createWidget(QWidget* parent) {
    if (!m_mainWidget) {
        m_mainWidget = new CommentManagerWidget(parent, m_dbManager, m_configManager, m_context);
    }
    return m_mainWidget;
}

void CommentManagerPlugin::onCommentReceived(const TwitchComment& comment) {
    if (!m_dbManager || !m_configManager || !m_context) return;

    // 1. 表示除外ユーザー判定
    if (m_configManager->isExcludedUser(comment.username)) {
        return; 
    }

    // 2. コメント保存
    if (!m_dbManager->saveComment(comment, m_currentSessionId)) {
        return;
    }

    // 3. UI表示更新
    if (m_mainWidget) {
        m_mainWidget->addComment(comment);
    }

    // 4. OBSへWebSocketブロードキャスト
    QJsonObject data;
    data["username"] = comment.username;
    data["displayName"] = comment.displayName.isEmpty() ? comment.username : comment.displayName;
    data["message"] = comment.comment;
    
    QJsonArray badgeArr;
    for (int i = 0; i < comment.badges.size(); ++i) {
        badgeArr.append(comment.badges.at(i).toString());
    }
    data["badges"] = badgeArr;
    
    m_context->sendToObs("CM_EVENT_COMMENT", data);

    // 5. TTS読み上げ
    if (!m_configManager->isExcludedTtsUser(comment.username)) {
        const PluginConfig& cfg = m_configManager->getConfig();
        // speed, pitch, volume はパーセンテージ(整数)に変換して送信
        int sp = static_cast<int>(cfg.ttsSpeed * 100);
        int pi = static_cast<int>(cfg.ttsPitch * 100);
        int vo = static_cast<int>(cfg.ttsVolume * 100);
        m_context->requestTts(comment.comment, QString::number(cfg.ttsSpeakerId), sp, pi, vo);
    }

    m_context->writeLog("Info", "CommentManagerPlugin", "onCommentReceived", 
        QString("Processed comment from: %1").arg(comment.username));
}

void CommentManagerPlugin::onRewardRedeemed(const TwitchRewardRedemption& redemption) {
    if (!m_dbManager || !m_context) return;

    m_dbManager->saveRedemption(redemption);
    
    m_context->writeLog("Info", "CommentManagerPlugin", "onRewardRedeemed", 
        QString("Redemption saved for user: %1, reward: %2").arg(redemption.username, redemption.rewardName));
}

void CommentManagerPlugin::onTick() {
    m_tickCount++;
    
    // 10秒に1回、コメント数に変化があれば最新ランキングをOBSにブロードキャスト
    if (m_tickCount % 10 == 0 && m_dbManager && m_context) {
        int currentCount = m_dbManager->getTotalCommentCount(m_currentSessionId);
        if (currentCount != m_lastCommentCount) {
            m_lastCommentCount = currentCount;
            
            QList<QPair<QString, int>> ranking = m_dbManager->getCommentRanking(m_currentSessionId, 10);
            QJsonArray rankArr;
            for (int i = 0; i < ranking.size(); ++i) {
                QJsonObject item;
                item["rank"] = i + 1;
                item["displayName"] = ranking[i].first;
                item["count"] = ranking[i].second;
                rankArr.append(item);
            }

            QJsonObject rankingPayload;
            rankingPayload["list"] = rankArr;
            m_context->sendToObs("CM_EVENT_RANKING", rankingPayload);
        }
        
        // UIが表示中なら視聴者リストと解析を自動更新
        if (m_mainWidget) {
            m_mainWidget->updateActiveViewers();
            m_mainWidget->refreshAnalysis();
        }
    }
}
