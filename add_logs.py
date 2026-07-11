import sys

with open('src/views/messagesview.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

replacements = [
    ("void MessagesView::setupUi()\n{", "void MessagesView::setupUi()\n{\n    mvLog(\"setupUi start\");"),
    ("    sessionLayout->addWidget(m_sessionListView, 1);\n\n    // Chat panel", "    sessionLayout->addWidget(m_sessionListView, 1);\n    mvLog(\"session panel done\");\n\n    // Chat panel"),
    ("    m_chatListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);\n    chatLayout->addWidget(m_chatListView, 1);\n\n    m_emptyLabel", "    m_chatListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);\n    chatLayout->addWidget(m_chatListView, 1);\n    mvLog(\"chat list done\");\n\n    m_emptyLabel"),
    ("    m_composer = new ComposerWidget(chatPanel);\n    chatLayout->addWidget(m_composer);\n\n    // Multi-select", "    m_composer = new ComposerWidget(chatPanel);\n    chatLayout->addWidget(m_composer);\n    mvLog(\"composer done\");\n\n    // Multi-select"),
    ("    chatLayout->addWidget(m_multiSelectBar);\n\n    root->addWidget(sessionPanel);\n    root->addWidget(chatPanel, 1);\n\n    setAcceptDrops(true);", "    chatLayout->addWidget(m_multiSelectBar);\n\n    root->addWidget(sessionPanel);\n    root->addWidget(chatPanel, 1);\n    mvLog(\"root layout done\");\n\n    setAcceptDrops(true);"),
    ("    setAcceptDrops(true);\n\n    connect(m_sessionListView", "    setAcceptDrops(true);\n    mvLog(\"acceptDrops set\");\n\n    connect(m_sessionListView"),
    ("    connect(m_multiSelectCancelBtn, &QPushButton::clicked, this, [this]() { setMultiSelectMode(false); });\n}", "    connect(m_multiSelectCancelBtn, &QPushButton::clicked, this, [this]() { setMultiSelectMode(false); });\n    mvLog(\"setupUi signals done\");\n}"),
]

for old, new in replacements:
    if old not in content:
        print('NOT FOUND:', old[:60])
        sys.exit(1)
    content = content.replace(old, new, 1)

with open('src/views/messagesview.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
print('done')
