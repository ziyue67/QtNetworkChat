import sys

with open('src/views/messagesview.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

old_block = """    m_searchEdit = new QLineEdit(sessionPanel);
    m_searchEdit->setObjectName(QStringLiteral(\"sessionSearchEdit\"));
    m_searchEdit->setPlaceholderText(QStringLiteral(\"搜索会话...\"));
    QPushButton* newChatBtn = new QPushButton(QStringLiteral(\"+\"), m_searchEdit);
    newChatBtn->setObjectName(QStringLiteral(\"sessionNewChatBtn\"));
    newChatBtn->setCursor(Qt::PointingHandCursor);
    m_searchEdit->setTextMargins(0, 0, 28, 0);
    QHBoxLayout* searchLayout = new QHBoxLayout(m_searchEdit);
    searchLayout->setContentsMargins(0, 0, 4, 0);
    searchLayout->addStretch();
    searchLayout->addWidget(newChatBtn, 0, Qt::AlignVCenter);
    sessionLayout->addWidget(m_searchEdit);"""

new_block = """    QFrame* searchContainer = new QFrame(sessionPanel);
    searchContainer->setObjectName(QStringLiteral(\"sessionSearchContainer\"));
    QHBoxLayout* searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(4, 0, 4, 0);
    searchLayout->setSpacing(0);

    m_searchEdit = new QLineEdit(searchContainer);
    m_searchEdit->setObjectName(QStringLiteral(\"sessionSearchEdit\"));
    m_searchEdit->setPlaceholderText(QStringLiteral(\"搜索会话...\"));
    QPushButton* newChatBtn = new QPushButton(QStringLiteral(\"+\"), searchContainer);
    newChatBtn->setObjectName(QStringLiteral(\"sessionNewChatBtn\"));
    newChatBtn->setCursor(Qt::PointingHandCursor);
    newChatBtn->setFixedSize(22, 22);
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(newChatBtn, 0, Qt::AlignCenter);
    sessionLayout->addWidget(searchContainer);"""

if old_block not in content:
    print('NOT FOUND: search block')
    sys.exit(1)

content = content.replace(old_block, new_block, 1)

with open('src/views/messagesview.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
print('done')
