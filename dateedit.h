#ifndef DATEEDIT_H
#define DATEEDIT_H

#include <QDateEdit>
#include <QCalendarWidget>

class DateEdit : public QDateEdit
{
    Q_OBJECT
public:
    explicit DateEdit(QWidget *parent = nullptr);

protected:
    virtual void focusInEvent(QFocusEvent *event) override;

private:
    QCalendarWidget *calendar = new QCalendarWidget(this);

};

#endif // DATEEDIT_H
