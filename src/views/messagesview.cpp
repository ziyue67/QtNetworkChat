#include "views/messagesview.h"

#include "theme/thememanager.h"
#include "widgets/composerwidget.h"
#include "chatbubbledelegate.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

MessagesView::MessagesView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("messagesView"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MessagesView::updateStyle);
}

MessagesView::~MessagesView() = default;

void MessagesView::setupUi()
{
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Session list panel
    QFrame* sessionPanel = new QFrame(this);
    sessionPanel->setObjectName(QStringLiteral("sessionPanel"));
    sessionPanel->setFixedWidth(ThemeManager::instance()->sessionListWidth());
    QVBoxLayout* sessionLayout = new QVBoxLayout(sessionPanel);
    sessionLayout->setContentsMargins(12, 12, 12, 12);
    sessionLayout->setSpacing(10);

    m_searchEdit = new QLineEdit(sessionPanel);
    m_searchEdit->setObjectName(QStringLiteral("sessionSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话..."));
    sessionLayout->addWidget(m_searchEdit);

    m_sessionModel = new QStandardItemModel(this);
    m_sessionListView = new QListView(sessionPanel);
    m_sessionListView->setObjectName(QStringLiteral("sessionListView"));
    m_sessionListView->setModel(m_sessionModel);
    m_sessionListView->setContextMenuPolicy(Qt::CustomContextMenu);
    sessionLayout->addWidget(m_sessionListView, 1);

    // Chat panel
    QFrame* chatPanel = new QFrame(this);
    chatPanel->setObjectName(QStringLiteral("chatPanel"));
    QVBoxLayout* chatLayout = new QVBoxLayout(chatPanel);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    QFrame* header = new QFrame(chatPanel);
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setFixedHeight(60);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 8, 16, 8);
    headerLayout->setSpacing(2);

    m_chatTitleLabel = new QLabel(QStringLiteral("QQ NT"), header);
    m_chatTitleLabel->setObjectName(QStringLiteral("chatTitleLabel"));
    m_chatSubtitleLabel = new QLabel(header);
    m_chatSubtitleLabel->setObjectName(QStringLiteral("chatSubtitleLabel"));
    m_chatHintLabel = new QLabel(header);
    m_chatHintLabel->setObjectName(QStringLiteral("chatHintLabel"));
    headerLayout->addWidget(m_chatTitleLabel);
    headerLayout->addWidget(m_chatSubtitleLabel);
    headerLayout->addWidget(m_chatHintLabel);
    chatLayout->addWidget(header);

    m_chatModel = new QStandardItemModel(this);
    m_chatListView = new QListView(chatPanel);
    m_chatListView->setObjectName(QStringLiteral("chatListView"));
    m_chatListView->setModel(m_chatModel);
    m_chatListView->setItemDelegate(new ChatBubbleDelegate(m_chatListView));
    m_chatListView->setIconSize(QSize(34, 34));
    m_chatListView->setSpacing(8);
    m_chatListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_chatListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatLayout->addWidget(m_chatListView, 1);

    m_emptyLabel = new QLabel(QStringLiteral("暂无消息\n开始聊天吧"), chatPanel);
    m_emptyLabel->setObjectName(QStringLiteral("emptyChatLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setVisible(false);
    chatLayout->addWidget(m_emptyLabel, 1, Qt::AlignCenter);

    m_loadingLabel = new QLabel(QStringLiteral("加载中..."), chatPanel);
    m_loadingLabel->setObjectName(QStringLiteral("loadingLabel"));
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->setVisible(false);
    chatLayout->addWidget(m_loadingLabel, 1, Qt::AlignCenter);

    m_composer = new ComposerWidget(chatPanel);
    chatLayout->addWidget(m_composer);

    root->addWidget(sessionPanel);
    root->addWidget(chatPanel, 1);

    connect(m_sessionListView, &QListView::clicked, this, &MessagesView::sessionSelected);
    connect(m_composer, &ComposerWidget::sendRequested, this, &MessagesView::sendRequested);
    connect(m_composer, &ComposerWidget::fileRequested, this, &MessagesView::fileRequested);
    connect(m_composer, &ComposerWidget::imageRequested, this, &MessagesView::imageRequested);
    connect(m_composer, &ComposerWidget::emojiRequested, this, &MessagesView::emojiRequested);
    connect(m_composer, &ComposerWidget::mentionRequested, this, &MessagesView::mentionRequested);
    connect(m_composer, &ComposerWidget::clearHistoryRequested, this, &MessagesView::clearHistoryRequested);
}

void MessagesView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#sessionPanel { background-color: %1; border-right: 1px solid %2; }"
        "QFrame#chatPanel { background-color: %3; }"
        "QFrame#chatHeader { background-color: %4; border-bottom: 1px solid %2; }"
        "QLineEdit#sessionSearchEdit { background-color: %5; color: %6; border: 1px solid %2; border-radius: 6px; padding: 6px 10px; }"
        "QLineEdit#sessionSearchEdit:focus { border: 1px solid %7; }"
        "QListView#sessionListView { background: transparent; border: none; outline: none; }"
        "QListView#sessionListView::item { color: %6; padding: 8px; border-radius: 6px; }"
        "QListView#sessionListView::item:selected { background-color: %8; color: %6; }"
        "QListView#sessionListView::item:hover { background-color: %9; }"
        "QLabel#chatTitleLabel { color: %6; font-size: 16px; font-weight: 600; }"
        "QLabel#chatSubtitleLabel { color: %10; font-size: 12px; }"
        "QLabel#chatHintLabel { color: %10; font-size: 12px; }"
        "QLabel#emptyChatLabel, QLabel#loadingLabel { color: %10; font-size: 14px; }"
    ).arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->backgroundColor().name())
     .arg(tm->backgroundColor().name())
     .arg(tm->backgroundTertiaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->color(QStringLiteral("session-selected")).name())
     .arg(tm->color(QStringLiteral("session-hover")).name())
     .arg(tm->textSecondaryColor().name()));
}

QStandardItemModel* MessagesView::sessionModel() const
{
    return m_sessionModel;
}

QStandardItemModel* MessagesView::chatModel() const
{
    return m_chatModel;
}

QLineEdit* MessagesView::searchEdit() const
{
    return m_searchEdit;
}

QListView* MessagesView::sessionListView() const
{
    return m_sessionListView;
}

QListView* MessagesView::chatListView() const
{
    return m_chatListView;
}

ComposerWidget* MessagesView::composer() const
{
    return m_composer;
}

void MessagesView::setCurrentUser(const QString& userId, const QString& userName)
{
    Q_UNUSED(userId)
    Q_UNUSED(userName)
}

void MessagesView::setChatTitle(const QString& title, const QString& subtitle, const QString& hint)
{
    if (m_chatTitleLabel) m_chatTitleLabel->setText(title);
    if (m_chatSubtitleLabel) m_chatSubtitleLabel->setText(subtitle);
    if (m_chatHintLabel) m_chatHintLabel->setText(hint);
}

QString MessagesView::chatTitle() const
{
    return m_chatTitleLabel ? m_chatTitleLabel->text() : QString();
}

QLabel* MessagesView::chatTitleLabel() const { return m_chatTitleLabel; }
QLabel* MessagesView::chatSubtitleLabel() const { return m_chatSubtitleLabel; }
QLabel* MessagesView::chatHintLabel() const { return m_chatHintLabel; }

void MessagesView::appendMessage(const QStandardItem* item)
{
    if (m_chatModel) {
        m_chatModel->appendRow(item->clone());
    }
}

void MessagesView::clearChat()
{
    if (m_chatModel) {
        m_chatModel->clear();
    }
}

void MessagesView::setEmptyStateVisible(bool visible)
{
    if (m_emptyLabel) m_emptyLabel->setVisible(visible);
    if (m_chatListView) m_chatListView->setVisible(!visible);
    if (m_loadingLabel) m_loadingLabel->setVisible(false);
}

void MessagesView::setLoadingVisible(bool visible)
{
    if (m_loadingLabel) m_loadingLabel->setVisible(visible);
    if (m_chatListView) m_chatListView->setVisible(!visible);
    if (m_emptyLabel) m_emptyLabel->setVisible(false);
}
