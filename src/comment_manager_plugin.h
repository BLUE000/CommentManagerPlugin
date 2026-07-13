#pragma once
#include "shared/plugin_interface.h"
#include <QObject>
#include <QDateTime>

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
    QDateTime m_sessionStartTime;
    int m_tickCount = 0;
    int m_lastCommentCount = 0;
};
