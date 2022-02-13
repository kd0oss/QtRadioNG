#ifndef HERMESFRAME_H
#define HERMESFRAME_H

//#include <QtCore>
//#if QT_VERSION >= 0x050000
//#include <QtWidgets/QFrame>
//#else
#include <QFrame>
#include <QSettings>
//#endif
#include "UI.h"
#include "../hpsdr/hermes.h"


namespace Ui {
    class HermesFrame;
}

class HermesFrame : public QFrame
{
    Q_OBJECT

public:
    explicit HermesFrame(Radio *pUI, QWidget *parent = 0);
    ~HermesFrame();
    void initializeRadio(void);
    void shutDown(void);

    int8_t radio_id;
    int8_t currentRxChannel;
    int8_t currentTxChannel;

private:
    Ui::HermesFrame *ui;
    bool tuning;
    Radio *pui;
    QSettings *settings;
    QTimer *itimer;

    void getSerial(void);

private slots:
    void adjustPower(int);
    void adjustMicBoost(bool);
    void enablePreamp(bool);
    void enableDither(bool);
    void enableRandom(bool);
    void setRxAntenna(void);
    void setTxRelay(void);
    void pwrSliderValueChanged(int);
    void tunePwrSliderValueChanged(int);
    void tuningEnabled(bool);
    void setAttenuation(bool);
    void setOCOutputs(void);
    void attSliderChanged(void);
    void tuneClicked(void);
    void initialize(void);

signals:
    void hhcommand(QByteArray);
    void pttTuneChange(int caller, bool ptt);//0 = MOX, 1 = Tune, 2 = VOX, 3 = Extern H'ware};
};
#endif // HERMESFRAME_H
