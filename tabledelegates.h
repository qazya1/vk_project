#ifndef TABLEDELEGATES_H
#define TABLEDELEGATES_H

#include <QStyledItemDelegate>
#include <QVector>

class MainWindow;

class CommonDelegate : public QStyledItemDelegate
{
public:
    explicit CommonDelegate(QObject* parent = nullptr);
    void setHoverCursorColumns(const QVector<int>& columns);
    void setHoverCursor(Qt::CursorShape cursorShape = Qt::PointingHandCursor);
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
private:
    QVector<int> m_hoverColumns;
    Qt::CursorShape m_hoverCursor;
};

class SearchDelegate : public QStyledItemDelegate
{
public:
    explicit SearchDelegate(MainWindow* mainWindow, QObject* parent = nullptr);
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void destroyEditor(QWidget* editor, const QModelIndex& index) const override;
private:
    MainWindow* mainWindowPtr;
};

class HTMLDelegate : public QStyledItemDelegate
{
public:
    explicit HTMLDelegate(QObject* parent = nullptr);
protected:
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
private:
    CommonDelegate* m_commonDelegate;
};

#endif // TABLEDELEGATES_H
