#include "widgets/composerwidget.h"

#include "theme/thememanager.h"
#include "widgets/composerTextEdit.h"

#include <QHBoxLayout>
#include <QButtonGroup>
#include <QLabel>
#include <QGridLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
enum class ComposerToolIcon { Emoji, Image, File, History, Screenshot };

class ComposerToolButton final : public QPushButton {
public:
    ComposerToolButton(ComposerToolIcon icon, const QString& tip, QWidget* parent)
        : QPushButton(parent), m_icon(icon)
    {
        setObjectName(QStringLiteral("composerToolBtn"));
        setFixedSize(28, 28);
        setToolTip(tip);
        setFlat(true);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor color = isEnabled()
            ? ThemeManager::instance()->textSecondaryColor()
            : ThemeManager::instance()->textTertiaryColor();
        painter.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QRectF r = rect().adjusted(6, 6, -6, -6);

        switch (m_icon) {
        case ComposerToolIcon::Emoji:
            painter.drawEllipse(r);
            painter.setBrush(color);
            painter.drawEllipse(QRectF(r.left() + 3.4, r.top() + 4.2, 1.8, 1.8));
            painter.drawEllipse(QRectF(r.right() - 5.2, r.top() + 4.2, 1.8, 1.8));
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(r.adjusted(3.2, 4.0, -3.2, -2.0), 205 * 16, 130 * 16);
            break;
        case ComposerToolIcon::Image:
            painter.drawRoundedRect(r, 2, 2);
            painter.setBrush(color);
            painter.drawEllipse(QRectF(r.left() + 3.0, r.top() + 3.0, 2.3, 2.3));
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(r.left() + 2, r.bottom() - 3), QPointF(r.center().x() - 1, r.center().y()));
            painter.drawLine(QPointF(r.center().x() - 1, r.center().y()), QPointF(r.right() - 2, r.bottom() - 3));
            painter.drawLine(QPointF(r.center().x() + 1, r.bottom() - 5), QPointF(r.right() - 3, r.bottom() - 3));
            break;
        case ComposerToolIcon::File:
            painter.drawRoundedRect(QRectF(r.left() + 2, r.top(), r.width() - 3, r.height()), 1.5, 1.5);
            painter.drawLine(QPointF(r.right() - 4, r.top()), QPointF(r.right() - 4, r.top() + 4));
            painter.drawLine(QPointF(r.right() - 4, r.top() + 4), QPointF(r.right(), r.top() + 4));
            painter.drawLine(QPointF(r.left() + 5, r.top() + 8), QPointF(r.right() - 3, r.top() + 8));
            painter.drawLine(QPointF(r.left() + 5, r.top() + 11), QPointF(r.right() - 5, r.top() + 11));
            break;
        case ComposerToolIcon::History:
            painter.drawArc(r, 35 * 16, 290 * 16);
            painter.drawLine(QPointF(r.center().x(), r.center().y()), QPointF(r.center().x(), r.top() + 3));
            painter.drawLine(QPointF(r.center().x(), r.center().y()), QPointF(r.right() - 3, r.center().y() + 2));
            painter.drawLine(QPointF(r.left() + 1, r.top() + 3), QPointF(r.left() + 1, r.top() + 7));
            painter.drawLine(QPointF(r.left() + 1, r.top() + 3), QPointF(r.left() + 5, r.top() + 3));
            break;
        case ComposerToolIcon::Screenshot:
            painter.drawLine(QPointF(r.left() + 2, r.top() + 2), QPointF(r.right() - 2, r.bottom() - 2));
            painter.drawLine(QPointF(r.left() + 2, r.bottom() - 2), QPointF(r.right() - 2, r.top() + 2));
            painter.drawEllipse(QRectF(r.left() - 1, r.top() - 1, 5, 5));
            painter.drawEllipse(QRectF(r.right() - 4, r.top() - 1, 5, 5));
            break;
        }
        Q_UNUSED(event);
    }

private:
    ComposerToolIcon m_icon;
};

class ComposerChevronButton final : public QPushButton {
public:
    explicit ComposerChevronButton(QWidget* parent) : QPushButton(parent)
    {
        setObjectName(QStringLiteral("composerScreenshotMoreBtn"));
        setFixedSize(14, 28);
        setToolTip(QStringLiteral("截图更多"));
        setFlat(true);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor color = isEnabled()
            ? ThemeManager::instance()->textSecondaryColor()
            : ThemeManager::instance()->textTertiaryColor();
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const QPoint center = rect().center();
        QPolygon triangle;
        triangle << QPoint(center.x() - 3, center.y() - 1)
                 << QPoint(center.x() + 3, center.y() - 1)
                 << QPoint(center.x(), center.y() + 3);
        painter.drawPolygon(triangle);
        Q_UNUSED(event);
    }
};

QPushButton* createToolButton(ComposerToolIcon icon, const QString& tip)
{
    return new ComposerToolButton(icon, tip, nullptr);
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
    root->setContentsMargins(18, 9, 18, 10);
    root->setSpacing(6);

    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setSpacing(3);
    m_emojiBtn = createToolButton(ComposerToolIcon::Emoji, QStringLiteral("表情"));
    m_screenshotBtn = createToolButton(ComposerToolIcon::Screenshot, QStringLiteral("截图"));
    m_screenshotMoreBtn = new ComposerChevronButton(this);
    m_fileBtn = createToolButton(ComposerToolIcon::File, QStringLiteral("发送文件"));
    m_imageBtn = createToolButton(ComposerToolIcon::Image, QStringLiteral("发送图片/视频"));
    m_historyBtn = createToolButton(ComposerToolIcon::History, QStringLiteral("聊天记录"));
    auto* leftTools = new QFrame(this);
    leftTools->setObjectName(QStringLiteral("composerLeftTools"));
    auto* leftLayout = new QHBoxLayout(leftTools);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(3);
    leftLayout->addWidget(m_emojiBtn);
    auto* screenshotGroup = new QFrame(this);
    screenshotGroup->setObjectName(QStringLiteral("composerScreenshotGroup"));
    screenshotGroup->setFixedSize(42, 28);
    auto* screenshotLayout = new QHBoxLayout(screenshotGroup);
    screenshotLayout->setContentsMargins(0, 0, 0, 0);
    screenshotLayout->setSpacing(0);
    screenshotLayout->addWidget(m_screenshotBtn);
    screenshotLayout->addWidget(m_screenshotMoreBtn);
    leftLayout->addWidget(screenshotGroup);
    leftLayout->addWidget(m_fileBtn);
    leftLayout->addWidget(m_imageBtn);
    toolbar->addWidget(leftTools);
    toolbar->addStretch();
    auto* historyWrap = new QFrame(this);
    historyWrap->setObjectName(QStringLiteral("composerHistoryWrap"));
    auto* historyLayout = new QHBoxLayout(historyWrap);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->addWidget(m_historyBtn);
    toolbar->addWidget(historyWrap);
    root->addLayout(toolbar);

    ComposerTextEdit* input = new ComposerTextEdit(this);
    m_input = input;
    m_input->setObjectName(QStringLiteral("composerInput"));
    m_input->setMaximumHeight(112);
    refreshHints();
    root->addWidget(m_input, 1);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(QStringLiteral("composerStateLabel"));
    m_stateLabel->setText(placeholderText());
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
    connect(input, &ComposerTextEdit::filesDropped, this, &ComposerWidget::filesDropped);
    connect(m_input, &QTextEdit::textChanged, this, [this]() {
        m_sendBtn->setEnabled(!m_input->toPlainText().trimmed().isEmpty());
        emit textChanged();
    });
    connect(m_emojiBtn, &QPushButton::clicked, this, &ComposerWidget::showEmojiPicker);
    connect(m_imageBtn, &QPushButton::clicked, this, &ComposerWidget::imageRequested);
    connect(m_fileBtn, &QPushButton::clicked, this, &ComposerWidget::fileRequested);
    connect(m_historyBtn, &QPushButton::clicked, this, &ComposerWidget::viewHistoryRequested);
    connect(m_screenshotBtn, &QPushButton::clicked, this, [this]() {
        emit screenshotRequested(m_hideWindowBeforeScreenshot);
    });
    connect(m_screenshotMoreBtn, &QPushButton::clicked, this, &ComposerWidget::showScreenshotMenu);
    connect(m_sendBtn, &QPushButton::clicked, this, &ComposerWidget::sendRequested);
}

void ComposerWidget::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#composerWidget { background-color: %1; border-top: 1px solid %2; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; }"
        "QTextEdit#composerInput { background-color: %3; color: %4; border: 1px solid %2; border-radius: 7px; padding: 7px 9px; }"
        "QTextEdit#composerInput:focus { border: 1px solid %5; }"
        "QPushButton#composerToolBtn { color: %6; border: none; background: transparent; border-radius: 6px; }"
        "QPushButton#composerToolBtn:hover { background-color: %7; color: %5; }"
        "QFrame#composerLeftTools,QFrame#composerHistoryWrap { background: transparent; }"
        "QFrame#composerScreenshotGroup { border-radius: 6px; }"
        "QFrame#composerScreenshotGroup:hover { background-color: %7; }"
        "QFrame#composerScreenshotGroup QPushButton#composerToolBtn { border-top-right-radius: 0; border-bottom-right-radius: 0; }"
        "QPushButton#composerScreenshotMoreBtn { color: %6; border: none; background: transparent; border-top-right-radius: 6px; border-bottom-right-radius: 6px; font-size: 11px; }"
        "QPushButton#composerScreenshotMoreBtn:hover { background-color: %7; color: %5; }"
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

void ComposerWidget::showEmojiPicker()
{
    if (m_emojiPopup) {
        m_emojiPopup->close();
        m_emojiPopup->deleteLater();
        m_emojiPopup = nullptr;
        return;
    }

    auto* popup = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName(QStringLiteral("composerEmojiPopup"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet(QStringLiteral(
        "QFrame#composerEmojiPopup { background: %1; border: 1px solid %2; border-radius: 12px; }"
        "QLabel#emojiPickerTitle { color: %3; font-size: 11px; font-weight: 500; }"
        "QPushButton#emojiPickerCell { border: none; background: transparent; border-radius: 8px; font-size: 20px; }"
        "QPushButton#emojiPickerCell:hover { background: %4; }"
        "QFrame#emojiPickerFooter { border-top: 1px solid %2; }"
        "QPushButton#emojiPickerTab { border: none; border-radius: 7px; color: %3; font-size: 19px; }"
        "QPushButton#emojiPickerTab:checked { color: %5; background: %4; }"
        "QLabel#emojiSavedEmptyLabel { color: %3; font-size: 12px; }"
    ).arg(ThemeManager::instance()->backgroundColor().name())
     .arg(ThemeManager::instance()->borderColor().name())
     .arg(ThemeManager::instance()->textTertiaryColor().name())
     .arg(ThemeManager::instance()->backgroundTertiaryColor().name())
     .arg(ThemeManager::instance()->primaryColor().name()));

    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* stack = new QStackedWidget(popup);
    auto* scroll = new QScrollArea(stack);
    scroll->setObjectName(QStringLiteral("emojiPickerScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 14, 16, 14);
    contentLayout->setSpacing(12);

    // Same ten built-in sections as tauri-qqnt Composer. Each section remains
    // in the scrollable pane; the footer switches built-in/saved emoji views.
    const QList<QPair<QString, QStringList>> categories = {
        {QStringLiteral("最近表情"), {QStringLiteral("🍬"), QStringLiteral("🤓"), QStringLiteral("😱"), QStringLiteral("🤢"), QStringLiteral("🐷"), QStringLiteral("😁"), QStringLiteral("😳"), QStringLiteral("🙂"), QStringLiteral("🤔"), QStringLiteral("😂"), QStringLiteral("🤣"), QStringLiteral("😇"), QStringLiteral("😭"), QStringLiteral("😎"), QStringLiteral("🥳"), QStringLiteral("😡"), QStringLiteral("😌"), QStringLiteral("🥰"), QStringLiteral("😪"), QStringLiteral("😴"), QStringLiteral("😵"), QStringLiteral("😲"), QStringLiteral("😢"), QStringLiteral("😘")}},
        {QStringLiteral("超级表情"), {QStringLiteral("😭"), QStringLiteral("🤦"), QStringLiteral("🙂"), QStringLiteral("😎"), QStringLiteral("🤢"), QStringLiteral("🦉"), QStringLiteral("😘"), QStringLiteral("😳"), QStringLiteral("😱"), QStringLiteral("🥳"), QStringLiteral("🤬"), QStringLiteral("🏀"), QStringLiteral("🎂"), QStringLiteral("🎁"), QStringLiteral("🎉"), QStringLiteral("🌙"), QStringLiteral("💤"), QStringLiteral("🤗"), QStringLiteral("🌈"), QStringLiteral("🐧"), QStringLiteral("🐶"), QStringLiteral("🤝"), QStringLiteral("🎯"), QStringLiteral("🚀")}},
        {QStringLiteral("小黄脸表情"), {QStringLiteral("😀"), QStringLiteral("😃"), QStringLiteral("😄"), QStringLiteral("😆"), QStringLiteral("😅"), QStringLiteral("😉"), QStringLiteral("😋"), QStringLiteral("😛"), QStringLiteral("😜"), QStringLiteral("🤪"), QStringLiteral("😝"), QStringLiteral("🤑"), QStringLiteral("🤭"), QStringLiteral("🤔"), QStringLiteral("🤐"), QStringLiteral("🤨"), QStringLiteral("😐"), QStringLiteral("😑"), QStringLiteral("😶"), QStringLiteral("😏"), QStringLiteral("😒"), QStringLiteral("🙄"), QStringLiteral("😬"), QStringLiteral("🤥"), QStringLiteral("😔"), QStringLiteral("🤤"), QStringLiteral("😷"), QStringLiteral("🤒"), QStringLiteral("🤕"), QStringLiteral("🤮"), QStringLiteral("🤧"), QStringLiteral("🤯"), QStringLiteral("🤠"), QStringLiteral("🤓"), QStringLiteral("🧐"), QStringLiteral("😕"), QStringLiteral("😟"), QStringLiteral("🙁"), QStringLiteral("☹️"), QStringLiteral("😮"), QStringLiteral("😯"), QStringLiteral("🥺"), QStringLiteral("😦"), QStringLiteral("😧"), QStringLiteral("😨"), QStringLiteral("😰"), QStringLiteral("😥"), QStringLiteral("😖"), QStringLiteral("😣"), QStringLiteral("😞"), QStringLiteral("😓"), QStringLiteral("😩"), QStringLiteral("😫"), QStringLiteral("😤"), QStringLiteral("😠"), QStringLiteral("😈"), QStringLiteral("👿"), QStringLiteral("😍"), QStringLiteral("😗"), QStringLiteral("😙"), QStringLiteral("😊"), QStringLiteral("☺️"), QStringLiteral("😚"), QStringLiteral("😡"), QStringLiteral("💀")}},
        {QStringLiteral("手势动作"), {QStringLiteral("👋"), QStringLiteral("🤚"), QStringLiteral("🖐️"), QStringLiteral("✋"), QStringLiteral("🖖"), QStringLiteral("👌"), QStringLiteral("✌️"), QStringLiteral("🤞"), QStringLiteral("🤟"), QStringLiteral("🤘"), QStringLiteral("🤙"), QStringLiteral("👈"), QStringLiteral("👉"), QStringLiteral("👆"), QStringLiteral("👇"), QStringLiteral("☝️"), QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("✊"), QStringLiteral("👊"), QStringLiteral("🤛"), QStringLiteral("🤜"), QStringLiteral("👏"), QStringLiteral("🙌"), QStringLiteral("👐"), QStringLiteral("🤲"), QStringLiteral("🙏"), QStringLiteral("✍️"), QStringLiteral("💅"), QStringLiteral("💪"), QStringLiteral("🏃"), QStringLiteral("💃"), QStringLiteral("🕺")}},
        {QStringLiteral("爱心符号"), {QStringLiteral("❤️"), QStringLiteral("🧡"), QStringLiteral("💛"), QStringLiteral("💚"), QStringLiteral("💙"), QStringLiteral("💜"), QStringLiteral("🖤"), QStringLiteral("🤍"), QStringLiteral("🤎"), QStringLiteral("💔"), QStringLiteral("❣️"), QStringLiteral("💕"), QStringLiteral("💞"), QStringLiteral("💓"), QStringLiteral("💗"), QStringLiteral("💖"), QStringLiteral("💘"), QStringLiteral("💝"), QStringLiteral("💟"), QStringLiteral("💌"), QStringLiteral("💢"), QStringLiteral("💯"), QStringLiteral("💬"), QStringLiteral("💭"), QStringLiteral("💥"), QStringLiteral("💫"), QStringLiteral("💦"), QStringLiteral("💨"), QStringLiteral("💧"), QStringLiteral("💋"), QStringLiteral("💐"), QStringLiteral("💍"), QStringLiteral("💎")}},
        {QStringLiteral("动物角色"), {QStringLiteral("💩"), QStringLiteral("🤡"), QStringLiteral("👹"), QStringLiteral("👺"), QStringLiteral("👻"), QStringLiteral("👽"), QStringLiteral("🤖"), QStringLiteral("👾"), QStringLiteral("👀"), QStringLiteral("👁️"), QStringLiteral("👅"), QStringLiteral("👄"), QStringLiteral("😺"), QStringLiteral("😸"), QStringLiteral("😹"), QStringLiteral("😻"), QStringLiteral("😼"), QStringLiteral("😽"), QStringLiteral("🙀"), QStringLiteral("😿"), QStringLiteral("😾"), QStringLiteral("🐵"), QStringLiteral("🐱"), QStringLiteral("🐭"), QStringLiteral("🐹"), QStringLiteral("🐰"), QStringLiteral("🐻"), QStringLiteral("🐼"), QStringLiteral("🐮"), QStringLiteral("🐸"), QStringLiteral("🐔"), QStringLiteral("🐦"), QStringLiteral("🦉"), QStringLiteral("🐴"), QStringLiteral("🐛"), QStringLiteral("🐳"), QStringLiteral("🐙"), QStringLiteral("🦋"), QStringLiteral("🐞"), QStringLiteral("🐢"), QStringLiteral("🐍"), QStringLiteral("🐲"), QStringLiteral("🦁"), QStringLiteral("🐯"), QStringLiteral("🐨"), QStringLiteral("🐺"), QStringLiteral("🐒"), QStringLiteral("🐕"), QStringLiteral("🐶"), QStringLiteral("🐧")}},
        {QStringLiteral("自然花草"), {QStringLiteral("🌝"), QStringLiteral("🌚"), QStringLiteral("🌛"), QStringLiteral("⭐"), QStringLiteral("🌟"), QStringLiteral("✨"), QStringLiteral("⚡"), QStringLiteral("🔥"), QStringLiteral("☁️"), QStringLiteral("☂️"), QStringLiteral("☃️"), QStringLiteral("⛄"), QStringLiteral("🌹"), QStringLiteral("🥀"), QStringLiteral("🌺"), QStringLiteral("🌻"), QStringLiteral("🌼"), QStringLiteral("🌷"), QStringLiteral("🌸"), QStringLiteral("🍀"), QStringLiteral("🍃"), QStringLiteral("🌵"), QStringLiteral("🌲"), QStringLiteral("🌳"), QStringLiteral("☀️"), QStringLiteral("🌞"), QStringLiteral("🌙"), QStringLiteral("🌈"), QStringLiteral("🌊"), QStringLiteral("❄️"), QStringLiteral("🌧️"), QStringLiteral("🌩️"), QStringLiteral("🌪️"), QStringLiteral("🌍")}},
        {QStringLiteral("食物饮品"), {QStringLiteral("🍎"), QStringLiteral("🍓"), QStringLiteral("🍒"), QStringLiteral("🍑"), QStringLiteral("🍍"), QStringLiteral("🍌"), QStringLiteral("🍋"), QStringLiteral("🍊"), QStringLiteral("🍅"), QStringLiteral("🍞"), QStringLiteral("🧀"), QStringLiteral("🍖"), QStringLiteral("🍗"), QStringLiteral("🍔"), QStringLiteral("🍟"), QStringLiteral("🍕"), QStringLiteral("🌭"), QStringLiteral("🌮"), QStringLiteral("🍜"), QStringLiteral("🍲"), QStringLiteral("🍱"), QStringLiteral("🍙"), QStringLiteral("🍚"), QStringLiteral("🍡"), QStringLiteral("🍦"), QStringLiteral("🍰"), QStringLiteral("🍭"), QStringLiteral("🍬"), QStringLiteral("☕"), QStringLiteral("🍵"), QStringLiteral("🍺"), QStringLiteral("🍻"), QStringLiteral("🍉")}},
        {QStringLiteral("物品娱乐"), {QStringLiteral("🎊"), QStringLiteral("🎈"), QStringLiteral("⚽"), QStringLiteral("🏆"), QStringLiteral("🥇"), QStringLiteral("🎮"), QStringLiteral("🎲"), QStringLiteral("🎤"), QStringLiteral("🎧"), QStringLiteral("📱"), QStringLiteral("💻"), QStringLiteral("📷"), QStringLiteral("🎬"), QStringLiteral("🚗"), QStringLiteral("🚕"), QStringLiteral("🚌"), QStringLiteral("🚀"), QStringLiteral("✈️"), QStringLiteral("⛵"), QStringLiteral("🚲"), QStringLiteral("🚨"), QStringLiteral("⏰"), QStringLiteral("⌛"), QStringLiteral("💡"), QStringLiteral("🔦"), QStringLiteral("🔒"), QStringLiteral("🔑"), QStringLiteral("🔨"), QStringLiteral("📎"), QStringLiteral("✂️"), QStringLiteral("🗑️"), QStringLiteral("💰"), QStringLiteral("💸")}},
        {QStringLiteral("符号旗帜"), {QStringLiteral("✅"), QStringLiteral("❌"), QStringLiteral("⭕"), QStringLiteral("❓"), QStringLiteral("❔"), QStringLiteral("❗"), QStringLiteral("❕"), QStringLiteral("⁉️"), QStringLiteral("🔞"), QStringLiteral("🇨🇳"), QStringLiteral("🏳️"), QStringLiteral("🏴"), QStringLiteral("🏁"), QStringLiteral("🚫"), QStringLiteral("⛔"), QStringLiteral("🔴"), QStringLiteral("🟠"), QStringLiteral("🟡"), QStringLiteral("🟢"), QStringLiteral("🔵"), QStringLiteral("🟣"), QStringLiteral("⚫"), QStringLiteral("⚪")}}
    };
    for (const auto& category : categories) {
        auto* title = new QLabel(category.first, content);
        title->setObjectName(QStringLiteral("emojiPickerTitle"));
        contentLayout->addWidget(title);
        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        for (int i = 0; i < category.second.size(); ++i) {
            const QString emoji = category.second.at(i);
            auto* cell = new QPushButton(emoji, content);
            cell->setObjectName(QStringLiteral("emojiPickerCell"));
            cell->setFixedSize(32, 32);
            QFont emojiFont(QStringLiteral("Segoe UI Emoji"));
            emojiFont.setPixelSize(22);
            cell->setFont(emojiFont);
            connect(cell, &QPushButton::clicked, this, [this, popup, emoji]() {
                insertText(emoji);
                popup->close();
            });
            grid->addWidget(cell, i / 12, i % 12);
        }
        contentLayout->addLayout(grid);
    }
    contentLayout->addStretch();
    scroll->setWidget(content);
    stack->addWidget(scroll);
    auto* savedPage = new QLabel(QStringLiteral("暂无已添加表情"), stack);
    savedPage->setObjectName(QStringLiteral("emojiSavedEmptyLabel"));
    savedPage->setAlignment(Qt::AlignCenter);
    stack->addWidget(savedPage);
    layout->addWidget(stack);
    auto* footer = new QFrame(popup);
    footer->setObjectName(QStringLiteral("emojiPickerFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(8, 5, 8, 5);
    footerLayout->setSpacing(4);
    auto* builtInTab = new QPushButton(QStringLiteral("☺"), footer);
    auto* savedTab = new QPushButton(QStringLiteral("♡"), footer);
    builtInTab->setObjectName(QStringLiteral("emojiPickerTab"));
    savedTab->setObjectName(QStringLiteral("emojiPickerTab"));
    builtInTab->setFixedSize(36, 32);
    savedTab->setFixedSize(36, 32);
    builtInTab->setCheckable(true);
    savedTab->setCheckable(true);
    builtInTab->setChecked(true);
    auto* tabGroup = new QButtonGroup(footer);
    tabGroup->setExclusive(true);
    tabGroup->addButton(builtInTab, 0);
    tabGroup->addButton(savedTab, 1);
    connect(tabGroup, QOverload<int>::of(&QButtonGroup::idClicked), stack, &QStackedWidget::setCurrentIndex);
    footerLayout->addWidget(builtInTab);
    footerLayout->addWidget(savedTab);
    footerLayout->addStretch();
    layout->addWidget(footer);
    popup->setFixedSize(472, 338);
    popup->move(m_emojiBtn->mapToGlobal(QPoint(0, -popup->height() - 8)));
    connect(popup, &QObject::destroyed, this, [this]() { m_emojiPopup = nullptr; });
    m_emojiPopup = popup;
    popup->show();
}

void ComposerWidget::showScreenshotMenu()
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("composerScreenshotMenu"));
    QAction* capture = menu.addAction(QStringLiteral("截图\tCtrl+Alt+A"));
    QAction* hide = menu.addAction(QStringLiteral("隐藏当前窗口"));
    hide->setCheckable(true);
    hide->setChecked(m_hideWindowBeforeScreenshot);
    QAction* chosen = menu.exec(m_screenshotMoreBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    if (chosen == capture) {
        emit screenshotRequested(m_hideWindowBeforeScreenshot);
    } else if (chosen == hide) {
        m_hideWindowBeforeScreenshot = hide->isChecked();
    }
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

void ComposerWidget::setSessionName(const QString& sessionName)
{
    m_sessionName = sessionName;
    refreshHints();
}

QString ComposerWidget::placeholderText() const
{
    const QString name = m_sessionName.trimmed().isEmpty()
        ? QStringLiteral("好友")
        : m_sessionName;
    return QStringLiteral("发给 %1…（Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿）").arg(name);
}

void ComposerWidget::refreshHints()
{
    if (m_input) {
        m_input->setPlaceholderText(placeholderText());
    }
    if (m_stateLabel) {
        m_stateLabel->setText(QStringLiteral("Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿"));
    }
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

void ComposerWidget::setMentionCompletions(const QStringList& completions)
{
    if (ComposerTextEdit* input = qobject_cast<ComposerTextEdit*>(m_input)) {
        input->setMentionCompletions(completions);
    }
}
