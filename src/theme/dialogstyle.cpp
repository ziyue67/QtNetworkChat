#include "theme/dialogstyle.h"

#include "theme/thememanager.h"

namespace DialogStyle {

QString common()
{
    ThemeManager* tm = ThemeManager::instance();
    // Placeholders are numbered %1..%10 with no gaps so the chained .arg()
    // calls map positionally (QString::arg replaces the lowest-numbered marker
    // present, so a missing %1 would silently shift every value).
    return QStringLiteral(
        // Inputs
        "QLineEdit#dialogInput { background-color: %3; color: %1; border: 1px solid %4; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %5; }"
        // Hint / title labels
        "QLabel#dialogHintLabel { color: %2; font-size: 13px; }"
        "QLabel#dialogTitleLabel { color: %1; font-size: 18px; font-weight: 600; }"
        // Primary button (+ hover / disabled / success state)
        "QPushButton#dialogPrimaryBtn { background-color: %5; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %6; }"
        "QPushButton#dialogPrimaryBtn:disabled { background-color: %4; color: %2; }"
        "QPushButton#dialogPrimaryBtn[state=\"success\"] { background-color: %8; color: white; }"
        // Secondary button
        "QPushButton#dialogSecondaryBtn { background-color: %3; color: %1; border: 1px solid %4; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %4; }"
        // Danger button
        "QPushButton#dialogDangerBtn { background-color: %7; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogDangerBtn:hover { background-color: %9; }"
        // Generic list view
        "QListView#dialogListView { background-color: %3; border: 1px solid %4; border-radius: 6px; color: %1; }"
        "QListView#dialogListView::item { padding: 8px 12px; }"
        "QListView#dialogListView::item:selected { background-color: %10; color: %1; }"
    ).arg(tm->textColor().name())                  // %1
     .arg(tm->textSecondaryColor().name())          // %2
     .arg(tm->backgroundSecondaryColor().name())    // %3
     .arg(tm->borderColor().name())                 // %4
     .arg(tm->primaryColor().name())                // %5
     .arg(tm->primaryHoverColor().name())           // %6
     .arg(tm->dangerColor().name())                 // %7
     .arg(tm->successColor().name())                // %8
     .arg(tm->dangerColor().lighter(120).name())    // %9
     .arg(tm->primarySoftColor().name());           // %10
}

} // namespace DialogStyle
