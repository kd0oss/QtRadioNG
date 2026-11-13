#ifndef FLEXFRAME_H
#define FLEXFRAME_H

#include <QDialog>
#include <QSettings>
#include "UI.h"
// ../flexradio/flex.h is included by flexframe.cpp only.

namespace Ui {
class FlexFrame;
}

/* Panel for FLEX-6000/8000/Aurora radios served by the flexradio program. */
class FlexFrame : public QDialog
{
    Q_OBJECT

public:
    explicit FlexFrame(Radio *pUI, QWidget *parent = nullptr);
    ~FlexFrame();
    void initializeRadio(void);
    void shutDown(void);
    void setMode(const QString &mode);   // QtRadio mode name, for the radio's modulator

    int8_t radio_id;
    int8_t currentRxRfstream;
    int8_t currentTxRfstream;
    long   sample_rate[1];      // DAX IQ rate, also the receiver rate

private:
    Ui::FlexFrame *ui;
    Radio *pui;
    QSettings *settings;
    QTimer *itimer;
    QString currentMode;

    void saveSetting(const QString &key, const QVariant &value);
    void sendStarCommand(char command, const QByteArray &argument);
    long checkedRate(void) const;

private slots:
    void initialize(void);
    void sampleRateChanged(void);
    void antennaChanged(int);
    void rfGainChanged(int);
    void rfPowerChanged(int);

signals:
    void hhcommand(QByteArray);
    void sampleRateUpdated(long);
    void pttTuneChange(int caller, bool ptt);   // not used; UI.cpp connects it for every panel
};

#endif // FLEXFRAME_H
