#include "group_info_panel_ui.h"
#include "theme/thememanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) qWarning("%s", message);
    return condition;
}

bool checkScreenshot(QWidget& window, const QString& name) {
    QApplication::processEvents();
    const QImage image = window.grab().toImage();
    if (!expect(!image.isNull() && image.size() == window.size(),
                "group info screenshot dimensions should match the dialog")) return false;
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 8) {
        for (int x = 0; x < image.width(); x += 8) colors.insert(image.pixel(x, y));
    }
    return expect(colors.size() > 12, "group info screenshot should not be blank")
        && expect(image.save(QDir(QCoreApplication::applicationDirPath()).filePath(name)),
                  "group info screenshot should be saved");
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    QApplication::setApplicationName(QStringLiteral("group_info_panel_ui_test"));
    QStandardPaths::setTestModeEnabled(true);

    QDialog panel;
    panel.setObjectName(QStringLiteral("groupInfoDialog"));
    panel.setFixedSize(384, 592);
    auto* layout = new QVBoxLayout(&panel);
    GroupInfoPanelUi ui(panel, *layout);
    const auto overview = ui.addOverview(QStringLiteral("654321"), QStringLiteral("团队群"),
                                         8, true, true, QPixmap());
    QPushButton* edit = ui.addAnnouncement(QStringLiteral("今天发布测试版"), true);
    ui.addCaption(QStringLiteral("消息设置"));
    QPushButton* receive = ui.addRow(QStringLiteral("群消息设置"), QStringLiteral("接收消息但不提醒"), true);
    QCheckBox* mute = ui.addToggleRow(QStringLiteral("消息免打扰"));
    layout->addStretch();
    GroupInfoPanelUi::applyStyle(panel);
    panel.show();
    QApplication::processEvents();

    bool ok = expect(overview.avatarButton && overview.avatarButton->isEnabled()
                     && overview.shareButton && edit && receive && mute,
                     "manager panel controls should be present");
    ok = expect(panel.findChild<QLabel*>(QStringLiteral("groupInfoName"))->text()
                    == QStringLiteral("团队群"), "group name should render") && ok;
    ok = expect(panel.findChild<QLabel*>(QStringLiteral("groupInfoMeta"))->text()
                    .contains(QStringLiteral("654321")), "group number should render") && ok;
    ok = expect(panel.findChild<QLabel*>(QStringLiteral("groupInfoAnnouncementBody"))->text()
                    == QStringLiteral("今天发布测试版"), "announcement should render") && ok;
    ok = expect(receive->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->text()
                    == QStringLiteral("接收消息但不提醒"), "setting value should render") && ok;

    int editClicks = 0;
    int receiveClicks = 0;
    QObject::connect(edit, &QPushButton::clicked, &panel, [&]() { ++editClicks; });
    QObject::connect(receive, &QPushButton::clicked, &panel, [&]() { ++receiveClicks; });
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::mouseClick(receive, Qt::LeftButton);
    QTest::mouseClick(mute, Qt::LeftButton);
    ok = expect(editClicks == 1 && receiveClicks == 1 && mute->isChecked(),
                "announcement, settings, and toggle controls should respond to clicks") && ok;
    ok = checkScreenshot(panel, QStringLiteral("group_info_panel_light.png")) && ok;

    ThemeManager::instance()->setTheme(ThemeManager::Theme::Dark);
    GroupInfoPanelUi::applyStyle(panel);
    ok = checkScreenshot(panel, QStringLiteral("group_info_panel_dark.png")) && ok;

    QDialog memberPanel;
    auto* memberLayout = new QVBoxLayout(&memberPanel);
    GroupInfoPanelUi memberUi(memberPanel, *memberLayout);
    const auto memberOverview = memberUi.addOverview(QStringLiteral("123456"),
                                                     QStringLiteral("普通群"), 2, true, false,
                                                     QPixmap());
    ok = expect(!memberOverview.avatarButton->isEnabled()
                    && memberUi.addAnnouncement(QString(), false) == nullptr,
                "non-manager panel should not expose editing controls") && ok;
    return ok ? 0 : 1;
}
