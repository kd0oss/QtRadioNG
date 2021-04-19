/* Copyright (C)
 * Rick Schnicker KD0OSS 2019
 *
 * Based on the work of:
* 2009 - John Melton, G0ORX/N6LYT
*
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

#include <QDebug>
#include <QSettings>
#include <QPainter>
#include <QtCore>
#include <QTimer>
#include <QThread>
#if QT_VERSION >= 0x050000
#include <QtWidgets/QMessageBox>
#else
#include <QMessageBox>
#endif

#include "UI.h"
#include "About.h"
#include "Audio.h"
#include "Filters.h"
#include "Configure.h"
#include "Band.h"
#include "Mode.h"
#include "XvtrEntry.h"
#include "vfo.h"
#include "Meter.h"
#include "Panadapter.h"
#include "smeter.h"
#include "servers.h"
#include "ctl.h"
#include "powermate.h"
#include "Frequency.h"
#include "EqualizerDialog.h"
#include "calc.h"

#include "hermesframe.h"

UI::UI(const QString server)
{
    widget.setupUi(this);

    initRigCtl();
    fprintf(stderr, "rigctl: Calling init\n");

    servers = 0;
    servername = "Unknown";
    configure.thisuser = "None";
    configure.thispass= "None";
    canTX = true;  // set to false if dspserver says so
    chkTX = false;
    txNow = false;

    loffset = 0;
    viewZoomLevel = 0;
    hardwareType.clear();
    hww = NULL;

    audio = new Audio;
    configure.initAudioDevices(audio);
    audio_sample_rate = 8000;
    audio_channels = 1;
    audioinput = new AudioInput;
    configure.initMicDevices(audioinput);
    mic_buffer_count = 0;
    mic_frame_count = 0;
    connection_valid = false;

 //   spectrumConnection = SpectrumConnection();

    isConnected = false;
    modeFlag = false;
    infotick = 0;
    infotick2 = 0;
    dspversion = 0;
    dspversiontxt = "Unknown";
    meter = -121;

    widget.statusbar->showMessage("QtRadio II branch: KD0OSS 2019");

    // connect up all the menus
    connect(&connection, SIGNAL(isConnected(int)), this, SLOT(connected(int)));
    connect(&connection, SIGNAL(disconnected(QString)), this, SLOT(disconnected(QString)));
    connect(&audioConnection, SIGNAL(audioBuffer(char*,char*)), this, SLOT(audioBuffer(char*,char*)));
    connect(&connection, SIGNAL(printStatusBar(QString)), this, SLOT(printStatusBar(QString)));
    connect(&connection, SIGNAL(slaveSetFreq(long long)), this, SLOT(frequencyChanged(long long)));
    connect(&connection, SIGNAL(slaveSetMode(int)), this, SLOT(slaveSetMode(int)));
    connect(&connection, SIGNAL(setCurrentChannel(int)), this, SLOT(setCurrentChannel(int)));
    connect(&connection, SIGNAL(slaveSetFilter(int,int)), this, SLOT(slaveSetFilter(int,int)));
    connect(&connection, SIGNAL(slaveSetZoom(int)), this, SLOT(slaveSetZoom(int)));
    connect(&connection, SIGNAL(setdspversion(long, QString)), this, SLOT(setdspversion(long, QString)));
    connect(&connection, SIGNAL(setservername(QString)), this, SLOT(setservername(QString)));
    connect(&connection, SIGNAL(setCanTX(bool)), this, SLOT(setCanTX(bool)));
    connect(&connection, SIGNAL(setChkTX(bool)), this, SLOT(setChkTX(bool)));
    connect(&connection, SIGNAL(resetbandedges(double)), this, SLOT(resetbandedges(double)));
    connect(&connection, SIGNAL(setFPS()), this, SLOT(setFPS()));
    connect(&connection, SIGNAL(setSampleRate(int)), this, SLOT(sampleRateChanged(long)));
    connect(&connection, SIGNAL(hardware(QString)), this, SLOT(hardwareSet(QString)));
    connect(&spectrumConnection, SIGNAL(spectrumBuffer(spectrum)), this, SLOT(spectrumBuffer(spectrum)));

    connect(audioinput, SIGNAL(mic_send_audio(QQueue<qint16>*)), this, SLOT(micSendAudio(QQueue<qint16>*)));

    connect(widget.vfoFrame, SIGNAL(getBandFrequency()), this, SLOT(getBandFrequency()));
    connect(widget.actionAbout, SIGNAL(triggered()), this, SLOT(actionAbout()));
    connect(widget.actionConnectToServer, SIGNAL(triggered()), this, SLOT(actionConnect()));
    connect(widget.actionQuick_Server_List, SIGNAL(triggered()), this, SLOT(actionQuick_Server_List()));
    connect(widget.actionDisconnectFromServer, SIGNAL(triggered()), this, SLOT(actionDisconnect()));
    connect(widget.actionBandscope, SIGNAL(triggered()), this, SLOT(actionBandscope()));
    connect(widget.actionRecord, SIGNAL(triggered()), this, SLOT(actionRecord()));
    connect(widget.actionConfig, SIGNAL(triggered()), this, SLOT(actionConfigure()));
    connect(widget.actionEqualizer, SIGNAL(triggered()), this, SLOT(actionEqualizer()));
    connect(widget.actionMuteMainRx, SIGNAL(triggered()), this, SLOT(actionMuteMainRx()));
    connect(widget.ctlFrame, SIGNAL(audioMuted(bool)), this, SLOT(setAudioMuted(bool)));
    connect(widget.ctlFrame, SIGNAL(audioGainChanged()), this, SLOT(audioGainChanged()));
    connect(widget.actionSquelchEnable, SIGNAL(triggered()), this, SLOT(actionSquelch()));
    connect(widget.actionSquelchReset, SIGNAL(triggered()), this, SLOT(actionSquelchReset()));
    connect(widget.actionKeypad, SIGNAL(triggered()), this, SLOT(actionKeypad()));
    connect(widget.vfoFrame, SIGNAL(bandBtnClicked(int)), this, SLOT(getBandBtn(int)));
    connect(widget.action160, SIGNAL(triggered()), this, SLOT(action160()));
    connect(widget.action80, SIGNAL(triggered()), this, SLOT(action80()));
    connect(widget.action60, SIGNAL(triggered()), this, SLOT(action60()));
    connect(widget.action40, SIGNAL(triggered()), this, SLOT(action40()));
    connect(widget.action30, SIGNAL(triggered()), this, SLOT(action30()));
    connect(widget.action20, SIGNAL(triggered()), this, SLOT(action20()));
    connect(widget.action17, SIGNAL(triggered()), this, SLOT(action17()));
    connect(widget.action15, SIGNAL(triggered()), this, SLOT(action15()));
    connect(widget.action12, SIGNAL(triggered()), this, SLOT(action12()));
    connect(widget.action10, SIGNAL(triggered()), this, SLOT(action10()));
    connect(widget.action6, SIGNAL(triggered()), this, SLOT(action6()));
    connect(widget.actionGen, SIGNAL(triggered()), this, SLOT(actionGen()));
    connect(widget.actionWWV, SIGNAL(triggered()), this, SLOT(actionWWV()));
    connect(widget.actionCWL, SIGNAL(triggered()), this, SLOT(actionCWL()));
    connect(widget.actionCWU, SIGNAL(triggered()), this, SLOT(actionCWU()));
    connect(widget.actionLSB, SIGNAL(triggered()), this, SLOT(actionLSB()));
    connect(widget.actionUSB, SIGNAL(triggered()), this, SLOT(actionUSB()));
    connect(widget.actionDSB, SIGNAL(triggered()), this, SLOT(actionDSB()));
    connect(widget.actionAM, SIGNAL(triggered()), this, SLOT(actionAM()));
    connect(widget.actionSAM, SIGNAL(triggered()), this, SLOT(actionSAM()));
    connect(widget.actionFMN, SIGNAL(triggered()), this, SLOT(actionFMN()));
    connect(widget.actionDIGL, SIGNAL(triggered()), this, SLOT(actionDIGL()));
    connect(widget.actionDIGU, SIGNAL(triggered()), this, SLOT(actionDIGU()));
    connect(widget.actionFilter_0, SIGNAL(triggered()), this, SLOT(actionFilter0()));
    connect(widget.actionFilter_1, SIGNAL(triggered()), this, SLOT(actionFilter1()));
    connect(widget.actionFilter_2, SIGNAL(triggered()), this, SLOT(actionFilter2()));
    connect(widget.actionFilter_3, SIGNAL(triggered()), this, SLOT(actionFilter3()));
    connect(widget.actionFilter_4, SIGNAL(triggered()), this, SLOT(actionFilter4()));
    connect(widget.actionFilter_5, SIGNAL(triggered()), this, SLOT(actionFilter5()));
    connect(widget.actionFilter_6, SIGNAL(triggered()), this, SLOT(actionFilter6()));
    connect(widget.actionFilter_7, SIGNAL(triggered()), this, SLOT(actionFilter7()));
    connect(widget.actionFilter_8, SIGNAL(triggered()), this, SLOT(actionFilter8()));
    connect(widget.actionFilter_9, SIGNAL(triggered()), this, SLOT(actionFilter9()));
    connect(widget.actionFilter_10, SIGNAL(triggered()), this, SLOT(actionFilter10()));
    connect(widget.actionANF, SIGNAL(triggered()), this, SLOT(actionANF()));
    connect(widget.actionNR, SIGNAL(triggered()), this, SLOT(actionNR()));
    connect(widget.actionNB, SIGNAL(triggered()), this, SLOT(actionNB()));
    connect(widget.actionSDROM, SIGNAL(triggered()), this, SLOT(actionSDROM()));
    connect(widget.actionFixed, SIGNAL(triggered()), this, SLOT(actionFixed()));
    connect(widget.actionLong, SIGNAL(triggered()), this, SLOT(actionLong()));
    connect(widget.actionSlow, SIGNAL(triggered()), this, SLOT(actionSlow()));
    connect(widget.actionMedium, SIGNAL(triggered()), this, SLOT(actionMedium()));
    connect(widget.actionFast, SIGNAL(triggered()), this, SLOT(actionFast()));
    connect(widget.agcTLevelSlider, SIGNAL(valueChanged(int)), this, SLOT(AGCTLevelChanged(int)));
    connect(widget.rxEqEnableCB, SIGNAL(toggled(bool)), this, SLOT(enableRxEq(bool)));
    connect(widget.txEqEnableCB, SIGNAL(toggled(bool)), this, SLOT(enableTxEq(bool)));
    connect(widget.tnfButton, SIGNAL(clicked(bool)), widget.spectrumView, SLOT(enableNotchFilter(bool)));
    connect(widget.tnfAddButton, SIGNAL(clicked()), this, SLOT(addNotchFilter(void)));
    connect(widget.actionBookmarkThisFrequency, SIGNAL(triggered()), this, SLOT(actionBookmark()));
    connect(widget.actionEditBookmarks, SIGNAL(triggered()), this, SLOT(editBookmarks()));
    // connect up spectrum view
    connect(widget.spectrumView, SIGNAL(variableFilter(int,int)), this, SLOT(variableFilter(int,int)));
    connect(widget.spectrumView, SIGNAL(frequencyMoved(int,int)), this, SLOT(frequencyMoved(int,int)));
    connect(widget.spectrumView, SIGNAL(spectrumHighChanged(int)), this, SLOT(spectrumHighChanged(int)));
    connect(widget.spectrumView, SIGNAL(spectrumLowChanged(int)), this, SLOT(spectrumLowChanged(int)));
    connect(widget.spectrumView, SIGNAL(waterfallHighChanged(int)), this, SLOT(waterfallHighChanged(int)));
    connect(widget.spectrumView, SIGNAL(waterfallLowChanged(int)), this, SLOT(waterfallLowChanged(int)));
    connect(widget.spectrumView, SIGNAL(meterValue(float,float)), this, SLOT(getMeterValue(float,float)));
    connect(widget.spectrumView, SIGNAL(squelchValueChanged(int)), this, SLOT(squelchValueChanged(int)));
    connect(widget.spectrumView, SIGNAL(statusMessage(QString)), this, SLOT(statusMessage(QString)));
    connect(widget.spectrumView, SIGNAL(removeNotchFilter()), this, SLOT(removeNotchFilter()));
    connect(widget.vfoFrame, SIGNAL(frequencyMoved(int,int)), this, SLOT(frequencyMoved(int,int)));
    connect(widget.vfoFrame, SIGNAL(frequencyChanged(long long)), this, SLOT(frequencyChanged(long long)));
    connect(widget.vfoFrame, SIGNAL(vfoStepBtnClicked(int)), this, SLOT(vfoStepBtnClicked(int)));
    connect(widget.ctlFrame, SIGNAL(pttChange(int,bool)), this, SLOT(pttChange(int,bool)));
    connect(widget.vfoFrame, SIGNAL(rightBandClick()), this, SLOT(quickMemStore()));
    connect(widget.ctlFrame, SIGNAL(masterBtnClicked()), this, SLOT(masterButtonClicked()));

    connect(&keypad, SIGNAL(setKeypadFrequency(long long)), this, SLOT(setKeypadFrequency(long long)));

    // connect up band and frequency changes
    connect(&band, SIGNAL(bandChanged(int,int)), this, SLOT(bandChanged(int,int)));
    connect(&band, SIGNAL(printStatusBar(QString)), this, SLOT(printStatusBar(QString)));

    // connect up mode changes
    connect(&mode, SIGNAL(modeChanged(int,int)), this, SLOT(modeChanged(int,int)));

    // connect up filter changes
    connect(&filters, SIGNAL(filtersChanged(FiltersBase*,FiltersBase*)), this, SLOT(filtersChanged(FiltersBase*,FiltersBase*)));
    connect(&filters, SIGNAL(filterChanged(int,int)), this, SLOT(filterChanged(int,int)));

    // connect up configuration changes
    connect(&configure, SIGNAL(spectrumHighChanged(int)), this, SLOT(spectrumHighChanged(int)));
    connect(&configure, SIGNAL(spectrumLowChanged(int)), this, SLOT(spectrumLowChanged(int)));
    connect(&configure, SIGNAL(fpsChanged(int)), this, SLOT(fpsChanged(int)));
    connect(&configure, SIGNAL(avgSpinChanged(int)), widget.spectrumView, SLOT(setAvg(int)));
    connect(&configure, SIGNAL(waterfallHighChanged(int)), this, SLOT(waterfallHighChanged(int)));
    connect(&configure, SIGNAL(waterfallLowChanged(int)), this, SLOT(waterfallLowChanged(int)));
    connect(&configure, SIGNAL(waterfallAutomaticChanged(bool)), this, SLOT(waterfallAutomaticChanged(bool)));
//    connect(&configure, SIGNAL(encodingChanged(int)), this, SLOT(encodingChanged(int)));
//    connect(&configure, SIGNAL(encodingChanged(int)), audio, SLOT(set_audio_encoding(int)));
//    connect(&configure, SIGNAL(micEncodingChanged(int)), audioinput, SLOT(setMicEncoding(int)));
    connect(&configure, SIGNAL(audioDeviceChanged(QAudioDeviceInfo,int,int,QAudioFormat::Endian)), this, SLOT(audioDeviceChanged(QAudioDeviceInfo,int,int,QAudioFormat::Endian)));
    connect(&configure, SIGNAL(micDeviceChanged(QAudioDeviceInfo,int,int,QAudioFormat::Endian)), this, SLOT(micDeviceChanged(QAudioDeviceInfo,int,int,QAudioFormat::Endian)));
    connect(&configure, SIGNAL(hostChanged(QString)), this, SLOT(hostChanged(QString)));
    connect(&configure, SIGNAL(receiverChanged(int)), this, SLOT(receiverChanged(int)));
  //  connect(&configure, SIGNAL(rxDCBlockChanged(bool)), this, SLOT(rxDCBlockChanged(bool)));
 //   connect(&configure, SIGNAL(rxDCBlockGainChanged(int)), this, SLOT(rxDCBlockGainChanged(int)));
 //   connect(&configure, SIGNAL(txDCBlockChanged(bool)), this, SLOT(txDCBlockChanged(bool)));
//    connect(&configure, SIGNAL(txIQPhaseChanged(double)), this, SLOT(setTxIQPhase(double)));
 //   connect(&configure, SIGNAL(txIQGainChanged(double)), this, SLOT(setTxIQGain(double)));
    connect(&configure, SIGNAL(nrValuesChanged(int,int,double,double)), this, SLOT(nrValuesChanged(int,int,double,double)));
    connect(&configure, SIGNAL(anfValuesChanged(int,int,double,double)), this, SLOT(anfValuesChanged(int,int,double,double)));
    connect(&configure, SIGNAL(nbThresholdChanged(double)), this, SLOT(nbThresholdChanged(double)));
    connect(&configure, SIGNAL(sdromThresholdChanged(double)), this, SLOT(sdromThresholdChanged(double)));
    connect(&configure, SIGNAL(windowTypeChanged(int)), this, SLOT(windowTypeChanged(int)));
    connect(&configure, SIGNAL(agcAttackChanged(int)), this, SLOT(agcAttackChanged(int)));
    connect(&configure, SIGNAL(agcMaxGainChanged(int)), this, SLOT(agcMaxGainChanged(int)));
    connect(&configure, SIGNAL(agcSlopeChanged(int)), this, SLOT(agcSlopeChanged(int)));
    connect(&configure, SIGNAL(agcDecayChanged(int)), this, SLOT(agcDecayChanged(int)));
    connect(&configure, SIGNAL(agcHangChanged(int)), this, SLOT(agcHangChanged(int)));
    connect(&configure, SIGNAL(agcFixedGainChanged(int)), this, SLOT(agcFixedGainChanged(int)));
    connect(&configure, SIGNAL(agcHangThreshChanged(int)), this, SLOT(agcHangThreshChanged(int)));
    connect(&configure, SIGNAL(levelerStateChanged(int)), this, SLOT(levelerStateChanged(int)));
    connect(&configure, SIGNAL(levelerMaxGainChanged(int)), this, SLOT(levelerMaxGainChanged(int)));
    connect(&configure, SIGNAL(levelerAttackChanged(int)), this, SLOT(levelerAttackChanged(int)));
    connect(&configure, SIGNAL(levelerDecayChanged(int)), this, SLOT(levelerDecayChanged(int)));
    connect(&configure, SIGNAL(levelerHangChanged(int)), this, SLOT(levelerHangChanged(int)));
    connect(&configure, SIGNAL(alcStateChanged(int)), this, SLOT(alcStateChanged(int)));
    connect(&configure, SIGNAL(alcAttackChanged(int)), this, SLOT(alcAttackChanged(int)));
    connect(&configure, SIGNAL(alcDecayChanged(int)), this, SLOT(alcDecayChanged(int)));
    connect(&configure, SIGNAL(alcHangChanged(int)), this, SLOT(alcHangChanged(int)));
    connect(&configure, SIGNAL(addXVTR(QString,long long,long long,long long,long long,int,int)), this, SLOT(addXVTR(QString,long long,long long,long long,long long,int,int)));
    connect(&configure, SIGNAL(deleteXVTR(int)), this, SLOT(deleteXVTR(int)));
    connect(&configure, SIGNAL(spinBox_cwPitchChanged(int)), this, SLOT(cwPitchChanged(int)));

    connect(&bookmarks, SIGNAL(bookmarkSelected(QAction*)), this, SLOT(selectBookmark(QAction*)));
    connect(&bookmarkDialog, SIGNAL(accepted()), this, SLOT(addBookmark()));

    connect(&xvtr, SIGNAL(xvtrSelected(QAction*)), this, SLOT(selectXVTR(QAction*)));

    connect(this, SIGNAL(process_audio(char*,char*,int)), audio, SLOT(process_audio(char*,char*,int)));
    connect(this, SIGNAL(HideTX(bool)), widget.ctlFrame, SLOT(HideTX(bool)));

    bandscope = NULL;
    fps = 15;
    gain = 100;
    agc = AGC_SLOW;
    cwPitch = configure.getCwPitch();
    squelchValue = -100;
    squelch = false;
    notchFilterIndex = 0;

    audio->get_audio_device(&audio_device);

    widget.spectrumView->connection = &spectrumConnection;
    widget.spectrumView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    widget.spectrumView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    equalizer = new EqualizerDialog(&connection);

    // load any saved settings
    loadSettings();

    fps = configure.getFps();

    configure.updateXvtrList(&xvtr);
    xvtr.buildMenu(widget.menuXVTR);

    widget.spectrumView->setHost(configure.getHost());

    printWindowTitle("Remote disconnected"); //added by gvj

    //Configure statusBar
    widget.statusbar->addPermanentWidget(&modeInfo);

    band.initBand(band.getBand());

    // make spectrum timer
    spectrumTimer = new QTimer(this);
    connect(spectrumTimer, SIGNAL(timeout()), this, SLOT(updateSpectrum()));

    // automatically select a server and connect to it //IW0HDV
    if (server.length())
    {
        qDebug() << "Connecting to " << server;
        emit actionConnectNow(server);
    }
} // end contructor


UI::~UI()
{
    connection.disconnect();
    equalizer->deleteLater();
    saveSettings();
} // end destructor


void UI::keyPressEvent(QKeyEvent *event)
{
    if (event->nativeVirtualKey() == configure.pttKeyId && !event->isAutoRepeat() && !txNow)
    {
        pttChange(0, true);
    }

    QMainWindow::keyPressEvent(event);
} // end keyPressEvent


void UI::keyReleaseEvent(QKeyEvent *event)
{
    if (event->nativeVirtualKey() == configure.pttKeyId && !event->isAutoRepeat() && txNow)
    {
        pttChange(0, false);
        widget.ctlFrame->micBar->setValue(0);
    }

    QMainWindow::keyReleaseEvent(event);
} // end keyReleaseEvent


void UI::actionAbout()
{
    about.setVisible(true);
} // end actionAbout


void UI::loadSettings()
{
    QSettings settings("FreeSDR", "QtRadioII");
    qDebug() << "loadSettings: " << settings.fileName();

    widget.ctlFrame->loadSettings(&settings);
    band.loadSettings(&settings);
    xvtr.loadSettings(&settings);
    configure.loadSettings(&settings);
    configure.updateXvtrList(&xvtr);
    bookmarks.loadSettings(&settings);
    bookmarks.buildMenu(widget.menuView_Bookmarks);

    settings.beginGroup("UI");
    if(settings.contains("gain")) gain=settings.value("gain").toInt();
    emit widget.ctlFrame->audioGainInitalized(gain);
    if(settings.contains("agc")) agc=settings.value("agc").toInt();
    if(settings.contains("squelch")) squelchValue=settings.value("squelch").toInt();
    if(settings.contains("pwsmode")) pwsmode=settings.value("pwsmode").toInt();
    settings.endGroup();

    settings.beginGroup("mainWindow");
    if (configure.getGeometryState())
        restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();

    settings.beginGroup("AudioEqualizer");
    if (settings.contains("eqMode"))
    {
        if (settings.value("eqMode") == 3)
            equalizer->loadSettings3Band();
        else
            equalizer->loadSettings10Band();

        if (settings.value("rxEqEnabled") == 1)
        {
            widget.rxEqEnableCB->setChecked(true);
            enableRxEq(true);
        }

        if (settings.value("txEqEnabled") == 1)
        {
            widget.txEqEnableCB->setChecked(true);
            enableTxEq(true);
        }
    }
    else
    {
        settings.setValue("eqMode", 10);
        equalizer->set10BandEqualizer();
    }
    settings.endGroup();

    widget.vfoFrame->readSettings(&settings);

    switch(agc)
    {
    case AGC_FIXED:
        widget.actionFixed->setChecked(true);
        break;
    case AGC_SLOW:
        widget.actionSlow->setChecked(true);
        break;
    case AGC_MEDIUM:
        widget.actionMedium->setChecked(true);
        break;
    case AGC_FAST:
        widget.actionFast->setChecked(true);
        break;
    case AGC_LONG:
        widget.actionLong->setChecked(true);
        break;
    }

  //  setPwsMode(pwsmode);
} // end loadSettings


void UI::saveSettings()
{
    QSettings settings("FreeSDR","QtRadioII");

    qDebug() << "saveSettings: " << settings.fileName();

    //  settings.clear();
    widget.ctlFrame->saveSettings(&settings);
    configure.saveSettings(&settings);
    band.saveSettings(&settings);
    xvtr.saveSettings(&settings);
    bookmarks.saveSettings(&settings);

    settings.beginGroup("UI");
    settings.setValue("gain", gain);
    settings.setValue("agc", agc);
    settings.setValue("squelch", squelchValue);
    settings.endGroup();

    settings.beginGroup("mainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.endGroup();

    settings.beginGroup("AudioEqualizer");
    settings.setValue("rxEqEnabled", widget.rxEqEnableCB->isChecked());
    settings.setValue("txEqEnabled", widget.txEqEnableCB->isChecked());
    settings.endGroup();

    widget.vfoFrame->writeSettings(&settings);
} // end saveSettings


void UI::hostChanged(QString host)
{
    widget.spectrumView->setHost(host);
    printWindowTitle("Remote disconnected");
} // end hostChanged


void UI::receiverChanged(int rx)
{
    widget.spectrumView->setReceiver(rx);
    printWindowTitle("Remote disconnected");
} // end receiverChanged


void UI::closeServers()
{
    if (servers)
    {
        delete servers;
        servers = 0;
    }
} // end closeServers


void UI::closeEvent(QCloseEvent* event)
{
    Q_UNUSED(event);
    saveSettings();
    if (servers)
    {
        servers->close();   // synchronous call, triggers a closeServer signal (see above)
                            // no needs to delete the object pointed by "servers"
    }
} // end closeEvent


void UI::actionConfigure()
{
    configure.show();
} // end actionConfigure


void UI::actionEqualizer()
{
    equalizer->show();
} // end actionEqualizer


void UI::spectrumHighChanged(int high)
{
    //qDebug() << __FUNCTION__ << ": " << high;

    widget.spectrumView->setHigh(high);
    configure.setSpectrumHigh(high);
    band.setSpectrumHigh(high);
} // end spectrumHighChanged


void UI::spectrumLowChanged(int low)
{
    //qDebug() << __FUNCTION__ << ": " << low;

    widget.spectrumView->setLow(low);
    configure.setSpectrumLow(low);
    band.setSpectrumLow(low);
} // end spectrumLowChanged


void UI::fpsChanged(int f)
{
    //qDebug() << "fpsChanged:" << f;
    fps=f;
} // end fpsChanged


void UI::setFPS(void)
{
    QByteArray command;
    command.clear();
    command.append((char)SETFPS);
    command.append(QString("2000,%1").arg(fps));
    connection.sendCommand(command);
    spectrumConnection.sendCommand(command);
} // end setFPS


void UI::resizeEvent(QResizeEvent *)
{
  //  setFPS();
} // end resizeEvent


void UI::waterfallHighChanged(int high)
{
    //qDebug() << __LINE__ << __FUNCTION__ << ": " << high;

    widget.spectrumView->panadapterScene->waterfallItem->setHigh(high);
    configure.setWaterfallHigh(high);
    band.setWaterfallHigh(high);
} // end waterfallHighChanged


void UI::waterfallLowChanged(int low)
{
    //qDebug() << __FUNCTION__ << ": " << low;

    widget.spectrumView->panadapterScene->waterfallItem->setLow(low);
    configure.setWaterfallLow(low);
    band.setWaterfallLow(low);
} // end waterfallLowChanged


void UI::waterfallAutomaticChanged(bool state)
{
    widget.spectrumView->panadapterScene->waterfallItem->setAutomatic(state);
} // end waterfallautomaticChanged


void UI::audioDeviceChanged(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian byteOrder)
{
    audio_device = info;
    audio_sample_rate = rate;
    audio_channels = channels;
    audio_byte_order = byteOrder;
    audio->select_audio(info, rate, channels, byteOrder);
} // end audioDeviceChanged


void UI::micDeviceChanged(QAudioDeviceInfo info,int rate,int channels,QAudioFormat::Endian byteOrder)
{
    audioinput->select_audio(info, rate, channels, byteOrder);
} // end micDeviceChanged


void UI::actionConnect()
{
    connection.connect(configure.getHost(), DSPSERVER_BASE_PORT+configure.getReceiver());
    widget.spectrumView->setReceiver(configure.getReceiver());
    isConnected = true;
} // end actionConnect


void UI::actionDisconnectNow()
{
    if (isConnected == false)
    {
        QMessageBox msgBox;
        msgBox.setText("Not connected to a server!");
        msgBox.exec();
    }
    else
    {
        actionDisconnect();
    }
} // end actionDesconnectNow


void UI::actionDisconnect()
{

    qDebug() << "actionDisconnect() QuickIP=" << QuickIP;
    if (QuickIP.length() > 6)
    {    // Remove from saved host list or IPs will pile up forever. If empty string we did not connect via Quick Connect
        configure.removeHost(QuickIP);
        qDebug() << "actionDisconnect() removeHost(" << QuickIP <<")";
    }
    QuickIP = "";
    spectrumTimer->stop();
    widget.zoomSpectrumSlider->setValue(0);

    connection.disconnect();
    widget.actionConnectToServer->setDisabled(false);
    widget.actionDisconnectFromServer->setDisabled(true);

    configure.connected(false);
    isConnected = false;
} // end actionDisconnect


void UI::actionQuick_Server_List()
{
    servers = new Servers();
    QObject::connect(servers, SIGNAL(disconnectNow()), this, SLOT(actionDisconnectNow()));
    QObject::connect(servers, SIGNAL(connectNow(QString)), this, SLOT(actionConnectNow(QString)));
    QObject::connect(servers, SIGNAL(dialogClosed()), this, SLOT(closeServers()));
    servers->show();
    servers->refreshList();
} // end actionQuick_Server_List


void UI::connected(int channel)
{
    QByteArray command;

    qDebug() << "UI::connected";
    isConnected = true;
    configure.connected(true);
    currentChannel = channel;

    // let them know who we are
    command.clear();
    command.append((char)SETCLIENT);
    command.append("QtRadio");
    connection.sendCommand(command);
    command.clear();

    command.append((char)STARTRECEIVER);
    command.append((char)channel);
    connection.sendCommand(command);
    command.clear();
/*
    command.append((char)STARTTRANSMITTER);
    command.append((char)(63-0)); // start tx at max channel
    connection.sendCommand(command);
    */
    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)ATTACH);
    connection.sendCommand(command);

    // send initial settings
    sampleRateChanged(connection.sample_rate);

    spectrumConnection.connect(configure.getHost(), DSPSERVER_BASE_PORT+1);
    audioConnection.connect(configure.getHost(), DSPSERVER_BASE_PORT+10);
    micAudioConnection.connect(configure.getHost(), DSPSERVER_BASE_PORT+20);

    frequency = band.getFrequency();

    command.clear();
    command.append((char)SETFREQ);
    command.append((char)channel);
    command.append(QString("%1").arg(frequency));
    connection.sendCommand(command);
    widget.spectrumView->setFrequency(frequency);

    //    gvj code
    widget.vfoFrame->setFrequency(frequency);

    command.clear();
    command.append((char)SETMODE);
    command.append((char)channel);
    command.append((char)band.getMode());
    connection.sendCommand(command);

    int low,high;
    if (mode.getMode() == MODE_CWL)
    {
        low = -cwPitch-filters.getLow();
        high = -cwPitch+filters.getHigh();
    }
    else
        if (mode.getMode() == MODE_CWU)
        {
            low = cwPitch-filters.getLow();
            high = cwPitch+filters.getHigh();
        }
        else
        {
            low = filters.getLow();
            high = filters.getHigh();
        }
    command.clear();
    command.append((char)SETFILTER);
    command.append((char)channel);
    command.append(QString("%1,%2").arg(low).arg(high));
    connection.sendCommand(command);

    widget.spectrumView->setFilter(low,high);

    widget.actionConnectToServer->setDisabled(true);
    widget.actionDisconnectFromServer->setDisabled(false);

    // select audio encoding
    command.clear();
    command.append((char)SETENCODING);
    command.append(audio->get_audio_encoding());
    connection.sendCommand(command);

    audio->select_audio(audio_device, audio_sample_rate, audio_channels, audio_byte_order);

    // start the audio
    audio_buffers=0;
    actionGain(gain);

    if (!getenv("QT_RADIO_NO_LOCAL_AUDIO"))
    {
        command.clear();
        command.append((char)STARTAUDIO);
        command.append(QString("%1,%2,%3,%4").arg(AUDIO_BUFFER_SIZE*(audio_sample_rate/8000)).arg(audio_sample_rate).arg(audio_channels).arg(audioinput->getMicEncoding()));
  //      connection.sendCommand(command);
    }

    command.clear();
    command.append((char)SETPAN);
    command.append(QString("%1").arg(0.5));
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETAGC);
    command.append(agc);
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETANFVALS);
    command.append(QString("%1,%2,%3,%4").arg(configure.getAnfTaps()).arg(configure.getAnfDelay()).arg(configure.getAnfGain()).arg(configure.getAnfLeak()));
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETNRVALS);
    command.append(QString("%1,%2,%3,%4").arg(configure.getNrTaps()).arg(configure.getNrDelay()).arg(configure.getNrGain()).arg(configure.getNrLeak()));
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETNBVAL);
    command.append(QString("%1").arg(configure.getNbThreshold()));
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETSQUELCHVAL);
    command.append(QString("%1").arg(squelchValue));
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETSQUELCHSTATE);
    command.append((int)widget.actionSquelchEnable->isChecked());
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETANF);
    command.append((int)widget.actionANF->isChecked());
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETNR);
    command.append((int)widget.actionNR->isChecked());
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETNB);
    command.append((int)widget.actionNB->isChecked());
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETRXBPASSWIN);
    command.append(QString("%1").arg(configure.getRxFilterWindow()));
    connection.sendCommand(command);

    command.clear();
    command.append((char)SETTXBPASSWIN);
    command.append(QString("%1").arg(configure.getTxFilterWindow()));
    connection.sendCommand(command);

    windowTypeChanged(configure.getWindowType());

    printWindowTitle("Remote connected");
/*
    qDebug("Sending advanced setup commands.");

    command.clear();
    QTextStream(&command) << "setrxagcmaxgain " << configure.getRxAGCMaxGainValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "setrxagcattack " << configure.getRxAGCAttackValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "setrxagcdecay " << configure.getRxAGCDecayValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "setrxagchang " << configure.getRxAGCHangValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "setfixedagc " << configure.getRxAGCFixedGainValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "setrxagchangthreshold " << configure.getRxAGCHangThreshValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "settxlevelerstate " << configure.getLevelerEnabledValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "settxlevelermaxgain " << configure.getLevelerMaxGainValue();
    connection.sendCommand(command);
    //        command.clear(); QTextStream(&command) << "settxlevelerattack " << configure.getLevelerAttackValue();
    //        connection.sendCommand(command);
    command.clear();
    QTextStream(&command) << "settxlevelerdecay " << configure.getLevelerDecayValue();
    connection.sendCommand(command);

    command.clear();
    QTextStream(&command) << "settxalcdecay " << configure.getALCDecayValue();
    connection.sendCommand(command);
*/
    //
    // hardware special command
    // queries hardware name from remote server
    //
//    command.clear();
//    command.append((char)STARCOMMAND);
//    command.append((char)STARHARDWARE);
//    connection.sendCommand(command);
    /*
    // start the spectrum
    //qDebug() << "starting spectrum timer";
    connection.SemSpectrum.release();
    */
    command.clear();
    command.append((char)STARTIQ);
    connection.sendCommand(command);

    command.clear();
    command.append((char)QUESTION);
    command.append((char)QINFO);
    connection.sendCommand(command);

 //   spectrumTimer->start(1000/fps);
    connection_valid = true;
    if ((mode.getStringMode() == "CWU") || (mode.getStringMode() == "CWL"))
        frequencyChanged(frequency); //gvj dummy call to set Rx offset for cw

    widget.spectrumView->enableNotchFilter(false);

    widget.zoomSpectrumSlider->setValue(1);
    on_zoomSpectrumSlider_sliderMoved(1);
    widget.spectrumView->panadapterScene->waterfallItem->bConnected = true;
} // end connected


void UI::disconnected(QString message)
{
    qDebug() << "UI::disconnected: " << message;
    connection_valid = false;
    isConnected = false;
    spectrumTimer->stop();
    configure.thisuser = "none";
    configure.thispass ="none";
    servername = "";
    canTX = true;
    chkTX = false;
    loffset = 0;

    spectrumConnection.disconnect();
    audioConnection.disconnect();
    micAudioConnection.disconnect();

    audio->clear_decoded_buffer();

    //    widget.statusbar->showMessage(message,0); //gvj deleted code
    printWindowTitle(message);
    widget.actionConnectToServer->setDisabled(false);
    widget.actionDisconnectFromServer->setDisabled(true);

    if (hardwareType != "" && hww != NULL)
    {
        widget.RadioScrollAreaWidgetContents->layout()->removeWidget(hww);
        hww->deleteLater();
        hardwareType.clear();
    }
    configure.connected(false);
    widget.spectrumView->panadapterScene->waterfallItem->bConnected = false;
} // end disconnected


void UI::updateSpectrum()
{
    QByteArray command;
//    command.append((char)GETSPECTRUM);
//    command.append(QString("%1").arg(widget.spectrumView->width()));
//    connection.sendCommand(command);

    if (infotick > 25)
    {
        command.clear();
        command.append((char)QUESTION);
        command.append((char)QMASTER);
        connection.sendCommand(command);
        command.clear();
        if (connection.getSlave() == true)
        {
            command.append((char)QUESTION);
            command.append((char)QINFO);  // get primary freq changes
 //           connection.sendCommand(command);
        }
        infotick = 0;
    }

    if (infotick2 == 0)
    { // set to 0 wehen we first connect
        if (chkTX)
        {
            command.clear();
            command.append((char)QUESTION);
            command.append((char)QCANTX);
            command.append(QString("%1").arg(configure.thisuser)); // can we tx here?
 //           connection.sendCommand(command);
        }
    }

    if (infotick2 > 50)
    {
        if (chkTX)
        {
            command.clear();
            command.append((char)QUESTION);
            command.append((char)QCANTX);
            command.append(QString("%1").arg(configure.thisuser)); // can we tx here?
  //          connection.sendCommand(command);
        }
        infotick2 = 0;
    }
    infotick++;
    infotick2++;
} // end updateSpectrum


void UI::spectrumBuffer(spectrum spec)
{
  // qDebug()<<Q_FUNC_INFO << "spectrumBuffer";

    sampleRate = spec.sample_rate;
    widget.spectrumView->updateSpectrumFrame(spec);
    spectrumConnection.freeBuffers(spec);
} // end spectrumBuffer


void UI::audioBuffer(char* header, char* buffer)
{
    //qDebug() << "audioBuffer";
    int length;

    // g0orx binary header
    length = ((header[3] & 0xFF) << 8) + (header[4] & 0xFF);
    emit process_audio(header, buffer, length);
} // end audioBuffer


void UI::micSendAudio(QQueue<qint16>* queue)
{
    if (!txNow)
    {
        queue->clear();
        return;
    }

    if (audioinput->getMicEncoding() == 0)
    {      // aLaw
        while (!queue->isEmpty())
        {
            qint16 sample=queue->dequeue();
            if (tuning) sample=0;
            unsigned char e=g711a.encode(sample);
            mic_encoded_buffer[mic_buffer_count++] = e;
            if (mic_buffer_count >= 512) //MIC_ALAW_BUFFER_SIZE)
            {
                //  we are going to really send samples only if
                //  the connection is valid
                //  the checkbox in GUI is checked
                //  the server side has Tx capability
                if (connection_valid && configure.getTxAllowed() && (canTX == true))
                {
                    micAudioConnection.sendAudio(512, mic_encoded_buffer);
                }
                mic_buffer_count=0;
            }
        }
    }
    else
    {
        qDebug() << "Error - UI: MicEncoding is not 0 nor 1 but " << audioinput->getMicEncoding();
    }
} // end micSendAudio


void UI::actionKeypad()
{
    keypad.clear();
    keypad.show();
} // end actionKeypad


void UI::setKeypadFrequency(long long f)
{
    frequencyChanged(f);
} // end setKeypadFrequency


void UI::getBandBtn(int btn)
{
    band.selectBand(btn+100);// +100 is used as a flag to indicate call came from vfo band buttons
} // end getBandBtn


void UI::quickMemStore()
{
    band.quickMemStore();
} // end quickMemStore


void UI::action160()
{
    band.selectBand(BAND_160);
}

void UI::action80()
{
    band.selectBand(BAND_80);
}

void UI::action60()
{
    band.selectBand(BAND_60);
}

void UI::action40()
{
    band.selectBand(BAND_40);
}

void UI::action30()
{
    band.selectBand(BAND_30);
}

void UI::action20()
{
    band.selectBand(BAND_20);
}

void UI::action17()
{
    band.selectBand(BAND_17);
}

void UI::action15()
{
    band.selectBand(BAND_15);
}

void UI::action12()
{
    band.selectBand(BAND_12);
}

void UI::action10()
{
    band.selectBand(BAND_10);
}

void UI::action6()
{
    band.selectBand(BAND_6);
}

void UI::actionGen()
{
    band.selectBand(BAND_GEN);
}

void UI::actionWWV()
{
    band.selectBand(BAND_WWV);
}

void UI::bandChanged(int previousBand,int newBand)
{
    qDebug()<<Q_FUNC_INFO<<":   previousBand, newBand = " << previousBand << "," << newBand;
    qDebug()<<Q_FUNC_INFO<<":   band.getFilter = "<<band.getFilter();

    // uncheck previous band
    switch (previousBand)
    {
    case BAND_160:
        widget.action160->setChecked(false);
        break;
    case BAND_80:
        widget.action80->setChecked(false);
        break;
    case BAND_60:
        widget.action60->setChecked(false);
        break;
    case BAND_40:
        widget.action40->setChecked(false);
        break;
    case BAND_30:
        widget.action30->setChecked(false);
        break;
    case BAND_20:
        widget.action20->setChecked(false);
        break;
    case BAND_17:
        widget.action17->setChecked(false);
        break;
    case BAND_15:
        widget.action15->setChecked(false);
        break;
    case BAND_12:
        widget.action12->setChecked(false);
        break;
    case BAND_10:
        widget.action10->setChecked(false);
        break;
    case BAND_6:
        widget.action6->setChecked(false);
        break;
    case BAND_GEN:
        widget.actionGen->setChecked(false);
        break;
    case BAND_WWV:
        widget.actionWWV->setChecked(false);
        break;
    }

    // check new band
    switch (newBand)
    {
    case BAND_160:
        widget.action160->setChecked(true);
        break;
    case BAND_80:
        widget.action80->setChecked(true);
        break;
    case BAND_60:
        widget.action60->setChecked(true);
        break;
    case BAND_40:
        widget.action40->setChecked(true);
        break;
    case BAND_30:
        widget.action30->setChecked(true);
        break;
    case BAND_20:
        widget.action20->setChecked(true);
        break;
    case BAND_17:
        widget.action17->setChecked(true);
        break;
    case BAND_15:
        widget.action15->setChecked(true);
        break;
    case BAND_12:
        widget.action12->setChecked(true);
        break;
    case BAND_10:
        widget.action10->setChecked(true);
        break;
    case BAND_6:
        widget.action6->setChecked(true);
        break;
    case BAND_GEN:
        widget.actionGen->setChecked(true);
        break;
    case BAND_WWV:
        widget.actionWWV->setChecked(true);
        break;
    }
    //Now select the correct band button in VFO
    widget.vfoFrame->checkBandBtn(newBand);

    // get the band setting
    mode.setMode(band.getMode());

    qDebug()<<Q_FUNC_INFO<<":   The value of band.getFilter is ... "<<band.getFilter();
    qDebug()<<Q_FUNC_INFO<<":   The value of filters.getFilter is  "<<filters.getFilter();


    widget.spectrumView->setBand(band.getStringBand());

    if (band.getFilter() != filters.getFilter())
    {
        emit filterChanged(filters.getFilter(), band.getFilter());
    }
    frequency=band.getFrequency();
    int samplerate = widget.spectrumView->samplerate();

    QByteArray command;
    command.clear();
    command.append((char)SETFREQ);
    command.append((char)currentChannel);
    command.append(QString("%1").arg(frequency));
    connection.sendCommand(command);

    widget.spectrumView->setFrequency(frequency);

    //    gvj code
    widget.vfoFrame->setFrequency(frequency);
    qDebug() << __FUNCTION__ << ": frequency, newBand = " << frequency << ", " << newBand;
    widget.spectrumView->setHigh(band.getSpectrumHigh());
    widget.spectrumView->setLow(band.getSpectrumLow());
    //    widget.waterfallView->setFrequency(frequency);
    widget.spectrumView->panadapterScene->waterfallItem->setHigh(band.getWaterfallHigh());
    widget.spectrumView->panadapterScene->waterfallItem->setLow(band.getWaterfallLow());


    BandLimit limits=band.getBandLimits(band.getFrequency()-(samplerate/2),band.getFrequency()+(samplerate/2));
    widget.spectrumView->setBandLimits(limits.min() + loffset,limits.max()+loffset);
    if ((mode.getStringMode() == "CWU") || (mode.getStringMode() == "CWL"))
        frequencyChanged(frequency); //gvj dummy call to set Rx offset for cw
} // end bandChanged


void UI::modeChanged(int previousMode,int newMode)
{
    QByteArray command;

    qDebug()<<Q_FUNC_INFO<< ":   previousMode, newMode" << previousMode << "," << newMode;
    qDebug()<<Q_FUNC_INFO<< ":   band.getFilter = " << band.getFilter();

    // uncheck previous mode
    switch (previousMode)
    {
    case MODE_CWL:
        widget.actionCWL->setChecked(false);
        break;
    case MODE_CWU:
        widget.actionCWU->setChecked(false);
        break;
    case MODE_LSB:
        widget.actionLSB->setChecked(false);
        break;
    case MODE_USB:
        widget.actionUSB->setChecked(false);
        break;
    case MODE_DSB:
        widget.actionDSB->setChecked(false);
        break;
    case MODE_AM:
        widget.actionAM->setChecked(false);
        break;
    case MODE_SAM:
        widget.actionSAM->setChecked(false);
        break;
    case MODE_FM:
        widget.actionFMN->setChecked(false);
        break;
    case MODE_DIGL:
        widget.actionDIGL->setChecked(false);
        break;
    case MODE_DIGU:
        widget.actionDIGU->setChecked(false);
        break;
    }
    qDebug()<<Q_FUNC_INFO<<":  999: value of band.getFilter before filters.selectFilters has been called = "<<band.getFilter();
    // check the new mode and set the filters
    switch (newMode)
    {
    case MODE_CWL:
        widget.actionCWL->setChecked(true);
        filters.selectFilters(&cwlFilters);
        break;
    case MODE_CWU:
        widget.actionCWU->setChecked(true);
        filters.selectFilters(&cwuFilters);
        break;
    case MODE_LSB:
        widget.actionLSB->setChecked(true);
        filters.selectFilters(&lsbFilters);
        break;
    case MODE_USB:
        widget.actionUSB->setChecked(true);
        filters.selectFilters(&usbFilters);
        break;
    case MODE_DSB:
        widget.actionDSB->setChecked(true);
        filters.selectFilters(&dsbFilters);
        break;
    case MODE_AM:
        widget.actionAM->setChecked(true);
        filters.selectFilters(&amFilters);
        break;
    case MODE_SAM:
        widget.actionSAM->setChecked(true);
        filters.selectFilters(&samFilters);
        break;
    case MODE_FM:
        widget.actionFMN->setChecked(true);
        filters.selectFilters(&fmnFilters);
        break;
    case MODE_DIGL:
        widget.actionDIGL->setChecked(true);
        filters.selectFilters(&diglFilters);
        break;
    case MODE_DIGU:
        widget.actionDIGU->setChecked(true);
        filters.selectFilters(&diguFilters);
        break;
    }
    qDebug()<<Q_FUNC_INFO<<":  1043: value of band.getFilter after filters.selectFilters has been called = "<<band.getFilter();
    widget.spectrumView->setMode(mode.getStringMode());
    //    widget.waterfallView->setMode(mode.getStringMode());
    command.clear();
    command.append((char)SETMODE);
    command.append((char)currentChannel);
    command.append((char)newMode);
    connection.sendCommand(command);
} // end modeChanged


void UI::filtersChanged(FiltersBase* previousFilters, FiltersBase* newFilters)
{
    qDebug()<<Q_FUNC_INFO<<":   newFilters->getText, newFilters->getSelected = " << newFilters->getText()<<", "<<newFilters->getSelected();
    qDebug()<<Q_FUNC_INFO<<":   band.getFilter = " <<band.getFilter();

    // uncheck old filter
    if (previousFilters!=NULL)
    {
        switch (previousFilters->getSelected())
        {
        case 0:
            widget.actionFilter_0->setChecked(false);
            break;
        case 1:
            widget.actionFilter_1->setChecked(false);
            break;
        case 2:
            widget.actionFilter_2->setChecked(false);
            break;
        case 3:
            widget.actionFilter_3->setChecked(false);
            break;
        case 4:
            widget.actionFilter_4->setChecked(false);
            break;
        case 5:
            widget.actionFilter_5->setChecked(false);
            break;
        case 6:
            widget.actionFilter_6->setChecked(false);
            break;
        case 7:
            widget.actionFilter_7->setChecked(false);
            break;
        case 8:
            widget.actionFilter_8->setChecked(false);
            break;
        case 9:
            widget.actionFilter_9->setChecked(false);
            break;
        case 10:
            widget.actionFilter_10->setChecked(false);
            break;
        }
    }

    qDebug()<<Q_FUNC_INFO<<":   1092 band.getFilter = "<<band.getFilter()<<", modeFlag = "<<modeFlag;

    if (!modeFlag)
    {
        newFilters->selectFilter(band.getFilter()); //TODO Still not there yet
        qDebug()<<Q_FUNC_INFO<<":    Using the value from band.getFilter = "<<band.getFilter();
    }

    // set the filter menu text
    widget.actionFilter_0->setText(newFilters->getText(0));
    widget.actionFilter_1->setText(newFilters->getText(1));
    widget.actionFilter_2->setText(newFilters->getText(2));
    widget.actionFilter_3->setText(newFilters->getText(3));
    widget.actionFilter_4->setText(newFilters->getText(4));
    widget.actionFilter_5->setText(newFilters->getText(5));
    widget.actionFilter_6->setText(newFilters->getText(6));
    widget.actionFilter_7->setText(newFilters->getText(7));
    widget.actionFilter_8->setText(newFilters->getText(8));
    widget.actionFilter_9->setText(newFilters->getText(9));
    widget.actionFilter_10->setText(newFilters->getText(10));

    // check new filter
    if (newFilters!=NULL)
    {
        switch (newFilters->getSelected())
        {
        case 0:
            widget.actionFilter_0->setChecked(true);
            break;
        case 1:
            widget.actionFilter_1->setChecked(true);
            break;
        case 2:
            widget.actionFilter_2->setChecked(true);
            break;
        case 3:
            widget.actionFilter_3->setChecked(true);
            break;
        case 4:
            widget.actionFilter_4->setChecked(true);
            break;
        case 5:
            widget.actionFilter_5->setChecked(true);
            break;
        case 6:
            widget.actionFilter_6->setChecked(true);
            break;
        case 7:
            widget.actionFilter_7->setChecked(true);
            break;
        case 8:
            widget.actionFilter_8->setChecked(true);
            break;
        case 9:
            widget.actionFilter_9->setChecked(true);
            break;
        case 10:
            widget.actionFilter_10->setChecked(true);
            break;
        }
    }

    filters.selectFilter(filters.getFilter());
    widget.spectrumView->setFilter(filters.getText());
    printStatusBar(" .. Initial frequency. ");    //added by gvj
} // end filtersChanged


void UI::actionCWL()
{
    modeFlag = true; //Signals menu selection of mode so we use the default filter
    mode.setMode(MODE_CWL);
    filters.selectFilters(&cwlFilters);
    band.setMode(MODE_CWL);
    frequencyChanged(frequency);  //force a recalculation of frequency offset for CW receive
    modeFlag = false;
} // end actionCWL


void UI::actionCWU()
{
    modeFlag = true;
    mode.setMode(MODE_CWU);
    filters.selectFilters(&cwuFilters);
    band.setMode(MODE_CWU);
    frequencyChanged(frequency);
    modeFlag = false;
} // end actionCWU


void UI::actionLSB()
{
    modeFlag = true;
    mode.setMode(MODE_LSB);
    filters.selectFilters(&lsbFilters);
    band.setMode(MODE_LSB);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionUSB()
{
    modeFlag = true;
    mode.setMode(MODE_USB);
    filters.selectFilters(&usbFilters);
    band.setMode(MODE_USB);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionDSB()
{
    modeFlag = true;
    mode.setMode(MODE_DSB);
    filters.selectFilters(&dsbFilters);
    band.setMode(MODE_DSB);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionAM()
{
    modeFlag=true;
    mode.setMode(MODE_AM);
    filters.selectFilters(&amFilters);
    band.setMode(MODE_AM);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionSAM()
{
    modeFlag = true;
    mode.setMode(MODE_SAM);
    filters.selectFilters(&samFilters);
    band.setMode(MODE_SAM);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionFMN()
{
    modeFlag = true;
    mode.setMode(MODE_FM);
    filters.selectFilters(&fmnFilters);
    band.setMode(MODE_FM);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionDIGL()
{
    modeFlag = true;
    mode.setMode(MODE_DIGL);
    filters.selectFilters(&diglFilters);
    band.setMode(MODE_DIGL);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionDIGU()
{
    modeFlag = true;
    mode.setMode(MODE_DIGU);
    filters.selectFilters(&diguFilters);
    band.setMode(MODE_DIGU);
    frequencyChanged(frequency);
    modeFlag = false;
}


void UI::actionFilter0()
{
    filters.selectFilter(0);
}


void UI::actionFilter1()
{
    filters.selectFilter(1);
}


void UI::actionFilter2()
{
    filters.selectFilter(2);
}


void UI::actionFilter3()
{
    filters.selectFilter(3);
}


void UI::actionFilter4()
{
    filters.selectFilter(4);
}


void UI::actionFilter5()
{
    filters.selectFilter(5);
}


void UI::actionFilter6()
{
    filters.selectFilter(6);
}


void UI::actionFilter7()
{
    filters.selectFilter(7);
}


void UI::actionFilter8()
{
    filters.selectFilter(8);
}


void UI::actionFilter9()
{
    filters.selectFilter(9);
}


void UI::actionFilter10()
{
    filters.selectFilter(10);
}


void UI::filterChanged(int previousFilter,int newFilter)
{
    QByteArray command;

    qDebug()<<Q_FUNC_INFO<< ":   1252 previousFilter, newFilter" << previousFilter << ":" << newFilter;

    int low, high;
    switch (previousFilter)
    {
    case 0:
        widget.actionFilter_0->setChecked(false);
        break;
    case 1:
        widget.actionFilter_1->setChecked(false);
        break;
    case 2:
        widget.actionFilter_2->setChecked(false);
        break;
    case 3:
        widget.actionFilter_3->setChecked(false);
        break;
    case 4:
        widget.actionFilter_4->setChecked(false);
        break;
    case 5:
        widget.actionFilter_5->setChecked(false);
        break;
    case 6:
        widget.actionFilter_6->setChecked(false);
        break;
    case 7:
        widget.actionFilter_7->setChecked(false);
        break;
    case 8:
        widget.actionFilter_8->setChecked(false);
        break;
    case 9:
        widget.actionFilter_9->setChecked(false);
        break;
    case 10:
        widget.actionFilter_10->setChecked(false);
        break;
    }

    switch (newFilter)
    {
    case 0:
        widget.actionFilter_0->setChecked(true);
        break;
    case 1:
        widget.actionFilter_1->setChecked(true);
        break;
    case 2:
        widget.actionFilter_2->setChecked(true);
        break;
    case 3:
        widget.actionFilter_3->setChecked(true);
        break;
    case 4:
        widget.actionFilter_4->setChecked(true);
        break;
    case 5:
        widget.actionFilter_5->setChecked(true);
        break;
    case 6:
        widget.actionFilter_6->setChecked(true);
        break;
    case 7:
        widget.actionFilter_7->setChecked(true);
        break;
    case 8:
        widget.actionFilter_8->setChecked(true);
        break;
    case 9:
        widget.actionFilter_9->setChecked(true);
        break;
    case 10:
        widget.actionFilter_10->setChecked(true);
        break;
    }

    if (previousFilter != 10 && newFilter == 10)
        return;

    if (mode.getMode() == MODE_CWL)
    {
        low = -cwPitch - filters.getLow();
        high = -cwPitch + filters.getHigh();
    }
    else
        if (mode.getMode() == MODE_CWU)
        {
            low = cwPitch - filters.getLow();
            high = cwPitch + filters.getHigh();
        }
        else
        {
            low = filters.getLow();
            high = filters.getHigh();
        }

    command.clear();
    command.append((char)SETFILTER);
    command.append((char)currentChannel);
    command.append(QString("%1,%2").arg(low).arg(high));
    connection.sendCommand(command);
    widget.spectrumView->setFilter(low, high);
    widget.spectrumView->setFilter(filters.getText());
    //    widget.waterfallView->setFilter(low,high);
    band.setFilter(newFilter);
} // end filterChanged


void UI::variableFilter(int low, int high)
{
    QByteArray command;

    switch (filters.getFilter())
    {
    case 0:
        widget.actionFilter_0->setChecked(false);
        break;
    case 1:
        widget.actionFilter_1->setChecked(false);
        break;
    case 2:
        widget.actionFilter_2->setChecked(false);
        break;
    case 3:
        widget.actionFilter_3->setChecked(false);
        break;
    case 4:
        widget.actionFilter_4->setChecked(false);
        break;
    case 5:
        widget.actionFilter_5->setChecked(false);
        break;
    case 6:
        widget.actionFilter_6->setChecked(false);
        break;
    case 7:
        widget.actionFilter_7->setChecked(false);
        break;
    case 8:
        widget.actionFilter_8->setChecked(false);
        break;
    case 9:
        widget.actionFilter_9->setChecked(false);
        break;
    }

    widget.actionFilter_10->setChecked(true);

    command.clear();
    command.append((char)SETFILTER);
    command.append((char)currentChannel);
    command.append(QString("%1,%2").arg(low).arg(high));
    connection.sendCommand(command);
    if (filters.getFilter() != 10)
    {
        band.setFilter(10);
        filters.selectFilter(10);
    }
} // end variableFilter


void UI::frequencyChanged(long long f)
{
    QByteArray command;
    long long freqOffset = f; //Normally no offset (only for CW Rx mode)

    frequency = f;
    if ((mode.getStringMode() == "CWU") && (!widget.vfoFrame->getPtt()))
    {
        freqOffset -= cwPitch;
    }
    if ((mode.getStringMode() == "CWL") && (!widget.vfoFrame->getPtt()))
    {
        freqOffset += cwPitch;
    }
    //Send command to server
    command.clear();
    command.append((char)SETFREQ);
    command.append((char)currentChannel);
    command.append(QString("%1").arg(freqOffset));
    connection.sendCommand(command);
    //Adjust all frequency displays & Check for exiting current band
    band.setFrequency(frequency);
    widget.spectrumView->setFrequency(frequency);
    widget.vfoFrame->setFrequency(frequency);
    //    widget.waterfallView->setFrequency(frequency);
} // end frequencyChanged


void UI::frequencyMoved(int increment, int step)
{
    qDebug() << __FUNCTION__ << ": increment=" << increment << " step=" << step;

    frequencyChanged(band.getFrequency() - (long long)(increment * step));
} // end frequencyMoved


void UI::sampleRateChanged(long rate)
{
    QByteArray command;
    //Send command to server
    command.clear();
    command.append((char)SETSAMPLERATE);
    command.append(QString("%1").arg(rate));
    connection.sendCommand(command);

} // end sampleRateChanged


void UI::actionANF()
{
    QByteArray command;
    command.clear();
    command.append((char)SETANF);
    command.append(widget.actionANF->isChecked());
    connection.sendCommand(command);
} // end actionANF


void UI::actionNR()
{
    QByteArray command;
    command.clear();
    command.append((char)SETNR);
    command.append(widget.actionNR->isChecked());
    connection.sendCommand(command);
} // end actionNR


void UI::actionNB()
{
    QByteArray command;
    command.clear();
    command.append((char)SETNB);
    command.append(widget.actionNB->isChecked());
    connection.sendCommand(command);
} // end actionNB


void UI::actionSDROM()
{
    QByteArray command;
    command.clear();
    command.append((char)SETNB2);
    command.append(widget.actionNB->isChecked());
    connection.sendCommand(command);
} // end actionSDROM


void UI::actionFixed()
{
    if (!newDspServerCheck())
    {
        widget.actionFixed->setChecked(false);
        switch (agc)
        {
        case AGC_LONG:
            widget.actionLong->setChecked(true);
            break;
        case AGC_SLOW:
            widget.actionSlow->setChecked(true);
            break;
        case AGC_MEDIUM:
            widget.actionMedium->setChecked(true);
            break;
        case AGC_FAST:
            widget.actionFast->setChecked(true);
            break;
        }
        return;
    }

    QByteArray command;
    // reset the current selection
    switch (agc)
    {
    case AGC_FIXED:
        widget.actionFixed->setChecked(false);
        break;
    case AGC_LONG:
        widget.actionLong->setChecked(false);
        break;
    case AGC_SLOW:
        widget.actionSlow->setChecked(false);
        break;
    case AGC_MEDIUM:
        widget.actionMedium->setChecked(false);
        break;
    case AGC_FAST:
        widget.actionFast->setChecked(false);
        break;
    }
    agc = AGC_FIXED;

    command.clear();
    command.append((char)SETAGC);
    command.append((char)agc);
    connection.sendCommand(command);
    AGCTLevelChanged(widget.agcTLevelSlider->value());
} // end actionFixed


void UI::actionSlow()
{
    QByteArray command;
    // reset the current selection
    switch (agc)
    {
    case AGC_FIXED:
        widget.actionFixed->setChecked(false);
        break;
    case AGC_LONG:
        widget.actionLong->setChecked(false);
        break;
    case AGC_SLOW:
        widget.actionSlow->setChecked(false);
        break;
    case AGC_MEDIUM:
        widget.actionMedium->setChecked(false);
        break;
    case AGC_FAST:
        widget.actionFast->setChecked(false);
        break;
    }
    agc = AGC_SLOW;

    command.clear();
    command.append((char)SETAGC);
    command.append((char)agc);
    connection.sendCommand(command);
} // end actionSlow


void UI::actionMedium()
{
    QByteArray command;

    // reset the current selection
    switch (agc)
    {
    case AGC_FIXED:
        widget.actionFixed->setChecked(false);
        break;
    case AGC_LONG:
        widget.actionLong->setChecked(false);
        break;
    case AGC_SLOW:
        widget.actionSlow->setChecked(false);
        break;
    case AGC_MEDIUM:
        widget.actionMedium->setChecked(false);
        break;
    case AGC_FAST:
        widget.actionFast->setChecked(false);
        break;
    }
    agc = AGC_MEDIUM;

    command.clear();
    command.append((char)SETAGC);
    command.append((char)agc);
    connection.sendCommand(command);
} // end actionMedium


void UI::actionFast()
{
    QByteArray command;
    // reset the current selection
    switch (agc)
    {
    case AGC_FIXED:
        widget.actionFixed->setChecked(false);
        break;
    case AGC_LONG:
        widget.actionLong->setChecked(false);
        break;
    case AGC_SLOW:
        widget.actionSlow->setChecked(false);
        break;
    case AGC_MEDIUM:
        widget.actionMedium->setChecked(false);
        break;
    case AGC_FAST:
        widget.actionFast->setChecked(false);
        break;
    }
    agc = AGC_FAST;

    command.clear();
    command.append((char)SETAGC);
    command.append((char)agc);
    connection.sendCommand(command);
}  // end actionFast


void UI::actionLong()
{
    QByteArray command;
    // reset the current selection
    switch (agc)
    {
    case AGC_FIXED:
        widget.actionFixed->setChecked(false);
        break;
    case AGC_LONG:
        widget.actionLong->setChecked(false);
        break;
    case AGC_SLOW:
        widget.actionSlow->setChecked(false);
        break;
    case AGC_MEDIUM:
        widget.actionMedium->setChecked(false);
        break;
    case AGC_FAST:
        widget.actionFast->setChecked(false);
        break;
    }
    agc = AGC_LONG;
    command.clear();
    command.append((char)SETAGC);
    command.append((char)agc);
    connection.sendCommand(command);
} // end actionLong


void UI::actionMuteMainRx()
{
    QString command;
    int g = gain;

    if (widget.actionMuteMainRx->isChecked())
    {
        g=0;
        emit widget.ctlFrame->setAudioMuted(true);
    }
    else
        emit widget.ctlFrame->setAudioMuted(false);

    command.clear();
    QTextStream(&command) << "SetRXOutputGain " << g;
    //connection.sendCommand(command);
}


void UI::actionBandscope()
{
    if (widget.actionBandscope->isChecked())
    {
        if (bandscope == NULL)
            bandscope = new Bandscope();
        bandscope->setWindowTitle("QtRadio Bandscope");
        bandscope->show();
        bandscope->connect(configure.getHost());
    }
    else
    {
        if (bandscope != NULL)
        {
            bandscope->setVisible(false);
            bandscope->disconnect();
        }
    }
}


void UI::actionRecord()
{
    QString command;
    command.clear();
    QTextStream(&command) << "record " << (widget.actionRecord->isChecked()?"on":"off");
    //connection.sendCommand(command);
}


void UI::actionGain(int g)
{
    QByteArray command;
    //    setGain(false);
    gain = g;
    //    setGain(true);
    command.clear();
    command.append((char)SETRXOUTGAIN);
    command.append(QString("%1").arg(g));
    connection.sendCommand(command);
} // end actionGain


void UI::setAudioMuted(bool enabled)
{
    if (enabled)
    {
        actionGain(0);
        widget.actionMuteMainRx->setChecked(true);
    }
    else
    {
        actionGain(widget.ctlFrame->audioGain);
        widget.actionMuteMainRx->setChecked(false);
    }
} // end setAudioMuted


void UI::audioGainChanged(void)
{
    actionGain(widget.ctlFrame->audioGain);
} // end audioGainChanged


void UI::nrValuesChanged(int taps,int delay,double gain,double leakage)
{
    QByteArray command;

    command.clear();
    command.append((char)SETNRVALS);
    command.append(QString("%1,%2,%3,%4").arg(taps).arg(delay).arg(gain).arg(leakage));
    connection.sendCommand(command);
} // end nrValuesChanged


void UI::anfValuesChanged(int taps,int delay,double gain,double leakage)
{
    QByteArray command;

    command.clear();
    command.append((char)SETANFVALS);
    command.append(QString("%1,%2,%3,%4").arg(taps).arg(delay).arg(gain).arg(leakage));
    connection.sendCommand(command);
} // end anfValuesChanged


void UI::nbThresholdChanged(double threshold)
{
    QByteArray command;
    command.clear();
    command.append((char)SETNBVAL);
    command.append(QString("%1").arg(threshold));
    connection.sendCommand(command);
}


void UI::sdromThresholdChanged(double threshold)
{
    QString command;
    command.clear();
    QTextStream(&command) << "SetSDROMVals " << threshold;
    //connection.sendCommand(command);
}


void UI::actionBookmark()
{
    QString strFrequency=stringFrequency(frequency);
    bookmarkDialog.setTitle(strFrequency);
    bookmarkDialog.setBand(band.getStringBand());
    bookmarkDialog.setFrequency(strFrequency);
    bookmarkDialog.setMode(mode.getStringMode());
    bookmarkDialog.setFilter(filters.getText());
    bookmarkDialog.show();
}


void UI::addBookmark()
{
    qDebug() << "addBookmark";
    Bookmark* bookmark=new Bookmark();
    bookmark->setTitle(bookmarkDialog.getTitle());
    bookmark->setBand(band.getBand());
    bookmark->setFrequency(band.getFrequency());
    bookmark->setMode(mode.getMode());
    bookmark->setFilter(filters.getFilter());
    bookmarks.add(bookmark);
    bookmarks.buildMenu(widget.menuView_Bookmarks);
}


void UI::selectBookmark(QAction* action)
{
    QByteArray command;

    bookmarks.select(action);

    band.selectBand(bookmarks.getBand());

    frequency = bookmarks.getFrequency();
    band.setFrequency(frequency);
    command.clear();
    command.append((char)SETFREQ);
    command.append(QString("%1").arg(frequency));
    connection.sendCommand(command);

    widget.spectrumView->setFrequency(frequency);
    //    widget.waterfallView->setFrequency(frequency);

    //    gvj code
    widget.vfoFrame->setFrequency(frequency);

    mode.setMode(bookmarks.getMode());

    filters.selectFilter(bookmarks.getFilter());
    qDebug() << "Bookmark Filter: " << bookmarks.getFilter();
}


void UI::selectABookmark()
{
    /*
    int entry=bookmarksDialog->getSelected();
    if(entry>=0 && entry<bookmarks.count()) {
        selectBookmark(entry);
    }
*/
}


void UI::editBookmarks()
{
    bookmarksEditDialog = new BookmarksEditDialog(this,&bookmarks);
    bookmarksEditDialog->setVisible(true);
    connect(bookmarksEditDialog, SIGNAL(bookmarkDeleted(int)), this, SLOT(bookmarkDeleted(int)));
    connect(bookmarksEditDialog, SIGNAL(bookmarkUpdated(int, QString)), this, SLOT(bookmarkUpdated(int, QString)));
    connect(bookmarksEditDialog, SIGNAL(bookmarkSelected(int)), this, SLOT(bookmarkSelected(int)));
}


void UI::bookmarkDeleted(int entry)
{
    //qDebug() << "UI::bookmarkDeleted: " << entry;
    bookmarks.remove(entry);
    bookmarks.buildMenu(widget.menuView_Bookmarks);
}


void UI::bookmarkUpdated(int entry,QString title)
{
    if (entry>=0 && entry<bookmarks.count())
    {
        Bookmark* bookmark=bookmarks.at(entry);
        bookmark->setTitle(title);
    }
}


void UI::bookmarkSelected(int entry)
{

    //qDebug() << "UI::bookmarkSelected " << entry;
    if (entry>=0 && entry<bookmarks.count())
    {
        Bookmark* bookmark=bookmarks.at(entry);
        FiltersBase* filters;
        //TODO Get rid of message "warning: 'filters' may be used uninitialized in this function"

        bookmarksEditDialog->setTitle(bookmark->getTitle());
        bookmarksEditDialog->setBand(band.getStringBand(bookmark->getBand()));
        bookmarksEditDialog->setFrequency(stringFrequency(bookmark->getFrequency()));
        bookmarksEditDialog->setMode(mode.getStringMode(bookmark->getMode()));

        switch(bookmark->getMode())
        {
        case MODE_CWL:
            filters=&cwlFilters;
            break;
        case MODE_CWU:
            filters=&cwuFilters;
            break;
        case MODE_LSB:
            filters=&lsbFilters;
            break;
        case MODE_USB:
            filters=&usbFilters;
            break;
        case MODE_DSB:
            filters=&dsbFilters;
            break;
        case MODE_AM:
            filters=&amFilters;
            break;
        case MODE_SAM:
            filters=&samFilters;
            break;
        case MODE_FM:
            filters=&fmnFilters;
            break;
        case MODE_DIGL:
            filters=&diglFilters;
            break;
        case MODE_DIGU:
            filters=&diguFilters;
            break;
        }
        bookmarksEditDialog->setFilter(filters->getText(bookmark->getFilter()));
    }
    else
    {
        bookmarksEditDialog->setTitle("");
        bookmarksEditDialog->setBand("");
        bookmarksEditDialog->setFrequency("");
        bookmarksEditDialog->setMode("");
        bookmarksEditDialog->setFilter("");
    }
}


QString UI::stringFrequency(long long frequency)
{
    QString strFrequency;
    strFrequency.sprintf("%lld.%03lld.%03lld",frequency/1000000,frequency%1000000/1000,frequency%1000);
    return strFrequency;
}


void UI::addXVTR(QString title,long long minFrequency,long long maxFrequency,long long ifFrequency,long long freq,int m,int filt)
{

    qDebug()<<"UI::addXVTR"<<title;
    xvtr.add(title,minFrequency,maxFrequency,ifFrequency,freq,m,filt);

    // update the menu
    xvtr.buildMenu(widget.menuXVTR);
    configure.updateXvtrList(&xvtr);
}


void UI::deleteXVTR(int index)
{
    xvtr.del(index);

    // update the menu
    xvtr.buildMenu(widget.menuXVTR);
    configure.updateXvtrList(&xvtr);
}


void UI::selectXVTR(QAction* action)
{
    xvtr.select(action);
}


void UI::getMeterValue(float m, float s)
{
    widget.sMeterFrame->meter1 = m;
    widget.sMeterFrame->meter2 = s;
    widget.sMeterFrame->update();
}


void UI::printWindowTitle(QString message)
{
    if (message.compare("Remote disconnected") == 0)
    {
        dspversion = 0;
        dspversiontxt = "";
    }
    setWindowTitle("QtRadioII - Server: " + servername + " " + configure.getHost() + "(Rx "
                   + QString::number(configure.getReceiver()) +") .. "
                   + getversionstring() +  message + "  [" + QString("Qt: %1").arg(QT_VERSION, 0, 16) + "]  16 Jan 2019");
    lastmessage = message;
}


void UI::printStatusBar(QString message)
{
    Frequency freqInfo;
    static QString description;
    static long long lastFreq;

    if (lastFreq != frequency)
        description = freqInfo.getFrequencyInfo(frequency).getDescription();

    modeInfo.setText(description + "  " + band.getStringMem()+", "+mode.getStringMode()+", "+filters.getText()+message);
    lastFreq = frequency;
}


void UI::initRigCtl ()
{
    rigCtl = new RigCtlServer ( this, this );
}


long long UI::rigctlGetFreq()
{
    return(frequency);
}


QString UI::rigctlGetMode()
{
    QString  m = mode.getStringMode();
    if(m == "CWU")
    {
        m="CW";
    }
    if(m == "CWL")
    {
        m="CWR";
    }
    return m;
}


QString UI::rigctlGetFilter()
{
    QString fwidth;
    QString  m = mode.getStringMode();
    
    if (m == "CWU")
    {
        return fwidth.setNum(filters.getHigh() + filters.getLow());
    }
    else
        if (m == "CWL")
        {
            return fwidth.setNum(filters.getHigh() + filters.getLow());
        }
        else
            return fwidth.setNum(filters.getHigh() - filters.getLow());
}


QString UI::rigctlGetVFO()
{
    return widget.vfoFrame->rigctlGetvfo();
}


void UI::rigctlSetVFOA()
{
    widget.vfoFrame->on_pBtnvfoA_clicked();
}


void UI::rigctlSetVFOB()
{
    widget.vfoFrame->on_pBtnvfoB_clicked();
}


void UI::rigctlSetFreq(long long f)
{
    frequencyChanged(f);
}


void UI::rigctlSetMode(int newmode)
{
    modeChanged(mode.getMode(), newmode);
    mode.setMode(newmode);
}


void UI::rigctlSetFilter(int newfilter)
{
    qDebug() << "UI.cpp: dl6kbg: wanted filter via hamlib: " << newfilter;
    filters.selectFilter(newfilter);
}


void UI::slaveSetMode(int m)
{
    rigctlSetMode(m);
}


void UI::slaveSetFilter(int low, int high)
{
    widget.spectrumView->setFilter(low,high);
    //    widget.waterfallView->setFilter(low,high);
}


void UI::slaveSetZoom(int position)
{
    widget.zoomSpectrumSlider->setValue(position);
    widget.spectrumView->setZoom(position);
    //   widget.waterfallView->setZoom(position);
}


void UI::getBandFrequency()
{
    widget.vfoFrame->setBandFrequency(band.getFrequency());
}


void UI::vfoStepBtnClicked(int direction)
{
    long long f;
    int samplerate = widget.spectrumView->samplerate();

    //qDebug()<<Q_FUNC_INFO<<": vfo up or down button clicked. Direction = "<<direction<<", samplerate = "<<samplerate;
    switch ( samplerate )
    {
    case 24000 : f = 20000; break;
    case 48000 : f = 40000; break;
    case 96000 : f = 80000; break;
    case 192000 : f = 160000; break;
    case 384000 : f = 320000; break;

    default : f = (samplerate * 8) / 10;
    }
    frequencyMoved(f, direction);
}


// The ptt service has been activated. Caller values, 0 = MOX, 1 = Tune, 2 = VOX, 3 = Extern H'ware
void UI::pttChange(int caller, bool ptt)
{
    QByteArray command;
    static int workingMode;
    static double currentPwr;

    tuning = caller;
    if (tuning == 1)
        emit tuningEnable(true);
    else
        emit tuningEnable(false);

    if (configure.getTxAllowed())
    {
        if (ptt)
        {    // Going from Rx to Tx ................
            widget.spectrumView->panadapterScene->bMox = true;
            delete widget.sMeterFrame->sMeterMain;
            widget.sMeterFrame->sMeterMain = new Meter("Main Pwr", POWMETER);
            workingMode = mode.getMode(); //Save the current mode for restoration when we finish tuning
            if (caller == 1)
            { //We have clicked the tune button so switch to AM and set carrier level
     //////           currentPwr = (double)widget.ctlFrame->getTxPwr();
                actionAM();
                //   workingMode = mode.getMode(); //Save the current mode for restoration when we finish tuning
                // Set the AM carrier level to match the tune power slider value in a scale 0 to 1.0
                if ((dspversion >= 20120201)  && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)SETTXAMCARLEV);
     //////               command.append(QString("%1 %2 %3").arg(widget.ctlFrame->getTunePwr()).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)SETTXAMCARLEV);
    //////                command.append(QString("%1").arg(widget.ctlFrame->getTunePwr()));
                }
                connection.sendCommand(command);
                //Mute the receiver audio and freeze the spectrum and waterfall display
                connection.setMuted(true);
                //Key the radio
                if ((dspversion >= 20130901) && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(1).arg(configure.thisuser).arg(configure.thispass));
                    //QTextStream(&command) << "Mox " << "on " << configure.thisuser << " " << configure.thispass;
                }
                else
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1").arg(1));
                    //QTextStream(&command) << "Mox " << "on";
                }
            }
            else
            {
                //Mute the receiver audio and freeze the spectrum and waterfall display
                connection.setMuted(true);
                //Key the radio
                if ((dspversion >= 20130901) && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(1).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1").arg(1));
                }
            }
            connection.sendCommand(command);
            widget.vfoFrame->pttChange(ptt); //Update the VFO to reflect that we are transmitting
            connect(audioinput, SIGNAL(mic_update_level(qreal)), widget.ctlFrame, SLOT(update_mic_level(qreal)));
            txNow = true;
        }
        else
        {    // Going from Tx to Rx .................
            delete widget.sMeterFrame->sMeterMain;
            widget.sMeterFrame->sMeterMain = new Meter("Main Rx", SIGMETER);
            if (caller == 1)
            {
                //Send signal to sdr to go to Rx
                if ((dspversion >= 20130901) && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(0).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1").arg(0));
                }
                connection.sendCommand(command);
                widget.spectrumView->panadapterScene->bMox = false;

                //Restore AM carrier level to previous level.
                if ((dspversion >= 20120201) && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)SETTXAMCARLEV);
                    command.append(QString("%1 %2 %3").arg(currentPwr).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)SETTXAMCARLEV);
                    command.append(QString("%1").arg(currentPwr));
                }
                connection.sendCommand(command);
                //Restore the mode back to original before tuning
                if (workingMode != MODE_AM)
                {
                    switch (workingMode)
                    {
                    case MODE_CWL: actionCWL(); break;
                    case MODE_CWU: actionCWU(); break;
                    case MODE_LSB: actionLSB(); break;
                    case MODE_USB: actionUSB(); break;
                    case MODE_DSB: actionDSB(); break;
                    case MODE_AM:  actionAM();  break;
                    case MODE_SAM: actionSAM(); break;
                    case MODE_FM:  actionFMN(); break;
                    case MODE_DIGL:actionDIGL();break;
                    case MODE_DIGU:actionDIGU();break;
                    }
                }
            }
            else
            {
                //Send signal to sdr to go to Rx
                if ((dspversion >= 20130901)  && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(0).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)MOX);
                    command.append(QString("%1").arg(0));
                }
                connection.sendCommand(command);
                widget.spectrumView->panadapterScene->bMox = false;
            }
            txNow = false;

            //Un-mute the receiver audio
            connection.setMuted(false);
            widget.vfoFrame->pttChange(ptt); //Set band select buttons etc. to Rx state on VFO
            disconnect(audioinput, SIGNAL(mic_update_level(qreal)),widget.ctlFrame, SLOT(update_mic_level(qreal)));
        }
    }
    else
        widget.ctlFrame->clearMoxBtn();
} // end pttChange


void UI::actionConnectNow(QString IP)
{
    qDebug() << "Connect Slot:"  << IP;
    if (isConnected == false)
    {
        QuickIP = IP;
        configure.addHost(IP);
        connection.connect(IP, DSPSERVER_BASE_PORT+configure.getReceiver());
        widget.spectrumView->setReceiver(configure.getReceiver());
    }
    else
    {
        QMessageBox msgBox;
        msgBox.setText("Already Connected to a server!\nDisconnect first.");
        msgBox.exec();
    }
}


void UI::actionSquelch()
{
    if (squelch)
    {
        squelch = false;
        QByteArray command;
        command.clear();
        command.append((char)SETSQUELCHSTATE);
        command.append((char)0);
        connection.sendCommand(command);
        widget.spectrumView->setSquelch(false);
        widget.actionSquelchEnable->setChecked(false);
    }
    else
    {
        squelch = true;
        QByteArray command;
        command.clear();
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue));
        connection.sendCommand(command);
        command.clear();
        command.append((char)SETSQUELCHSTATE);
        command.append((char)1);
        connection.sendCommand(command);
        widget.spectrumView->setSquelch(true);
        widget.spectrumView->setSquelchVal(squelchValue);
        widget.actionSquelchEnable->setChecked(true);
    }
}


void UI::actionSquelchReset()
{
    squelchValue=-100;
    if (squelch)
    {
        QByteArray command;
        command.clear();
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue));
        connection.sendCommand(command);
        widget.spectrumView->setSquelchVal(squelchValue);
    }
}


void UI::squelchValueChanged(int val)
{
    squelchValue=squelchValue+val;
    if (squelch)
    {
        QByteArray command;
        command.clear();
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue));
        connection.sendCommand(command);
        widget.spectrumView->setSquelchVal(squelchValue);
    }
}


QString UI::getversionstring()
{
    QString str;
    if (dspversion != 0)
    {
        str.setNum(dspversion);
        str.prepend("  (Remote = ");
        str.append("  ");
        str.append(dspversiontxt);
        str.append(")  ");
    }
    else
    {
        str ="";
    }
    return str;
}


void UI::setdspversion(long ver, QString vertxt)
{
    dspversion = ver;
    dspversiontxt = vertxt;
    printWindowTitle(lastmessage);
}


void UI::setservername(QString sname)
{
    servername = sname;
    printWindowTitle(lastmessage);
    if (!configure.setPasswd(servername))
    {
        configure.thisuser = "None";
        configure.thispass= "None";
    }
}


void UI::cwPitchChanged(int arg1)
{
    cwPitch = arg1;
    if (isConnected)
    {
        filters.selectFilter(filters.getFilter()); //Dummy call to centre filter on tone
        frequencyChanged(frequency); //Dummy call to set freq into correct place in filter
    }
}


void UI::setCanTX(bool tx)
{
    canTX = tx;
  //  qDebug("Can TX: %d", tx);
    emit HideTX(tx);
}


void UI::setChkTX(bool chk)
{
    chkTX = chk;
    infotick2 = 0;
}


void UI::resetbandedges(double offset)
{
    loffset= offset;
    BandLimit limits=band.getBandLimits(band.getFrequency()-(widget.spectrumView->samplerate()/2),band.getFrequency()+(widget.spectrumView->samplerate()/2));
    widget.spectrumView->setBandLimits(limits.min() + loffset,limits.max()+loffset);
    qDebug()<<"loffset = "<<loffset;
}


void UI::on_zoomSpectrumSlider_sliderMoved(int position)
{
    viewZoomLevel = position;
    widget.spectrumView->setZoom(position);
}


void UI::rigSetPTT(int enabled)
{
    if (enabled)
    {
        widget.ctlFrame->RigCtlTX(true);
    }
    else
    {
        widget.ctlFrame->RigCtlTX(false);
    }
}


void UI::windowTypeChanged(int type)
{
    QByteArray command;

    command.clear();
    command.append((char)SETWINDOW);
    command.append(QString("%1").arg(type));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
} // end windowTypeChanged


void UI::setPwsMode(int mode)
{
    QString command;
    command.clear(); QTextStream(&command) << "setpwsmode " << mode;
    //        qDebug("%s", command);
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;

    switch (mode)
    {
    case 0:
        widget.actionPWS_Post_Filter->setChecked(true);
        break;
    case 1:
        widget.actionPWS_Pre_Filter->setChecked(true);
        break;
    case 2:
        widget.actionPWS_Semi_Raw->setChecked(true);
        break;
    case 3:
        widget.actionPWS_Post_Det->setChecked(true);
        break;
    }
}


void UI::AGCTLevelChanged(int level)
{
    QByteArray command;
    command.clear();
    command.append((char)SETFIXEDAGC);
    command.append(QString("%1").arg(level));
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
    widget.agcTLevelLabel->setText(QString("%1").arg(level));
}


void UI::enableRxEq(bool enable)
{
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)ENABLERXEQ);
    command.append((char)enable);
    connection.sendCommand(command);
    qDebug() << Q_FUNC_INFO << ":   The command sent is " << command;
}


void UI::enableTxEq(bool enable)
{
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)ENABLETXEQ);
    command.append((char)enable);
    connection.sendCommand(command);
    qDebug() << Q_FUNC_INFO << ":   The command sent is " << command;
}


void UI::addNotchFilter(void)
{
    if (!newDspServerCheck()) return;

    if (notchFilterIndex >= 9)
    {
        QMessageBox::warning(this, "Tracking Notch Filter Error", "Maximum of 9 notch filters reached!");
        return;
    }
    widget.tnfButton->setChecked(true);
    widget.spectrumView->addNotchFilter(notchFilterIndex++);
}


void UI::removeNotchFilter(void)
{
    if (!newDspServerCheck()) return;

    notchFilterIndex--;
    if (notchFilterIndex < 0)
        notchFilterIndex = 0;
    if (notchFilterIndex == 0)
        widget.tnfButton->setChecked(false);
}


void UI::statusMessage(QString message)
{
    widget.statusbar->showMessage(message);
}


void UI::masterButtonClicked(void)
{
    //connection.sendCommand("setMaster " + configure.thisuser + " " + configure.thispass);
}


bool UI::newDspServerCheck(void)
{
    if (dspversion >= 20130609 || dspversion == 0)
        return true;
    else
    {
        QMessageBox::warning(this, "Advanced Features Error", "DSP server version 20130609 or greater required.");
        return false;
    }
}


void UI::agcSlopeChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setrxagcslope " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcMaxGainChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setrxagcmaxgain " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcAttackChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setrxagcattack " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcDecayChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setrxagcdecay " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcHangChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setrxagchang " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcHangThreshChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setrxagchangthreshold " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcFixedGainChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "setfixedagc " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerStateChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxlevelerstate " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerMaxGainChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxlevelermaxgain " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerAttackChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
//    command.clear(); QTextStream(&command) << "settxlevelerattack " << value;
//    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerDecayChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxlevelerdecay " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerHangChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxlevelerhang " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::alcStateChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxalcstate " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::alcDecayChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxalcdecay " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::alcAttackChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxalcattack " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::alcHangChanged(int value)
{
    if (!newDspServerCheck()) return;

    QString command;
    command.clear(); QTextStream(&command) << "settxalchang " << value;
    //connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::hardwareSet(QString hardware)
{
    if (hardware == "hermes")
    {
        hf = (void*)new HermesFrame(this);
        widget.RadioScrollAreaWidgetContents->layout()->addWidget((HermesFrame*)hf);
        hww = (QWidget*)hf;
        hardwareType = "hermes";
        connect((HermesFrame*)hf, SIGNAL(hhcommand(QByteArray)), this, SLOT(sendHardwareCommand(QByteArray)));
    }
} // end hardwareSet


void UI::sendHardwareCommand(QByteArray command)
{
    connection.sendCommand(command);
    fprintf(stderr, "Send hardware command...\n");
} // end sendHardwareCommand


void UI::setCurrentChannel(int channel)
{
    currentChannel = channel;
    widget.ctlFrame->setCurrentChannel(channel+1);
} // end setCurrentChannel
