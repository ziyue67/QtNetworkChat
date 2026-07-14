#include "widgets/contactcard.h"

#include "theme/thememanager.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
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
    return palette.isEmpty() ? QColor(QStringLiteral("#12a4ff")) : palette.at(qAbs(sum) % palette.size());
}

QPixmap renderTextAvatar(const QString& text, const QColor& bg, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath circle;
    circle.addEllipse(QRectF(1, 1, size - 2, size - 2));
    painter.setClipPath(circle);
    painter.fillRect(pixmap.rect(), bg);
    QFont f = QApplication::font();
    f.setPixelSize(size * 2 / 5);
    f.setBold(true);
    painter.setFont(f);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text.left(1).toUpper());
    painter.setClipping(false);
    return pixmap;
}
}

ContactCard::ContactCard(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("contactCard"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ContactCard::updateStyle);
}

void ContactCard::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* content = new QWidget;
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setAlignment(Qt::AlignCenter);
    contentLayout->setContentsMargins(32, 40, 32, 40);
    contentLayout->setSpacing(12);

    m_avatar = new QLabel(content);
    m_avatar->setFixedSize(96, 96);
    m_avatar->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_avatar, 0, Qt::AlignHCenter);

    m_name = new QLabel(content);
    m_name->setObjectName(QStringLiteral("contactCardName"));
    m_name->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_name, 0, Qt::AlignHCenter);

    m_state = new QLabel(content);
    m_state->setObjectName(QStringLiteral("contactCardState"));
    m_state->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_state, 0, Qt::AlignHCenter);

    m_signature = new QLabel(content);
    m_signature->setObjectName(QStringLiteral("contactCardSignature"));
    m_signature->setAlignment(Qt::AlignCenter);
    m_signature->setWordWrap(true);
    contentLayout->addWidget(m_signature, 0, Qt::AlignHCenter);

    m_remark = new QLabel(content);
    m_remark->setObjectName(QStringLiteral("contactCardRemark"));
    m_remark->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_remark, 0, Qt::AlignHCenter);

    m_announcementCard = new QFrame(content);
    m_announcementCard->setObjectName(QStringLiteral("contactCardAnnouncementCard"));
    QVBoxLayout* annLayout = new QVBoxLayout(m_announcementCard);
    annLayout->setContentsMargins(20, 18, 20, 18);
    annLayout->setSpacing(10);

    m_announcementTitle = new QLabel(QStringLiteral("群公告"), m_announcementCard);
    m_announcementTitle->setObjectName(QStringLiteral("contactCardAnnouncementTitle"));
    annLayout->addWidget(m_announcementTitle);

    m_announcementBody = new QLabel(m_announcementCard);
    m_announcementBody->setObjectName(QStringLiteral("contactCardAnnouncementBody"));
    m_announcementBody->setWordWrap(true);
    annLayout->addWidget(m_announcementBody);

    QHBoxLayout* memberHeader = new QHBoxLayout();
    memberHeader->setSpacing(6);
    m_memberTitle = new QLabel(QStringLiteral("群成员"), m_announcementCard);
    m_memberTitle->setObjectName(QStringLiteral("contactCardAnnouncementTitle"));
    m_memberCount = new QLabel(m_announcementCard);
    m_memberCount->setObjectName(QStringLiteral("contactCardState"));
    memberHeader->addWidget(m_memberTitle);
    memberHeader->addStretch();
    memberHeader->addWidget(m_memberCount);
    annLayout->addLayout(memberHeader);

    m_memberGrid = new QListView(m_announcementCard);
    m_memberGrid->setViewMode(QListView::IconMode);
    m_memberGrid->setFlow(QListView::LeftToRight);
    m_memberGrid->setResizeMode(QListView::Adjust);
    m_memberGrid->setWrapping(true);
    m_memberGrid->setSpacing(12);
    m_memberGrid->setFrameShape(QFrame::NoFrame);
    m_memberGrid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_memberGrid->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_memberGrid->setFixedHeight(150);
    m_memberModel = new QStandardItemModel(this);
    m_memberGrid->setModel(m_memberModel);
    m_memberGrid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    annLayout->addWidget(m_memberGrid, 1);

    contentLayout->addWidget(m_announcementCard, 0, Qt::AlignHCenter);
    contentLayout->addStretch();

    m_actionBtn = new QPushButton(content);
    m_actionBtn->setObjectName(QStringLiteral("contactCardActionBtn"));
    m_actionBtn->setFixedHeight(42);
    m_actionBtn->setMinimumWidth(220);
    connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
        emit sendMessageRequested(m_currentId, m_isGroup);
    });
    contentLayout->addWidget(m_actionBtn, 0, Qt::AlignHCenter);

    scroll->setWidget(content);
    root->addWidget(scroll);

    m_announcementCard->setVisible(false);
    m_actionBtn->setVisible(false);
}

void ContactCard::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QWidget#contactCard { background-color: %1; }"
        "QLabel#contactCardName { color: %2; font-size: 20px; font-weight: 600; }"
        "QLabel#contactCardState { color: %3; font-size: 13px; }"
        "QLabel#contactCardSignature { color: %4; font-size: 13px; max-width: 320px; }"
        "QLabel#contactCardRemark { color: %4; font-size: 12px; }"
        "QFrame#contactCardAnnouncementCard { background-color: %7; border: 1px solid %5; border-radius: 10px; min-width: 360px; max-width: 420px; }"
        "QLabel#contactCardAnnouncementTitle { color: %2; font-size: 14px; font-weight: 600; }"
        "QLabel#contactCardAnnouncementBody { color: %3; font-size: 13px; line-height: 140%; }"
        "QPushButton#contactCardActionBtn { background-color: %6; color: white; border: none; border-radius: 8px; font-size: 14px; font-weight: 600; }"
        "QPushButton#contactCardActionBtn:hover { background-color: %8; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->textTertiaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->primaryHoverColor().name()));
}

void ContactCard::setAvatar(const QString& nickname, const QString& id)
{
    const QPixmap pix = renderTextAvatar(nickname, avatarColor(id.isEmpty() ? nickname : id), 96);
    m_avatar->setPixmap(pix);
}

void ContactCard::setFriendData(const ContactDisplayData& data)
{
    clear();
    m_currentId = data.id;
    m_isGroup = false;
    setAvatar(data.nickname, data.id);
    m_name->setText(data.nickname);
    m_state->setText(data.isOnline || data.status == QStringLiteral("online")
                         ? QStringLiteral("在线")
                         : QStringLiteral("离线"));
    m_signature->setText(data.signature.isEmpty() ? QStringLiteral(" ") : data.signature);
    m_remark->setText(data.remark.isEmpty() ? QString() : QStringLiteral("备注：%1").arg(data.remark));
    m_remark->setVisible(!data.remark.isEmpty());
    m_announcementCard->setVisible(false);
    m_actionBtn->setText(QStringLiteral("发送消息"));
    m_actionBtn->setVisible(true);
}

void ContactCard::setGroupData(const ContactDisplayData& group,
                              const QList<ContactDisplayData>& members)
{
    clear();
    m_currentId = group.id;
    m_isGroup = true;
    setAvatar(group.nickname, group.id);
    m_name->setText(group.nickname);
    m_state->setText(QStringLiteral("群聊"));
    m_signature->setText(group.signature.isEmpty() ? QStringLiteral(" ") : group.signature);
    m_remark->clear();
    m_remark->setVisible(false);
    m_announcementTitle->setText(QStringLiteral("群公告"));
    m_announcementBody->setText(group.signature.isEmpty()
                                     ? QStringLiteral("暂无群公告")
                                     : group.signature);
    m_memberTitle->setText(QStringLiteral("群成员"));
    m_memberCount->setText(QStringLiteral("%1 人").arg(members.isEmpty() ? group.memberCount : members.size()));
    refreshMembers(members);
    m_announcementCard->setVisible(true);
    m_actionBtn->setText(QStringLiteral("发送群消息"));
    m_actionBtn->setVisible(true);
}

void ContactCard::refreshMembers(const QList<ContactDisplayData>& members)
{
    m_memberModel->clear();
    int shown = 0;
    for (const ContactDisplayData& m : members) {
        if (shown++ >= 12) break;
        QStandardItem* item = new QStandardItem();
        QPixmap pix = renderTextAvatar(m.nickname, avatarColor(m.id), 48);
        item->setIcon(pix);
        item->setText(m.nickname);
        item->setData(m.id, Qt::UserRole);
        item->setFlags(Qt::ItemIsEnabled);
        item->setSizeHint(QSize(60, 70));
        const QString tooltip = m.remark.isEmpty() ? m.nickname : m.remark;
        item->setToolTip(tooltip);
        m_memberModel->appendRow(item);
    }
    m_memberGrid->setVisible(m_memberModel->rowCount() > 0);
}

void ContactCard::clear()
{
    m_currentId.clear();
    m_isGroup = false;
    m_avatar->clear();
    m_name->clear();
    m_state->clear();
    m_signature->clear();
    m_remark->clear();
    m_announcementBody->clear();
    m_memberModel->clear();
    m_announcementCard->setVisible(false);
    m_actionBtn->setVisible(false);
}
