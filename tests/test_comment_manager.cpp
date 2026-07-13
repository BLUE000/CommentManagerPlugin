#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QSpacerItem>
#include <QGroupBox>
#include "database_manager.h"
#include "config_manager.h"
#include "comment_manager_widget.h"
#include "comment_manager_plugin.h"

// --- MockCoreContext の実装 ---
class MockCoreContext : public ICoreContext {
public:
    QList<QString> sentMessages;
    struct TtsRequest {
        QString text;
        QString speakerId;
        int speed;
        int pitch;
        int volume;
    };
    QList<TtsRequest> sentTtsRequests;

    struct ObsRequest {
        QString action;
        QJsonObject payload;
    };
    QList<ObsRequest> sentObsRequests;

    struct WebhookRequest {
        QString url;
        QJsonObject payload;
    };
    QList<WebhookRequest> sentWebhookRequests;

    struct LogEntry {
        QString level;
        QString className;
        QString funcName;
        QString description;
    };
    QList<LogEntry> loggedEntries;

    QMap<QString, QByteArray> mockFiles;
    QString mockPluginDir = "./mock_plugin_dir";
    QString mockCipherKey = "mock_secret_key";

    virtual void sendChatMessage(const QString& message) override {
        sentMessages.append(message);
    }

    virtual void requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume) override {
        sentTtsRequests.append({text, speakerId, speed, pitch, volume});
    }

    virtual void sendToObs(const QString& action, const QJsonObject& payload) override {
        sentObsRequests.append({action, payload});
    }

    virtual void postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload) override {
        sentWebhookRequests.append({webhookUrl, payload});
    }

    virtual QString getPluginDirectory() const override { return mockPluginDir; }
    virtual QString getCipherKey() const override { return mockCipherKey; }

    virtual bool writeEncryptedFile(const QString& relativePath, const QByteArray& data) override {
        mockFiles[relativePath] = data;
        return true;
    }

    virtual QByteArray readEncryptedFile(const QString& relativePath) override {
        return mockFiles.value(relativePath);
    }

    virtual void writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) override {
        loggedEntries.append({level, className, funcName, description});
    }
};

// --- GTest フィクスチャ ---
class CommentManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // テスト用の一時フォルダを作成・クリーン
        QDir().mkpath("./mock_plugin_dir/data");
        QDir().mkpath("./mock_plugin_dir/logs");
    }

    void TearDown() override {
        // 一時フォルダをクリーンアップ
        QDir("./mock_plugin_dir").removeRecursively();
    }
};

// ========================================================
// 2.1. DatabaseManager の単体試験 (UT-DB)
// ========================================================

TEST_F(CommentManagerTest, UT_DB_01_Initialize) {
    DatabaseManager db;
    // メモリDBで初期化
    EXPECT_TRUE(db.initialize(":memory:"));
}

TEST_F(CommentManagerTest, UT_DB_02_StartNewSession) {
    DatabaseManager db;
    ASSERT_TRUE(db.initialize(":memory:"));
    
    int sid = db.startNewSession();
    EXPECT_GT(sid, 0);
}

TEST_F(CommentManagerTest, UT_DB_03_SaveCommentNewUser) {
    DatabaseManager db;
    ASSERT_TRUE(db.initialize(":memory:"));
    int sid = db.startNewSession();

    TwitchComment comment;
    comment.userId = "1001";
    comment.username = "user1";
    comment.displayName = "ユーザー1";
    comment.comment = "こんにちは！";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();
    comment.badges = QJsonArray{"moderator"};

    EXPECT_TRUE(db.saveComment(comment, sid));
    EXPECT_EQ(db.getTotalCommentCount(sid), 1);
    
    auto ranking = db.getCommentRanking(sid, 10);
    ASSERT_EQ(ranking.size(), 1);
    EXPECT_EQ(ranking[0].first, "ユーザー1");
    EXPECT_EQ(ranking[0].second, 1);
}

TEST_F(CommentManagerTest, UT_DB_04_SaveCommentRepeat) {
    DatabaseManager db;
    ASSERT_TRUE(db.initialize(":memory:"));
    int sid = db.startNewSession();

    TwitchComment comment;
    comment.userId = "1001";
    comment.username = "user1";
    comment.displayName = "ユーザー1";
    comment.comment = "こんにちは！";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();

    EXPECT_TRUE(db.saveComment(comment, sid));
    
    comment.comment = "連投です。";
    EXPECT_TRUE(db.saveComment(comment, sid));

    EXPECT_EQ(db.getTotalCommentCount(sid), 2);
    auto ranking = db.getCommentRanking(sid, 10);
    ASSERT_EQ(ranking.size(), 1);
    EXPECT_EQ(ranking[0].second, 2);
}

TEST_F(CommentManagerTest, UT_DB_05_SaveRedemption) {
    DatabaseManager db;
    ASSERT_TRUE(db.initialize(":memory:"));

    TwitchRewardRedemption red;
    red.userId = "1001";
    red.username = "user1";
    red.displayName = "ユーザー1";
    red.rewardId = "reward_id_abc";
    red.rewardName = "テスト報酬";
    red.userInput = "メッセージ";
    red.timestamp = QDateTime::currentMSecsSinceEpoch();

    EXPECT_TRUE(db.saveRedemption(red));
}

TEST_F(CommentManagerTest, UT_DB_06_GetCommentRanking) {
    DatabaseManager db;
    ASSERT_TRUE(db.initialize(":memory:"));
    int sid = db.startNewSession();

    TwitchComment comment1;
    comment1.userId = "1001";
    comment1.username = "user1";
    comment1.displayName = "ユーザー1";
    comment1.comment = "A";
    comment1.timestamp = QDateTime::currentMSecsSinceEpoch();
    db.saveComment(comment1, sid);

    TwitchComment comment2;
    comment2.userId = "1002";
    comment2.username = "user2";
    comment2.displayName = "ユーザー2";
    comment2.comment = "B";
    comment2.timestamp = QDateTime::currentMSecsSinceEpoch();
    db.saveComment(comment2, sid);
    db.saveComment(comment2, sid); // user2 は2回発言

    auto ranking = db.getCommentRanking(sid, 10);
    ASSERT_EQ(ranking.size(), 2);
    EXPECT_EQ(ranking[0].first, "ユーザー2");
    EXPECT_EQ(ranking[0].second, 2);
    EXPECT_EQ(ranking[1].first, "ユーザー1");
    EXPECT_EQ(ranking[1].second, 1);
}

TEST_F(CommentManagerTest, UT_DB_07_ExportSessionToCsv) {
    DatabaseManager db;
    ASSERT_TRUE(db.initialize(":memory:"));
    int sid = db.startNewSession();

    TwitchComment comment;
    comment.userId = "1001";
    comment.username = "user1";
    comment.displayName = "ユーザー1";
    comment.comment = "こんにちは！\n改行あり。";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();
    db.saveComment(comment, sid);

    QString csvPath = "./mock_plugin_dir/logs/test_session.csv";
    EXPECT_TRUE(db.exportSessionToCsv(sid, csvPath));
    
    QFile file(csvPath);
    EXPECT_TRUE(file.exists());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray data = file.readAll();
    file.close();

    // BOM (EF BB BF) を含むか確認
    EXPECT_EQ(data.left(3), QByteArray("\xEF\xBB\xBF"));

    // 改行がスペースに置換されていることを検証
    QString csvText = QString::fromUtf8(data);
    EXPECT_TRUE(csvText.contains("こんにちは！ 改行あり。"));
}

// ========================================================
// 2.2. ConfigManager の単体試験 (UT-CFG)
// ========================================================

TEST_F(CommentManagerTest, UT_CFG_01_LoadConfigDefault) {
    MockCoreContext ctx;
    ConfigManager cfg(&ctx);
    
    EXPECT_TRUE(cfg.loadConfig());
    EXPECT_EQ(cfg.getConfig().ttsSpeed, 1.0);
    EXPECT_EQ(cfg.getConfig().ttsVolume, 0.8);
}

TEST_F(CommentManagerTest, UT_CFG_02_SaveAndLoadConfig) {
    MockCoreContext ctx;
    {
        ConfigManager cfg(&ctx);
        cfg.loadConfig();
        PluginConfig pcfg = cfg.getConfig();
        pcfg.ttsSpeed = 1.8;
        pcfg.excludedUsers.append("spammer");
        cfg.setConfig(pcfg);
        EXPECT_TRUE(cfg.saveConfig());
    }

    {
        ConfigManager cfg2(&ctx);
        EXPECT_TRUE(cfg2.loadConfig());
        EXPECT_EQ(cfg2.getConfig().ttsSpeed, 1.8);
        EXPECT_TRUE(cfg2.isExcludedUser("spammer"));
    }
}

TEST_F(CommentManagerTest, UT_CFG_03_AddExcludedUserCaseInsensitive) {
    MockCoreContext ctx;
    ConfigManager cfg(&ctx);
    
    cfg.addExcludedUser("SpAmMeR");
    EXPECT_TRUE(cfg.isExcludedUser("spammer"));
    EXPECT_TRUE(cfg.isExcludedUser("SPAMMER"));
}

// ========================================================
// 2.3. CommentManagerWidget (UIロジック) の単体試験 (UT-UI)
// ========================================================

TEST_F(CommentManagerTest, UT_UI_01_AddCommentNormal) {
    MockCoreContext ctx;
    DatabaseManager db;
    ConfigManager cfg(&ctx);
    
    db.initialize(":memory:");
    cfg.loadConfig();

    CommentManagerWidget widget(nullptr, &db, &cfg, &ctx);
    
    TwitchComment comment;
    comment.userId = "1001";
    comment.username = "user1";
    comment.displayName = "ユーザー1";
    comment.comment = "こんにちは！";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();

    widget.addComment(comment);
    
    // リストビューにモデル項目が1件追加されていることを検証
    auto* model = widget.findChild<QStandardItemModel*>();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->rowCount(), 1);
}

TEST_F(CommentManagerTest, UT_UI_02_AddCommentFiltered) {
    MockCoreContext ctx;
    DatabaseManager db;
    ConfigManager cfg(&ctx);
    
    db.initialize(":memory:");
    cfg.loadConfig();
    cfg.addExcludedUser("spammer");

    CommentManagerWidget widget(nullptr, &db, &cfg, &ctx);
    
    TwitchComment comment;
    comment.userId = "1001";
    comment.username = "spammer";
    comment.displayName = "迷惑ユーザー";
    comment.comment = "スパムコメント";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();

    widget.addComment(comment);
    
    auto* model = widget.findChild<QStandardItemModel*>();
    ASSERT_NE(model, nullptr);
    // スパマーは除外されているので画面のリスト数は0のまま
    EXPECT_EQ(model->rowCount(), 0);
}

TEST_F(CommentManagerTest, UT_UI_03_SendAnnouncement) {
    MockCoreContext ctx;
    DatabaseManager db;
    ConfigManager cfg(&ctx);
    db.initialize(":memory:");
    cfg.loadConfig();

    CommentManagerWidget widget(nullptr, &db, &cfg, &ctx);
    
    auto* lineEdit = widget.findChild<QLineEdit*>("editAnnounceMessage");
    ASSERT_NE(lineEdit, nullptr);
    lineEdit->setText("Hello World");
    
    auto* combo = widget.findChild<QComboBox*>("comboAnnounceColor");
    ASSERT_NE(combo, nullptr);
    // 色プルダウンで「青(Blue)」を選択
    combo->setCurrentIndex(1); // comboのインデックス1はblue

    auto* btn = widget.findChild<QPushButton*>("btnSendAnnounce");
    // ボタンをクリックさせるか、直接 slot をコール
    // コントロール下のスロットをテストするために直接メソッド呼出
    QMetaObject::invokeMethod(&widget, "onSendAnnouncement");

    ASSERT_EQ(ctx.sentMessages.size(), 1);
    EXPECT_EQ(ctx.sentMessages[0], "/announceblue Hello World");
}

TEST_F(CommentManagerTest, UT_UI_04_ModerationAction) {
    MockCoreContext ctx;
    DatabaseManager db;
    ConfigManager cfg(&ctx);
    db.initialize(":memory:");
    cfg.loadConfig();

    int sid = db.startNewSession();
    
    // DBにモデレーターのコメントを投入
    TwitchComment comment;
    comment.userId = "1002";
    comment.username = "mod_user";
    comment.displayName = "mod_user";
    comment.comment = "hello";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();
    comment.badges = QJsonArray{"moderator"};
    db.saveComment(comment, sid);

    CommentManagerWidget widget(nullptr, &db, &cfg, &ctx);
    widget.updateActiveViewers();
    
    // 直接選択したユーザーのプロパティを模倣
    // 選択されたユーザーに対するモデレーションシミュレート
    // m_selectedViewerUsername に値を注入するためにリフレクションするか
    // widget.m_selectedViewerUsername は private。
    // 代わりにツリービューでアイテムを選択した状態をシミュレート
    QTreeView* tree = widget.findChild<QTreeView*>();
    ASSERT_NE(tree, nullptr);
    
    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(tree->model());
    ASSERT_NE(model, nullptr);

    // 親項目（モデレーター）の子のmod_userを選択
    QModelIndex catIndex = model->index(2, 0); // index 2 = モデレーター
    QModelIndex userIndex = model->index(0, 0, catIndex);
    ASSERT_TRUE(userIndex.isValid());

    tree->setCurrentIndex(userIndex);
    QMetaObject::invokeMethod(&widget, "onViewerSelected", Q_ARG(QModelIndex, userIndex));

    // BAN ボタンを実行
    QMetaObject::invokeMethod(&widget, "onModerationAction"); 
    // sender() が nullptr になるため、直接呼び出す代わりにスロットを個別にシミュレート
    // widgetの実装上、onModerationActionはsender()を使用。
    // そのためテスト内でダミーのQPushButtonオブジェクトからシグナルをトリガーする。
    QPushButton* btnBan = widget.findChild<QPushButton*>(); // 最初に見つかったボタン
    // "BAN"ボタンを特定
    QList<QPushButton*> buttons = widget.findChildren<QPushButton*>();
    QPushButton* targetBtn = nullptr;
    for (auto* b : buttons) {
        if (b->text() == "BAN") {
            targetBtn = b;
            break;
        }
    }
    ASSERT_NE(targetBtn, nullptr);
    targetBtn->click();

    ASSERT_EQ(ctx.sentMessages.size(), 1);
    EXPECT_EQ(ctx.sentMessages[0], "/ban mod_user");
}

TEST_F(CommentManagerTest, UT_UI_05_ButtonLayoutSpacing) {
    MockCoreContext ctx;
    DatabaseManager db;
    ConfigManager cfg(&ctx);
    db.initialize(":memory:");
    cfg.loadConfig();

    CommentManagerWidget widget(nullptr, &db, &cfg, &ctx);
    
    QGroupBox* opGroupBox = widget.findChild<QGroupBox*>("opGroupBox");
    ASSERT_NE(opGroupBox, nullptr);
    
    QHBoxLayout* opLayout = qobject_cast<QHBoxLayout*>(opGroupBox->layout());
    ASSERT_NE(opLayout, nullptr);

    // レイアウトの構造検証:
    // [Shoutout] [VIP] [Moderator] [Spacing 16px] [Timeout] [BAN]
    // Spacingが正しく追加されているか
    bool foundSpacing = false;
    for (int i = 0; i < opLayout->count(); ++i) {
        QLayoutItem* item = opLayout->itemAt(i);
        if (item->spacerItem() && item->spacerItem()->sizeHint().width() == 16) {
            foundSpacing = true;
            break;
        }
    }
    EXPECT_TRUE(foundSpacing);
}

// ========================================================
// 2.4. CommentManagerPlugin の単体試験 (UT-PLG)
// ========================================================

TEST_F(CommentManagerTest, UT_PLG_01_InitializeShutdown) {
    MockCoreContext ctx;
    CommentManagerPlugin plugin;
    
    plugin.initialize(&ctx);
    EXPECT_EQ(plugin.pluginVersion(), "0.1.0");

    plugin.shutdown();
}

TEST_F(CommentManagerTest, UT_PLG_02_Metadata) {
    MockCoreContext ctx;
    CommentManagerPlugin plugin;
    
    EXPECT_EQ(plugin.pluginId(), "com.blue000.twitchchannelmanagementtool.CommentManagerPlugin");
    EXPECT_EQ(plugin.pluginName(), "CommentManagerPlugin");
    
    auto assets = plugin.defaultAssets();
    EXPECT_TRUE(assets.contains("comment.html"));
    EXPECT_TRUE(assets.contains("ranking.html"));
}

TEST_F(CommentManagerTest, UT_PLG_03_OnTickPeriodical) {
    MockCoreContext ctx;
    CommentManagerPlugin plugin;
    
    plugin.initialize(&ctx);

    TwitchComment comment;
    comment.userId = "1001";
    comment.username = "user1";
    comment.displayName = "ユーザー1";
    comment.comment = "テストコメント";
    comment.timestamp = QDateTime::currentMSecsSinceEpoch();

    plugin.onCommentReceived(comment);

    // tick を10回実行して定期ランキングブロードキャストを発火させる
    for (int i = 0; i < 10; ++i) {
        plugin.onTick();
    }

    // OBSへランキングイベントが送信されたか検証
    bool foundRanking = false;
    for (const auto& req : ctx.sentObsRequests) {
        if (req.action == "CM_EVENT_RANKING") {
            foundRanking = true;
            break;
        }
    }
    EXPECT_TRUE(foundRanking);

    plugin.shutdown();
}

// --- テストメイン関数の定義 ---
int main(int argc, char* argv[]) {
    // GUIテストのために QApplication をインスタンス化
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
