#ifndef HERMESFRAME_H
#define HERMESFRAME_H

#include <QFrame>
#include <QSettings>
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
    int8_t currentRxRfstream;
    int8_t currentTxRfstream;
    long   sample_rate[1];

private:
    Ui::HermesFrame *ui;
    bool tuning;
    Radio *pui;
    QSettings *settings;
    QTimer *itimer;


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
    void sampleRateChanged(void);

signals:
    void hhcommand(QByteArray);
    void sampleRateUpdated(long);
    void pttTuneChange(int caller, bool ptt);//0 = MOX, 1 = Tune, 2 = VOX, 3 = Extern H'ware};
};
#endif // HERMESFRAME_H
