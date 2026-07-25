#ifndef MVP_APP_NAVIGATION_BAR_H_
#define MVP_APP_NAVIGATION_BAR_H_

#include <QWidget>

class NavItem;
class IconButton;

/// Left navigation column: logo area, "主页" item, and an expandable
/// "快速访问" group (default expanded) containing "播放器"/"转码器".
/// Emits PageRequested(Page) when the user clicks any item.
class NavigationBar : public QWidget {
    Q_OBJECT

  public:
    enum class Page { kHome, kPlayer, kTranscoder };

    explicit NavigationBar(QWidget* parent = nullptr);

    /// Updates the selected visual state without emitting PageRequested —
    /// used when navigation is triggered from elsewhere (e.g. a Home card).
    void SetSelectedPage(Page page);

  signals:
    void PageRequested(Page page);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void SetupUi();
    QWidget* BuildQuickAccessHeader();
    QWidget* BuildQuickAccessBody();
    void SelectItem(NavItem* item, Page page);
    void OnQuickAccessToggled();

    NavItem* home_item_{nullptr};
    QWidget* quick_access_header_{nullptr};
    IconButton* quick_access_chevron_{nullptr};
    QWidget* quick_access_body_{nullptr};
    NavItem* player_item_{nullptr};
    NavItem* transcoder_item_{nullptr};
    Page current_page_{Page::kHome};
};

#endif  // MVP_APP_NAVIGATION_BAR_H_
