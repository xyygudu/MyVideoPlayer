#ifndef MVP_APP_APP_THEME_H_
#define MVP_APP_APP_THEME_H_

#include <QColor>

/// Centralized visual design tokens (see openspec/changes/product-ui-shell/
/// design.md "Visual Design Spec"). Every color/size used by the custom
/// shell widgets SHOULD come from here instead of being a magic number
/// scattered across widget .cc files.
namespace ui_theme {

// --- Palette (light theme) ---
inline const QColor kBgNav{"#F5F6F8"};
inline const QColor kBgContent{"#FFFFFF"};
inline const QColor kAccent{"#4F7CFF"};
inline const QColor kAccentSoft{"#EAF0FF"};
inline const QColor kHoverTint{"#EFEFF2"};
inline const QColor kTextPrimary{"#1F2329"};
inline const QColor kTextSecondary{"#767C88"};
inline const QColor kBorder{"#E5E6EB"};
inline const QColor kCloseHover{"#E81123"};

// --- Window ---
inline constexpr int kDefaultWindowWidth = 1200;
inline constexpr int kDefaultWindowHeight = 720;
inline constexpr int kResizeMargin = 6;  // px hit-test margin for edge/corner drag-resize

// --- Title bar ---
inline constexpr int kTitleBarHeight = 40;
inline constexpr int kAvatarSize = 24;
inline constexpr int kWindowButtonWidth = 40;

// --- Navigation bar ---
inline constexpr int kNavWidth = 200;
inline constexpr int kLogoAreaHeight = 56;
inline constexpr int kNavItemHeight = 40;
inline constexpr int kNavSelectedBarWidth = 3;
inline constexpr int kNavItemLeftPadding = 12;
inline constexpr int kNavIconSize = 18;       // reserved icon slot — blank until real icons are supplied
inline constexpr int kNavIconTextGap = 8;

// --- Content / cards ---
inline constexpr int kContentPadding = 24;
inline constexpr int kCardWidth = 280;
inline constexpr int kCardHeight = 240;
inline constexpr int kCardThumbnailHeight = 140;
inline constexpr int kCardCornerRadius = 8;

}  // namespace ui_theme

#endif  // MVP_APP_APP_THEME_H_
