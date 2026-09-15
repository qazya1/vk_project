#include "customtabwidget.h"
#include "QTabBar"
#include "QStylePainter"
#include "QStyleOptionTab"
#include "QDebug"

class CustomTabBar : public QTabBar
{
public:
    CustomTabBar(QWidget *parent = nullptr) : QTabBar(parent)
    {
        // Можно настроить отступы по умолчанию
        //setMargins(41, 11);
        setTabMargins(21, 6, 21, 6);
    }

    // Метод для установки отступов
    void setMargins(int horizontal, int vertical)
    {
        m_horizontalMargin = horizontal;
        m_verticalMargin = vertical;
        update(); // Перерисовываем
        updateGeometry(); // Обновляем геометрию
    }

    // Установка разных отступов для каждой вкладки
    void setTabMargins(int left, int top, int right, int bottom)
    {
        m_tabMargins[1] = QMargins(left, top, right, bottom);
        m_tabMargins[0] = QMargins(left+49, top, right, bottom);
        //m_tabMargins[2] = QMargins(left, top, right+parentWidget()->parentWidget()->parentWidget()->width(), bottom);
        m_tabMargins[2] = QMargins(left, top, right+1000000, bottom);
        update();
        updateGeometry();
    }

protected:
    QSize tabSizeHint(int index) const override
    {
        QSize size = QTabBar::tabSizeHint(index);

        // Базовые размеры вкладок
        if (index != 1) {
            size = QSize(192, 40);
        } else {
            size = QSize(300, 40);
        }

        // Добавляем отступы
        QMargins margins = getMarginsForTab(index);
        size += QSize(margins.left() + margins.right(),
                     margins.top() + margins.bottom());

        return size;
    }

    QSize minimumTabSizeHint(int index) const override
    {
        return tabSizeHint(index);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QStylePainter painter(this);
        QStyleOptionTab option;

        for (int i = 0; i < count(); ++i) {
            initStyleOption(&option, i);

            QRect rect = QTabBar::tabRect(i);
            QMargins margins = getMarginsForTab(i);

            // Применяем отступы
            rect.adjust(margins.left(), margins.top(), -margins.right(), -margins.bottom());
            option.rect = rect;

            painter.drawControl(QStyle::CE_TabBarTab, option);
            if (currentIndex() ==i) drawSelectedTabDecoration(painter, rect, option);
        }
    }

    void drawSelectedTabDecoration(QStylePainter &painter, const QRect &tabRect, const QStyleOptionTab &option)
    {
        // Сохраняем текущие настройки пера
        QPen oldPen = painter.pen();

        // Устанавливаем перо для подчеркивания
        QPen underlinePen(QColor(255, 3, 3));
        underlinePen.setWidth(1);
        painter.setPen(underlinePen);

        // Вычисляем позицию линии подчеркивания
        int underlineY = tabRect.bottom() - 7;

        // Рисуем линию подчеркивания
        painter.drawLine(tabRect.left()+43, underlineY, tabRect.right()-5, underlineY);

        // Восстанавливаем перо
        painter.setPen(oldPen);

        // Альтернативный вариант: можно нарисовать рамку вокруг выделенной вкладки
        /*
        QPen borderPen(Qt::blue, 2);
        painter.setPen(borderPen);
        painter.drawRect(tabRect.adjusted(1, 1, -1, -1));
        painter.setPen(oldPen);
        */
    }

private:
    int m_horizontalMargin = 41;
    int m_verticalMargin = 11;
    QHash<int, QMargins> m_tabMargins; // Индивидуальные отступы для вкладок

    QMargins getMarginsForTab(int index) const
    {
        if (m_tabMargins.contains(index)) {
            return m_tabMargins[index];
        }
        return QMargins(m_horizontalMargin, m_verticalMargin,
                       m_horizontalMargin, m_verticalMargin);
    }
};

customTabWidget::customTabWidget(QWidget *parent) : QTabWidget(parent)
{
    CustomTabBar* customTabBar = new CustomTabBar(this);
    setTabBar(customTabBar);
}
