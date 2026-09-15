#include "pagebuttonclass.h"

pageButtonClass::pageButtonClass(QWidget *parent, const int &page)
    : QPushButton(parent)
{
    this->n = page;
    // Connect the QPushButton's default clicked() signal to your private slot
    connect(this, &QPushButton::clicked, this, &pageButtonClass::handleClicked);
}

void pageButtonClass::handleClicked()
{
    emit clickedPage(n);
}

void pageButtonClass::setPage(int new_page)
{
    this->n = new_page;
}
