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
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QMap>
#include "shared/plugin_interface.h"

class DatabaseManager;
class ConfigManager;

class CommentManagerWidget : public QWidget {
    Q_OBJECT
public:
    CommentManagerWidget(QWidget* parent, DatabaseManager* db, ConfigManager* cfg, ICoreContext* ctx, int currentSessionId = -1);
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
    void onCopyCommentUrl();
    void onCopyRankingUrl();

private:
    DatabaseManager* m_db = nullptr;
    ConfigManager* m_cfg = nullptr;
    ICoreContext* m_ctx = nullptr;
    int m_currentSessionId = -1;

    // UIポインタ
    QSplitter* m_splitter = nullptr;
    
    // 左ペイン
    QTreeView* m_commentTreeView = nullptr;
    QStandardItemModel* m_commentModel = nullptr;

    // 右ペイン
    QTabWidget* m_tabWidget = nullptr;
    
    // 右ペイン: 視聴者タブ
    QWidget* m_viewerTab = nullptr;
    QTreeView* m_viewerTreeView = nullptr;
    QStandardItemModel* m_viewerModel = nullptr;
    QPushButton* m_btnShoutout = nullptr;
    QPushButton* m_btnVip = nullptr;
    QPushButton* m_btnMod = nullptr;
    QPushButton* m_btnTimeout = nullptr;
    QPushButton* m_btnBan = nullptr;
    QString m_selectedViewerUsername;

    // 右ペイン: 解析タブ
    QWidget* m_analysisTab = nullptr;
    QComboBox* m_comboTargetSession = nullptr;
    QComboBox* m_comboGraphType = nullptr;
    QComboBox* m_comboLimit = nullptr;
    QChartView* m_chartView = nullptr;
    QChart* m_chart = nullptr;
    QTableWidget* m_rankingTable = nullptr;
    QLabel* m_lblTotalComments = nullptr;

    // 右ペイン: 設定タブ
    QWidget* m_settingsTab = nullptr;
    QComboBox* m_comboOverlayTheme = nullptr;
    QLineEdit* m_editCommentOverlayUrl = nullptr;
    QLineEdit* m_editRankingOverlayUrl = nullptr;
    QListView* m_listExcludedUsers = nullptr;
    QListView* m_listExcludedTtsUsers = nullptr;
    QStandardItemModel* m_excludedUsersModel = nullptr;
    QStandardItemModel* m_excludedTtsUsersModel = nullptr;

    // TTS設定用UI
    QSpinBox* m_spinTtsSpeakerId = nullptr;
    QDoubleSpinBox* m_spinTtsSpeed = nullptr;
    QDoubleSpinBox* m_spinTtsPitch = nullptr;
    QDoubleSpinBox* m_spinTtsVolume = nullptr;

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
    QString getObsOverlayUrl(const QString& filename) const;

    QNetworkAccessManager* m_networkManager = nullptr;
    QMap<QUrl, QImage> m_imageCache;
};
