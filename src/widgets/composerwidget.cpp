#include "widgets/composerwidget.h"

#include "theme/thememanager.h"
#include "widgets/composerTextEdit.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QPushButton* createToolButton(const QString& icon, const QString& tip, const QString& objName)
{
    QPushButton* btn = new QPushButton(icon);
    btn->setObjectName(objName);
    btn->setFixedSize(30, 30);
    btn->setToolTip(tip);
    btn->setFlat(true);
    btn->setFont(QFont(QStringLiteral("Segoe UI Symbol"), 14));
    return btn;
}
}

ComposerWidget::ComposerWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("composerWidget"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ComposerWidget::updateStyle);
}

void ComposerWidget::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);
    root->setSpacing(8);

    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setSpacing(4);
    m_emojiBtn = createToolButton(QStringLiteral("😊"), QStringLiteral("表情"), QStringLiteral("composerToolBtn"));
    m_imageBtn = createToolButton(QStringLiteral("🖼️"), QStringLiteral("图片/视频"), QStringLiteral("composerToolBtn"));
    m_fileBtn = createToolButton(QStringLiteral("📎"), QStringLiteral("文件"), QStringLiteral("composerToolBtn"));
    m_historyBtn = createToolButton(QStringLiteral("🗑️"), QStringLiteral("清空历史"), QStringLiteral("composerToolBtn"));
    m_mentionBtn = createToolButton(QStringLiteral("@"), QStringLiteral("@提及"), QStringLiteral("composerToolBtn"));
    toolbar->addWidget(m_emojiBtn);
    toolbar->addWidget(m_imageBtn);
    toolbar->addWidget(m_fileBtn);
    toolbar->addWidget(m_historyBtn);
    toolbar->addWidget(m_mentionBtn);
    toolbar->addStretch();
    root->addLayout(toolbar);

    ComposerTextEdit* input = new ComposerTextEdit(this);
    m_input = input;
    m_input->setObjectName(QStringLiteral("composerInput"));
    m_input->setMaximumHeight(120);
    m_input->setPlaceholderText(QStringLiteral("输入消息... (Enter 发送，Shift/Ctrl+Enter 换行)"));
    root->addWidget(m_input, 1);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(QStringLiteral("composerStateLabel"));
    m_stateLabel->setText(QStringLiteral("Enter 发送，Shift/Ctrl+Enter 换行"));
    root->addWidget(m_stateLabel);

    QHBoxLayout* sendRow = new QHBoxLayout();
    sendRow->addStretch();
    m_sendBtn = new QPushButton(QStringLiteral("发送"), this);
    m_sendBtn->setObjectName(QStringLiteral("composerSendBtn"));
    m_sendBtn->setFixedSize(80, 32);
    m_sendBtn->setEnabled(false);
    sendRow->addWidget(m_sendBtn);
    root->addLayout(sendRow);

    connect(input, &ComposerTextEdit::sendRequested, this, &ComposerWidget::sendRequested);
    connect(m_input, &QTextEdit::textChanged, this, [this]() {
        m_sendBtn->setEnabled(!m_input->toPlainText().trimmed().isEmpty());
        emit textChanged();
    });
    connect(m_emojiBtn, &QPushButton::clicked, this, &ComposerWidget::emojiRequested);
    connect(m_imageBtn, &QPushButton::clicked, this, &ComposerWidget::imageRequested);
    connect(m_fileBtn, &QPushButton::clicked, this, &ComposerWidget::fileRequested);
    connect(m_historyBtn, &QPushButton::clicked, this, &ComposerWidget::clearHistoryRequested);
    connect(m_mentionBtn, &QPushButton::clicked, this, &ComposerWidget::mentionRequested);
    connect(m_sendBtn, &QPushButton::clicked, this, &ComposerWidget::sendRequested);
}

void ComposerWidget::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#composerWidget { background-color: %1; border-top: 1px solid %2; }"
        "QTextEdit#composerInput { background-color: %3; color: %4; border: 1px solid %2; border-radius: 6px; padding: 6px; }"
        "QTextEdit#composerInput:focus { border: 1px solid %5; }"
        "QPushButton#composerToolBtn { color: %6; border: none; background: transparent; border-radius: 6px; }"
        "QPushButton#composerToolBtn:hover { background-color: %7; color: %4; }"
        "QPushButton#composerSendBtn { background-color: %5; color: white; border: none; border-radius: 6px; font-weight: 600; }"
        "QPushButton#composerSendBtn:hover { background-color: %8; }"
        "QPushButton#composerSendBtn:disabled { background-color: %7; color: %6; }"
        "QLabel#composerStateLabel { color: %6; font-size: 11px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->backgroundTertiaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->textTertiaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->primaryHoverColor().name()));
}

QString ComposerWidget::text() const
{
    return m_input ? m_input->toPlainText().trimmed() : QString();
}

void ComposerWidget::setText(const QString& text)
{
    if (m_input) {
        m_input->setPlainText(text);
    }
}

void ComposerWidget::clear()
{
    if (m_input) {
        m_input->clear();
    }
}

void ComposerWidget::setSendEnabled(bool enabled)
{
    if (m_sendBtn) {
        m_sendBtn->setEnabled(enabled && !m_input->toPlainText().trimmed().isEmpty());
    }
}

void ComposerWidget::setFileEnabled(bool enabled)
{
    if (m_fileBtn) m_fileBtn->setEnabled(enabled);
}

void ComposerWidget::setImageEnabled(bool enabled)
{
    if (m_imageBtn) m_imageBtn->setEnabled(enabled);
}

void ComposerWidget::setPlaceholderText(const QString& text)
{
    if (m_input) m_input->setPlaceholderText(text);
}

void ComposerWidget::setStateText(const QString& text)
{
    if (m_stateLabel) m_stateLabel->setText(text);
}

QTextEdit* ComposerWidget::inputEdit() const
{
    return m_input;
}

QPushButton* ComposerWidget::sendButton() const
{
    return m_sendBtn;
}

void ComposerWidget::insertText(const QString& text)
{
    if (m_input) {
        m_input->insertPlainText(text);
    }
}
