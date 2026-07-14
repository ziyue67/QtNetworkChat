import re
from pathlib import Path

path = Path(r"D:\C++VS pro\QtNetworkChat\src\views\messagesview.cpp")
text = path.read_text(encoding="utf-8")

# 1) Header layout: wrap title label in a horizontal row with group action buttons
text = re.sub(
    r'(m_chatTitleLabel = new QLabel\(QStringLiteral\("QQ NT"\), header\);\s*'
    r'm_chatTitleLabel->setObjectName\(QStringLiteral\("chatTitleLabel"\)\);\s*'
    r'm_chatSubtitleLabel = new QLabel\(header\);\s*'
    r'm_chatSubtitleLabel->setObjectName\(QStringLiteral\("chatSubtitleLabel"\)\);\s*'
    r'm_chatHintLabel = new QLabel\(header\);\s*'
    r'm_chatHintLabel->setObjectName\(QStringLiteral\("chatHintLabel"\)\);\s*)'
    r'headerLayout->addWidget\(m_chatTitleLabel\);\s*'
    r'headerLayout->addWidget\(m_chatSubtitleLabel\);\s*'
    r'headerLayout->addWidget\(m_chatHintLabel\);',
    r'''\1QHBoxLayout* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(m_chatTitleLabel);
    titleRow->addStretch();

    m_groupMembersBtn = new QPushButton(QStringLiteral("群成员"), header);
    m_groupMembersBtn->setObjectName(QStringLiteral("chatHeaderGroupMembersBtn"));
    m_groupMembersBtn->setCursor(Qt::PointingHandCursor);
    m_groupMembersBtn->setVisible(false);
    connect(m_groupMembersBtn, &QPushButton::clicked, this, &MessagesView::groupMembersRequested);

    m_essenceBtn = new QPushButton(QStringLiteral("精华消息"), header);
    m_essenceBtn->setObjectName(QStringLiteral("chatHeaderEssenceBtn"));
    m_essenceBtn->setCursor(Qt::PointingHandCursor);
    m_essenceBtn->setVisible(false);
    connect(m_essenceBtn, &QPushButton::clicked, this, &MessagesView::essenceRequested);

    titleRow->addWidget(m_groupMembersBtn);
    titleRow->addWidget(m_essenceBtn);

    headerLayout->addLayout(titleRow);
    headerLayout->addWidget(m_chatSubtitleLabel);
    headerLayout->addWidget(m_chatHintLabel);''',
    text,
    count=1,
)

# 2) Stylesheet: add group action button styles after chatHeader rule
text = re.sub(
    r'"QFrame#chatHeader \{ background-color: %4; border-bottom: 1px solid %2; border-top-left-radius: 8px; border-top-right-radius: 8px; \}"',
    '''"QFrame#chatHeader { background-color: %4; border-bottom: 1px solid %2; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QPushButton#chatHeaderGroupMembersBtn, QPushButton#chatHeaderEssenceBtn {"
        "  background-color: %5; color: %6; border: 1px solid %2; border-radius: 6px;"
        "  padding: 4px 10px; font-size: 12px;"
        "}"
        "QPushButton#chatHeaderGroupMembersBtn:hover, QPushButton#chatHeaderEssenceBtn:hover { background-color: %9; }"
        "QPushButton#chatHeaderGroupMembersBtn:pressed, QPushButton#chatHeaderEssenceBtn:pressed { background-color: %5; }"''',
    text,
    count=1,
)

# 3) Insert accessor methods before setSessionState
text = re.sub(
    r'(QString MessagesView::chatTitle\(\) const\s*\{\s*return m_chatTitleLabel \? m_chatTitleLabel->text\(\) : QString\(\);\s*\}\s*)'
    r'(void MessagesView::setSessionState\(const QString& sessionId, const QString& title,)',
    r'''\1void MessagesView::setGroupActionsVisible(bool visible)
{
    if (m_groupMembersBtn) m_groupMembersBtn->setVisible(visible);
    if (m_essenceBtn) m_essenceBtn->setVisible(visible);
}

QPushButton* MessagesView::groupMembersBtn() const
{
    return m_groupMembersBtn;
}

QPushButton* MessagesView::essenceBtn() const
{
    return m_essenceBtn;
}

\2''',
    text,
    count=1,
)

path.write_text(text, encoding="utf-8")
print("messagesview.cpp patched via regex")
