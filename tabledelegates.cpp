#include "tabledelegates.h"
#include "mainwindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTableWidget>
#include <QTextDocument>
#include <QToolButton>

CommonDelegate::CommonDelegate(QObject* parent) : QStyledItemDelegate(parent), m_hoverCursor(Qt::PointingHandCursor) {}
void CommonDelegate::setHoverCursorColumns(const QVector<int>& columns) { m_hoverColumns = columns; }
void CommonDelegate::setHoverCursor(Qt::CursorShape cursorShape) { m_hoverCursor = cursorShape; }
bool CommonDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    QWidget* view = qobject_cast<QWidget*>(parent());
    if (!view) return QStyledItemDelegate::editorEvent(event, model, option, index);
    if (event->type() == QEvent::MouseMove) {
        if (m_hoverColumns.contains(index.column()) && !model->data(index, Qt::DisplayRole).toString().isEmpty()) view->setCursor(m_hoverCursor);
        else view->unsetCursor();
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
void CommonDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    QString text = option->text;
    if (text.contains("Работа в нескольких районах")) {
        text = text.split(':').first() + "...";
        option->text = text;
    }
    option->features &= ~QStyleOptionViewItem::WrapText;
    option->features |= QStyleOptionViewItem::HasDisplay;
    option->textElideMode = Qt::ElideRight;
}
QWidget* CommonDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const
{
    QPlainTextEdit* editor = new QPlainTextEdit(parent);
    QFont font; font.setFamily("Roboto Light"); font.setPixelSize(20);
    editor->setFont(font); editor->setFrameStyle(QFrame::NoFrame);
    editor->setStyleSheet("QPlainTextEdit { color: #62560E; }");
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->document()->setDocumentMargin(0);
    return editor;
}
void CommonDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    QPlainTextEdit* textEdit = qobject_cast<QPlainTextEdit*>(editor);
    if (textEdit) {
        textEdit->setPlainText(index.data(Qt::EditRole).toString());
        QTextCursor cursor = textEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        textEdit->setTextCursor(cursor);
    }
}
void CommonDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    QPlainTextEdit* textEdit = qobject_cast<QPlainTextEdit*>(editor);
    if (textEdit) model->setData(index, textEdit->toPlainText(), Qt::EditRole);
}
void CommonDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const
{
    QRect rect = option.rect; rect.adjust(8,8,-8,-8); editor->setGeometry(rect);
}

SearchDelegate::SearchDelegate(MainWindow* mainWindow, QObject* parent) : QStyledItemDelegate(parent), mainWindowPtr(mainWindow) {}
QWidget* SearchDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const
{
    QTableWidget* table = qobject_cast<QTableWidget*>(this->parent());
    if (table) {
        QTableWidgetItem* item = table->item(index.row(), index.column());
        if (item) item->setIcon(QIcon());
    }
    QLineEdit* editor = new QLineEdit(parent);
    QFont font; font.setFamily("Roboto Light"); font.setPixelSize(20);
    editor->setFont(font); editor->setFrame(false);
    QString currentText = index.data().toString();
    QToolButton* clearButton = new QToolButton(editor);
    QPixmap pixmap(":/icons/clear_button.png");
    if (!pixmap.isNull()) clearButton->setIcon(QIcon(pixmap.scaled(40,40,Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    clearButton->setIconSize(QSize(40,40)); clearButton->setCursor(Qt::PointingHandCursor); clearButton->setToolTip("Очистить");
    clearButton->setStyleSheet("QToolButton { border: none; padding: 2px; background: transparent; } QToolButton:hover { background: rgba(0, 0, 0, 0.1); border-radius: 3px; }");
    QHBoxLayout* layout = new QHBoxLayout(editor); layout->addStretch(); layout->addWidget(clearButton); layout->setContentsMargins(0,0,8,0); layout->setSpacing(0); editor->setLayout(layout);
    editor->setTextMargins(0,0,35,0);
    QObject::connect(clearButton, &QToolButton::clicked, editor, &QLineEdit::clear);
    QObject::connect(editor, &QLineEdit::textChanged, clearButton, [clearButton](const QString& text) { clearButton->setVisible(!text.isEmpty()); });
    editor->setText(currentText); clearButton->setVisible(!currentText.isEmpty());
    SearchDelegate* self = const_cast<SearchDelegate*>(this);
    QObject::connect(editor, &QLineEdit::textChanged, editor, [self, editor, this]() { emit self->commitData(editor); if (this->mainWindowPtr) this->mainWindowPtr->restartFilterTimer(); });
    return editor;
}
void SearchDelegate::destroyEditor(QWidget* editor, const QModelIndex& index) const
{
    QTableWidget* table = qobject_cast<QTableWidget*>(this->parent());
    if (table) {
        QTableWidgetItem* item = table->item(index.row(), index.column());
        if (item) {
            QIcon searchIcon; QPixmap pixmap(":/icons/search_icon.png");
            QPixmap scaled = pixmap.scaled(22, 23, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            searchIcon.addPixmap(scaled, QIcon::Normal); searchIcon.addPixmap(scaled, QIcon::Active); searchIcon.addPixmap(scaled, QIcon::Selected);
            item->setIcon(searchIcon);
        }
    }
    QStyledItemDelegate::destroyEditor(editor, index);
}

HTMLDelegate::HTMLDelegate(QObject* parent) : QStyledItemDelegate(parent), m_commonDelegate(new CommonDelegate(this)) {}
QWidget* HTMLDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const { return m_commonDelegate->createEditor(parent, option, index); }
void HTMLDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.flags() & Qt::ItemIsEditable) { m_commonDelegate->paint(painter, option, index); return; }
    QString htmlText = index.data(Qt::DisplayRole).toString();
    QStyleOptionViewItem options = option;
    QString originalText = options.text; options.text = "";
    QStyle *style = options.widget ? options.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &options, painter, options.widget);
    options.text = originalText;
    if (htmlText.isEmpty()) return;
    painter->save();
    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &options);
    QTextDocument doc; doc.setHtml(htmlText); doc.setTextWidth(textRect.width());
    painter->translate(textRect.left(), textRect.top());
    QRect clip(0,0,textRect.width(),textRect.height());
    doc.drawContents(painter, clip);
    painter->restore();
}
QSize HTMLDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.flags() & Qt::ItemIsEditable) return m_commonDelegate->sizeHint(option, index);
    QString htmlText = index.data(Qt::DisplayRole).toString();
    if (htmlText.isEmpty()) return QSize(0,0);
    QTextDocument doc; doc.setHtml(htmlText); doc.setTextWidth(option.rect.width());
    return QSize(doc.idealWidth(), doc.size().height());
}
