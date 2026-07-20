#include "config_manager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

ConfigManager::ConfigManager(ICoreContext* context) : m_context(context) {
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadConfig() {
    if (!m_context) return false;

    QByteArray data = m_context->readEncryptedFile(m_configFileName);
    if (data.isEmpty()) {
        // ファイルが無い場合はデフォルト設定を適用し、true を返す仕様
        m_config = PluginConfig();
        return true;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        m_context->writeLog("Warning", "ConfigManager", "loadConfig", 
            QString("Failed to parse config JSON: %1").arg(err.errorString()));
        m_config = PluginConfig();
        return true;
    }

    if (doc.isObject()) {
        jsonToConfig(doc.object());
    }

    return true;
}

bool ConfigManager::saveConfig() {
    if (!m_context) return false;

    QJsonObject obj = configToJson();
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    bool ok = m_context->writeEncryptedFile(m_configFileName, data);
    if (!ok) {
        m_context->writeLog("Error", "ConfigManager", "saveConfig", "Failed to write encrypted config file.");
    }
    return ok;
}

void ConfigManager::setConfig(const PluginConfig& config) {
    m_config = config;
}

void ConfigManager::addExcludedUser(const QString& username) {
    QString norm = username.trimmed().toLower();
    if (!norm.isEmpty() && !m_config.excludedUsers.contains(norm)) {
        m_config.excludedUsers.append(norm);
    }
}

void ConfigManager::removeExcludedUser(const QString& username) {
    QString norm = username.trimmed().toLower();
    m_config.excludedUsers.removeAll(norm);
}

bool ConfigManager::isExcludedUser(const QString& username) const {
    QString norm = username.trimmed().toLower();
    return m_config.excludedUsers.contains(norm);
}

void ConfigManager::addExcludedTtsUser(const QString& username) {
    QString norm = username.trimmed().toLower();
    if (!norm.isEmpty() && !m_config.excludedTtsUsers.contains(norm)) {
        m_config.excludedTtsUsers.append(norm);
    }
}

void ConfigManager::removeExcludedTtsUser(const QString& username) {
    QString norm = username.trimmed().toLower();
    m_config.excludedTtsUsers.removeAll(norm);
}

bool ConfigManager::isExcludedTtsUser(const QString& username) const {
    QString norm = username.trimmed().toLower();
    return m_config.excludedTtsUsers.contains(norm);
}

QJsonObject ConfigManager::configToJson() const {
    QJsonObject obj;
    obj["overlayTheme"] = m_config.overlayTheme;

    // 除外ユーザー
    QJsonArray exclArr;
    for (const auto& u : m_config.excludedUsers) {
        exclArr.append(u);
    }
    obj["excludedUsers"] = exclArr;

    // TTS除外ユーザー
    QJsonArray exclTtsArr;
    for (const auto& u : m_config.excludedTtsUsers) {
        exclTtsArr.append(u);
    }
    obj["excludedTtsUsers"] = exclTtsArr;

    // TTS設定
    QJsonObject ttsObj;
    ttsObj["speakerId"] = m_config.ttsSpeakerId;
    ttsObj["speed"] = m_config.ttsSpeed;
    ttsObj["pitch"] = m_config.ttsPitch;
    ttsObj["volume"] = m_config.ttsVolume;
    obj["ttsConfig"] = ttsObj;

    return obj;
}

void ConfigManager::jsonToConfig(const QJsonObject& json) {
    if (json.contains("overlayTheme") && json["overlayTheme"].isString()) {
        m_config.overlayTheme = json["overlayTheme"].toString("default");
    } else {
        m_config.overlayTheme = "default";
    }

    // 除外ユーザー
    m_config.excludedUsers.clear();
    if (json.contains("excludedUsers") && json["excludedUsers"].isArray()) {
        QJsonArray arr = json["excludedUsers"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            m_config.excludedUsers.append(arr.at(i).toString().trimmed().toLower());
        }
    }

    // TTS除外ユーザー
    m_config.excludedTtsUsers.clear();
    if (json.contains("excludedTtsUsers") && json["excludedTtsUsers"].isArray()) {
        QJsonArray arr = json["excludedTtsUsers"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            m_config.excludedTtsUsers.append(arr.at(i).toString().trimmed().toLower());
        }
    }

    // TTS設定
    if (json.contains("ttsConfig") && json["ttsConfig"].isObject()) {
        QJsonObject ttsObj = json["ttsConfig"].toObject();
        m_config.ttsSpeakerId = ttsObj.value("speakerId").toInt(1);
        m_config.ttsSpeed = ttsObj.value("speed").toDouble(1.0);
        m_config.ttsPitch = ttsObj.value("pitch").toDouble(1.0);
        m_config.ttsVolume = ttsObj.value("volume").toDouble(0.8);
    }
}
