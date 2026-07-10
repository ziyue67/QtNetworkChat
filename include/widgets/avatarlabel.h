#ifndef AVATARLABEL_H
#define AVATARLABEL_H

#include <QLabel>
#include <QPixmap>

class AvatarLabel : public QLabel {
    Q_OBJECT

public:
    enum Status { Online, Offline, Away, None };
    Q_ENUM(Status)

    explicit AvatarLabel(QWidget* parent = nullptr, int size = 40);

    void setPixmap(const QPixmap& pixmap);
    void setTextAvatar(const QString& text, const QColor& background);
    void setStatus(Status status);

    int avatarSize() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_size;
    QPixmap m_pixmap;
    QString m_text;
    QColor m_textBackground;
    Status m_status = None;
};

#endif // AVATARLABEL_H
