#ifndef VFO_H
#define VFO_H


#include <QtCore>

#if QT_VERSION >= 0x050000
#include <QtWidgets/QFrame>
#else
#include <QFrame>
#endif

#include <QWheelEvent>
#include <QBasicTimer>
#include <QSettings>
#include "Band.h"

namespace Ui {
    class vfo;
}

class vfo : public QFrame
{
    Q_OBJECT

public:
    explicit vfo(QWidget *parent = 0);
    ~vfo();

    void readSettings(QSettings* settings);
    void writeSettings(QSettings* settings);
    void setFrequency(int freq); //Displays "freq" on current vfo according to ptt state
    void checkBandBtn(int band);
    void setBandFrequency(long long f) {bandFrequency = f;}
    QString rigctlGetvfo();
    long long getTxFrequency();
    void pttChange(bool pttState);
    bool getPtt();

public slots:
    void on_pBtnvfoA_clicked();  // moved from private for rigctl
    void on_pBtnvfoB_clicked();
    void refocus(); // returns focus to vfo frame after clicking subrx
    void setSelectedReceiver(QString);
    void resetSelectedReceiver(void);
    void changeReceiver(int);
    void setCurrentReceiver(int);

signals:
    void sendVfoFreq(int freq);
    void sendTxFreq(int freq, bool ptt);
    void setFreq(int freq, bool ptt);
    void frequencyChanged(long long freq);
    void frequencyMoved(int, int);
    void bandBtnClicked(int band);
    void rightBandClick();
    void getBandFrequency();
    void vfoStepBtnClicked(int direction);
    void receiverChanged(int);

protected:
    void wheelEvent( QWheelEvent*);
    void mousePressEvent( QMouseEvent*);

private slots:
    void btnGrpClicked(int);
    void on_pBtnScanDn_clicked();
    void on_pBtnScanUp_clicked();
    void on_pBtnExch_clicked();
    void on_pBtnAtoB_clicked();
    void on_pBtnBtoA_clicked();
    void processRIT(int);
    void on_pBtnRIT_clicked();
    void on_toolBtnUp_clicked();
    void on_toolBtnDn_clicked();
    void press(); // Powermate pressed
    void release(); // Powermate released
    void increase(int n); // Powermate increased
    void decrease(int n); // Powermate decreased
    void receiverChangedSlot(int);

private:
    Ui::vfo *ui;
    void keyPressEvent( QKeyEvent * event);
    void vfohotkey(QString cmd);
    int vfohotstep;
    int curstep;
    void setStepMark();
    QSettings *settings;
    QBasicTimer timer;
    void writeA(long long);
    void writeB(long long);
    void vfoEnabled(bool setA, bool setB);
    void setVfoBtnColour();
    void timerEvent(QTimerEvent *event);
    long long readA();
    long long readB();
    int getDigit(int x, int y);
    bool ptt; // ptt on = true, ptt off = false
    char selectedVFO; //'A', 'B', 'S' to indicate which vfo state.
    long long bandFrequency; //A copy of band.currentFrequency & updated by emit getbandFrequency
    enum BandData
        {
            bDat_mem00,
            bDat_mem01,
            bDat_mem02,
            bDat_mem03,
            bDat_cFreq,
            bDat_fqmin,
            bDat_fqmax,
            bDat_modee,
            bDat_filtH,
            bDat_filtL,
            bDat_index
        };
};

#endif // VFO_H
