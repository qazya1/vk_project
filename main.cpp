#include "mainwindow.h"
#include <QFontDatabase>

#include <QApplication>

int main(int argc, char *argv[])
{

    QApplication a(argc, argv);

    // Загрузка шрифта из ресурсов
    QFontDatabase::addApplicationFont(":/fonts/Roboto-Light.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Roboto-Medium.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Roboto-Regular.ttf");

    MainWindow w;
    w.show();
    return a.exec();
}
