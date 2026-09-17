#ifndef XMLIMPORTTABCONTROLLER_H
#define XMLIMPORTTABCONTROLLER_H

#include <QObject>
#include <QString>

namespace Ui { class MainWindow; }
class AnnouncementService;
class QWidget;

class XmlImportTabController : public QObject
{
    Q_OBJECT
public:
    explicit XmlImportTabController(Ui::MainWindow *ui,
                                    AnnouncementService *announcementService,
                                    QWidget *dialogParent,
                                    QObject *parent = nullptr);

    void setup();

signals:
    void xmlImportedSuccessfully();

private slots:
    void chooseXmlFile();
    void importXml();

private:
    Ui::MainWindow *m_ui;
    AnnouncementService *m_announcementService;
    QWidget *m_dialogParent;
};

#endif // XMLIMPORTTABCONTROLLER_H
