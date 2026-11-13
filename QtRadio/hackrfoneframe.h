#ifndef HACKRFONEFRAME_H
#define HACKRFONEFRAME_H

#include <QDialog>
#include <QFrame>
#include <QSettings>
#include "UI.h"
// ../hackrf_one/hackrf.h is included by hackrfoneframe.cpp only: its command
// names clash with ../hpsdr/hermes.h, which UI.cpp also sees.

namespace Ui {
class hackrfOneframe;
}

class hackrfOneframe : public QDialog
{
    Q_OBJECT

public:
    explicit hackrfOneframe(Radio *pUI, QWidget *parent = nullptr);
    ~hackrfOneframe();
    void initializeRadio(void);
    void shutDown(void);

    // bandscope span: the HackRF LO sits HACKRF_IF_OFFSET below the receiver
    double bandscopeCentre(long long rxFrequency) const;
    long   hardwareRate(void) const { return hw_rate; }

    int8_t radio_id;
    int8_t currentRxRfstream;
    int8_t currentTxRfstream;
    long   sample_rate[1];      // receiver rate sent to dspserver (48 - 384 kHz)

private:
    Ui::hackrfOneframe *ui;

    Radio *pui;
    QSettings *settings;
    QTimer *itimer;
    long   hw_rate;             // HackRF ADC rate (8 - 20 MHz)

    void loadSettings(void);
    void saveSetting(const QString &key, const QVariant &value);
    void sendStarCommand(char command, const QByteArray &argument);
    long checkedRxRate(void) const;
    long checkedHwRate(void) const;

private slots:
    void initialize(void);
    void sampleRateChanged(void);
    void hwRateChanged(void);
    void lnaGainChanged(int);
    void vgaGainChanged(int);
    void rfAmpToggled(bool);
    void antPowerToggled(bool);
    void ppmChanged(double);
    void txGainChanged(int);
    void txAmpToggled(bool);
    void micGainChanged(int);

signals:
    void hhcommand(QByteArray);
    void sampleRateUpdated(long);
    void hwRateUpdated(long);
    void pttTuneChange(int caller, bool ptt);   // not used (receive only); UI.cpp connects it for every panel
};

#endif // HACKRFONEFRAME_H
