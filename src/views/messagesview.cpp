#include "views/messagesview.h"

#include "sessionitemdelegate.h"
#include "theme/thememanager.h"
#include "widgets/composerwidget.h"
#include "widgets/groupmembersidebar.h"
#include "chatbubbledelegate.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

namespace {
void mvLog(const QString& msg)
{
    const QString line = QString("[MV][%1] %2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")))
                             .arg(msg);
    qDebug().noquote() << line;
    QFile f(QStringLiteral("qqnt-debug.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&f);
        s << line << "\n";
        s.flush();
        f.close();
    }
}
}
MessagesView::MessagesView(QWidget* parent)
    : QWidget(parent)
{
    mvLog("MessagesView constructor start");
    setObjectName(QStringLiteral("messagesView"));
    setupUi();
    mvLog("MessagesView setupUi done");
    updateStyle();
    mvLog("MessagesView updateStyle done");
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MessagesView::updateStyle);
    mvLog("MessagesView constructor end");
}

MessagesView::~MessagesView() = default;

void MessagesView::setupUi()
{
    mvLog("setupUi start");
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // Session list panel
    QFrame* sessionPanel = new QFrame(this);
    sessionPanel->setObjectName(QStringLiteral("sessionPanel"));
    sessionPanel->setFixedWidth(qMax(248, ThemeManager::instance()->sessionListWidth()));
    QVBoxLayout* sessionLayout = new QVBoxLayout(sessionPanel);
    sessionLayout->setContentsMargins(14, 16, 14, 12);
    sessionLayout->setSpacing(10);

    QFrame* sessionTitle = new QFrame(sessionPanel);
    QHBoxLayout* titleLayout = new QHBoxLayout(sessionTitle);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* titleLabel = new QLabel(QStringLiteral("消息"), sessionTitle);
    titleLabel->setObjectName(QStringLiteral("sessionTitleLabel"));
    titleLabel->setFont(QFont(ThemeManager::instance()->font().family(), 11, QFont::Bold));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    sessionLayout->addWidget(sessionTitle);

    QFrame* searchContainer = new QFrame(sessionPanel);
    searchContainer->setObjectName(QStringLiteral("sessionSearchContainer"));
    QHBoxLayout* searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(4, 0, 4, 0);
    searchLayout->setSpacing(0);

    m_searchEdit = new QLineEdit(searchContainer);
    m_searchEdit->setObjectName(QStringLiteral("sessionSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话..."));
    QPushButton* newChatBtn = new QPushButton(QStringLiteral("+"), searchContainer);
    newChatBtn->setObjectName(QStringLiteral("sessionNewChatBtn"));
    newChatBtn->setCursor(Qt::PointingHandCursor);
    newChatBtn->setFixedSize(22, 22);
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(newChatBtn, 0, Qt::AlignCenter);
    sessionLayout->addWidget(searchContainer);

    m_sessionModel = new QStandardItemModel(this);
    m_sessionListView = new QListView(sessionPanel);
    m_sessionListView->setObjectName(QStringLiteral("sessionListView"));
    m_sessionListView->setModel(m_sessionModel);
    m_sessionListView->setItemDelegate(new SessionItemDelegate(m_sessionListView));
    m_sessionListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sessionListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sessionListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sessionLayout->addWidget(m_sessionListView, 1);
    mvLog("session panel done");

    // Chat panel
    QFrame* chatPanel = new QFrame(this);
    m_chatPanel = chatPanel;
    chatPanel->setObjectName(QStringLiteral("chatPanel"));
    QVBoxLayout* chatLayout = new QVBoxLayout(chatPanel);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    m_dropOverlay = new QLabel(chatPanel);
    m_dropOverlay->setObjectName(QStringLiteral("dropOverlay"));
    m_dropOverlay->setText(QStringLiteral("拖放文件到这里发送"));
    m_dropOverlay->setAlignment(Qt::AlignCenter);
    m_dropOverlay->setWordWrap(true);
    m_dropOverlay->setVisible(false);
    m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Overlay is not added to the layout; it is resized manually to cover chatPanel.

    QFrame* header = new QFrame(chatPanel);
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setFixedHeight(68);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(22, 10, 22, 9);
    headerLayout->setSpacing(2);

    // Top row: title/subtitle/hint column on the left, group actions on the right.
    QHBoxLayout* headerTopRow = new QHBoxLayout();
    headerTopRow->setContentsMargins(0, 0, 0, 0);
    headerTopRow->setSpacing(8);
    QVBoxLayout* headerLabelCol = new QVBoxLayout();
    headerLabelCol->setContentsMargins(0, 0, 0, 0);
    headerLabelCol->setSpacing(2);

    m_chatTitleLabel = new QLabel(QStringLiteral("QQ NT"), header);
    m_chatTitleLabel->setObjectName(QStringLiteral("chatTitleLabel"));
    m_chatSubtitleLabel = new QLabel(header);
    m_chatSubtitleLabel->setObjectName(QStringLiteral("chatSubtitleLabel"));
    m_chatHintLabel = new QLabel(header);
    m_chatHintLabel->setObjectName(QStringLiteral("chatHintLabel"));
    headerLabelCol->addWidget(m_chatTitleLabel);
    headerLabelCol->addWidget(m_chatSubtitleLabel);
    headerLabelCol->addWidget(m_chatHintLabel);
    headerTopRow->addLayout(headerLabelCol, 1);

    // Group-only essence entry. MainWindow toggles visibility per session type.
    m_essenceBtn = new QPushButton(QStringLiteral("精华"), header);
    m_essenceBtn->setObjectName(QStringLiteral("chatEssenceBtn"));
    m_essenceBtn->setCursor(Qt::PointingHandCursor);
    m_essenceBtn->setVisible(false);
    connect(m_essenceBtn, &QPushButton::clicked, this, &MessagesView::essenceRequested);
    headerTopRow->addWidget(m_essenceBtn, 0, Qt::AlignVCenter);

    m_groupMoreBtn = new QPushButton(QStringLiteral("⋯"), header);
    m_groupMoreBtn->setObjectName(QStringLiteral("chatGroupMoreBtn"));
    m_groupMoreBtn->setCursor(Qt::PointingHandCursor);
    m_groupMoreBtn->setFixedSize(32, 30);
    m_groupMoreBtn->setToolTip(QStringLiteral("群资料与设置"));
    m_groupMoreBtn->setAccessibleName(QStringLiteral("更多群设置"));
    m_groupMoreBtn->setVisible(false);
    connect(m_groupMoreBtn, &QPushButton::clicked, this, &MessagesView::groupMoreRequested);
    headerTopRow->addWidget(m_groupMoreBtn, 0, Qt::AlignVCenter);

    headerLayout->addLayout(headerTopRow);
    chatLayout->addWidget(header);

    m_chatModel = new QStandardItemModel(this);
    m_chatListView = new QListView(chatPanel);
    m_chatListView->setObjectName(QStringLiteral("chatListView"));
    m_chatListView->setModel(m_chatModel);
    m_chatListView->setItemDelegate(new ChatBubbleDelegate(m_chatListView));
    m_chatListView->setIconSize(QSize(34, 34));
    // The delegate includes the message-card breathing room; avoid adding a
    // second large gap on top of the tauri-qqnt mb-4 rhythm.
    m_chatListView->setSpacing(4);
    m_chatListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_chatListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatLayout->addWidget(m_chatListView, 1);
    mvLog("chat list done");

    m_emptyLabel = new QLabel(QStringLiteral("暂无会话"), chatPanel);
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
    mvLog("composer done");

    // Multi-select bottom action bar
    m_multiSelectBar = new QFrame(chatPanel);
    m_multiSelectBar->setObjectName(QStringLiteral("multiSelectBar"));
    m_multiSelectBar->setFixedHeight(56);
    m_multiSelectBar->setVisible(false);
    QHBoxLayout* multiSelectLayout = new QHBoxLayout(m_multiSelectBar);
    multiSelectLayout->setContentsMargins(16, 8, 16, 8);
    multiSelectLayout->setSpacing(12);
    multiSelectLayout->addStretch();

    m_multiSelectForwardBtn = new QPushButton(QStringLiteral("转发"), m_multiSelectBar);
    m_multiSelectForwardBtn->setObjectName(QStringLiteral("multiSelectForwardBtn"));
    multiSelectLayout->addWidget(m_multiSelectForwardBtn);

    m_multiSelectFavoriteBtn = new QPushButton(QStringLiteral("收藏"), m_multiSelectBar);
    m_multiSelectFavoriteBtn->setObjectName(QStringLiteral("multiSelectFavoriteBtn"));
    multiSelectLayout->addWidget(m_multiSelectFavoriteBtn);

    m_multiSelectDeleteBtn = new QPushButton(QStringLiteral("删除"), m_multiSelectBar);
    m_multiSelectDeleteBtn->setObjectName(QStringLiteral("multiSelectDeleteBtn"));
    multiSelectLayout->addWidget(m_multiSelectDeleteBtn);

    m_multiSelectCancelBtn = new QPushButton(QStringLiteral("取消"), m_multiSelectBar);
    m_multiSelectCancelBtn->setObjectName(QStringLiteral("multiSelectCancelBtn"));
    multiSelectLayout->addWidget(m_multiSelectCancelBtn);
    multiSelectLayout->addStretch();

    chatLayout->addWidget(m_multiSelectBar);

    root->addWidget(sessionPanel);
    root->addWidget(chatPanel, 1);

    // Group member sidebar. Hidden by default; MainWindow shows it for group
    // sessions and drives its data/actions. Replaces the legacy (now-invisible)
    // groupMemberListView from the pre-QQNT central widget.
    m_groupMemberSidebar = new GroupMemberSidebar(this);
    m_groupMemberSidebar->setVisible(false);
    root->addWidget(m_groupMemberSidebar);
    mvLog("root layout done");

    setAcceptDrops(true);
    mvLog("acceptDrops set");

    connect(m_sessionListView, &QListView::clicked, this, &MessagesView::sessionSelected);
    connect(m_chatListView, &QListView::doubleClicked, this, &MessagesView::onChatItemActivated);
    connect(m_chatListView, &QListView::customContextMenuRequested, this, &MessagesView::onChatContextMenu);
    connect(m_composer, &ComposerWidget::sendRequested, this, &MessagesView::sendRequested);
    connect(m_composer, &ComposerWidget::fileRequested, this, &MessagesView::fileRequested);
    connect(m_composer, &ComposerWidget::imageRequested, this, &MessagesView::imageRequested);
    connect(m_composer, &ComposerWidget::emojiRequested, this, &MessagesView::emojiRequested);
    connect(m_composer, &ComposerWidget::mentionRequested, this, &MessagesView::mentionRequested);
    connect(m_composer, &ComposerWidget::clearHistoryRequested, this, &MessagesView::clearHistoryRequested);
    connect(m_composer, &ComposerWidget::filesDropped, this, &MessagesView::filesDropped);

    connect(m_multiSelectForwardBtn, &QPushButton::clicked, this, &MessagesView::multiSelectForwardRequested);
    connect(m_multiSelectDeleteBtn, &QPushButton::clicked, this, &MessagesView::multiSelectDeleteRequested);
    connect(m_multiSelectFavoriteBtn, &QPushButton::clicked, this, &MessagesView::multiSelectFavoriteRequested);
    connect(m_multiSelectCancelBtn, &QPushButton::clicked, this, [this]() { setMultiSelectMode(false); });
    mvLog("setupUi signals done");
}

void MessagesView::setMultiSelectMode(bool enabled)
{
    if (m_multiSelectMode == enabled) return;
    m_multiSelectMode = enabled;

    if (m_multiSelectMode) {
        m_chatListView->setSelectionMode(QAbstractItemView::MultiSelection);
        m_multiSelectBar->setVisible(true);
        m_composer->setVisible(false);
    } else {
        m_chatListView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_chatListView->clearSelection();
        m_multiSelectBar->setVisible(false);
        m_composer->setVisible(true);
    }
}

void MessagesView::setLocalActionState(const QString& actionId, bool visible, bool enabled)
{
    m_localActionStates[actionId] = {visible, enabled};
}

namespace {

QAction* createLocalAction(QMenu* menu, const QString& title, const QString& actionId,
                           const QMap<QString, QPair<bool, bool>>& states)
{
    QAction* action = nullptr;
    const auto it = states.find(actionId);
    if (it != states.end() && !it.value().first) {
        return action; // not visible
    }
    action = menu->addAction(title);
    if (it != states.end() && !it.value().second) {
        action->setEnabled(false);
    }
    return action;
}

} // namespace

void MessagesView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    const QString overlayBg = QStringLiteral("rgba(%1, %2, %3, 30)")
                                  .arg(tm->primaryColor().red())
                                  .arg(tm->primaryColor().green())
                                  .arg(tm->primaryColor().blue());
    const QString sheet = QString(
        "QWidget#messagesView { background-color: %3; }"
        "QFrame#sessionPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel#sessionTitleLabel { color: %6; font-size: 15px; font-weight: 600; }"
        "QFrame#chatPanel { background-color: %4; border: 1px solid %2; border-radius: 8px; }"
        "QFrame#chatHeader { background-color: %4; border-bottom: 1px solid %2; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QLineEdit#sessionSearchEdit { background-color: %5; color: %6; border: 1px solid %2; border-radius: 7px; padding: 7px 10px; }"
        "QLineEdit#sessionSearchEdit:focus { border: 1px solid %7; }"
        "QListView#sessionListView { background: transparent; border: none; outline: none; }"
        "QListView#sessionListView::item { color: %6; padding: 2px 0px; border-radius: 7px; }"
        "QListView#sessionListView::item:selected { background-color: %8; color: %6; }"
        "QListView#sessionListView::item:hover { background-color: %9; }"
        "QListView#chatListView { background: %4; border: none; outline: none; padding: 12px 16px; }"
        "QLabel#chatTitleLabel { color: %6; font-size: 16px; font-weight: 600; }"
        "QLabel#chatSubtitleLabel { color: %10; font-size: 12px; }"
        "QLabel#chatHintLabel { color: %10; font-size: 12px; }"
        "QLabel#emptyChatLabel, QLabel#loadingLabel { color: %10; font-size: 14px; }"
        "QPushButton#chatEssenceBtn { background: %5; color: %7; border: 1px solid %2; border-radius: 6px; padding: 5px 14px; font-size: 13px; }"
        "QPushButton#chatEssenceBtn:hover { background: %8; }"
        "QPushButton#chatGroupMoreBtn { background: transparent; color: %6; border: 1px solid %2; border-radius: 6px; font-size: 20px; font-weight:600; padding:0; text-align:center; }"
        "QPushButton#chatGroupMoreBtn:hover { background: %5; border-color: %7; color: %7; }"
        "QLabel#dropOverlay { background-color: %11; color: %7; border: 2px dashed %7; font-size: 18px; font-weight: 600; }"
        "QPushButton#sessionNewChatBtn { background: transparent; color: %7; border: none; font-size: 18px; }"
        "QPushButton#sessionNewChatBtn:hover { background: %9; border-radius: 6px; }"
        "QMenu#chatContextMenu { background-color: %4; color: %6; border: 1px solid %2; padding: 6px; border-radius: 6px; }"
        "QMenu#chatContextMenu::item { padding: 6px 18px; border-radius: 4px; }"
        "QMenu#chatContextMenu::item:selected { background-color: %5; color: %4; }"
        "QMenu#chatContextMenu::separator { background-color: %2; height: 1px; margin: 4px 8px; }"
    ).arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->backgroundColor().name())
     .arg(tm->backgroundColor().name())
     .arg(tm->backgroundTertiaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->color(QStringLiteral("session-selected")).name())
     .arg(tm->color(QStringLiteral("session-hover")).name())
     .arg(tm->textSecondaryColor().name())
     .arg(overlayBg);
    setStyleSheet(sheet);
}

QStandardItemModel* MessagesView::sessionModel() const
{
    return m_sessionModel;
}

QStandardItemModel* MessagesView::chatModel() const
{
    return m_chatModel;
}

void MessagesView::setSessionModel(QStandardItemModel* model)
{
    if (!model || model == m_sessionModel) return;
    m_sessionModel = model;
    m_sessionListView->setModel(model);
}

void MessagesView::setChatModel(QStandardItemModel* model)
{
    if (!model || model == m_chatModel) return;
    m_chatModel = model;
    m_chatListView->setModel(model);
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
    if (m_composer) m_composer->setSessionName(title);
}

QString MessagesView::chatTitle() const
{
    return m_chatTitleLabel ? m_chatTitleLabel->text() : QString();
}

void MessagesView::setSessionState(const QString& sessionId, const QString& title,
                                   const QString& subtitle, const QString& hint,
                                   bool loading, bool empty)
{
    setCurrentSessionId(sessionId);
    setChatTitle(title, subtitle, hint);
    setLoadingVisible(loading);
    if (!loading) {
        setEmptyStateVisible(empty);
    }
}

void MessagesView::setComposerState(bool enabled, const QString& placeholder,
                                    const QString& stateText, const QStringList& mentionCandidates)
{
    if (!m_composer) return;
    m_composer->setSendEnabled(enabled);
    if (!placeholder.isEmpty()) {
        m_composer->setPlaceholderText(placeholder);
    }
    m_composer->setStateText(stateText);
    if (!mentionCandidates.isEmpty()) {
        m_composer->setMentionCompletions(mentionCandidates);
    }
}

void MessagesView::scrollChatToBottom()
{
    if (m_chatListView) {
        m_chatListView->scrollToBottom();
    }
}

void MessagesView::setEssenceButtonVisible(bool visible)
{
    if (m_essenceBtn) {
        m_essenceBtn->setVisible(visible);
    }
}

void MessagesView::setGroupMoreButtonVisible(bool visible)
{
    if (m_groupMoreBtn) m_groupMoreBtn->setVisible(visible);
}

GroupMemberSidebar* MessagesView::groupMemberSidebar() const
{
    return m_groupMemberSidebar;
}

void MessagesView::setGroupMemberSidebarVisible(bool visible)
{
    if (m_groupMemberSidebar) {
        m_groupMemberSidebar->setVisible(visible);
    }
}

void MessagesView::setCurrentSessionId(const QString& sessionId)
{
    m_currentSessionId = sessionId;
}

QString MessagesView::currentSessionId() const
{
    return m_currentSessionId;
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

bool MessagesView::isInChatPanel(const QPoint& pos) const
{
    return m_chatPanel && m_chatPanel->rect().contains(m_chatPanel->mapFromParent(pos));
}

void MessagesView::showDropOverlay()
{
    if (!m_dropOverlay || !m_chatPanel) return;
    m_dropOverlay->setGeometry(m_chatPanel->rect());
    m_dropOverlay->raise();
    m_dropOverlay->setVisible(true);
}

void MessagesView::hideDropOverlay()
{
    if (m_dropOverlay) m_dropOverlay->setVisible(false);
}

void MessagesView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        showDropOverlay();
    } else {
        event->ignore();
    }
}

void MessagesView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MessagesView::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event)
    hideDropOverlay();
}

void MessagesView::dropEvent(QDropEvent* event)
{
    hideDropOverlay();
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty() && !paths.contains(path)) {
                paths.append(path);
            }
        }
    }
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MessagesView::onChatItemActivated(const QModelIndex& index)
{
    if (!index.isValid()) return;

    emit mediaActivated(index);
}

void MessagesView::onChatContextMenu(const QPoint& pos)
{
    const QModelIndex index = m_chatListView->indexAt(pos);
    if (!index.isValid()) return;

    const QPoint globalPos = m_chatListView->viewport()->mapToGlobal(pos);
    const bool system = index.data(ChatBubbleSystemRole).toBool();
    Q_UNUSED(globalPos)

    const int avatarHitWidth = 48; // 38px avatar + margins
    const bool onAvatar = (pos.x() < avatarHitWidth) ||
                          (pos.x() > m_chatListView->viewport()->width() - avatarHitWidth);

    if (onAvatar && !system) {
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("chatContextMenu"));
        QAction* sendMsg = createLocalAction(&menu, QStringLiteral("发消息"), QStringLiteral("send_message"), m_localActionStates);
        QAction* atAction = createLocalAction(&menu, QStringLiteral("@TA"), QStringLiteral("at"), m_localActionStates);
        QAction* viewProfile = createLocalAction(&menu, QStringLiteral("查看资料"), QStringLiteral("view_profile"), m_localActionStates);
        QAction* addFriend = createLocalAction(&menu, QStringLiteral("添加好友"), QStringLiteral("add_friend"), m_localActionStates);
        menu.addSeparator();
        QAction* setNick = createLocalAction(&menu, QStringLiteral("修改群昵称"), QStringLiteral("set_group_nickname"), m_localActionStates);
        QAction* mute = createLocalAction(&menu, QStringLiteral("禁言/解除"), QStringLiteral("mute_member"), m_localActionStates);
        QAction* setAdmin = createLocalAction(&menu, QStringLiteral("设置/取消管理员"), QStringLiteral("set_admin"), m_localActionStates);
        menu.addSeparator();
        QAction* block = createLocalAction(&menu, QStringLiteral("屏蔽"), QStringLiteral("block"), m_localActionStates);
        QAction* report = createLocalAction(&menu, QStringLiteral("举报"), QStringLiteral("report"), m_localActionStates);

        if (sendMsg) connect(sendMsg, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("sendMessage")); });
        if (atAction) connect(atAction, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("at")); });
        if (viewProfile) connect(viewProfile, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("viewProfile")); });
        if (addFriend) connect(addFriend, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("addFriend")); });
        if (setNick) connect(setNick, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("setGroupNickname")); });
        if (mute) connect(mute, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("mute")); });
        if (setAdmin) connect(setAdmin, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("setAdmin")); });
        if (block) connect(block, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("block")); });
        if (report) connect(report, &QAction::triggered, this, [this, index]() { emit avatarActionRequested(index, QStringLiteral("report")); });
        menu.exec(m_chatListView->viewport()->mapToGlobal(pos));
    } else if (!system) {
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("chatContextMenu"));
        QAction* copy = createLocalAction(&menu, QStringLiteral("复制"), QStringLiteral("copy"), m_localActionStates);
        QAction* forward = createLocalAction(&menu, QStringLiteral("转发"), QStringLiteral("forward"), m_localActionStates);
        QAction* favorite = createLocalAction(&menu, QStringLiteral("收藏"), QStringLiteral("favorite"), m_localActionStates);
        QAction* multiSelect = createLocalAction(&menu, QStringLiteral("多选"), QStringLiteral("multi_select"), m_localActionStates);
        menu.addSeparator();
        QAction* quote = createLocalAction(&menu, QStringLiteral("引用"), QStringLiteral("quote"), m_localActionStates);
        const bool isEssence = index.data(ChatBubbleForwardedRole).toBool();
        QAction* essence = createLocalAction(&menu,
                                             isEssence ? QStringLiteral("取消精华") : QStringLiteral("精华"),
                                             isEssence ? QStringLiteral("unessence") : QStringLiteral("essence"),
                                             m_localActionStates);
        QAction* recall = createLocalAction(&menu, QStringLiteral("撤回"), QStringLiteral("recall"), m_localActionStates);
        QAction* deleteMsg = createLocalAction(&menu, QStringLiteral("删除"), QStringLiteral("delete"), m_localActionStates);

        if (copy) connect(copy, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("copy")); });
        if (forward) connect(forward, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("forward")); });
        if (favorite) connect(favorite, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("favorite")); });
        if (multiSelect) connect(multiSelect, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("multiSelect")); });
        if (quote) connect(quote, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("quote")); });
        if (essence) connect(essence, &QAction::triggered, this, [this, index, isEssence]() { emit messageActionRequested(index, isEssence ? QStringLiteral("unessence") : QStringLiteral("essence")); });
        if (recall) connect(recall, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("recall")); });
        if (deleteMsg) connect(deleteMsg, &QAction::triggered, this, [this, index]() { emit messageActionRequested(index, QStringLiteral("delete")); });
        menu.exec(m_chatListView->viewport()->mapToGlobal(pos));
    }
}

