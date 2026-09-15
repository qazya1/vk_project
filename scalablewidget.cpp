#include <QWidget>

class ScalableWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScalableWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        updateScaling();
    }

    void updateScaling()
    {
        qreal dpiScale = devicePixelRatioF();
        int baseSize = 100; // базовый размер
        setMinimumSize(baseSize * dpiScale, baseSize * dpiScale);
    }

protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        updateScaling();
    }
};
