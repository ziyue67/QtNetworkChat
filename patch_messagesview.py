import sys
from pathlib import Path

path = Path(r"D:\C++VS pro\QtNetworkChat\src\views\messagesview.cpp")
if not path.exists():
    print("File not found:", path)
    sys.exit(1)

text = path.read_text(encoding="utf-8")

# 1) Header layout: put chatTitleLabel in a horizontal row with group action buttons
old_header = '''    m_chatTitleLabel = new QLabel(QStringLiteral("QQ NT"), header);
    m_chatTitleLabel->setObjectName(QStringLiteral("chatTitleLabel"));
    m_chatSubtitleLabel = new QLabel(header);
    m_chatSubtitleLabel->setObjectName(QStringLiteral("chatSubtitleLabel"));
    m_chatHintLabel = new QLabel(header);
    m_chatHintLabel->setObjectName(QStringLiteral("chatHintLabel"));
    headerLayout->addWidget(m_chatTitleLabel);
    headerLayout->addWidget(m_chatSubtitleLabel);
    headerLayout->addWidget(m_chatHintLabel);'''

new_header = '''    m_chatTitleLabel = new QLabel(QStringLiteral("QQ NT"), header);
    m_chatTitleLabel->setObjectName(QStringLiteral("chatTitleLabel"));
    m_chatSubtitleLabel = new QLabel(header);
    m_chatSubtitleLabel->setObjectName(QStringLiteral("chatSubtitleLabel"));
    m_chatHintLabel = new QLabel(header);
    m_chatHintLabel->setObjectName(QStringLiteral("chatHintLabel"));

    QHBoxLayout* titleRow = new QHBoxLayout();
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
    headerLayout->addWidget(m_chatHintLabel);'''

if old_header not in text:
    print("Header layout block not found")
    sys.exit(1)
text = text.replace(old_header, new_header)

# 2) Stylesheet: add group action button styles after chatHeader rule
old_style = '''        "QFrame#chatHeader { background-color: %4; border-bottom: 1px solid %2; border-top-left-radius: 8px; border-top-right-radius: 8px; }"'''
new_style = '''        "QFrame#chatHeader { background-color: %4; border-bottom: 1px solid %2; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QPushButton#chatHeaderGroupMembersBtn, QPushButton#chatHeaderEssenceBtn {"
        "  background-color: %5; color: %6; border: 1px solid %2; border-radius: 6px;"
        "  padding: 4px 10px; font-size: 12px;"
        "}"
        "QPushButton#chatHeaderGroupMembersBtn:hover, QPushButton#chatHeaderEssenceBtn:hover { background-color: %9; }"
        "QPushButton#chatHeaderGroupMembersBtn:pressed, QPushButton#chatHeaderEssenceBtn:pressed { background-color: %5; }"'''
if old_style not in text:
    print("chatHeader style line not found")
    sys.exit(1)
text = text.replace(old_style, new_style)

# 3) Insert accessor methods before setSessionState
insert_marker = '''QString MessagesView::chatTitle() const
{
    return m_chatTitleLabel ? m_chatTitleLabel->text() : QString();
}

void MessagesView::setSessionState('''

insert_replacement = '''QString MessagesView::chatTitle() const
{
    return m_chatTitleLabel ? m_chatTitleLabel->text() : QString();
}

void MessagesView::setGroupActionsVisible(bool visible)
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

void MessagesView::setSessionState('''

if insert_marker not in text:
    print("setSessionState insert marker not found")
    sys.exit(1)
text = text.replace(insert_marker, insert_replacement)

path.write_text(text, encoding="utf-8")
print("messagesview.cpp patched successfully")
