#ifndef FAVORITESVIEW_H
#define FAVORITESVIEW_H

#include <QWidget>

class QListView;
class QStandardItemModel;

class FavoritesView : public QWidget {
    Q_OBJECT

public:
    explicit FavoritesView(QWidget* parent = nullptr);

    QListView* listView() const;
    QStandardItemModel* model() const;

signals:
    void favoriteSelected(const QString& sessionId, const QString& messageId);

private:
    void setupUi();
    void updateStyle();

    QListView* m_listView = nullptr;
    QStandardItemModel* m_model = nullptr;
};

#endif // FAVORITESVIEW_H
