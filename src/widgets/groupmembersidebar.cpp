#include "widgets/groupmembersidebar.h"

#include "theme/thememanager.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QColor avatarColor(const QString& id)
{
    const QStringList palette = {
        QStringLiteral("#12a4ff"), QStringLiteral("#18c98b"),
        QStringLiteral("#ff9f1a"), QStringLiteral("#fb6f92"),
        QStringLiteral("#12c9bd"), QStringLiteral("#8b7cf6"),
    };
    int sum = 0;
    for (const QChar& c : id) sum += c.unicode();
    return palette.at(qAbs(sum) % palette.size());
}

QPixmap renderAvatar(const QString& name, int size)
{
    QColor bg = avatarColor(name);
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(1, 1, size - 2, size - 2);
    p.setClipPath(path);
    p.fillRect(pix.rect(), bg);
    QFont f = QApplication::font();
    f.setPixelSize(size * 2 / 5);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pix.rect(), Qt::AlignCenter, name.left(1).toUpper());
    p.setClipping(false);
    return pix;
}

QString roleLabel(const QString& role)
{
    if (role == QStringLiteral("owner")) return QStringLiteral("群主");
    if (role == QStringLiteral("admin")) return QStringLiteral("管理员");
    return QString();
}

} // namespace

GroupMemberItemDelegate::GroupMemberItemDelegate(QObject* parent)
    : QAbstractItemDelegate(parent)
{
}

void GroupMemberItemDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    const bool selected = option.state & QStyle::State_Selected;
    const QRect rect = option.rect.adjusted(4, 2, -4, -2);
    ThemeManager* tm = ThemeManager::instance();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (selected) {
        painter->fillRect(rect, tm->primarySoftColor());
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(rect, tm->color(QStringLiteral("quiet-hover")));
    }

    const GroupMemberDisplayData data = index.data(Qt::UserRole).value<GroupMemberDisplayData>();
    const int avatarSize = 32;
    QRect avatarRect(rect.left() + 8, rect.top() + 6, avatarSize, avatarSize);
    painter->drawPixmap(avatarRect, renderAvatar(data.nickname, avatarSize));

    const QString displayName = data.groupNickname.isEmpty() ? data.nickname : data.groupNickname;
    const QString subtext = data.isMuted ? QStringLiteral("禁言中")
                                         : (data.groupNickname.isEmpty() ? data.nickname : data.groupNickname);

    QFont nameFont = option.font;
    nameFont.setPixelSize(13);
    painter->setFont(nameFont);
    painter->setPen(tm->textColor());
    QRect nameRect(avatarRect.right() + 10, rect.top() + 5, rect.width() - avatarRect.right() - 50, 18);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(displayName, Qt::ElideRight, nameRect.width()));

    QFont roleFont = option.font;
    roleFont.setPixelSize(10);
    painter->setFont(roleFont);
    const QString role = roleLabel(data.role);
    if (!role.isEmpty()) {
        painter->setPen(tm->color(data.role == QStringLiteral("owner") ? QStringLiteral("role-owner")
                                                                         : QStringLiteral("role-admin")));
        painter->drawText(nameRect.right() + 6, rect.top() + 8, role);
    }

    QFont smallFont = option.font;
    smallFont.setPixelSize(11);
    painter->setFont(smallFont);
    painter->setPen(data.isMuted ? tm->dangerColor() : tm->textSecondaryColor());
    QRect subRect(avatarRect.right() + 10, rect.top() + 22, rect.width() - avatarRect.right() - 18, 16);
    painter->drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(subtext, Qt::ElideRight, subRect.width()));
}

QSize GroupMemberItemDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(100, 46);
}

GroupMemberSidebar::GroupMemberSidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("groupMemberSidebar"));
    setMinimumWidth(256);
    setMaximumWidth(320);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &GroupMemberSidebar::updateStyle);
}

void GroupMemberSidebar::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QWidget* header = new QWidget(this);
    header->setFixedHeight(54);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 0, 8, 0);
    m_titleLabel = new QLabel(QStringLiteral("群成员"), header);
    m_titleLabel->setObjectName(QStringLiteral("sidebarTitle"));
    headerLayout->addWidget(m_titleLabel);
    m_countLabel = new QLabel(QStringLiteral("(0)"), header);
    m_countLabel->setObjectName(QStringLiteral("sidebarHint"));
    headerLayout->addWidget(m_countLabel);
    headerLayout->addStretch();
    m_closeBtn = new QPushButton(QStringLiteral("×"), header);
    m_closeBtn->setObjectName(QStringLiteral("sidebarCloseBtn"));
    m_closeBtn->setFixedSize(24, 24);
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    headerLayout->addWidget(m_closeBtn);
    root->addWidget(header);

    QFrame* divider1 = new QFrame(this);
    divider1->setFrameShape(QFrame::HLine);
    divider1->setObjectName(QStringLiteral("sidebarDivider"));
    root->addWidget(divider1);

    QWidget* searchArea = new QWidget(this);
    QHBoxLayout* searchLayout = new QHBoxLayout(searchArea);
    searchLayout->setContentsMargins(12, 8, 12, 8);
    m_searchEdit = new QLineEdit(searchArea);
    m_searchEdit->setObjectName(QStringLiteral("sidebarSearch"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索成员"));
    searchLayout->addWidget(m_searchEdit);
    root->addWidget(searchArea);

    QWidget* announceArea = new QWidget(this);
    QVBoxLayout* announceLayout = new QVBoxLayout(announceArea);
    announceLayout->setContentsMargins(14, 8, 14, 8);
    announceLayout->setSpacing(4);
    QLabel* announceTitle = new QLabel(QStringLiteral("群公告"), announceArea);
    announceTitle->setObjectName(QStringLiteral("sidebarSectionTitle"));
    announceLayout->addWidget(announceTitle);
    m_announcementLabel = new QLabel(QStringLiteral("暂无公告"), announceArea);
    m_announcementLabel->setObjectName(QStringLiteral("sidebarHint"));
    m_announcementLabel->setWordWrap(true);
    announceLayout->addWidget(m_announcementLabel);
    root->addWidget(announceArea);

    QFrame* divider2 = new QFrame(this);
    divider2->setFrameShape(QFrame::HLine);
    divider2->setObjectName(QStringLiteral("sidebarDivider"));
    root->addWidget(divider2);

    m_model = new QStandardItemModel(this);
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("memberListView"));
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(new GroupMemberItemDelegate(m_listView));
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QListView::clicked, this, &GroupMemberSidebar::onMemberClicked);
    connect(m_listView, &QListView::customContextMenuRequested, this, &GroupMemberSidebar::showContextMenu);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString t = text.trimmed();
        m_filtered.clear();
        if (t.isEmpty()) {
            m_filtered = m_members;
        } else {
            for (const auto& m : m_members) {
                if (m.nickname.contains(t, Qt::CaseInsensitive)
                    || m.groupNickname.contains(t, Qt::CaseInsensitive)
                    || m.id.contains(t)) {
                    m_filtered.append(m);
                }
            }
        }
        refreshRows();
    });
    root->addWidget(m_listView, 1);
}

void GroupMemberSidebar::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QWidget#groupMemberSidebar { background-color: %1; border-left: 1px solid %5; }"
        "QLabel#sidebarTitle { color: %2; font-size: 15px; font-weight: 600; }"
        "QLabel#sidebarSectionTitle { color: %2; font-size: 13px; font-weight: 600; }"
        "QLabel#sidebarHint { color: %3; font-size: 12px; }"
        "QLineEdit#sidebarSearch { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 6px 10px; }"
        "QLineEdit#sidebarSearch:focus { border: 1px solid %6; }"
        "QPushButton#sidebarCloseBtn { color: %3; background-color: transparent; border: none; font-size: 18px; }"
        "QPushButton#sidebarCloseBtn:hover { color: %2; }"
        "QFrame#sidebarDivider { color: %5; }"
        "QListView#memberListView { background-color: %1; border: none; color: %2; outline: none; }"
        "QListView#memberListView::item { border-radius: 6px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name()));
}

void GroupMemberSidebar::setGroupId(const QString& groupId)
{
    m_groupId = groupId;
}

void GroupMemberSidebar::setGroupName(const QString& name)
{
    m_groupName = name;
    m_titleLabel->setText(name.isEmpty() ? QStringLiteral("群成员") : name);
}

void GroupMemberSidebar::setAnnouncement(const QString& text)
{
    m_announcement = text;
    m_announcementLabel->setText(text.isEmpty() ? QStringLiteral("暂无公告") : text);
}

void GroupMemberSidebar::setMembers(const QList<GroupMemberDisplayData>& members)
{
    m_members = members;
    m_filtered = members;
    refreshRows();
}

void GroupMemberSidebar::setOnlineUsers(const QSet<QString>& onlineIds)
{
    m_onlineIds = onlineIds;
    for (auto& m : m_members) {
        m.isOnline = m_onlineIds.contains(m.id);
    }
    refreshRows();
}

void GroupMemberSidebar::refreshRows()
{
    m_model->clear();
    // owner/admins first
    QList<GroupMemberDisplayData> sorted = m_filtered;
    std::sort(sorted.begin(), sorted.end(), [](const GroupMemberDisplayData& a, const GroupMemberDisplayData& b) {
        int ra = (a.role == QStringLiteral("owner")) ? 2 : (a.role == QStringLiteral("admin") ? 1 : 0);
        int rb = (b.role == QStringLiteral("owner")) ? 2 : (b.role == QStringLiteral("admin") ? 1 : 0);
        return ra > rb;
    });
    for (const auto& m : sorted) {
        QStandardItem* item = new QStandardItem();
        item->setData(QVariant::fromValue(m), Qt::UserRole);
        m_model->appendRow(item);
    }
    m_countLabel->setText(QStringLiteral("(%1)").arg(sorted.size()));
}

void GroupMemberSidebar::onMemberClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    const GroupMemberDisplayData data = index.data(Qt::UserRole).value<GroupMemberDisplayData>();
    emit viewProfile(data.id);
}

void GroupMemberSidebar::showContextMenu(const QPoint& pos)
{
    QModelIndex index = m_listView->indexAt(pos);
    if (!index.isValid()) return;
    const GroupMemberDisplayData data = index.data(Qt::UserRole).value<GroupMemberDisplayData>();
    QMenu menu(this);
    menu.addAction(QStringLiteral("发送消息"), this, [this, data]() { emit chatWithMember(data.id); });
    menu.addAction(QStringLiteral("@TA"), this, [this, data]() {
        const QString name = data.groupNickname.isEmpty() ? data.nickname : data.groupNickname;
        emit atMember(data.id, name);
    });
    menu.addAction(QStringLiteral("查看资料"), this, [this, data]() { emit viewProfile(data.id); });
    menu.addAction(QStringLiteral("添加好友"), this, [this, data]() { emit addFriend(data.id); });
    menu.addSeparator();
    QMenu* muteMenu = menu.addMenu(QStringLiteral("设置禁言"));
    muteMenu->addAction(QStringLiteral("10分钟"), this, [this, data]() { emit muteMember(data.id, 10); });
    muteMenu->addAction(QStringLiteral("1小时"), this, [this, data]() { emit muteMember(data.id, 60); });
    muteMenu->addAction(QStringLiteral("12小时"), this, [this, data]() { emit muteMember(data.id, 720); });
    muteMenu->addAction(QStringLiteral("1天"), this, [this, data]() { emit muteMember(data.id, 1440); });
    muteMenu->addAction(QStringLiteral("自定义"), this, [this, data]() { emit muteMember(data.id, -1); });
    menu.addAction(QStringLiteral("解除禁言"), this, [this, data]() { emit unmuteMember(data.id); });
    menu.addAction(QStringLiteral("修改群昵称"), this, [this, data]() {
        emit renameMember(data.id, data.groupNickname);
    });
    menu.addSeparator();
    QMenu* adminMenu = menu.addMenu(QStringLiteral("群管理"));
    adminMenu->addAction(QStringLiteral("设为管理员"), this, [this, data]() { emit promoteAdmin(data.id); });
    adminMenu->addAction(QStringLiteral("取消管理员"), this, [this, data]() { emit demoteAdmin(data.id); });
    adminMenu->addSeparator();
    adminMenu->addAction(QStringLiteral("移出本群"), this, [this, data]() { emit kickMember(data.id); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("举报"), this, [this, data]() { emit reportMember(data.id); });
    menu.addAction(QStringLiteral("屏蔽"), this, [this, data]() { emit blockMember(data.id); });
    menu.exec(m_listView->viewport()->mapToGlobal(pos));
}
