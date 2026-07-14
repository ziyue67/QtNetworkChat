import pathlib
p = pathlib.Path('D:\\C++VS pro\\QtNetworkChat\\src\\mainwindow.cpp')
t = p.read_text(encoding='utf-8')

# Replace onShowEssencePanel body
old = '''void MainWindow::onShowEssencePanel()
{
    if (!m_essencePanel) return;
    m_essencePanel->show();
}'''

new = '''void MainWindow::onShowEssencePanel()
{
    if (!m_essencePanel) return;
    m_essencePanel->clearMessages();
    const int rowCount = m_chatModel ? m_chatModel->rowCount() : 0;
    for (int row = 0; row < rowCount; ++row) {
        QStandardItem* item = m_chatModel->item(row);
        if (!item) continue;
        if (!item->data(ChatBubbleForwardedRole).toBool()) continue;
        const QString text = item->data(Qt::DisplayRole).toString();
        if (text.isEmpty()) continue;
        const QString sender = item->data(ChatBubbleSenderNameRole).toString();
        const QString timestamp = item->data(ChatBubbleTimestampRole).toString();
        const QString messageId = item->data(Qt::UserRole).toString();
        m_essencePanel->addEssenceMessage(messageId.isEmpty() ? QString::number(row) : messageId,
                                          sender.isEmpty() ? QStringLiteral("未知") : sender,
                                          text, timestamp.isEmpty() ? QStringLiteral("--:--") : timestamp);
    }
    m_essencePanel->show();
}'''

if old in t:
    t = t.replace(old, new)
else:
    print('WARN: onShowEssencePanel not found')

# After essence command succeeds, also add to panel
old2 = '''    } else if (commandId == QLatin1String("essence") || commandId == QLatin1String("unessence")) {
        QStandardItem* item = m_chatModel->item(index.row());
        if (item) {
            const bool isEssence = (commandId == QLatin1String("essence"));
            item->setData(isEssence, ChatBubbleForwardedRole);
            item->setToolTip(isEssence ? QStringLiteral("精华消息") : QStringLiteral("已取消精华"));
        }
    }'''

new2 = '''    } else if (commandId == QLatin1String("essence") || commandId == QLatin1String("unessence")) {
        QStandardItem* item = m_chatModel->item(index.row());
        if (item) {
            const bool isEssence = (commandId == QLatin1String("essence"));
            item->setData(isEssence, ChatBubbleForwardedRole);
            item->setToolTip(isEssence ? QStringLiteral("精华消息") : QStringLiteral("已取消精华"));
            if (isEssence && m_essencePanel) {
                const QString text = item->data(Qt::DisplayRole).toString();
                const QString sender = index.data(ChatBubbleSenderNameRole).toString();
                const QString timestamp = index.data(ChatBubbleTimestampRole).toString();
                const QString messageId = item->data(Qt::UserRole).toString();
                m_essencePanel->addEssenceMessage(messageId.isEmpty() ? QString::number(index.row()) : messageId,
                                                  sender.isEmpty() ? QStringLiteral("未知") : sender,
                                                  text, timestamp.isEmpty() ? QStringLiteral("--:--") : timestamp);
            }
        }
    }'''

if old2 in t:
    t = t.replace(old2, new2)
else:
    print('WARN: essence branch not found')

p.write_text(t, encoding='utf-8')
print('essence patched')
