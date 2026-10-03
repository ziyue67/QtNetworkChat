#ifndef FAVORITESVIEW_H
#define FAVORITESVIEW_H

#include <QWidget>
#include <QJsonObject>

class QListView;
class QStandardItemModel;
class QLineEdit;
class QLabel;
class QSortFilterProxyModel;

class FavoritesView : public QWidget {
    Q_OBJECT

public:
    explicit FavoritesView(QWidget* parent = nullptr);

    QListView* listView() const;
    QStandardItemModel* model() const;
    void setEmptyStateVisible(bool visible);

signals:
    void favoriteSelected(const QString& sessionId, const QString& messageId);
    void favoriteRemovalRequested(const QJsonObject& message);

private:
    void setupUi();
    void updateStyle();

    QListView* m_listView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_emptyLabel = nullptr;
};

#endif // FAVORITESVIEW_H
