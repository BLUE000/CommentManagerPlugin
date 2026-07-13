# CommentManagerPlugin 詳細設計書

本ドキュメントは、TwitchChannelManagementTool（以下、ホストアプリ）のプラグインとして動作する「CommentManagerPlugin」の詳細設計（モジュール内部クラス設計、C++インターフェース、SQLiteデータベーススキーマ、UIレイアウト構造、通信プロトコル）を定義します。

---

## 1. クラス設計・詳細インターフェース

### 1.1. `CommentManagerPlugin` クラス
ホストアプリの `IChannelPlugin` インターフェースを実装するエントリーポイントクラスです。DLLの公開クラスとしてエクスポートされます。

```cpp
#pragma once
#include "shared/plugin_interface.h"
#include <QObject>

class DatabaseManager;
class ConfigManager;
class CommentManagerWidget;

class CommentManagerPlugin : public QObject, public IChannelPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IChannelPlugin_iid)
    Q_INTERFACES(IChannelPlugin)

public:
    CommentManagerPlugin();
    virtual ~CommentManagerPlugin() override;

    // IChannelPlugin 実装
    virtual void initialize(ICoreContext* context) override;
    virtual void shutdown() override;
    virtual QString pluginId() const override;
    virtual QString pluginName() const override;
    virtual QString pluginVersion() const override;
    virtual QString pluginDescription() const override;
    virtual QByteArray iconPngData() const override;
    virtual QMap<QString, QByteArray> defaultAssets() const override;
    virtual QWidget* createWidget(QWidget* parent = nullptr) override;
    virtual void onCommentReceived(const TwitchComment& comment) override;
    virtual void onRewardRedeemed(const TwitchRewardRedemption& redemption) override;
    virtual void onTick() override;

private:
    ICoreContext* m_context = nullptr;
    DatabaseManager* m_dbManager = nullptr;
    ConfigManager* m_configManager = nullptr;
    CommentManagerWidget* m_mainWidget = nullptr;
    int m_currentSessionId = -1;
};
```

---

### 1.2. `DatabaseManager` クラス
SQLiteデータベース（`users.db`）へのアクセスと操作を隠蔽し、スレッドセーフ（メインスレッドからの直列呼び出し想定）にデータを永続化・集計するクラスです。

```cpp
#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QDateTime>
#include <QMap>
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
    
    // トレンドデータ（intervalSeconds 単位での発言数推移）
    QList<QPair<QDateTime, int>> getCommentTrendTotal(int sessionId, int intervalSeconds = 60) const;
    QList<QPair<QDateTime, int>> getCommentTrendByUser(int sessionId, const QString& userId, int intervalSeconds = 60) const;

    // 視聴者リスト用カテゴリ別取得
    QMap<QString, QStringList> getCategorizedActiveUsers(int sessionId) const;

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    
    bool createTables();
    bool executeNonQuery(const QString& queryStr, const QVariantMap& placeholders = QVariantMap());
};
```

---

### 1.3. `ConfigManager` クラス
プラグイン専用の暗号化されたバイナリ設定ファイル（`config.bin`）をJSONにパースし、メモリ保持および同期保存を行うクラスです。

```cpp
#pragma once
#include <QStringList>
#include <QJsonObject>
#include "shared/plugin_interface.h"

struct PluginConfig {
    QStringList excludedUsers;     // コメント表示除外ユーザー
    QStringList excludedTtsUsers;  // TTS読み上げ除外ユーザー
    int ttsSpeakerId = 1;          // 話者ID
    double ttsSpeed = 1.0;         // 読み上げ速度 (0.5 - 2.5)
    double ttsPitch = 1.0;         // 読み上げ音程 (0.5 - 1.5)
    double ttsVolume = 0.8;        // 読み上げ音量 (0.0 - 1.0)
};

class ConfigManager {
public:
    explicit ConfigManager(ICoreContext* context);
    ~ConfigManager();

    bool loadConfig();
    bool saveConfig();

    const PluginConfig& getConfig() const { return m_config; }
    void setConfig(const PluginConfig& config);

    // 設定要素の個別操作
    void addExcludedUser(const QString& username);
    void removeExcludedUser(const QString& username);
    bool isExcludedUser(const QString& username) const;

    void addExcludedTtsUser(const QString& username);
    void removeExcludedTtsUser(const QString& username);
    bool isExcludedTtsUser(const QString& username) const;

private:
    ICoreContext* m_context = nullptr;
    PluginConfig m_config;
    QString m_configFileName = "config.bin";

    QJsonObject configToJson() const;
    void jsonToConfig(const QJsonObject& json);
};
```

---

### 1.4. `CommentManagerWidget` クラス (UI主コンポーネント)
プラグイン全体の画面描画・レイアウト、およびユーザー入力を制御するメイン GUI クラスです。

```cpp
#pragma once
#include <QWidget>
#include <QSplitter>
#include <QListView>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTreeView>
#include <QTableWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include "shared/plugin_interface.h"

class DatabaseManager;
class ConfigManager;

class CommentManagerWidget : public QWidget {
    Q_OBJECT
public:
    CommentManagerWidget(QWidget* parent, DatabaseManager* db, ConfigManager* cfg, ICoreContext* ctx);
    virtual ~CommentManagerWidget() override;

    void addComment(const TwitchComment& comment);
    void updateActiveViewers();
    void refreshAnalysis();

private slots:
    void onSendAnnouncement();
    void onViewerSelected(const QModelIndex& index);
    void onModerationAction(); // Shoutout, VIP, Mod, Timeout, BAN共通
    void onGraphSettingsChanged();
    void onSaveSettings();
    void onAddExcludedUser();
    void onRemoveExcludedUser();
    void onAddExcludedTtsUser();
    void onRemoveExcludedTtsUser();

private:
    DatabaseManager* m_db = nullptr;
    ConfigManager* m_cfg = nullptr;
    ICoreContext* m_ctx = nullptr;

    // UIポインタ
    QSplitter* m_splitter = nullptr;
    
    // 左ペイン
    QListView* m_commentListView = nullptr;
    QStandardItemModel* m_commentModel = nullptr;

    // 右ペイン
    QTabWidget* m_tabWidget = nullptr;
    
    // 右ペイン: 視聴者タブ
    QTreeView* m_viewerTreeView = nullptr;
    QStandardItemModel* m_viewerModel = nullptr;
    QPushButton* m_btnShoutout = nullptr;
    QPushButton* m_btnVip = nullptr;
    QPushButton* m_btnMod = nullptr;
    QPushButton* m_btnTimeout = nullptr;
    QPushButton* m_btnBan = nullptr;
    QString m_selectedViewerUsername;

    // 右ペイン: 解析タブ
    QComboBox* m_comboTargetSession = nullptr;
    QComboBox* m_comboGraphType = nullptr;
    QComboBox* m_comboLimit = nullptr;
    QChartView* m_chartView = nullptr;
    QChart* m_chart = nullptr;
    QTableWidget* m_rankingTable = nullptr;

    // 右ペイン: 設定タブ
    QLineEdit* m_editCommentOverlayUrl = nullptr;
    QLineEdit* m_editRankingOverlayUrl = nullptr;
    QListView* m_listExcludedUsers = nullptr;
    QListView* m_listExcludedTtsUsers = nullptr;
    QStandardItemModel* m_excludedUsersModel = nullptr;
    QStandardItemModel* m_excludedTtsUsersModel = nullptr;

    // 下部コントロール
    QComboBox* m_comboAnnounceColor = nullptr;
    QLineEdit* m_editAnnounceMessage = nullptr;
    QPushButton* m_btnSendAnnounce = nullptr;

    void setupUi();
    void setupLeftPane();
    void setupRightPane();
    void setupBottomPane();
    
    void loadSettingsToUi();
    void populateViewerTree(const QMap<QString, QStringList>& categorizedUsers);
};
```

---

## 2. データベース（`users.db`）詳細設計

### 2.1. スキーマ（DDL定義）

```sql
-- sessions テーブル (配信セッション管理)
CREATE TABLE IF NOT EXISTS sessions (
    session_id INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at TEXT NOT NULL,
    ended_at TEXT
);

-- users テーブル (ユーザーマスタ)
CREATE TABLE IF NOT EXISTS users (
    user_id TEXT PRIMARY KEY,
    username TEXT NOT NULL,
    display_name TEXT,
    comment_count INTEGER DEFAULT 0,
    last_active_at TEXT,
    category TEXT
);

-- comments テーブル (コメント履歴)
CREATE TABLE IF NOT EXISTS comments (
    comment_id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER,
    user_id TEXT,
    badge_info TEXT,
    message TEXT NOT NULL,
    received_at TEXT NOT NULL,
    FOREIGN KEY(session_id) REFERENCES sessions(session_id),
    FOREIGN KEY(user_id) REFERENCES users(user_id)
);

-- points_redemptions テーブル (チャンネルポイント引き換え履歴)
CREATE TABLE IF NOT EXISTS points_redemptions (
    redemption_id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT,
    reward_id TEXT NOT NULL,
    reward_title TEXT NOT NULL,
    input_text TEXT,
    redeemed_at TEXT NOT NULL,
    FOREIGN KEY(user_id) REFERENCES users(user_id)
);
```

### 2.2. インデックス設計
```sql
CREATE INDEX IF NOT EXISTS idx_comments_session ON comments(session_id);
CREATE INDEX IF NOT EXISTS idx_comments_user ON comments(user_id);
CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
CREATE INDEX IF NOT EXISTS idx_comments_received ON comments(received_at);
```

---

### 2.3. 主要な SQL クエリ

#### 1. ユーザーマスタの UPSERT
コメント受信時にユーザーマスタを更新または新規登録します。
```sql
INSERT INTO users (user_id, username, display_name, comment_count, last_active_at, category)
VALUES (:user_id, :username, :display_name, 1, :last_active_at, :category)
ON CONFLICT(user_id) DO UPDATE SET
    username = excluded.username,
    display_name = excluded.display_name,
    comment_count = comment_count + 1,
    last_active_at = excluded.last_active_at,
    category = excluded.category;
```

#### 2. コメントランキング集計
```sql
SELECT u.display_name, u.username, COUNT(c.comment_id) as cnt
FROM comments c
JOIN users u ON c.user_id = u.user_id
WHERE c.session_id = :session_id
GROUP BY c.user_id
ORDER BY cnt DESC
LIMIT :limit;
```

#### 3. 時系列総コメント数推移 (トレンド)
60秒（または指定の秒数）ごとにグループ化し、累積合計コメント数の時系列推移を計算します。
```sql
SELECT 
    strftime('%Y-%m-%dT%H:%M:00', c.received_at) as time_slot,
    COUNT(c.comment_id) as slot_count
FROM comments c
WHERE c.session_id = :session_id
GROUP BY time_slot
ORDER BY time_slot ASC;
```

---

## 3. 画面設計・UIレイアウト詳細

### 3.1. 誤クリック防止レイアウト (モデレーション操作パネル)
「視聴者」タブの個別操作エリアにおけるモデレーションボタン群は、以下の間隔で安全に配置します。

```
+-------------------------------------------------------------+
| [Shoutout] [VIP] [Moderator] | 余白 (1文字分) | [Timeout] [BAN] |
+-------------------------------------------------------------+
```
- **配置実装**:
  - `QHBoxLayout` を使用してボタンを配置。
  - `[Shoutout]`, `[VIP]`, `[Moderator]` はマージン 0 で密集配置。
  - その後に `QHBoxLayout::addSpacing(16)` （1文字相当の約 16px 余白）を挿入。
  - 最後に `[Timeout]`, `[BAN]` をマージン 0 で配置。

---

### 3.2. 解析グラフ表示設計 (`QtCharts`)
- `QChartView` を使用して、配信時間軸に応じたコメント推移を描画します。
- **表示推移切替**
  - **総コメント数推移**: 
    `QLineSeries` で、配信経過時間軸（X軸: `QDateTimeAxis`、Y軸: `QValueAxis`）に対して累積コメント数をプロットします。
  - **ユーザーごとの推移**:
    上位 N 人（ランキング上位）のユーザーごとに個別の `QLineSeries` を作成し、配信経過時間に応じた個々の累積コメント数を色分けしてプロットします。

---

## 4. OBS配信オーバーレイ・通信プロトコル仕様

本プラグインは、`defaultAssets()` で以下の HTML リソースを提供し、ホストアプリのローカルHTTPサーバーを通じてOBSへ配信します。
- `/assets/overlay/CommentManagerPlugin/default/comment.html`
- `/assets/overlay/CommentManagerPlugin/default/ranking.html`

### 4.1. WebSocket 送信プロトコル

#### 1. 新規コメント受信時 (`CM_EVENT_COMMENT`)
コメント受信をトリガーに、プラグインから `ICoreContext::sendToObs` を呼び出し、ブロードキャストします。

```json
{
  "event": "CM_EVENT_COMMENT",
  "data": {
    "username": "streamer_id",
    "displayName": "配信主名",
    "message": "チャットメッセージ本文",
    "badges": ["broadcaster", "subscriber"]
  }
}
```

#### 2. ランキング更新時 (`CM_EVENT_RANKING`)
`onTick()` の定周期処理（例: 10秒に1回）において、コメント数に変更があった場合に、最新ランキングをブロードキャストします。

```json
{
  "event": "CM_EVENT_RANKING",
  "data": [
    {"rank": 1, "displayName": "リスナーA", "count": 25},
    {"rank": 2, "displayName": "リスナーB", "count": 18},
    {"rank": 3, "displayName": "リスナーC", "count": 15}
  ]
}
```
