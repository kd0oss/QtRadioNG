/*
 * File:   UI.h
 * Author: John Melton, G0ORX/N6LYT
 *
 * Created on 13 August 2010, 14:28
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

#ifndef _UI_H
#define	_UI_H

#include "ui_UI.h"

#include <QDebug>
#include <QSettings>
#include <QTimer>
#include <QAudioFormat>
#include <QVector>
#include <QQueue>
#include <QThread>

#include "servers.h"
#include "About.h"
#include "Configure.h"
#include "Audio.h"
#include "Audioinput.h"
#include "Connection.h"
#include "Panadapter.h"
#include "Band.h"
#include "BandLimit.h"
#include "Mode.h"
#include "Filters.h"
#include "Bandscope.h"
#include "BookmarkDialog.h"
#include "Bookmark.h"
#include "Bookmarks.h"
#include "BookmarksDialog.h"
#include "BookmarksEditDialog.h"
#include "Xvtr.h"
#include "XvtrEntry.h"
#include "KeypadDialog.h"
#include "vfo.h"
#include "rigctl.h"
#include "ctl.h"
#include "G711A.h"
//#include "hermesframe.h"
#include "EqualizerDialog.h"

#define AGC_FIXED 0
#define AGC_LONG 1
#define AGC_SLOW 2
#define AGC_MEDIUM 3
#define AGC_FAST 4

#define MIC_BUFFER_SIZE 600     // make sure it is bigger than codec2_samples_per_frame (max 320)
#define MIC_NO_OF_FRAMES 4      // need to ensure this is the same value in dspserver
#define MIC_ALAW_BUFFER_SIZE 512 //58 // limited by the 64 byte TCP message frame

#define MAX_RECEIVERS 7

class UI : public QMainWindow {
    Q_OBJECT

public:
    UI(const QString server = QString(""));
    virtual ~UI();
    void closeEvent(QCloseEvent* event);
    void keyPressEvent(QKeyEvent * event);
    void keyReleaseEvent(QKeyEvent * event);
    void loadSettings();
    void saveSettings();
   // void sendCommand(QString command);
    long long rigctlGetFreq();
    QString rigctlGetMode();
    QString rigctlGetFilter();
    QString rigctlGetVFO();
    void rigctlSetVFOA();
    void rigctlSetVFOB();
    void rigctlSetFreq(long long f);
    void rigctlSetMode(int newmode);
    void rigctlSetFilter(int newfilter);
    void rigSetPTT(int enabled);
    bool rigGetPTT(void);
    ServerConnection connection;
    SpectrumConnection spectrumConnection;
    AudioConnection audioConnection;
    MicAudioConnection micAudioConnection;
    TxPanadapter *txp;
    Mode mode;
    bool   receivers_active[7];
    int8_t receiver_channel[7];
    int8_t currentRxChannel;
    int8_t currentTxChannel;
    double currentPwr;

signals:
    void initialize_audio(int length);
    void select_audio(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian byteOrder);
    //void process_audio(QByteArray*);
    void process_audio(char* header,char* buffer,int length);
    void HideTX(bool cantx);

public slots:
    void getMeterValue(float s, float f, float r);

    bool newDspServerCheck(void);

    void actionConfigure();
    void actionEqualizer();
    void actionAbout();
    void actionConnect();
    void actionConnectNow(QString IP);
    void actionDisconnectNow();
    void actionDisconnect();
    void actionQuick_Server_List();
    void actionBandscope();
    void actionRecord();

    void actionMuteMainRx();

    void actionSquelch();
    void actionSquelchReset();
    void squelchValueChanged(int);

    void actionKeypad();
    void setKeypadFrequency(long long);

    void getBandBtn(int btn);
    void quickMemStore();
    void action160();
    void action80();
    void action60();
    void action40();
    void action30();
    void action20();
    void action17();
    void action15();
    void action12();
    void action10();
    void action6();
    void actionGen();
    void actionWWV();

    void actionCWL();
    void actionCWU();
    void actionLSB();
    void actionUSB();
    void actionDSB();
    void actionAM();
    void actionSAM();
    void actionFMN();
    void actionDIGL();
    void actionDIGU();

    void actionFilter0();
    void actionFilter1();
    void actionFilter2();
    void actionFilter3();
    void actionFilter4();
    void actionFilter5();
    void actionFilter6();
    void actionFilter7();
    void actionFilter8();
    void actionFilter9();
    void actionFilter10();

    void actionANF();
    void actionNR();
    void actionNB();
    void actionSDROM();

    void actionFixed();
    void actionSlow();
    void actionMedium();
    void actionFast();
    void actionLong();


    void connected(bool*, int8_t*, int8_t*);
    void disconnected(QString message);
    void audioBuffer(char* header,char* buffer);
    void spectrumBuffer(CHANNEL);

    void bandChanged(int previousBand,int newBand);
    void modeChanged(int previousMode,int newMode);
    void filtersChanged(FiltersBase* previousFilters,FiltersBase* newFilters);
    void filterChanged(int previousFilter,int newFilter);
    void variableFilter(int low, int high);
    void frequencyChanged(long long frequency);
    void sampleRateChanged(long rate);

 //   void updateSpectrum();
    void masterButtonClicked(void);

    void audioDeviceChanged(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian byteOrder);
 //   void encodingChanged(int choice);

    void micDeviceChanged(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian byteOrder);
    void micSendAudio(QQueue<qint16>*);

    void frequencyMoved(int increment,int step);

    void spectrumHighChanged(int high);
    void spectrumLowChanged(int low);
    void fpsChanged(int f);
    void setFPS(void);
    void waterfallHighChanged(int high);
    void waterfallLowChanged(int low);
    void waterfallAutomaticChanged(bool state);

    void hostChanged(QString host);
    void receiverChanged(int rx);

    void agcSlopeChanged(int);
    void agcMaxGainChanged(double);
    void agcAttackChanged(int);
    void agcDecayChanged(int);
    void agcHangChanged(int);
    void agcFixedGainChanged(double);
    void agcHangThreshChanged(int);
    void levelerStateChanged(int);
 //   void levelerMaxGainChanged(double);
    void levelerAttackChanged(int);
    void levelerDecayChanged(int);
    void levelerTopChanged(double);
    void TXalcStateChanged(int);
    void TXalcAttackChanged(int);
    void TXalcDecayChanged(int);
    void TXalcMaxGainChanged(double);

    void AGCTLevelChanged(int level);
    void enableRxEq(bool);
    void enableTxEq(bool);

    void nrValuesChanged(int,int,double,double);
    void anfValuesChanged(int,int,double,double);
    void nbThresholdChanged(double);
 //   void sdromThresholdChanged(double);
    void windowTypeChanged(int);
    void statusMessage(QString);
    void removeNotchFilter(void);

    void actionBookmark();
    void addBookmark();
    void selectABookmark();
    void editBookmarks();
    void bookmarkDeleted(int);
    void bookmarkUpdated(int,QString);
    void bookmarkSelected(int entry);

    void addXVTR(QString,long long,long long,long long,long long,int,int);
    void deleteXVTR(int index);
    void selectXVTR(QAction* action);
    void selectBookmark(QAction* action);
    void getBandFrequency();
    void vfoStepBtnClicked(int direction);
    void pttChange(int caller, bool ptt);
    void printStatusBar(QString message);
    void slaveSetMode(int newmode);
    void slaveSetFilter(int l, int r);
    void slaveSetZoom(int z);
    void setdspversion(long dspversion, QString dspversiontxt);
    void setChkTX(bool chk);
    void setservername(QString sname);
    void setCanTX(bool tx);
    void closeServers ();
    void cwPitchChanged(int cwPitch);
    void resetbandedges(double offset);

signals:
    void set_src_ratio(double ratio);
    void tuningEnable(bool);

protected:
//    void paintEvent(QPaintEvent*);
    void resizeEvent(QResizeEvent *);

private slots:
    void on_zoomSpectrumSlider_sliderMoved(int position);
    void addNotchFilter(void);
    void setAudioMuted(bool);
    void audioGainChanged(void);
    void hardwareSet(QString);
    void sendHardwareCommand(QByteArray);
    void setCurrentChannel(int);
    void closeBandScope(void);
    void cessbOvershootChanged(bool);
    void aeFilterChanged(bool);
    void nrGainMethodChanged(int);
    void nrNpeMethodChanged(int);
    void preAGCFiltersChanged(bool);
    void rxFilterWindowChanged(int);
    void txFilterWindowChanged(int);

private:
    void printWindowTitle(QString message);
    void actionGain(int g);
    void setGain(bool state);
    void initRigCtl();
    void setPwsMode(int mode);
    void appendBookmark(Bookmark* bookmark);
    QString stringFrequency(long long frequency);
    QString getversionstring();

    QLabel modeInfo;
    RigCtlServer *rigCtl;
    QString hardwareType;
    QWidget *hww;

    Ui::UI widget;
    Audio* audio;
    AudioInput* audioinput;
    QAudioDeviceInfo audio_device;
    QAudioFormat::Endian audio_byte_order;
    QMutex audio_mutex;
    char* first_audio_buffer;
    char* first_audio_header;
    int audio_sample_rate;
    int audio_channels;
    int audio_buffers;
    int gain;
    int pwsmode;
    int mic_buffer_count;       // counter of mic_buffer, to encode if reaches CODEC2_SAMPLE_PER_FRAME
    int mic_frame_count;        // counter of mic_buffer, to encode enough frames before sending

    qint16 mic_buffer[MIC_BUFFER_SIZE];
    unsigned char mic_encoded_buffer[MIC_BUFFER_SIZE];

    bool connection_valid;

    Band band;

    Filters filters;
    CWLFilters cwlFilters;
    CWUFilters cwuFilters;
    LSBFilters lsbFilters;
    USBFilters usbFilters;
    DSBFilters dsbFilters;
    AMFilters amFilters;
    SAMFilters samFilters;
    FMNFilters fmnFilters;
    DIGUFilters diguFilters;
    DIGLFilters diglFilters;

    Xvtr xvtr;

  //  void *hf;

    int agc;
    int cwPitch;
    long long frequency;
    int sampleRate;
    int fps;
    QTimer* spectrumTimer;

    About about;
    Configure configure;
    Servers *servers;

    Bandscope* bandscope;

    EqualizerDialog *equalizer;

    int sampleZoomLevel;
    int viewZoomLevel;

    int notchFilterIndex;

    BookmarkDialog bookmarkDialog;
    BookmarksDialog* bookmarksDialog;
    BookmarksEditDialog* bookmarksEditDialog;

    Bookmarks bookmarks;

    KeypadDialog keypad;
//    Meter* sMeter;
    int meter;
//    int txPwr;
    long long txFrequency;
    bool isConnected;
    QString QuickIP;

    bool squelch;
    float squelchValue;
    bool modeFlag; //Signals mode is changed from main menu

    G711A g711a;

    int tuning;
    int infotick;
    int infotick2;
    long dspversion;
    QString dspversiontxt;
    QString lastmessage;
    QString servername;
    bool canTX;
    bool chkTX;
    bool txNow;
    double loffset;
};

#endif	/* _UI_H */
