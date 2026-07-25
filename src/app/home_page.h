#ifndef MVP_APP_HOME_PAGE_H_
#define MVP_APP_HOME_PAGE_H_

#include <QWidget>

#include "navigation_bar.h"

/// Home dashboard: a responsive FlowLayout grid of DashboardCards, one per
/// tool. Clicking a card emits NavigateRequested(page) so MainWindow can
/// route to that page — identical effect to clicking the corresponding
/// navigation bar item.
class HomePage : public QWidget {
    Q_OBJECT

  public:
    explicit HomePage(QWidget* parent = nullptr);

  signals:
    void NavigateRequested(NavigationBar::Page page);

  private:
    void SetupUi();
};

#endif  // MVP_APP_HOME_PAGE_H_
