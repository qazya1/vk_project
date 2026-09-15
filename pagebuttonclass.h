#ifndef PAGEBUTTONCLASS_H
#define PAGEBUTTONCLASS_H

#include <QPushButton>

class pageButtonClass : public QPushButton
{
    Q_OBJECT // Required for signals and slots

public:
    explicit pageButtonClass(QWidget *parent = nullptr, const int &page=1);
    void setPage(int new_page);

private:
    int n;

signals:
    // Declare your custom signal here
    void clickedPage(int value);

private slots:
    // A private slot to handle the default clicked() signal
    void handleClicked();
};

#endif // PAGEBUTTONCLASS_H
