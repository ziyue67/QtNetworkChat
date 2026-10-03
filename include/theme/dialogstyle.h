#ifndef DIALOGSTYLE_H
#define DIALOGSTYLE_H

#include <QString>

// Shared QQNT dialog styling.
//
// The four contacts/group dialogs (AddFriend / FriendManager / GlobalSearch /
// CreateGroup) and the smaller member dialogs all share the same input,
// primary/secondary/danger button, list and title-label look. Instead of
// duplicating those rules (twice each, once per light/dark qss file), this
// helper builds them once from the live ThemeManager colors so theme changes
// apply everywhere via each dialog's existing updateStyle() + themeChanged
// connection.
//
// Usage in a dialog's updateStyle():
//   setStyleSheet(DialogStyle::common() + QStringLiteral("<dialog-specific>"));
//
// Dialog-specific selectors should reuse the shared object names where they
// fit (dialogInput / dialogPrimaryBtn / dialogSecondaryBtn / dialogDangerBtn /
// dialogHintLabel / dialogTitleLabel / dialogListView) and only add rules for
// their unique widgets.
namespace DialogStyle {

// Common rules keyed on the shared object names, resolved from ThemeManager.
QString common();

} // namespace DialogStyle

#endif // DIALOGSTYLE_H
