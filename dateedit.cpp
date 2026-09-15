#include "dateedit.h"

DateEdit::DateEdit(QWidget *parent) : QDateEdit (parent) {
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setCalendarPopup(false);
    setDate(QDate::currentDate());

    calendar->setWindowFlags(Qt::Popup);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    connect(calendar, &QCalendarWidget::clicked, this, [&](const QDate &date) {
        setDate(date);
        calendar->hide();
    });
}

void DateEdit::focusInEvent(QFocusEvent *event) {
    if (!calendar->isVisible()) {
        calendar->setSelectedDate(date());
        calendar->move(mapToGlobal(QPoint(0, height())));
        calendar->show();
    }

    return QDateEdit::focusInEvent(event);
}
