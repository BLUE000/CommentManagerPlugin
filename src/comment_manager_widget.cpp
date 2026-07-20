#include "comment_manager_widget.h"
#include "database_manager.h"
#include "config_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QGroupBox>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QDateTimeAxis>
#include <QValueAxis>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QHeaderView>

#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

class HtmlDelegate : public QStyledItemDelegate {
private:
    const QMap<QUrl, QImage>& m_imageCache;
public:
    HtmlDelegate(const QMap<QUrl, QImage>& imageCache, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_imageCache(imageCache) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();

        QTextDocument doc;
        // キャッシュされた画像をQTextDocumentの内部リソースにロード
        for (auto it = m_imageCache.begin(); it != m_imageCache.end(); ++it) {
            if (!it.value().isNull()) {
                doc.addResource(QTextDocument::ImageResource, it.key(), it.value());
            }
        }
        doc.setHtml(opt.text);

        // 背景の描画
        opt.text = "";
        if (opt.widget) {
            opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);
        } else {
            QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);
        }

        // テキストの描画
        painter->translate(opt.rect.left(), opt.rect.top());
        QRect clip(0, 0, opt.rect.width(), opt.rect.height());
        doc.drawContents(painter, clip);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        QTextDocument doc;
        for (auto it = m_imageCache.begin(); it != m_imageCache.end(); ++it) {
            if (!it.value().isNull()) {
                doc.addResource(QTextDocument::ImageResource, it.key(), it.value());
            }
        }
        doc.setHtml(opt.text);
        doc.setTextWidth(opt.rect.width());
        return QSize(doc.idealWidth(), doc.size().height());
    }
};

CommentManagerWidget::CommentManagerWidget(QWidget* parent, DatabaseManager* db, ConfigManager* cfg, ICoreContext* ctx, int currentSessionId)
    : QWidget(parent), m_db(db), m_cfg(cfg), m_ctx(ctx), m_currentSessionId(currentSessionId) {
    m_networkManager = new QNetworkAccessManager(this);
    setupUi();
    loadSettingsToUi();
}

CommentManagerWidget::~CommentManagerWidget() {
}

void CommentManagerWidget::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(m_splitter);

    // 左ペイン・右ペインの初期化
    setupLeftPane();
    setupRightPane();

    // 下部コントロールの初期化
    setupBottomPane();

    // 解析初期化用のコネクション
    connect(m_comboTargetSession, &QComboBox::currentIndexChanged, this, &CommentManagerWidget::onGraphSettingsChanged);
    connect(m_comboGraphType, &QComboBox::currentIndexChanged, this, &CommentManagerWidget::onGraphSettingsChanged);
    connect(m_comboLimit, &QComboBox::currentIndexChanged, this, &CommentManagerWidget::onGraphSettingsChanged);
}

void CommentManagerWidget::setupLeftPane() {
    QWidget* leftWidget = new QWidget(m_splitter);
    QVBoxLayout* layout = new QVBoxLayout(leftWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    QLabel* label = new QLabel("チャットコメント履歴", leftWidget);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    layout->addWidget(label);

    m_commentTreeView = new QTreeView(leftWidget);
    m_commentModel = new QStandardItemModel(this);
    m_commentModel->setHorizontalHeaderLabels({ "Time", "Username", "Message" });
    m_commentTreeView->setModel(m_commentModel);
    m_commentTreeView->setItemDelegate(new HtmlDelegate(m_imageCache, m_commentTreeView));
    m_commentTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_commentTreeView->setSelectionMode(QAbstractItemView::NoSelection);
    m_commentTreeView->setWordWrap(true);

    QHeaderView* header = m_commentTreeView->header();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    m_commentTreeView->setColumnWidth(1, 150);

    layout->addWidget(m_commentTreeView);
    m_splitter->addWidget(leftWidget);
}

void CommentManagerWidget::setupRightPane() {
    m_tabWidget = new QTabWidget(m_splitter);

    // ----------------------------------------------------
    // 1. 視聴者タブ
    // ----------------------------------------------------
    m_viewerTab = new QWidget(m_tabWidget);
    QVBoxLayout* viewerLayout = new QVBoxLayout(m_viewerTab);

    // 更新エリア
    QHBoxLayout* updateLayout = new QHBoxLayout();
    QPushButton* btnRefreshViewers = new QPushButton("更新", m_viewerTab);
    QLabel* lblLastUpdate = new QLabel("最終更新: --:--:--", m_viewerTab);
    updateLayout->addWidget(btnRefreshViewers);
    updateLayout->addWidget(lblLastUpdate);
    updateLayout->addStretch();
    viewerLayout->addLayout(updateLayout);

    connect(btnRefreshViewers, &QPushButton::clicked, this, [=]() {
        updateActiveViewers();
        lblLastUpdate->setText(QString("最終更新: %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    });

    // アコーディオン・ツリービュー
    m_viewerTreeView = new QTreeView(m_viewerTab);
    m_viewerModel = new QStandardItemModel(this);
    m_viewerTreeView->setModel(m_viewerModel);
    m_viewerTreeView->setHeaderHidden(true);
    m_viewerTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    viewerLayout->addWidget(m_viewerTreeView);

    connect(m_viewerTreeView, &QTreeView::clicked, this, &CommentManagerWidget::onViewerSelected);

    // 誤クリック防止レイアウトの個別操作パネル
    QGroupBox* opGroupBox = new QGroupBox("選択中ユーザーの操作", m_viewerTab);
    opGroupBox->setObjectName("opGroupBox");
    QHBoxLayout* opLayout = new QHBoxLayout(opGroupBox);
    opLayout->setContentsMargins(6, 6, 6, 6);
    opLayout->setSpacing(0); // 密集させる

    m_btnShoutout = new QPushButton("Shoutout", opGroupBox);
    m_btnVip = new QPushButton("VIP", opGroupBox);
    m_btnMod = new QPushButton("Moderator", opGroupBox);
    m_btnTimeout = new QPushButton("Timeout", opGroupBox);
    m_btnBan = new QPushButton("BAN", opGroupBox);

    m_btnShoutout->setEnabled(false);
    m_btnVip->setEnabled(false);
    m_btnMod->setEnabled(false);
    m_btnTimeout->setEnabled(false);
    m_btnBan->setEnabled(false);

    opLayout->addWidget(m_btnShoutout);
    opLayout->addWidget(m_btnVip);
    opLayout->addWidget(m_btnMod);

    // 誤クリック防止のための1文字相当(約16px)の余白
    opLayout->addSpacing(16);

    opLayout->addWidget(m_btnTimeout);
    opLayout->addWidget(m_btnBan);
    opLayout->addStretch();

    viewerLayout->addWidget(opGroupBox);

    connect(m_btnShoutout, &QPushButton::clicked, this, &CommentManagerWidget::onModerationAction);
    connect(m_btnVip, &QPushButton::clicked, this, &CommentManagerWidget::onModerationAction);
    connect(m_btnMod, &QPushButton::clicked, this, &CommentManagerWidget::onModerationAction);
    connect(m_btnTimeout, &QPushButton::clicked, this, &CommentManagerWidget::onModerationAction);
    connect(m_btnBan, &QPushButton::clicked, this, &CommentManagerWidget::onModerationAction);

    m_tabWidget->addTab(m_viewerTab, "視聴者");

    // ----------------------------------------------------
    // 2. 解析タブ
    // ----------------------------------------------------
    m_analysisTab = new QWidget(m_tabWidget);
    QVBoxLayout* analysisLayout = new QVBoxLayout(m_analysisTab);

    // 上部コントロール
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(new QLabel("対象:", m_analysisTab));
    m_comboTargetSession = new QComboBox(m_analysisTab);
    m_comboTargetSession->addItem("当セッション", m_currentSessionId);
    m_comboTargetSession->addItem("全セッション", 0);
    filterLayout->addWidget(m_comboTargetSession);

    filterLayout->addWidget(new QLabel("グラフ:", m_analysisTab));
    m_comboGraphType = new QComboBox(m_analysisTab);
    m_comboGraphType->addItem("ユーザーごとの推移", 0);
    m_comboGraphType->addItem("総コメント数推移", 1);
    filterLayout->addWidget(m_comboGraphType);

    filterLayout->addWidget(new QLabel("表示:", m_analysisTab));
    m_comboLimit = new QComboBox(m_analysisTab);
    m_comboLimit->addItem("TOP 5", 5);
    m_comboLimit->addItem("TOP 10", 10);
    m_comboLimit->addItem("TOP 20", 20);
    filterLayout->addWidget(m_comboLimit);
    filterLayout->addStretch();
    analysisLayout->addLayout(filterLayout);

    // トレンドグラフ (QtCharts)
    m_chartView = new QChartView(m_analysisTab);
    m_chart = new QChart();
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chartView->setChart(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(200);
    analysisLayout->addWidget(m_chartView);

    // 下部統計・ランキング
    m_lblTotalComments = new QLabel("総コメント数: 0", m_analysisTab);
    analysisLayout->addWidget(m_lblTotalComments);

    m_rankingTable = new QTableWidget(m_analysisTab);
    m_rankingTable->setColumnCount(3);
    m_rankingTable->setHorizontalHeaderLabels({"順位", "ユーザー名", "発言回数"});
    m_rankingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_rankingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rankingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rankingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    analysisLayout->addWidget(m_rankingTable);

    m_tabWidget->addTab(m_analysisTab, "解析");

    // ----------------------------------------------------
    // 3. 設定タブ
    // ----------------------------------------------------
    m_settingsTab = new QWidget(m_tabWidget);
    QVBoxLayout* settingsLayout = new QVBoxLayout(m_settingsTab);

    // OBSブラウザソース用URL表示
    QGroupBox* obsGroup = new QGroupBox("OBS配信オーバーレイ設定", m_settingsTab);
    QFormLayout* obsForm = new QFormLayout(obsGroup);
    
    m_comboOverlayTheme = new QComboBox(obsGroup);
    m_comboOverlayTheme->setEditable(true); // カスタムスキン名の直接入力も可能
    
    // スキン（テーマ）フォルダのスキャン
    QString appDir = QCoreApplication::applicationDirPath();
    QString overlayBaseDir = QDir(appDir).filePath("assets/overlay/CommentManagerPlugin");
    QDir dir(overlayBaseDir);
    QStringList themes;
    if (dir.exists()) {
        themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    }
    if (!themes.contains("default")) {
        themes.prepend("default");
    }
    m_comboOverlayTheme->addItems(themes);
    obsForm->addRow("スキン（テーマ）:", m_comboOverlayTheme);

    m_editCommentOverlayUrl = new QLineEdit(obsGroup);
    m_editCommentOverlayUrl->setReadOnly(true);
    QPushButton* btnCopyComment = new QPushButton("コピー", obsGroup);
    QHBoxLayout* commentUrlLayout = new QHBoxLayout();
    commentUrlLayout->addWidget(m_editCommentOverlayUrl);
    commentUrlLayout->addWidget(btnCopyComment);
    obsForm->addRow("コメント表示URL:", commentUrlLayout);

    m_editRankingOverlayUrl = new QLineEdit(obsGroup);
    m_editRankingOverlayUrl->setReadOnly(true);
    QPushButton* btnCopyRanking = new QPushButton("コピー", obsGroup);
    QHBoxLayout* rankingUrlLayout = new QHBoxLayout();
    rankingUrlLayout->addWidget(m_editRankingOverlayUrl);
    rankingUrlLayout->addWidget(btnCopyRanking);
    obsForm->addRow("ランキング表示URL:", rankingUrlLayout);

    connect(m_comboOverlayTheme, &QComboBox::currentTextChanged, this, [this](const QString&) {
        m_editCommentOverlayUrl->setText(getObsOverlayUrl("comment.html"));
        m_editRankingOverlayUrl->setText(getObsOverlayUrl("ranking.html"));
    });

    connect(btnCopyComment, &QPushButton::clicked, this, &CommentManagerWidget::onCopyCommentUrl);
    connect(btnCopyRanking, &QPushButton::clicked, this, &CommentManagerWidget::onCopyRankingUrl);

    settingsLayout->addWidget(obsGroup);

    // 除外ユーザー設定・読み上げ除外ユーザー設定
    QHBoxLayout* exclLayout = new QHBoxLayout();
    
    // 表示除外
    QGroupBox* exclDisplayGroup = new QGroupBox("表示除外ユーザー設定", m_settingsTab);
    QVBoxLayout* exclDisplayLayout = new QVBoxLayout(exclDisplayGroup);
    m_listExcludedUsers = new QListView(exclDisplayGroup);
    m_excludedUsersModel = new QStandardItemModel(this);
    m_listExcludedUsers->setModel(m_excludedUsersModel);
    exclDisplayLayout->addWidget(m_listExcludedUsers);

    QHBoxLayout* exclDisplayBtnLayout = new QHBoxLayout();
    QLineEdit* editExclDisplay = new QLineEdit(exclDisplayGroup);
    QPushButton* btnAddExclDisplay = new QPushButton("追加", exclDisplayGroup);
    QPushButton* btnRemExclDisplay = new QPushButton("削除", exclDisplayGroup);
    exclDisplayBtnLayout->addWidget(editExclDisplay);
    exclDisplayBtnLayout->addWidget(btnAddExclDisplay);
    exclDisplayBtnLayout->addWidget(btnRemExclDisplay);
    exclDisplayLayout->addLayout(exclDisplayBtnLayout);
    exclLayout->addWidget(exclDisplayGroup);

    // 読み上げ除外
    QGroupBox* exclTtsGroup = new QGroupBox("読み上げ除外ユーザー設定", m_settingsTab);
    QVBoxLayout* exclTtsLayout = new QVBoxLayout(exclTtsGroup);
    m_listExcludedTtsUsers = new QListView(exclTtsGroup);
    m_excludedTtsUsersModel = new QStandardItemModel(this);
    m_listExcludedTtsUsers->setModel(m_excludedTtsUsersModel);
    exclTtsLayout->addWidget(m_listExcludedTtsUsers);

    QHBoxLayout* exclTtsBtnLayout = new QHBoxLayout();
    QLineEdit* editExclTts = new QLineEdit(exclTtsGroup);
    QPushButton* btnAddExclTts = new QPushButton("追加", exclTtsGroup);
    QPushButton* btnRemExclTts = new QPushButton("削除", exclTtsGroup);
    exclTtsBtnLayout->addWidget(editExclTts);
    exclTtsBtnLayout->addWidget(btnAddExclTts);
    exclTtsBtnLayout->addWidget(btnRemExclTts);
    exclTtsLayout->addLayout(exclTtsBtnLayout);
    exclLayout->addWidget(exclTtsGroup);

    settingsLayout->addLayout(exclLayout);

    connect(btnAddExclDisplay, &QPushButton::clicked, this, [=]() {
        QString user = editExclDisplay->text().trimmed();
        if (!user.isEmpty()) {
            m_cfg->addExcludedUser(user);
            editExclDisplay->clear();
            onSaveSettings();
        }
    });
    connect(btnRemExclDisplay, &QPushButton::clicked, this, [=]() {
        QModelIndex idx = m_listExcludedUsers->currentIndex();
        if (idx.isValid()) {
            m_cfg->removeExcludedUser(idx.data().toString());
            onSaveSettings();
        }
    });

    connect(btnAddExclTts, &QPushButton::clicked, this, [=]() {
        QString user = editExclTts->text().trimmed();
        if (!user.isEmpty()) {
            m_cfg->addExcludedTtsUser(user);
            editExclTts->clear();
            onSaveSettings();
        }
    });
    connect(btnRemExclTts, &QPushButton::clicked, this, [=]() {
        QModelIndex idx = m_listExcludedTtsUsers->currentIndex();
        if (idx.isValid()) {
            m_cfg->removeExcludedTtsUser(idx.data().toString());
            onSaveSettings();
        }
    });

    // TTSパラメータ
    QGroupBox* ttsGroup = new QGroupBox("TTS読み上げパラメータ", m_settingsTab);
    QFormLayout* ttsForm = new QFormLayout(ttsGroup);
    
    m_spinTtsSpeakerId = new QSpinBox(ttsGroup);
    m_spinTtsSpeakerId->setRange(0, 1000);
    ttsForm->addRow("話者ID (Speaker ID):", m_spinTtsSpeakerId);

    m_spinTtsSpeed = new QDoubleSpinBox(ttsGroup);
    m_spinTtsSpeed->setRange(0.5, 2.5);
    m_spinTtsSpeed->setSingleStep(0.1);
    ttsForm->addRow("速度 (Speed):", m_spinTtsSpeed);

    m_spinTtsPitch = new QDoubleSpinBox(ttsGroup);
    m_spinTtsPitch->setRange(0.5, 1.5);
    m_spinTtsPitch->setSingleStep(0.1);
    ttsForm->addRow("音程 (Pitch):", m_spinTtsPitch);

    m_spinTtsVolume = new QDoubleSpinBox(ttsGroup);
    m_spinTtsVolume->setRange(0.0, 1.0);
    m_spinTtsVolume->setSingleStep(0.1);
    ttsForm->addRow("音量 (Volume):", m_spinTtsVolume);

    settingsLayout->addWidget(ttsGroup);

    // 保存ボタン
    QPushButton* btnSave = new QPushButton("設定を保存", m_settingsTab);
    connect(btnSave, &QPushButton::clicked, this, &CommentManagerWidget::onSaveSettings);
    settingsLayout->addWidget(btnSave);

    m_tabWidget->addTab(m_settingsTab, "設定");

    m_splitter->addWidget(m_tabWidget);
}

void CommentManagerWidget::setupBottomPane() {
    QWidget* bottomWidget = new QWidget(this);
    QHBoxLayout* bottomLayout = new QHBoxLayout(bottomWidget);
    bottomLayout->setContentsMargins(0, 0, 0, 0);

    m_comboAnnounceColor = new QComboBox(bottomWidget);
    m_comboAnnounceColor->setObjectName("comboAnnounceColor");
    m_comboAnnounceColor->addItem("通常(Normal)", "");
    m_comboAnnounceColor->addItem("青(Blue)", "blue");
    m_comboAnnounceColor->addItem("緑(Green)", "green");
    m_comboAnnounceColor->addItem("橙(Orange)", "orange");
    m_comboAnnounceColor->addItem("紫(Purple)", "purple");
    bottomLayout->addWidget(m_comboAnnounceColor);

    m_editAnnounceMessage = new QLineEdit(bottomWidget);
    m_editAnnounceMessage->setObjectName("editAnnounceMessage");
    m_editAnnounceMessage->setPlaceholderText("アナウンスメッセージを入力...");
    bottomLayout->addWidget(m_editAnnounceMessage);

    m_btnSendAnnounce = new QPushButton("アナウンス送信", bottomWidget);
    m_btnSendAnnounce->setObjectName("btnSendAnnounce");
    bottomLayout->addWidget(m_btnSendAnnounce);

    connect(m_btnSendAnnounce, &QPushButton::clicked, this, &CommentManagerWidget::onSendAnnouncement);
    connect(m_editAnnounceMessage, &QLineEdit::returnPressed, this, &CommentManagerWidget::onSendAnnouncement);

    layout()->addWidget(bottomWidget);
}

void CommentManagerWidget::addComment(const TwitchComment& comment) {
    if (m_cfg->isExcludedUser(comment.username)) {
        return; // 表示除外
    }

    // 1. Time
    QString timeStr = QDateTime::fromMSecsSinceEpoch(comment.timestamp).toString("HH:mm:ss");
    QString timeHtml = QString("<font color=\"#888888\">%1</font>").arg(timeStr);
    QStandardItem* timeItem = new QStandardItem(timeHtml);

    // 2. Username & Badges mapping to Emoji
    QStringList badgeEmojis;
    if (!comment.badges.isEmpty()) {
        for (int i = 0; i < comment.badges.size(); ++i) {
            QJsonObject bObj = comment.badges.at(i).toObject();
            QString name = bObj.value("name").toString();
            if (!name.isEmpty()) {
                QString type = name.split('/').first().toLower();
                if (type == "broadcaster") badgeEmojis.append("👑");
                else if (type == "moderator") badgeEmojis.append("🛡️");
                else if (type == "vip") badgeEmojis.append("💎");
                else if (type == "subscriber") badgeEmojis.append("⭐");
            }
        }
    }
    QString badgePrefix = badgeEmojis.isEmpty() ? "" : (badgeEmojis.join(" ") + " ");
    QString userColor = "#A970FF";
    QString userHtml = QString("%1<b><font color=\"%2\">%3</font></b>")
        .arg(badgePrefix)
        .arg(userColor)
        .arg(comment.displayName.isEmpty() ? comment.username : comment.displayName);
    QStandardItem* userItem = new QStandardItem(userHtml);

    // 3. Message (ホスト側で安全にHTML化・エモート置換されて渡されるためそのまま受け取る)
    QString msgHtml = comment.comment;
    QStandardItem* msgItem = new QStandardItem(msgHtml);

    m_commentModel->appendRow({ timeItem, userItem, msgItem });
    m_commentTreeView->scrollToBottom();

    // <img> タグ内の画像の非同期ダウンロードのトリガー
    static QRegularExpression imgRegex("<img[^>]*src=\"([^\"]+)\"");
    QRegularExpressionMatchIterator it = imgRegex.globalMatch(comment.comment);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QUrl url(match.captured(1));
        if (url.isValid() && !m_imageCache.contains(url)) {
            // 重複ダウンロード防止のため仮のエントリーを追加
            m_imageCache.insert(url, QImage());
            
            QNetworkReply* reply = m_networkManager->get(QNetworkRequest(url));
            connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
                if (reply->error() == QNetworkReply::NoError) {
                    QByteArray data = reply->readAll();
                    QImage image = QImage::fromData(data);
                    if (!image.isNull()) {
                        m_imageCache[url] = image;
                        // Viewport の再描画を要求して画像を反映
                        m_commentTreeView->viewport()->update();
                    }
                }
                reply->deleteLater();
            });
        }
    }
}

void CommentManagerWidget::updateActiveViewers() {
    int targetSessionId = m_currentSessionId;
    if (m_ctx && m_comboTargetSession) {
        targetSessionId = m_comboTargetSession->currentData().toInt();
    }
    
    QMap<QString, QStringList> categorized = m_db->getCategorizedActiveUsers(targetSessionId);
    populateViewerTree(categorized);
}

void CommentManagerWidget::populateViewerTree(const QMap<QString, QStringList>& categorizedUsers) {
    m_viewerModel->clear();

    struct CatInfo {
        QString key;
        QString label;
    };
    QList<CatInfo> cats = {
        {"all", "すべて"},
        {"streamer", "ストリーマー"},
        {"moderator", "モデレーター"},
        {"vip", "VIP"},
        {"artist", "アーティスト"},
        {"bot", "チャットボット"},
        {"regular", "一般"}
    };

    for (const auto& cat : cats) {
        QStringList users = categorizedUsers.value(cat.key);
        QStandardItem* catItem = new QStandardItem(QString("%1 (%2)").arg(cat.label).arg(users.size()));
        m_viewerModel->appendRow(catItem);

        for (const auto& u : users) {
            QStandardItem* userItem = new QStandardItem(u);
            catItem->appendRow(userItem);
        }
    }
    m_viewerTreeView->expandToDepth(0);
}

void CommentManagerWidget::onViewerSelected(const QModelIndex& index) {
    if (!index.isValid()) return;
    
    // 親項目（カテゴリ）ではない場合のみ選択可能
    QModelIndex parentIdx = index.parent();
    if (parentIdx.isValid()) {
        m_selectedViewerUsername = index.data().toString();
        m_btnShoutout->setEnabled(true);
        m_btnVip->setEnabled(true);
        m_btnMod->setEnabled(true);
        m_btnTimeout->setEnabled(true);
        m_btnBan->setEnabled(true);
    } else {
        m_selectedViewerUsername = "";
        m_btnShoutout->setEnabled(false);
        m_btnVip->setEnabled(false);
        m_btnMod->setEnabled(false);
        m_btnTimeout->setEnabled(false);
        m_btnBan->setEnabled(false);
    }
}

void CommentManagerWidget::onModerationAction() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn || m_selectedViewerUsername.isEmpty() || !m_ctx) return;

    QString cmd = "";
    if (btn == m_btnShoutout) cmd = QString("/shoutout %1").arg(m_selectedViewerUsername);
    else if (btn == m_btnVip) cmd = QString("/vip %1").arg(m_selectedViewerUsername);
    else if (btn == m_btnMod) cmd = QString("/mod %1").arg(m_selectedViewerUsername);
    else if (btn == m_btnTimeout) cmd = QString("/timeout %1").arg(m_selectedViewerUsername);
    else if (btn == m_btnBan) cmd = QString("/ban %1").arg(m_selectedViewerUsername);

    if (!cmd.isEmpty()) {
        m_ctx->sendChatMessage(cmd);
    }
}

void CommentManagerWidget::onSendAnnouncement() {
    QString msg = m_editAnnounceMessage->text().trimmed();
    if (msg.isEmpty() || !m_ctx) return;

    QString color = m_comboAnnounceColor->currentData().toString();
    QString cmd;
    if (color.isEmpty()) {
        cmd = QString("/announce %1").arg(msg);
    } else {
        cmd = QString("/announce%1 %2").arg(color, msg);
    }

    m_ctx->sendChatMessage(cmd);
    m_editAnnounceMessage->clear();
}

void CommentManagerWidget::onGraphSettingsChanged() {
    refreshAnalysis();
}

void CommentManagerWidget::refreshAnalysis() {
    m_chart->removeAllSeries();
    
    // 古い軸があれば削除
    QList<QAbstractAxis*> axes = m_chart->axes();
    for (QAbstractAxis* axis : axes) {
        m_chart->removeAxis(axis);
        delete axis;
    }

    int sessionId = m_comboTargetSession->currentData().toInt();
    int graphType = m_comboGraphType->currentIndex();
    int limit = m_comboLimit->currentData().toInt();

    m_lblTotalComments->setText(QString("総コメント数: %1").arg(m_db->getTotalCommentCount(sessionId)));

    // ランキングテーブルの描画
    QList<QPair<QString, int>> ranking = m_db->getCommentRanking(sessionId, limit);
    m_rankingTable->setRowCount(ranking.size());
    for (int i = 0; i < ranking.size(); ++i) {
        m_rankingTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_rankingTable->setItem(i, 1, new QTableWidgetItem(ranking[i].first));
        m_rankingTable->setItem(i, 2, new QTableWidgetItem(QString("%1 回").arg(ranking[i].second)));
    }

    // チャートの描画
    QDateTimeAxis* axisX = new QDateTimeAxis();
    axisX->setFormat("HH:mm:ss");
    axisX->setTitleText("経過時間");
    m_chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    axisY->setLabelFormat("%d");
    axisY->setTitleText("累積コメント数");
    m_chart->addAxis(axisY, Qt::AlignLeft);

    if (graphType == 1) {
        // 総コメント数推移
        QList<QPair<QDateTime, int>> trend = m_db->getCommentTrendTotal(sessionId);
        QLineSeries* series = new QLineSeries();
        series->setName("総コメント数");
        for (const auto& pt : trend) {
            series->append(pt.first.toMSecsSinceEpoch(), pt.second);
        }
        m_chart->addSeries(series);
        series->attachAxis(axisX);
        series->attachAxis(axisY);
    } else {
        // ユーザーごとの推移
        auto userTrends = m_db->getCommentTrendByUser(sessionId, limit);
        for (const auto& ut : userTrends) {
            QLineSeries* series = new QLineSeries();
            series->setName(ut.first);
            for (const auto& pt : ut.second) {
                series->append(pt.first.toMSecsSinceEpoch(), pt.second);
            }
            m_chart->addSeries(series);
            series->attachAxis(axisX);
            series->attachAxis(axisY);
        }
    }
}

void CommentManagerWidget::loadSettingsToUi() {
    const PluginConfig& cfg = m_cfg->getConfig();

    // オーバーレイテーマ
    if (m_comboOverlayTheme) {
        QString theme = cfg.overlayTheme.isEmpty() ? "default" : cfg.overlayTheme;
        int idx = m_comboOverlayTheme->findText(theme);
        if (idx != -1) {
            m_comboOverlayTheme->setCurrentIndex(idx);
        } else {
            m_comboOverlayTheme->setCurrentText(theme);
        }
    }

    // 除外ユーザー
    m_excludedUsersModel->clear();
    for (const auto& u : cfg.excludedUsers) {
        m_excludedUsersModel->appendRow(new QStandardItem(u));
    }

    // TTS除外
    m_excludedTtsUsersModel->clear();
    for (const auto& u : cfg.excludedTtsUsers) {
        m_excludedTtsUsersModel->appendRow(new QStandardItem(u));
    }

    // TTSパラメータ
    m_spinTtsSpeakerId->setValue(cfg.ttsSpeakerId);
    m_spinTtsSpeed->setValue(cfg.ttsSpeed);
    m_spinTtsPitch->setValue(cfg.ttsPitch);
    m_spinTtsVolume->setValue(cfg.ttsVolume);

    // OBS URL
    m_editCommentOverlayUrl->setText(getObsOverlayUrl("comment.html"));
    m_editRankingOverlayUrl->setText(getObsOverlayUrl("ranking.html"));
}

void CommentManagerWidget::onSaveSettings() {
    PluginConfig cfg;

    if (m_comboOverlayTheme) {
        cfg.overlayTheme = m_comboOverlayTheme->currentText().trimmed();
        if (cfg.overlayTheme.isEmpty()) {
            cfg.overlayTheme = "default";
        }
    }
    
    // UI値の読み取り
    for (int i = 0; i < m_excludedUsersModel->rowCount(); ++i) {
        cfg.excludedUsers.append(m_excludedUsersModel->item(i)->text().trimmed().toLower());
    }
    for (int i = 0; i < m_excludedTtsUsersModel->rowCount(); ++i) {
        cfg.excludedTtsUsers.append(m_excludedTtsUsersModel->item(i)->text().trimmed().toLower());
    }

    cfg.ttsSpeakerId = m_spinTtsSpeakerId->value();
    cfg.ttsSpeed = m_spinTtsSpeed->value();
    cfg.ttsPitch = m_spinTtsPitch->value();
    cfg.ttsVolume = m_spinTtsVolume->value();

    m_cfg->setConfig(cfg);
    if (m_cfg->saveConfig()) {
        m_ctx->writeLog("Info", "CommentManagerWidget", "onSaveSettings", "Plugin settings saved successfully.");
    }
    loadSettingsToUi();
}

QString CommentManagerWidget::getObsOverlayUrl(const QString& filename) const {
    int port = 8081; // デフォルトフォールバック
    
    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        file.close();
        
        QByteArray rawJson = encryptedData;
#ifdef Q_OS_WIN
        DATA_BLOB input;
        input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encryptedData.constData()));
        input.cbData = encryptedData.size();
        DATA_BLOB output;
        if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
            rawJson = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
            LocalFree(output.pbData);
        }
#endif
        QJsonDocument doc = QJsonDocument::fromJson(rawJson);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            port = obj.value("obs_port").toInt(8081);
        }
    }

    QString theme = "default";
    if (m_comboOverlayTheme && !m_comboOverlayTheme->currentText().trimmed().isEmpty()) {
        theme = m_comboOverlayTheme->currentText().trimmed();
    } else if (m_cfg) {
        theme = m_cfg->getConfig().overlayTheme;
        if (theme.isEmpty()) theme = "default";
    }

    return QString("http://localhost:%1/overlay/CommentManagerPlugin/%2/%3").arg(QString::number(port), theme, filename);
}

void CommentManagerWidget::onCopyCommentUrl() {
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(m_editCommentOverlayUrl->text());
}

void CommentManagerWidget::onCopyRankingUrl() {
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(m_editRankingOverlayUrl->text());
}
