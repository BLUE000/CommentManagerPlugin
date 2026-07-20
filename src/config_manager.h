#pragma once
#include <QStringList>
#include <QJsonObject>
#include "shared/plugin_interface.h"

struct PluginConfig {
    QString overlayTheme = "default"; // OBSオーバーレイテーマ（スキン）名
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
