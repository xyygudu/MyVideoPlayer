## 1. Resource & Build Setup

- [x] 1.1 Create `src/app/resources/icons.qrc` listing all 11 SVG files
- [x] 1.2 Update `src/app/CMakeLists.txt`: add `find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)`, link `Qt6::Svg`, and add `qt_add_resources(mvp_app "icons" "src/app/resources/icons.qrc")`

## 2. Navigation Bar Icons

- [x] 2.1 In `navigation_bar.cc` `SetupUi()`: add `SetIcon()` calls — `home.svg` → `home_item_`, `player.svg` → `player_item_`, `converter.svg` → `transcoder_item_`
- [x] 2.2 Build and verify nav bar shows all three icons correctly

## 3. Home Dashboard Card Thumbnails

- [x] 3.1 In `home_page.cc`: replace `PaintPlayGlyph` lambda with `QIcon(":/icons/player.svg")` rendering
- [x] 3.2 In `home_page.cc`: replace `PaintConvertGlyph` lambda with `QIcon(":/icons/converter.svg")` rendering
- [x] 3.3 Build and verify home page cards show SVG thumbnails

## 4. Verify & Cleanup

- [x] 4.1 Full build with no warnings or errors
- [x] 4.2 Run the app and visually confirm all icons render at correct sizes and positions
