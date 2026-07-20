# CommentManagerPlugin 単体試験仕様書

本仕様書は、「CommentManagerPlugin」における各モジュール（データベース、設定管理、UIロジック、プラグイン本体）の単体機能を自動検証するためのテスト設計および試験項目を定義します。

---

## 1. 自動テスト用モック設計

自動テストの実行時、ホストアプリ本体の動作に依存せずにプラグインを単体検証するため、`ICoreContext` をシミュレートする `MockCoreContext` を作成します。

### 1.1. `MockCoreContext` 設計

```cpp
#pragma once
#include "shared/plugin_interface.h"
#include <QMap>
#include <QList>

class MockCoreContext : public ICoreContext {
public:
    // 送信ログ追跡用メンバ
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

    // ファイルI/Oモック用バッファ
    QMap<QString, QByteArray> mockFiles;
    QString mockPluginDir = "./mock_plugin_dir";
    QString mockCipherKey = "mock_secret_key";

    // ICoreContext 仮想関数オーバーライド
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
```

---

## 2. 単体試験項目一覧

### 2.1. `DatabaseManager` の単体試験 (UT-DB)

データベース接続、テーブル生成、データ読み書き、セッション管理、および集計APIの検証を行います。

| 試験ID | 対象メソッド | 試験条件 | 期待される結果 (アサート項目) |
| :--- | :--- | :--- | :--- |
| **UT-DB-01** | `initialize` | メモリデータベース（`":memory:"`）を指定して実行。 | 1. 戻り値が `true` であること。<br>2. 必要な全4テーブルおよびインデックスが生成されること。 |
| **UT-DB-02** | `startNewSession` | 新規セッションを開始する。 | 1. セッションID（1〜の整数）が取得できること。<br>2. `sessions` テーブルに開始時刻付きでレコードが挿入されること。 |
| **UT-DB-03** | `saveComment` | 存在しないユーザーの `TwitchComment` を保存する。 | 1. 戻り値が `true` であること。<br>2. `users` テーブルに該当ユーザーが新規追加され `comment_count=1` になること。<br>3. `comments` テーブルに発言内容が正しく追加されること。 |
| **UT-DB-04** | `saveComment` (リピート) | 同一ユーザーから2回目の `TwitchComment` を保存する。 | 1. `users` テーブルの既存レコードの `comment_count` が `2` に加算更新されること。<br>2. `comments` テーブルのレコード数が `2` になること。 |
| **UT-DB-05** | `saveRedemption` | チャンネルポイント利用情報を保存する。 | 1. 戻り値が `true` であること。<br>2. `points_redemptions` テーブルにデータが正しく追加されること。 |
| **UT-DB-06** | `getCommentRanking` | 同一セッション内で複数ユーザーからコメントを流した後に呼び出す。 | コメント数の多い順（降順）で指定した上限数（`limit`）までのユーザー名と発言数が取得できること。 |
| **UT-DB-07** | `exportSessionToCsv` | セッションIDを指定し、一時ファイルパスへエクスポートする。 | 1. 戻り値が `true` であること。<br>2. エクスポートされたCSVファイルがBOM付きUTF-8であること。<br>3. メッセージ内の改行がスペースに置換され、全体がダブルクォーテーションで囲まれていること。 |

---

### 2.2. `ConfigManager` の単体試験 (UT-CFG)

JSONデータシリアライズと、`ICoreContext` の暗号化ファイル I/O を利用した設定の読み書きの検証を行います。

| 試験ID | 対象メソッド | 試験条件 | 期待される結果 (アサート項目) |
| :--- | :--- | :--- | :--- |
| **UT-CFG-01** | `loadConfig` (初期状態) | 暗号化ファイルが存在しない状態で `loadConfig` を実行。 | 1. 戻り値が `true` であること（ファイルが無い場合はエラーとせずデフォルト設定を読み込む仕様）。<br>2. 設定値がデフォルト（TTS速度=1.0、音量=0.8等）にセットされること。 |
| **UT-CFG-02** | `saveConfig` ＆ `loadConfig` | 設定データを変更して `saveConfig` し、その後再ロードする。 | 1. `MockCoreContext` に暗号化ファイルとしてバッファが書き込まれること。<br>2. 再ロード後、変更した設定（除外ユーザーリスト等）が完全に復元されること。 |
| **UT-CFG-03** | `addExcludedUser` | 特定のユーザー `"spammer"` を除外リストに追加する。 | 1. `isExcludedUser("spammer")` が `true` を返すこと。<br>2. 大文字小文字を区別せず `"SPAMMER"` でも `true` を返すこと。 |
| **UT-CFG-04** | `overlayTheme` | `overlayTheme` を `"custom_theme"` に変更して `saveConfig` ＆ `loadConfig` を実行。 | 1. 初期状態のデフォルト値が `"default"` であること。<br>2. 設定変更後に保存・再ロードした際、`"custom_theme"` が正しく復元されること。 |

---

### 2.3. `CommentManagerWidget` (UIロジック) の単体試験 (UT-UI)

GUI部品の生成、シグナル・スロット、表示フィルタ処理、および誤クリック防止余白の整合性を検証します。

| 試験ID | 対象メソッド | 試験条件 | 期待される結果 (アサート項目) |
| :--- | :--- | :--- | :--- |
| **UT-UI-01** | `addComment` (通常表示) | 除外対象外のユーザーのコメントを受信する。 | `CommentListWidget` にコメントが表示されること。 |
| **UT-UI-02** | `addComment` (フィルタリング) | `excludedUsers` に登録されたユーザーからのコメントを受信する。 | 1. `CommentListWidget` のモデルデータ数が変化しないこと（非表示・破棄されること）。<br>2. データベースにはコメントが正常に保存されること（画面上だけ非表示にする仕様）。 |
| **UT-UI-03** | `onSendAnnouncement` | 色 `"Blue"`、メッセージ `"Hello World"` を入力して送信ボタンを押す。 | `MockCoreContext` に Twitch コマンド `"/announceblue Hello World"` が送信要求されること。 |
| **UT-UI-04** | `onModerationAction` | 視聴者 treeview で `"viewer_x"` を選択し、`[Ban]` ボタンをクリックする。 | `MockCoreContext` に Twitch コマンド `"/ban viewer_x"` が送信要求されること。 |
| **UT-UI-05** | ボタンレイアウト検証 | モデレーション操作パネルのボタン構成を取得。 | 1. `[Shoutout]`, `[VIP]`, `[Moderator]` の連続したボタンの後に、1文字相当の幅（約16px）を持つ空のスペース（`addSpacing`）が存在すること。<br>2. その後に `[Timeout]`, `[BAN]` が配置されていること。 |

---

### 2.4. `CommentManagerPlugin` の単体試験 (UT-PLG)

プラグインとしてのメタ情報およびライフサイクルの検証を行います。

| 試験ID | 対象メソッド | 試験条件 | 期待される結果 (アサート項目) |
| :--- | :--- | :--- | :--- |
| **UT-PLG-01** | `initialize` / `shutdown` | `MockCoreContext` を用いて初期化し、その後破棄する。 | 1. 初期化時にDBおよび設定が読み込まれ、正常動作ログ（`"Info"`）が書き込まれること。<br>2. 破棄時にDBセッションが終了し、設定とCSVエクスポートが実行され、安全にクローズされること。 |
| **UT-PLG-02** | メタ情報メソッド | `pluginId()`, `pluginName()`, `iconPngData()`, `defaultAssets()` 等を呼ぶ。 | 1. IDとして `"com.blue000.twitchchannelmanagementtool.CommentManagerPlugin"` が返ること。<br>2. `iconPngData()` が同期ファイル I/O なしに有効な PNG バイナリデータ（非空）を即時に返却すること。<br>3. デフォルトアセットとして `comment.html` および `ranking.html` のバイナリデータがマップに格納されていること。 |
| **UT-PLG-03** | `onTick` ＆ 定期配信 | 複数コメントを受信した状態で `onTick` （1秒に1回）が発生する。 | 10秒ごとのタイミングにおいて、OBSに対して最新のランキングデータ（`CM_EVENT_RANKING`）が WebSocket ブロードキャスト経由で送信されること。 |
