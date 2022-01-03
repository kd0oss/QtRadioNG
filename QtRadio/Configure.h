/*
 * File:   Configure.h
 * Author: John Melton, G0ORX/N6LYT
 *
 * Created on 16 August 2010, 20:03
 */

/* Copyright (C)
* 2009 - John Melton, G0ORX/N6LYT
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

#ifndef _CONFIGURE_H
#define	_CONFIGURE_H

#include <QSettings>
#include <QDebug>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QKeyEvent>

#include "ui_Configure.h"

#include "Audio.h"
#include "Xvtr.h"
#include "Audioinput.h"

class Configure : public QDialog {
    Q_OBJECT

public:
    Configure();
    virtual ~Configure();
    void initAudioDevices(Audio* audio);
    void initMicDevices(AudioInput* audioinput);
    void initXvtr(Xvtr* xvtr);
    void loadSettings(QSettings* settings);
    void saveSettings(QSettings* settings);
    void keyReleaseEvent(QKeyEvent* event); // KD0OSS

    void connected(bool state);
    void updateXvtrList(Xvtr* xvtr);

    quint32  pttKeyId;  // KD0OSS

    QString getHost();
    void addHost(QString host);
    void removeHost(QString host);
    int getReceiver();
    
    int getSpectrumHigh();
    int getSpectrumLow();
    int getFps();
    int getAvg();
    void setSpectrumHigh(int high);
    void setSpectrumLow(int low);

    int getWaterfallHigh();
    int getWaterfallLow();

    void setWaterfallHigh(int high);
    void setWaterfallLow(int low);

    int  getWindowType();
    int  getRxFilterWindow();
    int  getTxFilterWindow();

    int getNrTaps();
    int getNrDelay();
    double getNrGain();
    double getNrLeak();

    int getAnfTaps();
    int getAnfDelay();
    double getAnfGain();
    double getAnfLeak();

    double getNbThreshold();
    double getSdromThreshold();
    bool getGeometryState();
    bool getTxAllowed();
    void setTxAllowed(bool newstate);
    bool setPasswd(QString ServerName);
    QString thisuser;
    QString thispass;
    bool getRxIQcheckboxState();
    double getRxIQspinBoxValue();
    int getCwPitch();
    bool getRxIQdivCheckBoxState();
    bool getRxDCBlockValue(); //KD0OSS
    bool getTxDCBlockValue(); //KD0OSS

    int  getRxAGCSlopeValue(); //KD0OSS
    int  getRxAGCMaxGainValue(); //KD0OSS
    int  getRxAGCAttackValue(); //KD0OSS
    int  getRxAGCDecayValue(); //KD0OSS
    int  getRxAGCHangValue(); //KD0OSS
    int  getRxAGCFixedGainValue(); //KD0OSS
    int  getRxAGCHangThreshValue(); //KD0OSS

    bool getLevelerEnabledValue(); //KD0OSS
//    int  getLevelerMaxGainValue(); //KD0OSS
    int  getLevelerAttackValue(); //KD0OSS
    int  getLevelerDecayValue(); //KD0OSS
    int  getLevelerHangValue(); //KD0OSS

    bool getALCEnabledValue(); //KD0OSS
    int  getALCAttackValue(); //KD0OSS
    int  getALCDecayValue(); //KD0OSS
    int  getALCMaxGainValue(); //KD0OSS

signals:
    void hostChanged(QString host);
    void receiverChanged(int receiver);
    void spectrumHighChanged(int high);
    void spectrumLowChanged(int low);
    void fpsChanged(int fps);
    void waterfallHighChanged(int high);
    void waterfallLowChanged(int low);
    void waterfallAutomaticChanged(bool state);
    void audioDeviceChanged(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian order);
    void micDeviceChanged(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian order);
    void get_audio_devices(QComboBox* comboBox);
    void micDeviceChanged(QAudioDeviceInfo info);

    void nrValuesChanged(int taps,int delay,double gain,double leak);
    void anfValuesChanged(int taps,int delay,double gain,double leak);

    void nbThresholdChanged(double threshold);
    void sdromThresholdChanged(double threshold);

    void addXVTR(QString title,long long minFrequency,long long maxFrequency,long long ifFrequency,long long freq,int m,int filt);
    void deleteXVTR(int index);

    void txIQPhaseChanged(double arg1); //KD0OSS
    void txIQGainChanged(double arg1); //KD0OSS
    void rxIQPhaseChanged(double arg1); //KD0OSS
    void rxIQGainChanged(double arg1); //KD0OSS
    void RxIQcheckChanged(bool state);
    void RxIQspinChanged(double num);
    void spinBox_cwPitchChanged(int pitch);
    void avgSpinChanged(int value);

    void rxDCBlockChanged(bool state);  //KD0OSS
    void rxDCBlockGainChanged(int value); //KD0OSS
    void txDCBlockChanged(bool state);  //KD0OSS
    void windowTypeChanged(int type); //KD0OSS
/*    void nbTransitionChanged(double); //KD0OSS
    void nbLeadChanged(double); //KD0OSS
    void nbLagChanged(double); //KD0OSS */
    void agcSlopeChanged(int); //KD0OSS
    void agcMaxGainChanged(int); //KD0OSS
    void agcAttackChanged(int); //KD0OSS
    void agcDecayChanged(int); //KD0OSS
    void agcHangChanged(int); //KD0OSS
    void agcFixedGainChanged(int); //KD0OSS
    void agcHangThreshChanged(int); //KD0OSS
    void levelerStateChanged(int); //KD0OSS
//    void levelerMaxGainChanged(int); //KD0OSS
    void levelerAttackChanged(int); //KD0OSS
    void levelerDecayChanged(int); //KD0OSS
    void levelerHangChanged(int); //KD0OSS
    void alcStateChanged(int); //KD0OSS
    void alcAttackChanged(int); //KD0OSS
    void alcDecayChanged(int); //KD0OSS
    void alcMaxGainChanged(int); //KD0OSS
    void cessbOvershootChanged(bool);
    void aeFilterChanged(bool);
    void preAGCChanged(bool);
    void nrGainMethodChanged(int);
    void nrNpeMethodChanged(int);
    void nb2ModeChanged(int);
    void rxFilterWindowChanged(int);
    void txFilterWindowChanged(int);


public slots:
    void slotHostChanged(int selection);
    void slotReceiverChanged(int receiver);
    void slotSpectrumHighChanged(int high);
    void slotSpectrumLowChanged(int low);
    void slotFpsChanged(int fps);
    void slotWaterfallHighChanged(int high);
    void slotWaterfallLowChanged(int low);
    void slotWaterfallAutomaticChanged(bool state);
    void slotAudioDeviceChanged(int selection);
    void slotMicDeviceChanged(int selection);
    void slotWindowType(int type); //KD0OSS

    void slotNrTapsChanged(int taps);
    void slotNrDelayChanged(int delay);
    void slotNrGainChanged(int gain);
    void slotNrLeakChanged(int leak);

    void slotAnfTapsChanged(int taps);
    void slotAnfDelayChanged(int delay);
    void slotAnfGainChanged(int gain);
    void slotAnfLeakChanged(int leak);

    void slotNbThresholdChanged(int threshold);
    void slotSdromThresholdChanged(int threshold);

    void slotCESSBOvershoot(bool enable);
    void slotAEFilterChanged(bool enable);
    void slotPreAGCChanged(bool enable);
    void slotNRgainMethodChanged(bool tmp);
    void slotNRnpeMethodChanged(bool tmp);
    void slotNB2ModeChanged(int mode);
    void slotRxFilterWindowChanged(int type);
    void slotTxFilterWindowChanged(int type);

    void slotXVTRAdd();
    void slotXVTRDelete();

private slots:
    void on_pBtnAddHost_clicked();
    void on_pBtnRemHost_clicked();
//    void on_encodingComboBox_currentIndexChanged(int index);
/*    void onNbTransitionChanged(double); //KD0OSS
    void onNbLeadChanged(double); //KD0OSS
    void onNbLagChanged(double); //KD0OSS */
    void onRxAgcSlopeChanged(int); //KD0OSS
    void onRxAgcMaxGainChanged(double); //KD0OSS
    void onRxAgcAttackChanged(int); //KD0OSS
    void onRxAgcDecayChanged(int); //KD0OSS
    void onRxAgcHangChanged(int); //KD0OSS
    void onRxAgcFixedGainChanged(int); //KD0OSS
    void onRxAgcHangThreshChanged(int); //KD0OSS
    void onLevelerStateChanged(int); //KD0OSS
    //void onLevelerMaxGainChanged(int); //KD0OSS
    void onLevelerAttackChanged(int); //KD0OSS
    void onLevelerDecayChanged(int); //KD0OSS
    void onLevelerHangChanged(int); //KD0OSS
    void onAlcStateChanged(int); //KD0OSS
    void onAlcAttackChanged(int); //KD0OSS
    void onAlcDecayChanged(int); //KD0OSS
    void onAlcMaxGainChanged(double); //KD0OSS

    void setPTTKey(bool); //KD0OSS

    void on_userpasssave_clicked();

    void on_spinBox_cwPitch_valueChanged(int arg1);

//    void on_MicEncodingComboBox_currentIndexChanged(int index);

    void on_avgSpinBox_valueChanged(int arg1);

private:
    Ui::Configure widget;

    bool capturePTTKey;  // KD0OSS

};

#endif	/* _CONFIGURE_H */
