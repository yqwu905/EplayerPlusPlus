#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStyle>

/**
 * @brief A layout that arranges child widgets left-to-right and wraps to a new
 * row when the available width is exhausted.
 *
 * Used for the BrowsePanel filter toolbar so the controls stay fully visible
 * regardless of how narrow the panel is locked to (one row when wide enough,
 * multiple rows when the panel only fits a single thumbnail column).
 *
 * Adapted from the Qt "Flow Layout" example.
 */
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> m_itemList;
    int m_hSpace;
    int m_vSpace;
};

#endif // FLOWLAYOUT_H
