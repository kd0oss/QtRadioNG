#ifndef SDR1000FRAME_H
#define SDR1000FRAME_H

//#include <QtCore>
//#if QT_VERSION >= 0x050000
//#include <QtWidgets/QFrame>
//#else
#include <QFrame>
#include <QSettings>
//#endif
#include "UI.h"


namespace Ui {
    class Sdr1000Frame;
}

class Sdr1000Frame : public QFrame
{
    Q_OBJECT

public:
    explicit Sdr1000Frame(Radio *pUI, QWidget *parent = 0);
    ~Sdr1000Frame();
    void initializeRadio(void);
    void shutDown(void);

    int8_t radio_id;
    int8_t currentRxChannel;
    int8_t currentTxChannel;

private:
    Ui::Sdr1000Frame *ui;
    bool tuning;
    Radio *pui;
    QSettings *settings;
    QTimer *itimer;

private slots:
//    void adjustPower(int);
//    void pwrSliderValueChanged(int);
//    void tunePwrSliderValueChanged(int);
    void tuningEnabled(bool);
    void setAttenuation(bool);
//    void attSliderChanged(void);
//    void tuneClicked(void);
    void initialize(void);

signals:
    void hhcommand(QByteArray);
    void pttTuneChange(int caller, bool ptt);//0 = MOX, 1 = Tune, 2 = VOX, 3 = Extern H'ware};
};
#endif // SDR1000FRAME_H
