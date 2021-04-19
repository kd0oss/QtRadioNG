#ifndef HERMESFRAME_H
#define HERMESFRAME_H

#include <QFrame>
#include "UI.h"

enum HCOMMAND_SET {
    HQUESTION = 0,
    HQLOFFSET,
    HQCOMMPROTOCOL1,
    HQINFO,
    HQCANTX,
    HSTARCOMMAND,
    STARGETSERIAL,
//    ISMIC,
//    HSETFREQ,
    SETPREAMP,
    SETMICBOOST,
    SETPOWEROUT,
    SETRXANT,
    SETDITHER,
    SETRANDOM,
//    ATTACH,
//    TX,
//    DETACH,
    SETLINEIN,
    SETLINEINGAIN,
    SETTXRELAY,
    SETOCOUTPUT,
    GETADCOVERFLOW,
    SETATTENUATOR,
//    SETRECORD,
//    STARTIQ,
//    STARTBANDSCOPE,
//    STOPIQ,
//    STOPBANDSCOPE

//    MOX = 254,
//    QHARDWARE = 255
};

namespace Ui {
class HermesFrame;
}

class HermesFrame : public QFrame
{
    Q_OBJECT

public:
    explicit HermesFrame(UI *pUI, QWidget *parent = 0);
    ~HermesFrame();

private:
    Ui::HermesFrame *ui;
    bool tuning;
    UI *pui;

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

signals:
    void hhcommand(QByteArray);
};

#endif // HERMESFRAME_H
