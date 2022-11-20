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
#include "Connection.h"
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
#include "Frequency.h"
#include "EqualizerDialog.h"
#include "calc.h"

#include "hermesframe.h"
#include "sdr1000frame.h"


UI::UI(const QString server)
{
    widget.setupUi(this);

    servers = 0;
    servername = "Unknown";
    configure.thisuser = "None";
    configure.thispass= "None";
    canTX = true;  // set to false if dspserver says so
    chkTX = false;
    txNow = false;

    loffset = 0;
    viewZoomLevel = 0;

    audio = new Audio;
    configure.initAudioDevices(audio);
    audio_sample_rate = 8000;
    audio_channels = 1;
    audioinput = new AudioInput;
    configure.initMicDevices(audioinput);
    mic_buffer_count = 0;
    mic_frame_count = 0;
    connection_valid = false;
    currentRxRfstream = -1;
    currentTxRfstream = -1;
    current_index = -1;

    isConnected = false;
    infotick = 0;
    infotick2 = 0;
    dspversion = 0;
    dspversiontxt = "Unknown";
    meter = -121;

    widget.statusbar->showMessage("QtRadio NG branch: KD0OSS 2022");
    widget.ctlFrame->HideTX(true);

//    widget.actionBandscope->setEnabled(false);
    widget.actionRecord->setEnabled(false);

    for (int r=0;r<MAX_RADIOS;r++)
        radio[r] = NULL;

    // connect up all the menus
    connect(&connection, SIGNAL(isConnected(bool*, int8_t*, int8_t*, bool*, bool*)), this, SLOT(connected(bool*, int8_t*, int8_t*, bool*, bool*)));
    connect(&connection, SIGNAL(disconnected(QString)), this, SLOT(disconnected(QString)));
    connect(&connection, SIGNAL(printStatusBar(QString)), this, SLOT(printStatusBar(QString)));
    connect(&connection, SIGNAL(slaveSetMode(int)), this, SLOT(slaveSetMode(int)));
    connect(&connection, SIGNAL(setCurrentRfstream(int)), this, SLOT(setCurrentRfstream(int)));
    connect(&connection, SIGNAL(slaveSetFilter(int,int)), this, SLOT(slaveSetFilter(int,int)));
    connect(&connection, SIGNAL(slaveSetZoom(int)), this, SLOT(slaveSetZoom(int)));
    connect(&connection, SIGNAL(setdspversion(long, QString)), this, SLOT(setdspversion(long, QString)));
    connect(&connection, SIGNAL(setservername(QString)), this, SLOT(setservername(QString)));
    connect(&connection, SIGNAL(setCanTX(bool)), this, SLOT(setCanTX(bool)));
    connect(&connection, SIGNAL(setChkTX(bool)), this, SLOT(setChkTX(bool)));
    connect(&connection, SIGNAL(resetbandedges(double)), this, SLOT(resetBandedges(double)));
  //  connect(&connection, SIGNAL(setSampleRate(int)), this, SLOT(sampleRateChanged(long)));

    connect(&audioConnection, SIGNAL(audioBuffer(char*,char*)), this, SLOT(audioBuffer(char*,char*)));
    connect(audioinput, SIGNAL(mic_send_audio(QQueue<qint16>*)), this, SLOT(micSendAudio(QQueue<qint16>*)));

    // connect up band and frequency changes
    connect(widget.vfoFrame, SIGNAL(receiverChanged(int)), this, SLOT(currentReceiverChanged(int)));
    connect(widget.vfoFrame, SIGNAL(getBandFrequency()), this, SLOT(getBandFrequency()));
    connect(widget.vfoFrame, SIGNAL(bandBtnClicked(int)), this, SLOT(getBandBtn(int)));
    connect(widget.vfoFrame, SIGNAL(frequencyMoved(int,int)), this, SLOT(frequencyMoved(int,int)));
    connect(widget.vfoFrame, SIGNAL(rightBandClick()), this, SLOT(quickMemStore()));
    connect(widget.vfoFrame, SIGNAL(vfoStepBtnClicked(int)), this, SLOT(vfoStepBtnClicked(int)));
    connect(widget.vfoFrame, SIGNAL(frequencyChanged(long long)), this, SLOT(frequencyChanged(long long)));

    connect(widget.ctlFrame, SIGNAL(audioMuted(bool)), this, SLOT(setAudioMuted(bool)));
    connect(widget.ctlFrame, SIGNAL(audioGainChanged()), this, SLOT(audioGainChanged()));
    connect(widget.ctlFrame, SIGNAL(pttChange(int,bool)), this, SLOT(pttChange(int,bool)));
    connect(widget.ctlFrame, SIGNAL(masterBtnClicked()), this, SLOT(masterButtonClicked()));

    connect(&keypad, SIGNAL(setKeypadFrequency(long long)), this, SLOT(setKeypadFrequency(long long)));

    connect(widget.actionAbout, SIGNAL(triggered()), this, SLOT(actionAbout()));
    connect(widget.actionConnectToServer, SIGNAL(triggered()), this, SLOT(actionConnect()));
    connect(widget.actionQuick_Server_List, SIGNAL(triggered()), this, SLOT(actionQuick_Server_List()));
    connect(widget.actionDisconnectFromServer, SIGNAL(triggered()), this, SLOT(actionDisconnect()));
//    connect(widget.actionBandscope, SIGNAL(triggered()), this, SLOT(actionBandscope()));
    connect(widget.actionRecord, SIGNAL(triggered()), this, SLOT(actionRecord()));
    connect(widget.actionConfig, SIGNAL(triggered()), this, SLOT(actionConfigure()));
    connect(widget.actionEqualizer, SIGNAL(triggered()), this, SLOT(actionEqualizer()));
    connect(widget.actionMuteMainRx, SIGNAL(triggered()), this, SLOT(actionMuteMainRx()));
    connect(widget.actionSquelchEnable, SIGNAL(triggered()), this, SLOT(actionSquelch()));
    connect(widget.actionSquelchReset, SIGNAL(triggered()), this, SLOT(actionSquelchReset()));
    connect(widget.actionKeypad, SIGNAL(triggered()), this, SLOT(actionKeypad()));
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
    //connect(widget.agcTLevelSlider, SIGNAL(valueChanged(int)), this, SLOT(AGCTLevelChanged(int)));
    connect(widget.actionBookmarkThisFrequency, SIGNAL(triggered()), this, SLOT(actionBookmark()));
    connect(widget.actionEditBookmarks, SIGNAL(triggered()), this, SLOT(editBookmarks()));

    // connect up spectrum view
    connect(&spectrumConnection, SIGNAL(spectrumBuffer(RFSTREAM)), this, SLOT(spectrumBuffer(RFSTREAM)));

    // connect up configuration changes
//    connect(&configure, SIGNAL(spectrumHighChanged(int)), this, SLOT(spectrumHighChanged(int))); FIXME: add configuration per radio/receiver
//    connect(&configure, SIGNAL(spectrumLowChanged(int)), this, SLOT(spectrumLowChanged(int))); FIXME: add configuration per radio/receiver
    connect(&configure, SIGNAL(fpsChanged(int)), this, SLOT(fpsChanged(int)));
//    connect(&configure, SIGNAL(waterfallHighChanged(int)), this, SLOT(waterfallHighChanged(int))); FIXME: add configuration per radio/receiver
//    connect(&configure, SIGNAL(waterfallLowChanged(int)), this, SLOT(waterfallLowChanged(int))); FIXME: add configuration per radio/receiver
//    connect(&configure, SIGNAL(waterfallAutomaticChanged(bool)), this, SLOT(waterfallAutomaticChanged(bool))); FIXME: add configuration per radio/receiver
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
//    connect(&configure, SIGNAL(sdromThresholdChanged(double)), this, SLOT(sdromThresholdChanged(double)));
    connect(&configure, SIGNAL(windowTypeChanged(int)), this, SLOT(windowTypeChanged(int)));
    connect(&configure, SIGNAL(agcAttackChanged(int)), this, SLOT(agcAttackChanged(int)));
//    connect(&configure, SIGNAL(agcMaxGainChanged(int)), this, SLOT(agcMaxGainChanged(int)));
    connect(&configure, SIGNAL(agcSlopeChanged(int)), this, SLOT(agcSlopeChanged(int)));
    connect(&configure, SIGNAL(agcDecayChanged(int)), this, SLOT(agcDecayChanged(int)));
    connect(&configure, SIGNAL(agcHangChanged(int)), this, SLOT(agcHangChanged(int)));
//    connect(&configure, SIGNAL(agcFixedGainChanged(double)), this, SLOT(agcFixedGainChanged(double)));
    connect(&configure, SIGNAL(agcHangThreshChanged(int)), this, SLOT(agcHangThreshChanged(int)));
    connect(&configure, SIGNAL(preAGCChanged(bool)), this, SLOT(preAGCFiltersChanged(bool)));
    connect(&configure, SIGNAL(levelerStateChanged(int)), this, SLOT(levelerStateChanged(int)));
//    connect(&configure, SIGNAL(levelerMaxGainChanged(double)), this, SLOT(levelerMaxGainChanged(double)));
    connect(&configure, SIGNAL(levelerAttackChanged(int)), this, SLOT(levelerAttackChanged(int)));
    connect(&configure, SIGNAL(levelerDecayChanged(int)), this, SLOT(levelerDecayChanged(int)));
//    connect(&configure, SIGNAL(levelerHangChanged(int)), this, SLOT(levelerHangChanged(int)));
//    connect(&configure, SIGNAL(alcStateChanged(int)), this, SLOT(alcStateChanged(int)));
//    connect(&configure, SIGNAL(alcAttackChanged(int)), this, SLOT(alcAttackChanged(int)));
//    connect(&configure, SIGNAL(alcDecayChanged(int)), this, SLOT(alcDecayChanged(int)));
//    connect(&configure, SIGNAL(alcMaxGainChanged(int)), this, SLOT(alcMaxGainChanged(int)));
    connect(&configure, SIGNAL(addXVTR(QString,long long,long long,long long,long long,int,int)), this, SLOT(addXVTR(QString,long long,long long,long long,long long,int,int)));
    connect(&configure, SIGNAL(deleteXVTR(int)), this, SLOT(deleteXVTR(int)));
    connect(&configure, SIGNAL(spinBox_cwPitchChanged(int)), this, SLOT(cwPitchChanged(int)));
    connect(&configure, SIGNAL(cessbOvershootChanged(bool)), this, SLOT(cessbOvershootChanged(bool)));
    connect(&configure, SIGNAL(aeFilterChanged(bool)), this, SLOT(aeFilterChanged(bool)));
    connect(&configure, SIGNAL(nrGainMethodChanged(int)), this, SLOT(nrGainMethodChanged(int)));
    connect(&configure, SIGNAL(nrNpeMethodChanged(int)), this, SLOT(nrNpeMethodChanged(int)));

    connect(&bookmarks, SIGNAL(bookmarkSelected(QAction*)), this, SLOT(selectBookmark(QAction*)));
    connect(&bookmarkDialog, SIGNAL(accepted()), this, SLOT(addBookmark()));

    connect(&xvtr, SIGNAL(xvtrSelected(QAction*)), this, SLOT(selectXVTR(QAction*)));

    connect(this, SIGNAL(process_audio(char*,char*,int)), audio, SLOT(process_audio(char*,char*,int)));
//    connect(this, SIGNAL(HideTX(bool)), widget.ctlFrame, SLOT(HideTX(bool)));

    kickDisplay.setSingleShot(true);
    connect(&kickDisplay, SIGNAL(timeout()), this, SLOT(kick_display()));

    gain = 100;
    agc = AGC_SLOW;
//    cwPitch = configure.getCwPitch();

    audio->get_audio_device(&audio_device);

    // load any saved settings
    loadSettings();

    configure.updateXvtrList(&xvtr);
    xvtr.buildMenu(widget.menuXVTR);

    printWindowTitle("Remote disconnected"); //added by gvj

    //Configure statusBar
    widget.statusbar->addPermanentWidget(&modeInfo);

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


void UI::kick_display()
{
    if (current_index < 0) return;
    for (int i=0;i<MAX_RECEIVERS-1;i++)
        if (receiver_active[i])
        {
            radio[connection.rfstream[receiver_rfstream[i]].radio.radio_id]->frequencyMoved(i, 0, 0);
            resetBandedges(radio[connection.rfstream[receiver_rfstream[i]].radio.radio_id]->loffset);
        }
} // end kick_display


void UI::currentReceiverChanged(int index)
{
    if (current_index < 0) return;
    current_index = index;
    frequencyMoved(0, 0);

    QByteArray command;
    command.clear();
    command.append((char)receiver_rfstream[current_index]);
    command.append((char)ENABLEAUDIO);
    command.append((char)true);
    connection.sendCommand(command);

    if (radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->txrxPair[1] != receiver_rfstream[current_index])
        widget.ctlFrame->HideTX(true);
    else
        widget.ctlFrame->HideTX(false);

    updateModeMenu(radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[index].getMode());
    updateFiltersMenu(radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[index].getFilter(), radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[index].getCurrentFilters());

    if (connection.rfstream[receiver_rfstream[current_index]].radio.bandscope_capable)
        widget.actionBandscope->setEnabled(true);
    else
        widget.actionBandscope->setEnabled(false);
    printStatusBar(" .. Switched rfstream. ");    //added by gvj
    qDebug("Receiver index changed.\n");
}


void UI::loadSettings()
{
    QSettings settings("FreeSDR", "QtRadioII");
    qDebug() << "loadSettings: " << settings.fileName();

    widget.ctlFrame->loadSettings(&settings);
    xvtr.loadSettings(&settings);
    configure.loadSettings(&settings);
    configure.updateXvtrList(&xvtr);
    bookmarks.loadSettings(&settings);
    bookmarks.buildMenu(widget.menuView_Bookmarks);

    settings.beginGroup("UI");
    if (settings.contains("gain")) gain=settings.value("gain").toInt();
    emit widget.ctlFrame->audioGainInitalized(gain);
    if (settings.contains("agc")) agc=settings.value("agc").toInt();
    if (settings.contains("pwsmode")) pwsmode=settings.value("pwsmode").toInt();
    settings.endGroup();

    settings.beginGroup("mainWindow");
    if (configure.getGeometryState())
        restoreGeometry(settings.value("geometry").toByteArray());
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
//    band.saveSettings(&settings);
    xvtr.saveSettings(&settings);
    bookmarks.saveSettings(&settings);

    settings.beginGroup("UI");
    settings.setValue("gain", gain);
    settings.setValue("agc", agc);
//    settings.setValue("squelch", squelchValue);
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
//    rxp[0][0]->setHost(host);
    printWindowTitle("Remote disconnected");
} // end hostChanged


void UI::receiverChanged(int rx)
{
//    rxp[0][0]->setReceiver(rx);
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
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->equalizer->show();
} // end actionEqualizer


void UI::resizeEvent(QResizeEvent *)
{
  //  setFPS();
} // end resizeEvent


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
    current_index = -1;
    connection.connect(configure.getHost(), DSPSERVER_BASE_PORT+configure.getReceiver());
//    rxp[0][0]->setReceiver(configure.getReceiver()); // FIXME: not sure what this is doing
//    isConnected = true;
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
    QByteArray command;

    if (currentTxRfstream > -1)
    {
        command.clear();
        command.append((char)currentTxRfstream);
        command.append((char)STARCOMMAND);
        command.append((char)DETACH);
        connection.sendCommand(command);
        QThread::sleep(1);

        command.clear();
        command.append((char)currentTxRfstream);
        command.append((char)STOPXCVR);
        connection.sendCommand(command);
    }

    for (int r=0;r<MAX_RADIOS;r++)
    {
        if (radio[r] == NULL) continue;

        if (radio[r]->bandscope != NULL)
             radio[r]->bandscope->close();

        for (int i=0;i<MAX_RECEIVERS;i++)
        {
            if (radio[r]->receiver_active[i])
            {
                command.clear();
                command.append((char)radio[r]->receiver_rfstream[i]);
                command.append((char)STARCOMMAND);
                command.append((char)DETACH);
                connection.sendCommand(command);
                QThread::sleep(1);

                command.clear();
                command.append((char)radio[r]->receiver_rfstream[i]);
                command.append((char)STOPXCVR);
                connection.sendCommand(command);

                if (radio[r]->rxp[i] != NULL)
                    radio[r]->rxp[i]->disconnect();
            }
        }
        radio[r]->saveAllSettings();
        radio[r]->shutdownRadio();
        radio[r]->disconnect();
    }

    qDebug() << "actionDisconnect() QuickIP=" << QuickIP;
    if (QuickIP.length() > 6)
    {    // Remove from saved host list or IPs will pile up forever. If empty string we did not connect via Quick Connect
        configure.removeHost(QuickIP);
        qDebug() << "actionDisconnect() removeHost(" << QuickIP <<")";
    }

    QuickIP = "";
    widget.zoomSpectrumSlider->setValue(0);

//    widget.actionBandscope->setEnabled(false);
//    for (int i=0;i<MAX_RECEIVERS;i++)
  //      receiver_active[i] = false;

    spectrumConnection.recv_mutex.lock();
    spectrumConnection.disconnect();
    spectrumConnection.recv_mutex.unlock();
    audioConnection.disconnect();
    micAudioConnection.disconnect();

    connection.disconnect();
    widget.actionConnectToServer->setDisabled(false);
    widget.actionDisconnectFromServer->setDisabled(true);

    configure.connected(false);
    isConnected = false;
    currentRxRfstream = -1;
    currentTxRfstream = -1;
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


void UI::connected(bool *rx_active, int8_t *rxstreams, int8_t *txrxPair, bool *local_audio, bool *local_mic)
{
    QByteArray command;
    int8_t  stream = -1;
    int i = 0;
    int x = 0;

    if (txrxPair[1] > -1)
    {
        currentTxRfstream = txrxPair[0];
        qDebug("TX rf stream: %d\n", currentTxRfstream);
        widget.ctlFrame->HideTX(false);
    }
    else
        widget.ctlFrame->HideTX(true);

    spectrumConnection.connect(configure.getHost(), DSPSERVER_BASE_PORT+1);
    widget.vfoFrame->resetSelectedReceiver();

    for (i=0;i<MAX_RECEIVERS-1;i++)  // 8 receivers less 1 transmitter
    {
        if (!rx_active[i] || connection.rfstream[rxstreams[i]].radio.radio_id == -1)
            continue;

        if (!connection.rfstream[rxstreams[i]].isTX)
        {
            currentRxRfstream = rxstreams[i];
            stream = rxstreams[i];
        }
        else
            continue;

        if (stream < 0) // We're being extra paranoid here.
        {
            qDebug("********* Invalid stream!\n");
            continue;
        }

        qDebug() << "UI::connected";
        qDebug("Setting up stream: %d\n", stream);

        int r = connection.rfstream[stream].radio.radio_id;
    //    int index = connection.rfstream[stream].index;

        receiver_rfstream[x] = rxstreams[i];
        receiver_active[x] = rx_active[i];

        if (radio[r] == NULL)
        {
            widget.spectrumLayout->removeWidget(widget.titlelabel);
            radio[r] = new Radio(r, &connection);
            connect(radio[r], SIGNAL(send_command(QByteArray)), &connection, SLOT(sendCommand(QByteArray)));
            connect(radio[r], SIGNAL(send_spectrum_command(QByteArray)), &spectrumConnection, SLOT(sendCommand(QByteArray)));
            connect(radio[r], SIGNAL(updateVFO(long long)), this, SLOT(updateVFO(long long)));
            connect(radio[r], SIGNAL(bandScopeClosed()), this, SLOT(bandScopeClosed()));
            connect(radio[r], SIGNAL(ctlSetPTT(bool)), this, SLOT(ctlSetPTT(bool)));
            connect(radio[r], SIGNAL(tnfSetChecked(bool)), this, SLOT(tnfSetChecked(bool)));
            connect(&connection, SIGNAL(slaveSetFreq(long long)), this, SLOT(slaveSetFrequency(long long)));
            radio[r]->txrxPair[0] = txrxPair[0];
            radio[r]->txrxPair[1] = txrxPair[1];
            radio[r]->local_audio = local_audio;
            radio[r]->local_mic = local_mic;
            qDebug("**********Initialized radio structure -> %d\n", r);
        }

        if (!isConnected)
        {
            radio[r]->hardwareSet(widget.RadioScrollAreaWidgetContents);
            qDebug("**********Initialized hardware specific interface.\n");
        }

        if (!connection.rfstream[stream].isTX)
        {
            radio[r]->initializeReceiver(&connection.rfstream[stream]);

            connect(widget.tnfButton, SIGNAL(clicked(bool)), radio[r]->rxp[x], SLOT(enableNotchFilter(bool)));
            connect(radio[r]->rxp[x], SIGNAL(variableFilter(int8_t,int,int)), radio[r], SLOT(variableFilter(int8_t,int,int)));
            connect(radio[r]->rxp[x], SIGNAL(spectrumHighChanged(int8_t,int)), radio[r], SLOT(spectrumHighChanged(int8_t,int)));
            connect(radio[r]->rxp[x], SIGNAL(spectrumLowChanged(int8_t,int)), radio[r], SLOT(spectrumLowChanged(int8_t,int)));
            connect(radio[r]->rxp[x], SIGNAL(waterfallHighChanged(int8_t,int)), radio[r], SLOT(waterfallHighChanged(int8_t,int)));
            connect(radio[r]->rxp[x], SIGNAL(waterfallLowChanged(int8_t,int)), radio[r], SLOT(waterfallLowChanged(int8_t,int)));
            connect(radio[r]->rxp[x], SIGNAL(meterValue(int8_t,float,float,float)), this, SLOT(getMeterValue(int8_t,float,float,float)));
            connect(radio[r]->rxp[x], SIGNAL(squelchValueChanged(int8_t,int)), radio[r], SLOT(squelchValueChanged(int8_t,int)));
            connect(radio[r]->rxp[x], SIGNAL(statusMessage(QString)), this, SLOT(statusMessage(QString)));
            connect(radio[r]->rxp[x], SIGNAL(removeNotchFilter(int8_t)), radio[r], SLOT(removeNotchFilter(int8_t)));
            connect(widget.rxEqEnableCB, SIGNAL(toggled(bool)), this, SLOT(enableRxEq(bool)));
            connect(widget.actionBandscope, SIGNAL(triggered(bool)), this, SLOT(enableBandscope(bool)));
            connect(widget.tnfAddButton, SIGNAL(clicked()), radio[r]->rxp[x], SLOT(addNotchFilter(void)));
            connect(&configure, SIGNAL(avgSpinChanged(int)), radio[r]->rxp[x], SLOT(setAvg(int)));
            connect(&radio[r]->band[x], SIGNAL(printStatusBar(QString)), this, SLOT(printStatusBar(QString)));
            connect(radio[r], SIGNAL(printStatusBar(QString)), this, SLOT(printStatusBar(QString)));
            connect(this, SIGNAL(frequencyChanged(int8_t,long long)), radio[r], SLOT(frequencyChanged(int8_t,long long)));
            //    connect(connection, SIGNAL(setFPS()), this, SLOT(setFPS()));

            widget.spectrumLayout->addWidget((QWidget*)radio[r]->rxp[x]);

            widget.vfoFrame->setSelectedReceiver(QString("Receiver %1").arg(x));
            widget.vfoFrame->setCurrentReceiver(x);

            radio[r]->equalizer->currentRxRfstream = stream;
            radio[r]->rxp[x]->setAvg(configure.getAvg());

            qDebug("***********Initialized receiver structure for stream: %d\n", stream);
        }

        if (!radio[r]->radio_started)
        {
            radio[r]->initializeRadio();
            qDebug("**********Initialized radio hardware.\n");
        }

//        if (radio[r]->radio_started)
      //      static_cast<HermesFrame*>(radio[r]->hww)->currentRxRfstream = rfstream;

        // let them know who we are
        command.clear();
        command.append((char)stream);
        command.append((char)SETCLIENT);
        command.append("QtRadio");
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)STARTXCVR);
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)STARTIQ);
        connection.sendCommand(command);

        qDebug() << "Sent RX STARTIQ command.";

        command.clear();
        command.append((char)stream);
        command.append((char)STARCOMMAND);
        command.append((char)ATTACHRX);
        connection.sendCommand(command);

        if (!isConnected)
        {
            connect(radio[r], SIGNAL(updateFilterMenu(int)), this, SLOT(updateFilterMenu(int)));
            connect(radio[r], SIGNAL(updateFiltersMenu(int, FiltersBase*)), this, SLOT(updateFiltersMenu(int, FiltersBase*)));
            connect(radio[r], SIGNAL(updateModeMenu(int)), this, SLOT(updateModeMenu(int)));
            connect(radio[r], SIGNAL(updateBandMenu(int)), this, SLOT(updateBandMenu(int)));
            connect(&radio[r]->filters[x], SIGNAL(filtersChanged(int8_t,FiltersBase*,FiltersBase*)), radio[r], SLOT(filtersChanged(int8_t,FiltersBase*,FiltersBase*)));
            connect(&radio[r]->filters[x], SIGNAL(filterChanged(int8_t,int,int)), radio[r], SLOT(filterChanged(int8_t,int,int)));
        }

        connect(&radio[r]->band[x], SIGNAL(bandChanged(int8_t,int8_t,int,int)), radio[r], SLOT(bandChanged(int8_t,int8_t,int,int)));
        connect(&radio[r]->mode[x], SIGNAL(modeChanged(int8_t,int8_t,int,int)), radio[r], SLOT(modeChanged(int8_t,int8_t,int,int)));

        isConnected = true;
        frequency = radio[r]->frequency[x];

        // Check for receive/transmit pair
        if (txrxPair[1] == stream && txrxPair[0] > -1)
        {
            memcpy((char*)&radio[r]->rfstream[8], (char*)&connection.rfstream[txrxPair[0]], sizeof(RFSTREAM));
            radio[r]->currentTxRfstream = txrxPair[0];
            radio[r]->equalizer->currentTxRfstream = txrxPair[0];
            if (radio[r]->radio_started)
                static_cast<HermesFrame*>(radio[r]->hww)->currentTxRfstream = txrxPair[0];
            radio[r]->txp = new TxPanadapter();
            widget.txSpectrumView->setScene(radio[r]->txp->txpanadapterScene);
            connect(radio[r]->txp, SIGNAL(meterValue(int8_t, float, float, float)), this, SLOT(getMeterValue(int8_t, float, float, float)));
            connect(widget.txEqEnableCB, SIGNAL(toggled(bool)), this, SLOT(enableTxEq(bool)));
            qDebug("Initialized transmit structure.\n");

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)STARTXCVR);
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)STARTIQ);
            connection.sendCommand(command);

            qDebug() << "Sent TX STARTIQ command.";

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)STARCOMMAND);
            command.append((char)ATTACHTX);
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXBPASSWIN);
            command.append(QString("%1").arg(configure.getTxFilterWindow()));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXALCATTACK);
            command.append(QString("%1").arg(configure.getALCAttackValue()));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXALCDECAY);
            command.append(QString("%1").arg(configure.getALCDecayValue()));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXALCMAXGAIN);
            command.append(QString("%1").arg(configure.getALCMaxGainValue()));
            connection.sendCommand(command);

//            radio[r]->band[x].selectBand(BAND_15+100);
            radio[r]->txFrequency = frequency;
            txFrequency = frequency;

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETFREQ);
            command.append(QString("%1").arg(frequency));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETMODE);
            command.append((char)radio[r]->band[x].getMode());
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETFPS);
            command.append(QString("2000,%1").arg(radio[r]->fps[x]));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXLEVELERATTACK);
            command.append(QString("%1").arg(configure.getLevelerAttackValue()));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXLEVELERDECAY);
            command.append(QString("%1").arg(configure.getLevelerDecayValue()));
            connection.sendCommand(command);

            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETTXALCDECAY);
            command.append(QString("%1").arg(configure.getALCDecayValue()));
            connection.sendCommand(command);

            //QTextStream(&command) << "settxlevelerstate " << configure.getLevelerEnabledValue();

            connection.rfstream[txrxPair[0]].enabled = true;
        }

        radio[r]->band[x].setFrequency(frequency);

        command.clear();
        command.append((char)stream);
        command.append((char)SETFREQ);
        command.append(QString("%1").arg(frequency));
        connection.sendCommand(command);

        //    gvj code
        widget.vfoFrame->setFrequency(frequency);
        getBandFrequency();
        updateBandMenu(radio[r]->band[x].getBand());

        command.clear();
        command.append((char)stream);
        command.append((char)SETMODE);
        command.append((char)radio[r]->band[x].getMode());
        connection.sendCommand(command);

        updateModeMenu(radio[r]->band[x].getMode());
  //      radio[r]->filters.selectFilters(x, &lsbFilters);

        int low,high;
        if (radio[r]->mode[x].getMode() == MODE_CWL)
        {
            low = -radio[r]->cwPitch - radio[r]->filters[x].getLow();
            high = -radio[r]->cwPitch + radio[r]->filters[x].getHigh();
        }
        else
            if (radio[r]->mode[x].getMode() == MODE_CWU)
            {
                low = radio[r]->cwPitch - radio[r]->filters[x].getLow();
                high = radio[r]->cwPitch + radio[r]->filters[x].getHigh();
            }
            else
            {
                low = radio[r]->filters[x].getLow();
                high = radio[r]->filters[x].getHigh();
            }
        command.clear();
        command.append((char)stream);
        command.append((char)SETFILTER);
        command.append(QString("%1,%2").arg(low).arg(high));
        connection.sendCommand(command);

        radio[r]->rxp[x]->setFilter(low, high);
        updateFilterMenu(radio[r]->filters[x].getFilter());

        // start the audio
        audio_buffers=0;
        actionGain(gain);

 //       if (!getenv("QT_RADIO_NO_LOCAL_AUDIO"))  //FIXME: this probably needs to be changed. Not exactly sure which audio this refers to.
        {
            command.clear();
            command.append((char)stream);
            command.append((char)STARTAUDIO);
            int tmp = 0;
            if (local_mic)
                tmp = 1;
            command.append(QString("%1,%2,%3,%4,%5").arg(AUDIO_BUFFER_SIZE*(audio_sample_rate/8000)).arg(audio_sample_rate).arg(audio_channels).arg(audioinput->getMicEncoding()).arg(tmp));
            connection.sendCommand(command);
        }

        radio[r]->setFPS(x);

        // select audio encoding
        command.clear();
        command.append((char)stream);
        command.append((char)SETENCODING);
        command.append(audio->get_audio_encoding());
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETPAN);
        command.append(QString("%1").arg(0.5));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETRXAAGCMODE);
        command.append(agc);
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETANFVALS);
        command.append(QString("%1,%2,%3,%4").arg(configure.getAnfTaps()).arg(configure.getAnfDelay()).arg(configure.getAnfGain(), 0, 'f').arg(configure.getAnfLeak(), 0, 'f'));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETNRVALS);
        command.append(QString("%1,%2,%3,%4").arg(configure.getNrTaps()).arg(configure.getNrDelay()).arg(configure.getNrGain(), 0, 'f').arg(configure.getNrLeak(), 0, 'f'));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETNBVAL);
        command.append(QString("%1").arg(configure.getNbThreshold()));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(radio[r]->squelchValue[x]));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETSQUELCHSTATE);
        command.append((int)widget.actionSquelchEnable->isChecked());
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETANF);
        command.append((int)widget.actionANF->isChecked());
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETNR);
        command.append((int)widget.actionNR->isChecked());
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETNB);
        command.append((int)widget.actionNB->isChecked());
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETRXBPASSWIN);
        command.append(QString("%1").arg(configure.getRxFilterWindow()));
        connection.sendCommand(command);

        windowTypeChanged(configure.getWindowType());

        printWindowTitle("Remote connected");

        qDebug("Sending advanced setup commands.");

        /*
    command.clear();
    QTextStream(&command) << "setrxagcmaxgain " << configure.getRxAGCMaxGainValue();
    connection.sendCommand(command);
*/
        command.clear();
        command.append((char)stream);
        command.append((char)SETRXAGCATTACK);
        command.append(QString("%1").arg(configure.getRxAGCAttackValue()));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETRXAGCDECAY);
        command.append(QString("%1").arg(configure.getRxAGCDecayValue()));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETRXAGCHANG);
        command.append(QString("%1").arg(configure.getRxAGCHangValue()));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETRXAGCFIXED);
        command.append(QString("%1").arg(configure.getRxAGCFixedGainValue()));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)SETRXAGCHANGTHRESH);
        command.append(QString("%1").arg(configure.getRxAGCHangThreshValue()));
        connection.sendCommand(command);

        command.clear();
        command.append((char)stream);
        command.append((char)QUESTION);
        command.append((char)QINFO);
        connection.sendCommand(command);

        connection.rfstream[stream].enabled = true;

        radio[r]->rxp[x]->currentRfstream = currentRxRfstream;
        radio[r]->rxp[x]->setFrequency(frequency);
        radio[r]->rxp[x]->enableNotchFilter(false);
        radio[r]->rxp[x]->panadapterScene->waterfallItem->bConnected = true;

        radio[r]->sampleRateChanged(x, connection.sample_rate);

//        radio[r]->band[x].selectBand(BAND_15+100);

        current_index = x;
        x++;
    } //for

    audioConnection.connect(configure.getHost(), DSPSERVER_BASE_PORT+10);
    micAudioConnection.connect(configure.getHost(), DSPSERVER_BASE_PORT+20);
    audio->select_audio(audio_device, audio_sample_rate, audio_channels, audio_byte_order);

    widget.actionConnectToServer->setDisabled(true);
    widget.actionDisconnectFromServer->setDisabled(false);

    configure.connected(true);
    connection_valid = true;

    widget.zoomSpectrumSlider->setValue(1);
    on_zoomSpectrumSlider_sliderMoved(1);

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)ENABLEAUDIO);
    command.append((char)true);
    connection.sendCommand(command);
    qDebug("Radio fully initialized.\n");
    kickDisplay.start(1000);
} // end connected


void UI::disconnected(QString message)
{
    qDebug() << "UI::disconnected: " << message;
    pttChange(0, false);

    connection_valid = false;
    isConnected = false;
    configure.thisuser = "none";
    configure.thispass = "none";
    servername = "";
    canTX = true;
    chkTX = false;
    loffset = 0;
    currentRxRfstream = -1;
    currentTxRfstream = -1;
    current_index = -1;

    for (int r=0;r<MAX_RADIOS;r++)
    {
        if (radio[r] == NULL) continue;
        if (radio[r]->hardwareType != "" && radio[r]->hww != NULL)
        {
            widget.RadioScrollAreaWidgetContents->layout()->removeWidget(radio[r]->hww);
            radio[r]->hww->deleteLater();
            radio[r]->hardwareType.clear();
            radio[r]->rigCtl->deleteLater();
        }

        for (int i=0;i<MAX_RECEIVERS;i++)
        {
            if (radio[r]->receiver_active[i])
            {
                if (radio[r]->rxp[i] != NULL)
                {
                    radio[r]->rxp[i]->disconnect();
                    widget.spectrumLayout->removeWidget(radio[r]->rxp[i]);
                    widget.spectrumLayout->update();
                    delete radio[r]->rxp[i];
                }
                receiver_active[i] = false;
            }
        }
        delete radio[r];
        radio[r] = NULL;
    }

    audio->clear_decoded_buffer();

    widget.sMeterFrame->meter0 = -121;
    widget.sMeterFrame->meter1 = -121;
    widget.sMeterFrame->meter2 = -121;
    widget.sMeterFrame->update();

    //    widget.statusbar->showMessage(message,0); //gvj deleted code
    printWindowTitle(message);
    widget.actionConnectToServer->setDisabled(false);
    widget.actionDisconnectFromServer->setDisabled(true);

    configure.connected(false);

    widget.ctlFrame->HideTX(true);
    widget.spectrumLayout->addWidget(widget.titlelabel);
    widget.spectrumLayout->update();
} // end disconnected


void UI::spectrumBuffer(RFSTREAM rfstream)
{
   //qDebug()<<Q_FUNC_INFO << "spectrumBuffer";
    if (current_index < 0) return;
    int8_t index = radio[rfstream.radio.radio_id]->getInternalIndex(rfstream.index);
    radio[rfstream.radio.radio_id]->sampleRate[index] = rfstream.spectrum.sample_rate;
  //  qDebug("Index: %d  SampR: %d  Meter: %f\n", index, rfstream.spectrum.sample_rate, rfstream.spectrum.meter);
    if (txNow && rfstream.isTX && radio[rfstream.radio.radio_id]->txp != NULL)
        radio[rfstream.radio.radio_id]->txp->updateSpectrumFrame(rfstream.spectrum);
    else
        if (radio[rfstream.radio.radio_id]->rxp[index] != NULL)
            radio[rfstream.radio.radio_id]->rxp[index]->updateSpectrumFrame(rfstream.spectrum);
    spectrumConnection.freeBuffers(rfstream.spectrum);
} // end spectrumBuffer


void UI::audioBuffer(char* header, char* buffer)
{
    if (current_index < 0) return;
    //qDebug() << "audioBuffer";
    int length;
    // g0orx binary header
    if (header[2] == receiver_rfstream[current_index]) // only process currently selected receive rfstream.
    {
//        qDebug("rfstream: %d   ch: %d\n", header[2], receiver_rfstream[current_index]);
        length = ((header[3] & 0xFF) << 8) + (header[4] & 0xFF);
        emit process_audio(header, buffer, length);
    }
} // end audioBuffer


void UI::micSendAudio(QQueue<qint16>* queue)
{
    if (!txNow || currentTxRfstream < 0)
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
                    micAudioConnection.sendAudio(currentTxRfstream, 512, mic_encoded_buffer);
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


void UI::enableBandscope(bool enable)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->enableBandscope(&spectrumConnection, enable);
} // end enableBandscope


void UI::bandScopeClosed()
{
    widget.actionBandscope->setChecked(false);
} // end bandScopeClosed


void UI::actionKeypad()
{
    keypad.clear();
    keypad.show();
} // end actionKeypad


void UI::setKeypadFrequency(long long f)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setFrequency(f);
} // end setKeypadFrequency


void UI::getBandBtn(int btn)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(btn+100);// +100 is used as a flag to indicate call came from vfo band buttons
} // end getBandBtn


void UI::quickMemStore()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].quickMemStore();
} // end quickMemStore


void UI::action160()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_160);
//    band.selectBand(BAND_160);
}

void UI::action80()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_80);
//    band.selectBand(BAND_80);
}

void UI::action60()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_60);
//    band.selectBand(BAND_60);
}

void UI::action40()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_40);
//    band.selectBand(BAND_40);
}

void UI::action30()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_30);
//    band.selectBand(BAND_30);
}

void UI::action20()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_20);
//    band.selectBand(BAND_20);
}

void UI::action17()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_17);
//    band.selectBand(BAND_17);
}

void UI::action15()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_15);
//    band.selectBand(BAND_15);
}

void UI::action12()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_12);
//    band.selectBand(BAND_12);
}

void UI::action10()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_10);
//    band.selectBand(BAND_10);
}

void UI::action6()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_6);
//    band.selectBand(BAND_6);
}

void UI::actionGen()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_GEN);
//    band.selectBand(BAND_GEN);
}

void UI::actionWWV()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(BAND_WWV);
//    band.selectBand(BAND_WWV);
}



void UI::actionCWL()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true; //Signals menu selection of mode so we use the default filter
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_CWL);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&cwlFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_CWL);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
} // end actionCWL


void UI::actionCWU()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_CWU);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&cwuFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_CWU);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
} // end actionCWU


void UI::actionLSB()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_LSB);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&lsbFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_LSB);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionUSB()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_USB);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&usbFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_USB);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionDSB()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_DSB);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&dsbFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_DSB);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionAM()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_AM);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&amFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_AM);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionSAM()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_SAM);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&samFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_SAM);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionFMN()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_FM);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&fmnFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_FM);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionDIGL()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_DIGL);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&diglFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_DIGL);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionDIGU()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = true;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], MODE_DIGU);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilters(&diguFilters);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setMode(MODE_DIGU);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->modeFlag[current_index] = false;
}


void UI::actionFilter0()
{
//    filters.selectFilter(0);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(0);
}


void UI::actionFilter1()
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(1);
}


void UI::actionFilter2()
{
//    filters.selectFilter(2);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(2);
}


void UI::actionFilter3()
{
//    filters.selectFilter(3);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(3);
}


void UI::actionFilter4()
{
//    filters.selectFilter(4);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(4);
}


void UI::actionFilter5()
{
//    filters.selectFilter(5);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(5);
}


void UI::actionFilter6()
{
//    filters.selectFilter(6);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(6);
}


void UI::actionFilter7()
{
//    filters.selectFilter(7);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(7);
}


void UI::actionFilter8()
{
//    filters.selectFilter(8);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(8);
}


void UI::actionFilter9()
{
//    filters.selectFilter(9);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(9);
}


void UI::actionFilter10()
{
//    filters.selectFilter(10);
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(10);
}


void UI::updateModeMenu(int mode)
{
    widget.actionCWL->setChecked(false);
    widget.actionCWU->setChecked(false);
    widget.actionLSB->setChecked(false);
    widget.actionUSB->setChecked(false);
    widget.actionDSB->setChecked(false);
    widget.actionAM->setChecked(false);
    widget.actionSAM->setChecked(false);
    widget.actionFMN->setChecked(false);
    widget.actionDIGL->setChecked(false);
    widget.actionDIGU->setChecked(false);

    switch (mode)
    {
    case MODE_CWL:
        widget.actionCWL->setChecked(true);
        break;
    case MODE_CWU:
        widget.actionCWU->setChecked(true);
        break;
    case MODE_LSB:
        widget.actionLSB->setChecked(true);
        break;
    case MODE_USB:
        widget.actionUSB->setChecked(true);
        break;
    case MODE_DSB:
        widget.actionDSB->setChecked(true);
        break;
    case MODE_AM:
        widget.actionAM->setChecked(true);
        break;
    case MODE_SAM:
        widget.actionSAM->setChecked(true);
        break;
    case MODE_FM:
        widget.actionFMN->setChecked(true);
        break;
    case MODE_DIGL:
        widget.actionDIGL->setChecked(true);
        break;
    case MODE_DIGU:
        widget.actionDIGU->setChecked(true);
        break;
    }
} // end updateModeMenu


void UI::updateBandMenu(int band)
{
    widget.action160->setChecked(false);
    widget.action80->setChecked(false);
    widget.action60->setChecked(false);
    widget.action40->setChecked(false);
    widget.action30->setChecked(false);
    widget.action20->setChecked(false);
    widget.action17->setChecked(false);
    widget.action15->setChecked(false);
    widget.action12->setChecked(false);
    widget.action10->setChecked(false);
    widget.action6->setChecked(false);
    widget.actionGen->setChecked(false);
    widget.actionWWV->setChecked(false);

    // check new band
    switch (band)
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
    widget.vfoFrame->checkBandBtn(band);
} // end updateBandMenu


void UI::updateFiltersMenu(int filter, FiltersBase* newFilters)
{
    widget.actionFilter_0->setChecked(false);
    widget.actionFilter_1->setChecked(false);
    widget.actionFilter_2->setChecked(false);
    widget.actionFilter_3->setChecked(false);
    widget.actionFilter_4->setChecked(false);
    widget.actionFilter_5->setChecked(false);
    widget.actionFilter_6->setChecked(false);
    widget.actionFilter_7->setChecked(false);
    widget.actionFilter_8->setChecked(false);
    widget.actionFilter_9->setChecked(false);
    widget.actionFilter_10->setChecked(false);

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

    switch (filter)
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
} // end updateFiltersMenu


void UI::updateFilterMenu(int filter)
{
    widget.actionFilter_0->setChecked(false);
    widget.actionFilter_1->setChecked(false);
    widget.actionFilter_2->setChecked(false);
    widget.actionFilter_3->setChecked(false);
    widget.actionFilter_4->setChecked(false);
    widget.actionFilter_5->setChecked(false);
    widget.actionFilter_6->setChecked(false);
    widget.actionFilter_7->setChecked(false);
    widget.actionFilter_8->setChecked(false);
    widget.actionFilter_9->setChecked(false);
    widget.actionFilter_10->setChecked(false);

    switch (filter)
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
} // end updateFilterMenu


void UI::actionANF()
{
    if (current_index < 0) return;
    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETANF);
    command.append(widget.actionANF->isChecked());
    connection.sendCommand(command);
} // end actionANF


void UI::actionNR()
{
    if (current_index < 0) return;
    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETNR);
    command.append(widget.actionNR->isChecked());
    connection.sendCommand(command);
} // end actionNR


void UI::actionNB()
{
    if (current_index < 0) return;
    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETNB);
    command.append(widget.actionNB->isChecked());
    connection.sendCommand(command);
} // end actionNB


void UI::actionSDROM()
{
    if (current_index < 0) return;
    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETNB2);
    command.append(widget.actionSDROM->isChecked());
    connection.sendCommand(command);
} // end actionSDROM


void UI::actionFixed()
{
    if (current_index < 0) return;
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
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAAGCMODE);
    command.append((char)agc);
    connection.sendCommand(command);
    //AGCTLevelChanged(widget.agcTLevelSlider->value());
} // end actionFixed


void UI::actionSlow()
{
    if (current_index < 0) return;
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
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAAGCMODE);
    command.append((char)agc);
    connection.sendCommand(command);
} // end actionSlow


void UI::actionMedium()
{
    if (current_index < 0) return;
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
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAAGCMODE);
    command.append((char)agc);
    connection.sendCommand(command);
} // end actionMedium


void UI::actionFast()
{
    if (current_index < 0) return;
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
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAAGCMODE);
    command.append((char)agc);
    connection.sendCommand(command);
}  // end actionFast


void UI::actionLong()
{
    if (current_index < 0) return;
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
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAAGCMODE);
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


void UI::actionRecord()
{
    QString command;
    command.clear();
    QTextStream(&command) << "record " << (widget.actionRecord->isChecked()?"on":"off");
    //connection.sendCommand(command);
}


void UI::actionGain(int g)
{
    if (current_index < 0) return;
    QByteArray command;
    //    setGain(false);
    gain = g;
    //    setGain(true);
    command.clear();
    command.append((char)currentRxRfstream);
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


void UI::nrValuesChanged(int taps, int delay, double gain, double leakage)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETNRVALS);
    command.append(QString("%1,%2,%3,%4").arg(taps).arg(delay).arg(gain, 0, 'f').arg(leakage, 0, 'f'));
    connection.sendCommand(command);
} // end nrValuesChanged


void UI::anfValuesChanged(int taps, int delay, double gain, double leakage)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETANFVALS);
    command.append(QString("%1,%2,%3,%4").arg(taps).arg(delay).arg(gain, 0, 'f').arg(leakage, 0, 'f'));
    connection.sendCommand(command);
} // end anfValuesChanged


void UI::nbThresholdChanged(double threshold)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETNBVAL);
    command.append(QString("%1").arg(threshold));
    connection.sendCommand(command);
} // end nbThresholdChanged


void UI::cessbOvershootChanged(bool enable)
{
    if (current_index < 0) return;
    QByteArray command;

    if (currentTxRfstream > -1)
    {
        command.clear();
        command.append((char)currentTxRfstream);
        command.append((char)SETTXAOSCTRLRUN);
        command.append((char)enable);
        connection.sendCommand(command);
    }
} // end cessOvershootChanged


void UI::aeFilterChanged(bool enable)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAEMNREARUN);
    command.append((char)enable);
    connection.sendCommand(command);
} // end aeFilterChanged


void UI::nrGainMethodChanged(int method)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAEMNRGAINMETHOD);
    command.append((char)method);
    connection.sendCommand(command);
} // end nrGainMethodChanged


void UI::nrNpeMethodChanged(int method)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAEMNRNPEMETHOD);
    command.append((char)method);
    connection.sendCommand(command);
} // end nrNpeMethodChanged


void UI::preAGCFiltersChanged(bool tmp)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    if (tmp)
    {
        command.append((char)currentRxRfstream);
        command.append((char)SETRXAANFPOSITION);
        command.append((char)!tmp);
        connection.sendCommand(command);
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETRXAANRPOSITION);
        command.append((char)!tmp);
        connection.sendCommand(command);
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETRXAEMNRPOSITION);
        command.append((char)!tmp);
        connection.sendCommand(command);
    }
    else
    {
        command.append((char)currentRxRfstream);
        command.append((char)SETRXAANFPOSITION);
        command.append((char)!tmp);
        connection.sendCommand(command);
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETRXAANRPOSITION);
        command.append((char)!tmp);
        connection.sendCommand(command);
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETRXAEMNRPOSITION);
        command.append((char)!tmp);
        connection.sendCommand(command);
    }
} // preAGCFilterChanged


void UI::actionBookmark()
{
    QString strFrequency=stringFrequency(frequency);
    bookmarkDialog.setTitle(strFrequency);
//    bookmarkDialog.setBand(band.getStringBand());
    bookmarkDialog.setFrequency(strFrequency);
//    bookmarkDialog.setMode(mode.getStringMode());
//    bookmarkDialog.setFilter(filters.getText());
    bookmarkDialog.show();
}


void UI::addBookmark()
{
    qDebug() << "addBookmark";
    Bookmark* bookmark=new Bookmark();
    bookmark->setTitle(bookmarkDialog.getTitle());
//    bookmark->setBand(band.getBand());
//    bookmark->setFrequency(band.getFrequency());
//    bookmark->setMode(mode.getMode());
//    bookmark->setFilter(filters.getFilter());
    bookmarks.add(bookmark);
    bookmarks.buildMenu(widget.menuView_Bookmarks);
}


void UI::selectBookmark(QAction* action)
{
    if (current_index < 0) return;
    bookmarks.select(action);

    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].selectBand(bookmarks.getBand());

    frequency = bookmarks.getFrequency();
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setFrequency(frequency);

    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setFrequency(frequency);

    //    gvj code
    widget.vfoFrame->setFrequency(frequency);

    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].setMode(current_index, receiver_rfstream[current_index], bookmarks.getMode());

    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].selectFilter(bookmarks.getFilter());
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
        bookmarksEditDialog->setBand(radio[connection.rfstream[current_index].radio.radio_id]->band[current_index].getStringBand(bookmark->getBand()));
        bookmarksEditDialog->setFrequency(stringFrequency(bookmark->getFrequency()));
        bookmarksEditDialog->setMode(radio[connection.rfstream[current_index].radio.radio_id]->mode[current_index].getStringMode(bookmark->getMode()));

        switch(bookmark->getMode()) {
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


void UI::getMeterValue(int8_t index, float s, float f, float r)
{
    if (index != current_index) return;
 //   qDebug("sMeter: %f\n", m);
    widget.sMeterFrame->meter0 = s;
    widget.sMeterFrame->meter1 = f;
    widget.sMeterFrame->meter2 = r;
    widget.sMeterFrame->update();
}


void UI::printWindowTitle(QString message)
{
    if (message.compare("Remote disconnected") == 0)
    {
        dspversion = 0;
        dspversiontxt = "";
    }
    setWindowTitle("QtRadioNG - Server: " + servername + " " + configure.getHost() + "(Rx "
                   + QString::number(configure.getReceiver()) +") .. "
                   + getversionstring() +  message + "  [" + QString("Qt: %1").arg(QT_VERSION, 0, 16) + "]  16 Jan 2019");
    lastmessage = message;
}


void UI::printStatusBar(QString message)
{
    if (current_index < 0) return;
    Frequency freqInfo;
    static QString description;
    static long long lastFreq;

    if (lastFreq != radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].getFrequency())
        description = freqInfo.getFrequencyInfo(radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].getFrequency()).getDescription();

    modeInfo.setText(description + "  " + radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].getStringMem()+
            ", " + radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].getStringMode()+", " +
            radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->filters[current_index].getText()+message);
    lastFreq = frequency;
}


void UI::slaveSetMode(int m)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rigctlSetMode(m);
}


void UI::slaveSetFrequency(long long freq)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].setFrequency(freq);
}


void UI::slaveSetFilter(int low, int high)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setFilter(low, high);
    //    widget.waterfallView->setFilter(low,high);
}


void UI::slaveSetZoom(int position)
{
    if (current_index < 0) return;
    widget.zoomSpectrumSlider->setValue(position);
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setZoom(position);
    //   widget.waterfallView->setZoom(position);
}


void UI::getBandFrequency()
{
    if (current_index < 0) return;
    widget.vfoFrame->setBandFrequency(radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->band[current_index].getFrequency());
}


void UI::enableRxEq(bool enable)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->enableRxEq(current_index, enable);
}


void UI::enableTxEq(bool enable)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->enableTxEq(current_index, enable);
}


void UI::updateVFO(long long f)
{
    widget.vfoFrame->setFrequency(f);
    getBandFrequency();
}


void UI::frequencyMoved(int fi, int fs)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->frequencyMoved(current_index, fi, fs);
    resetBandedges(radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->loffset);
}


void UI::frequencyChanged(long long f)
{
    if (current_index < 0) return;
    emit frequencyChanged(current_index, f);
}


void UI::vfoStepBtnClicked(int direction)
{
    if (current_index < 0) return;
    long long f;

    int samplerate = radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->samplerate();

    //qDebug()<<Q_FUNC_INFO<<": vfo up or down button clicked. Direction = "<<direction<<", samplerate = "<<samplerate;
    switch (samplerate)
    {
    case   24000 : f =   20000; break;
    case   48000 : f =   40000; break;
    case   96000 : f =   80000; break;
    case  192000 : f =  160000; break;
    case  384000 : f =  320000; break;
    case  768000 : f =  640000; break;
    case 1536000 : f = 1280000; break;

    default : f = (samplerate * 8) / 10;
    }
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->frequencyMoved(current_index, f, direction);
}


void UI::ctlSetPTT(bool enabled)
{
    widget.ctlFrame->RigCtlTX(enabled);
} // end ctlSetPTT


// The ptt service has been activated. Caller values, 0 = MOX, 1 = Tune, 2 = VOX, 3 = Extern H'ware
void UI::pttChange(int caller, bool ptt)
{
    if (current_index < 0) return;
    QByteArray command;
    static int workingMode;
//    static double currentPwr;

    if (currentTxRfstream < 0)
        return;

    if (caller == 1)
        emit tuningEnable(ptt);

    if (configure.getTxAllowed())
    {
        if (ptt)
        {    // Going from Rx to Tx ................
            radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->panadapterScene->bMox = true;
            delete widget.sMeterFrame->sMeterMain;
            widget.sMeterFrame->sMeterMain = new Meter("Main Pwr", POWMETER);
            workingMode = radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->mode[current_index].getMode(); //Save the current mode for restoration when we finish tuning
            if (caller == 1)
            { //We have clicked the tune button so switch to AM and set carrier level
      //          currentPwr = (double)widget.ctlFrame->getTxPwr();
     /////           workingMode = mode.getMode(); //Save the current mode for restoration when we finish tuning
                actionAM();
                // Set the AM carrier level to match the tune power slider value in a scale 0 to 1.0
                if ((dspversion >= 20120201)  && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)SETTXAMCARLEV);
     //////               command.append(QString("%1 %2 %3").arg(widget.ctlFrame->getTunePwr()).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)SETTXAMCARLEV);
    //////                command.append(QString("%1").arg(widget.ctlFrame->getTunePwr()));
                }
    //////            connection.sendCommand(command);
                //Mute the receiver audio and freeze the spectrum and waterfall display
                connection.setMuted(true);
                //Key the radio
                if ((dspversion >= 20130901) && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(1).arg(configure.thisuser).arg(configure.thispass));
                    //QTextStream(&command) << "Mox " << "on " << configure.thisuser << " " << configure.thispass;
                }
                else
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
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
                    command.append((char)currentTxRfstream);
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(1).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
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
                    command.append((char)currentTxRfstream);
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(0).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)MOX);
                    command.append(QString("%1").arg(0));
                }
                connection.sendCommand(command);
                radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->panadapterScene->bMox = false;

                //Restore AM carrier level to previous level.
                if ((dspversion >= 20120201) && canTX && chkTX)
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)SETTXAMCARLEV);
                    command.append(QString("%1 %2 %3").arg(currentPwr).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)SETTXAMCARLEV);
                    command.append(QString("%1").arg(radio[connection.rfstream[current_index].radio.radio_id]->currentPwr));
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
                    command.append((char)currentTxRfstream);
                    command.append((char)MOX);
                    command.append(QString("%1 %2 %3").arg(0).arg(configure.thisuser).arg(configure.thispass));
                }
                else
                {
                    command.clear();
                    command.append((char)currentTxRfstream);
                    command.append((char)MOX);
                    command.append(QString("%1").arg(0));
                }
                connection.sendCommand(command);
                radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->panadapterScene->bMox = false;
            }
            txNow = false;

            //Un-mute the receiver audio
            connection.setMuted(false);
            widget.vfoFrame->pttChange(ptt); //Set band select buttons etc. to Rx state on VFO
            disconnect(audioinput, SIGNAL(mic_update_level(qreal)),widget.ctlFrame, SLOT(update_mic_level(qreal)));
            SPECTRUM spec;
            spec.length = 0;
            radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->txp->updateSpectrumFrame(spec);
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
        current_index = -1;
        QuickIP = IP;
        configure.addHost(IP);
        connection.connect(IP, DSPSERVER_BASE_PORT+configure.getReceiver());
//        rxp[0][0]->setReceiver(configure.getReceiver());
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
    if (current_index < 0) return;
    if (squelch)
    {
        squelch = false;
        QByteArray command;
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETSQUELCHSTATE);
        command.append((char)0);
        connection.sendCommand(command);
        radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setSquelch(true);
        widget.actionSquelchEnable->setChecked(false);
    }
    else
    {
        squelch = true;
        QByteArray command;
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue));
        connection.sendCommand(command);
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETSQUELCHSTATE);
        command.append((char)1);
        connection.sendCommand(command);
        radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setSquelch(true);
        radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setSquelchVal(squelchValue);
        widget.actionSquelchEnable->setChecked(true);
    }
}


void UI::actionSquelchReset()
{
    if (current_index < 0) return;
    squelchValue = -100;
    if (squelch)
    {
        QByteArray command;
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue));
        connection.sendCommand(command);
        radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setSquelchVal(squelchValue);
    }
}


void UI::squelchValueChanged(int val)
{
    if (current_index < 0) return;
    squelchValue = squelchValue+val;
    if (squelch)
    {
        QByteArray command;
        command.clear();
        command.append((char)currentRxRfstream);
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue));
        connection.sendCommand(command);
        radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setSquelchVal(squelchValue);
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


void UI::cwPitchChanged(int cwPitch)
{
    if (current_index < 0) return;
    radio[connection.rfstream[currentTxRfstream].radio.radio_id]->cwPitch = cwPitch;
    if (isConnected)
    {
        long long frequency = radio[connection.rfstream[currentTxRfstream].radio.radio_id]->frequency[current_index];
        radio[connection.rfstream[currentTxRfstream].radio.radio_id]->filters[current_index].selectFilter(radio[connection.rfstream[currentTxRfstream].radio.radio_id]->filters[current_index].getFilter()); //Dummy call to centre filter on tone
        radio[connection.rfstream[currentTxRfstream].radio.radio_id]->frequencyChanged(currentTxRfstream, frequency); //Dummy call to set freq into correct place in filter
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


void UI::on_zoomSpectrumSlider_sliderMoved(int position)
{
    if (current_index < 0) return;
    viewZoomLevel = position;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->rxp[current_index]->setZoom(position);
}


void UI::fpsChanged(int fps)
{
    if (current_index < 0) return;
    radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->fpsChanged(current_index, fps);
}


void UI::windowTypeChanged(int type)
{
    if (current_index < 0) return;
    QByteArray command;

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETWINDOW);
    command.append(QString("%1").arg(type));
    connection.sendCommand(command);
    if (currentTxRfstream > -1)
    {
        command.clear();
        command.append((char)currentTxRfstream);
        command.append((char)SETWINDOW);
        command.append(QString("%1").arg(type));
        connection.sendCommand(command);
    }
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
    if (current_index < 0) return;
    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCFIXED);
    command.append(QString("%1").arg(level));
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
//    widget.agcTLevelLabel->setText(QString("%1").arg(level));
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
    if (current_index < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCSLOPE);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcMaxGainChanged(double value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCTOP);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcAttackChanged(int value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCATTACK);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcDecayChanged(int value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCDECAY);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcHangChanged(int value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCHANG);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcHangThreshChanged(int value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCHANGTHRESH);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::agcFixedGainChanged(double value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAGCFIXED);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerStateChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXLEVELERST);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::rxFilterWindowChanged(int value)
{
    if (current_index < 0) return;
    if (!newDspServerCheck()) return;

    QByteArray command;
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXBPASSWIN);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)RXANBPSETWINDOW);
    command.append((char)value);
    connection.sendCommand(command);
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETRXAEQWINTYPE);
    command.append((char)value);
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::txFilterWindowChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXBPASSWIN);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXAEQWINTYPE);
    command.append((char)value);
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerAttackChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXLEVELERATTACK);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerDecayChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXLEVELERDECAY);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::levelerTopChanged(double value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXLEVELERTOP);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::TXalcStateChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXALCST);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::TXalcMaxGainChanged(double value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXALCMAXGAIN);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::TXalcDecayChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXALCDECAY);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::TXalcAttackChanged(int value)
{
    if (!newDspServerCheck() || currentTxRfstream < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)currentTxRfstream);
    command.append((char)SETTXALCATTACK);
    command.append(QString("%1").arg(value));
    connection.sendCommand(command);
    qDebug()<<Q_FUNC_INFO<<":   The command sent is "<< command;
}


void UI::setCurrentRfstream(int rfstream)
{
    currentRxRfstream = rfstream;
} // end setCurrentRfstream


void UI::resetBandedges(double f)
{
    if (current_index > -1)
        radio[connection.rfstream[receiver_rfstream[current_index]].radio.radio_id]->resetbandedges(current_index, f);
}


Radio::Radio(int8_t index, ServerConnection *connection)
{
    initRigCtl(19090+index);
    fprintf(stderr, "rigctl: Calling init\n");

    radio_index = index;
    for (int i=0;i<MAX_RFSTREAMS;i++)
    {
        if (connection->rfstream[i].radio.radio_id == index)
        {
            hardwareType = connection->rfstream[i].radio.radio_type;
            break;
        }
    }
    radio_started = false;
    bandscope = NULL;
    txp = NULL;
    currentTxRfstream = -1;
    currentRxRfstream = -1;
    activeReceivers = 0;
    QSettings settings("FreeSDR", "QtRadioII");
    for (int i=0;i<MAX_RECEIVERS;i++)
    {
        band[i].loadSettings(&settings);
        receiver_active[i] = false;
        receiver_rfstream[i] = -1;
        fps[i] = 15;
        notchFilterIndex[i] = 0;
        frequency[i] = 14000000;
        rxp[i] = NULL;
        if (settings.contains("squelch"))
            squelchValue[i] = settings.value("squelch").toInt();
    }

    equalizer = new EqualizerDialog(connection);

    settings.beginGroup("AudioEqualizer");
    if (settings.contains("eqMode"))
    {
        if (settings.value("eqMode") == 3)
            equalizer->loadSettings3Band();
        else
            equalizer->loadSettings10Band();
    }
    else
    {
        settings.setValue("eqMode", 10);
        equalizer->set10BandEqualizer();
    }
    settings.endGroup();
}


Radio::~Radio()
{
    if (hardwareType == "hermes")
        static_cast<HermesFrame*>(hww)->disconnect();
    this->disconnect();
}


void Radio::saveSetting(QString group, QString key, QString value, int8_t index)
{
    QSettings settings("FreeSDR", "QtRadioII");

    settings.beginGroup(QString("radio%1_index%2_%3").arg(radio_index).arg(rfstream[index].index).arg(group));
    settings.setValue(key, value);
    settings.endGroup();
} // end saveSetting


QString Radio::loadSetting(QString group, QString key, int8_t index)
{
    QSettings settings("FreeSDR", "QtRadioII");
    QString value = "na";

    settings.beginGroup(QString("radio%1_index%2_%3").arg(radio_index).arg(rfstream[index].index).arg(group));
    if (settings.contains(key))
        value = settings.value(key).toString();
    settings.endGroup();
    return value;
} // end loadSetting


void Radio::saveAllSettings()
{
    for (int8_t index=0;index<activeReceivers;index++)
    {
        QApplication::processEvents();
        saveSetting(QString("signal"), QString("mode"), QString("%1").arg(mode[index].getMode()), index);
        saveSetting(QString("signal"), QString("filterlow"), QString("%1").arg(rxp[index]->filterLow), index);
        saveSetting(QString("signal"), QString("filterhigh"), QString("%1").arg(rxp[index]->filterHigh), index);
        saveSetting(QString("signal"), QString("lastfrequency"), QString("%1").arg(frequency[index]), index);
        QApplication::processEvents();
        saveSetting(QString("display"), QString("spectrumhigh"), QString("%1").arg(rxp[index]->getHigh()), index);
        saveSetting(QString("display"), QString("spectrumlow"), QString("%1").arg(rxp[index]->getLow()), index);
        saveSetting(QString("display"), QString("waterfallhigh"), QString("%1").arg(rxp[index]->panadapterScene->waterfallItem->getHigh()), index);
        saveSetting(QString("display"), QString("waterfalllow"), QString("%1").arg(rxp[index]->panadapterScene->waterfallItem->getLow()), index);
        QApplication::processEvents();
        saveSetting(QString("audio"), QString("squelch"), QString("%1").arg(squelchValue[index]), index);
    }
} // end saveAllSettings


void Radio::sendCommand(QByteArray command)
{
    emit send_spectrum_command(command);
} // end sendCommand


int8_t Radio::getInternalIndex(int8_t rfstream_index)
{
    for (int8_t i=0;i<MAX_RECEIVERS;i++)
        if (rfstream[i].id == rfstream_index)
            return i;
    return -1;
} // end getInternalIndex


void Radio::initializeReceiver(RFSTREAM *rfs)
{
    memcpy((char*)&rfstream[activeReceivers], (char*)rfs, sizeof(RFSTREAM));
    if (!rfstream->isTX)
    {
        currentRxRfstream = rfstream->id;
        squelchValue[activeReceivers] = -100;
        squelch = false;

        rxp[activeReceivers] = new Panadapter();
        rxp[activeReceivers]->currentRfstream = currentRxRfstream;
        rxp[activeReceivers]->index = activeReceivers;
        connect(rxp[activeReceivers], SIGNAL(send_spectrum_command(QByteArray)), this, SLOT(sendCommand(QByteArray)));
        connect(rxp[activeReceivers], SIGNAL(frequencyMoved(int8_t,int,int)), this, SLOT(frequencyMoved(int8_t,int,int)));

        band[activeReceivers].initBand(activeReceivers, currentRxRfstream, BAND_15);

        QString ret = loadSetting(QString("display"), QString("spectrumlow"), activeReceivers);
        if (ret != "na")
            rxp[activeReceivers]->setLow(ret.toInt());
        ret = loadSetting(QString("display"), QString("spectrumhigh"), activeReceivers);
        if (ret != "na")
            rxp[activeReceivers]->setHigh(ret.toInt());
        ret = loadSetting(QString("display"), QString("waterfalllow"), activeReceivers);
        if (ret != "na")
            rxp[activeReceivers]->panadapterScene->waterfallItem->setLow(ret.toInt());
        ret = loadSetting(QString("display"), QString("waterfallhigh"), activeReceivers);
        if (ret != "na")
            rxp[activeReceivers]->panadapterScene->waterfallItem->setHigh(ret.toInt());
        ret = loadSetting(QString("signal"), QString("lastfrequency"), activeReceivers);
        if (ret != "na")
        {
            frequency[activeReceivers] = ret.toLongLong();
            band[activeReceivers].setFrequency(ret.toLongLong());
        }
        filters[activeReceivers].setIndex(activeReceivers);
        ret = loadSetting(QString("signal"), QString("mode"), activeReceivers);
        if (ret == "na")
            ret = "0";
        switch (ret.toInt())
        {
        case MODE_CWL:
            filters[activeReceivers].selectFilters(&cwlFilters);
            break;
        case MODE_CWU:
            filters[activeReceivers].selectFilters(&cwuFilters);
            break;
        case MODE_LSB:
            filters[activeReceivers].selectFilters(&lsbFilters);
            break;
        case MODE_USB:
            filters[activeReceivers].selectFilters(&usbFilters);
            break;
        case MODE_DSB:
            filters[activeReceivers].selectFilters(&dsbFilters);
            break;
        case MODE_AM:
            filters[activeReceivers].selectFilters(&amFilters);
            break;
        case MODE_SAM:
            filters[activeReceivers].selectFilters(&samFilters);
            break;
        case MODE_FM:
            filters[activeReceivers].selectFilters(&fmnFilters);
            break;
        case MODE_DIGL:
            filters[activeReceivers].selectFilters(&diglFilters);
            break;
        case MODE_DIGU:
            filters[activeReceivers].selectFilters(&diguFilters);
            break;
        }

        mode[activeReceivers].setMode(activeReceivers, receiver_rfstream[activeReceivers], ret.toInt());
        band[activeReceivers].setMode(ret.toInt());
        rxp[activeReceivers]->setMode(mode[activeReceivers].getStringMode());
        settingsTimer[activeReceivers].setSingleShot(true);
  //      connect(&settingsTimer[activeReceivers], SIGNAL(timeout()), this, SLOT(saveAllSettings()));

        ret = loadSetting(QString("signal"), QString("squelch"), activeReceivers);
        if (ret != "na")
            squelchValue[activeReceivers] = ret.toFloat();
    }
//    rxp[rfstream->index]->setHost(configure.getHost());

    receiver_active[activeReceivers] = true;
    receiver_rfstream[activeReceivers] = rfs->id;
 //   memcpy((char*)&rfstream[activeReceivers], (char*)rfstream, sizeof(RFSTREAM));
    emit printStatusBar(" .. receiver initialized. ");

    activeReceivers++;
} // end initializeReceiver


void Radio::bandChanged(int8_t index, int8_t stream, int previousBand, int newBand)
{
    if (index < 0) return;
    qDebug() << Q_FUNC_INFO << ":   previousBand, newBand = " << previousBand << "," << newBand;
    qDebug() << Q_FUNC_INFO << ":   band.getFilter = " << band[index].getFilter();

    emit updateBandMenu(newBand);

    // get the band setting
    qDebug("RFStream: %d", receiver_rfstream[index]);
    mode[index].setMode(index, receiver_rfstream[index], band[index].getMode());

    qDebug() << Q_FUNC_INFO << ":   The value of band.getFilter is ... " << band[index].getFilter();
    qDebug() << Q_FUNC_INFO << ":   The value of filters.getFilter is  " << filters[index].getFilter();

    rxp[index]->setBand(band[index].getStringBand());

    if (band[index].getFilter() != filters[index].getFilter())
    {
        emit filterChanged(rfstream[stream].id, filters[index].getFilter(), band[index].getFilter());
    }
    frequency[index] = band[index].getFrequency();

    int samplerate = rxp[index]->samplerate();

    QByteArray command;
    command.clear();
    command.append((char)rfstream[stream].id);
    command.append((char)SETFREQ);
    command.append(QString("%1").arg(frequency[index]));
    emit send_command(command);

    rxp[index]->setFrequency(frequency[index]);

    //    gvj code
//    widget.vfoFrame->setFrequency(frequency);
    emit updateVFO(frequency[index]);
    qDebug() << __FUNCTION__ << ": frequency, newBand = " << frequency[index] << ", " << newBand;
    rxp[index]->setHigh(band[index].getSpectrumHigh());
    rxp[index]->setLow(band[index].getSpectrumLow());
    //    widget.waterfallView->setFrequency(frequency);
    rxp[index]->panadapterScene->waterfallItem->setHigh(band[index].getWaterfallHigh());
    rxp[index]->panadapterScene->waterfallItem->setLow(band[index].getWaterfallLow());

    BandLimit limits = band[index].getBandLimits(band[index].getFrequency()-(samplerate/2),band[index].getFrequency()+(samplerate/2));
    rxp[index]->setBandLimits(limits.min() + loffset,limits.max() + loffset);
    if ((mode[index].getStringMode() == "CWU") || (mode[index].getStringMode() == "CWL"))
        frequencyChanged(index, frequency[index]); //gvj dummy call to set Rx offset for cw
} // end bandChanged


void Radio::modeChanged(int8_t index, int8_t stream, int previousMode, int newMode)
{
    QByteArray command;

    qDebug() << Q_FUNC_INFO << ":   previousMode, newMode" << previousMode << "," << newMode;
    qDebug() << Q_FUNC_INFO << ":   band.getFilter = " << band[index].getFilter();

    qDebug() << Q_FUNC_INFO << ":  999: value of band.getFilter before filters.selectFilters has been called = " << band[index].getFilter();

    // check the new mode and set the filters
    switch (newMode)
    {
    case MODE_CWL:
        filters[index].selectFilters(&cwlFilters);
        break;
    case MODE_CWU:
        filters[index].selectFilters(&cwuFilters);
        break;
    case MODE_LSB:
        filters[index].selectFilters(&lsbFilters);
        break;
    case MODE_USB:
        filters[index].selectFilters(&usbFilters);
        break;
    case MODE_DSB:
        filters[index].selectFilters(&dsbFilters);
        break;
    case MODE_AM:
        filters[index].selectFilters(&amFilters);
        break;
    case MODE_SAM:
        filters[index].selectFilters(&samFilters);
        break;
    case MODE_FM:
        filters[index].selectFilters(&fmnFilters);
        break;
    case MODE_DIGL:
        filters[index].selectFilters(&diglFilters);
        break;
    case MODE_DIGU:
        filters[index].selectFilters(&diguFilters);
        break;
    }

    emit updateModeMenu(newMode);
//    mode[index].setMode(index, newMode);

    qDebug() << Q_FUNC_INFO<<":  1043: value of band.getFilter after filters.selectFilters has been called = " << band[index].getFilter();
    if (!rfstream[index].isTX)
        rxp[index]->setMode(mode[index].getStringMode()); // FIXME:
    //    widget.waterfallView->setMode(mode.getStringMode());

    if (radio_started)
    {
        command.clear();
        command.append((char)stream);
        command.append((char)SETMODE);
        command.append((char)newMode);
        qDebug("RFStream: %d", stream);
        emit send_command(command);
        if (txrxPair[1] == stream && txrxPair[0] > -1)
        {
            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETMODE);
            command.append((char)newMode);
            emit send_command(command);
        }
    }
    settingsTimer[index].start(3000);
} // end modeChanged


void Radio::filtersChanged(int8_t index, FiltersBase* previousFilters, FiltersBase* newFilters)
{
    if (index < 0) return;
    qDebug() << Q_FUNC_INFO<<":   newFilters->getText, newFilters->getSelected = " << newFilters->getText() << ", "<<newFilters->getSelected();
    qDebug() << Q_FUNC_INFO<<":   band.getFilter = " << band[index].getFilter();

    emit updateFiltersMenu(band[index].getFilter(), newFilters);

    qDebug() << Q_FUNC_INFO<<":   1092 band.getFilter = " << band[index].getFilter() << ", modeFlag = " << modeFlag[index];

    if (!modeFlag[index])
    {
        newFilters->selectFilter(band[index].getFilter()); //TODO Still not there yet
        qDebug() << Q_FUNC_INFO << ":    Using the value from band.getFilter = " << band[index].getFilter();
    }

    filters[index].selectFilter(filters[index].getFilter());

    rxp[index]->setFilter(filters[index].getText());
    emit printStatusBar(" .. Initial frequency. ");    //added by gvj
} // end filtersChanged


void Radio::filterChanged(int8_t index, int previousFilter,int newFilter)
{
    if (index < 0) return;
    QByteArray command;

    qDebug()<<Q_FUNC_INFO<< ":    previousFilter, newFilter" << previousFilter << ":" << newFilter;

    emit updateFilterMenu(newFilter);
    if (receiver_active[index] != true) return;

    int low, high;
    if (previousFilter != 10 && newFilter == 10)
        return;

    if (mode[index].getMode() == MODE_CWL)
    {
        low = -cwPitch - filters[index].getLow();
        high = -cwPitch + filters[index].getHigh();
    }
    else
        if (mode[index].getMode() == MODE_CWU)
        {
            low = cwPitch - filters[index].getLow();
            high = cwPitch + filters[index].getHigh();
        }
        else
        {
            low = filters[index].getLow();
            high = filters[index].getHigh();
        }

    if (radio_started)
    {
        command.clear();
        command.append((char)rfstream[index].id);
        command.append((char)SETFILTER);
        command.append(QString("%1,%2").arg(low).arg(high));
        emit send_command(command);
        if (txrxPair[1] == rfstream[index].id && txrxPair[0] > -1)
        {
            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETFILTER);
            command.append(QString("%1,%2").arg(low).arg(high));
            emit send_command(command);
        }
    }

    rxp[index]->setFilter(low, high);
    if (txp != NULL)
        txp->setFilter(low, high);
    rxp[index]->setFilter(filters[index].getText());
    //    widget.waterfallView->setFilter(low,high);
    band[index].setFilter(newFilter);
    settingsTimer[index].start(3000);
} // end filterChanged


void Radio::variableFilter(int8_t index, int low, int high)
{
    QByteArray command;

    emit updateFilterMenu(10);

    if (radio_started)
    {
        command.clear();
        command.append((char)rfstream[index].id);
        command.append((char)SETFILTER);
        command.append(QString("%1,%2").arg(low).arg(high));
        emit send_command(command);
        if (txrxPair[1] == rfstream[index].id && txrxPair[0] > -1)
        {
            command.clear();
            command.append((char)txrxPair[0]);
            command.append((char)SETFILTER);
            command.append(QString("%1,%2").arg(low).arg(high));
            emit send_command(command);
        }
    }

    if (filters[index].getFilter() != 10)
    {
        band[index].setFilter(10);
        filters[index].selectFilter(10);
    }
    settingsTimer[index].start(3000);
} // end variableFilter


void Radio::frequencyChanged(int8_t index, long long f)
{
    if (index < 0) return;
    QByteArray command;
    long long freqOffset = f; //Normally no offset (only for CW Rx mode)

    frequency[index] = f;
    /* FIXME: Need to make the following lines work.
    if ((mode.getStringMode() == "CWU") && (!widget.vfoFrame->getPtt()))
    {
        freqOffset -= cwPitch;
    }
    if ((mode.getStringMode() == "CWL") && (!widget.vfoFrame->getPtt()))
    {
        freqOffset += cwPitch;
    } */
    //Send command to server
    command.clear();
    command.append((char)receiver_rfstream[index]);
    command.append((char)SETFREQ);
    command.append(QString("%1").arg(freqOffset));
    emit send_command(command);
    command.clear();

    //Adjust all frequency displays & Check for exiting current band
    band[index].setFrequency(frequency[index]);

    if (!rfstream[index].isTX)
        rxp[index]->setFrequency(frequency[index]);
 //   widget.vfoFrame->setFrequency(frequency[connection->rfstream[stream].index]);
    //    widget.waterfallView->setFrequency(frequency);
    emit updateVFO(f);
    emit updateBandMenu(band[index].getBand());
    emit printStatusBar(" ..  frequency changed. ");    //added by gvj
    qDebug("Frequency changed for stream: %d\n", receiver_rfstream[index]);
    settingsTimer[index].start(3000);
} // end frequencyChanged


void Radio::frequencyMoved(int8_t index, int increment, int step)
{
    qDebug() << __FUNCTION__ << ": increment=" << increment << " step=" << step;

    frequencyChanged(index, band[index].getFrequency() - (long long)(increment * step));
} // end frequencyMoved


void Radio::sampleRateChanged(int8_t index, long rate)
{
    QByteArray command;
    //Send command to server
    command.clear();
    command.append((char)receiver_rfstream[index]);
    command.append((char)SETSAMPLERATE);
    command.append(QString("%1").arg(rate));
    emit send_command(command);
} // end sampleRateChanged


void Radio::fpsChanged(int8_t index, int f)
{
    //qDebug() << "fpsChanged:" << f;
    fps[index] = f;
} // end fpsChanged


void Radio::setFPS(int8_t index)
{
    QByteArray command;
    command.clear();
    command.append((char)receiver_rfstream[index]);
    command.append((char)SETFPS);
    command.append(QString("2000,%1").arg(fps[index]));
    emit send_command(command);
    emit send_spectrum_command(command);
} // end setFPS


void Radio::enableRxEq(int8_t index, bool enable)
{
    QByteArray command;
    command.clear();
    command.append((char)rfstream[index].id);
    command.append((char)ENABLERXEQ);
    command.append((char)enable);
    emit send_command(command);
    qDebug() << Q_FUNC_INFO << ":   The command sent is " << command;
}


void Radio::enableTxEq(int8_t index, bool enable)
{
    if (index < 0) return;

    QByteArray command;
    command.clear();
    command.append((char)rfstream[index].id);
    command.append((char)ENABLETXEQ);
    command.append((char)enable);
    emit send_command(command);
    qDebug() << Q_FUNC_INFO << ":   The command sent is " << command;
}


void Radio::removeNotchFilter(int8_t index)
{
    notchFilterIndex[index]--;
    if (notchFilterIndex[index] < 0)
        notchFilterIndex[index] = 0;
    if (notchFilterIndex[index] == 0)
        tnfSetChecked(false);
}


void Radio::hardwareSet(QWidget *widget)
{
    if (hardwareType == "hermes")
    {
        HermesFrame *hf = new HermesFrame(this);
        widget->layout()->addWidget((HermesFrame*)hf);
        hww = (QWidget*)hf;
        connect((HermesFrame*)hf, SIGNAL(hhcommand(QByteArray)), this, SLOT(sendHardwareCommand(QByteArray)));
//        connect((HermesFrame*)hf, SIGNAL(pttTuneChange(int,bool)), this, SLOT(pttChange(int,bool)));
        hf->radio_id = radio_index;
        hf->currentRxRfstream = currentRxRfstream;
        hf->currentTxRfstream = currentTxRfstream;
    }

    if (hardwareType == "sdr1000")
    {
        Sdr1000Frame *hf = new Sdr1000Frame(this);
        widget->layout()->addWidget((Sdr1000Frame*)hf);
        hww = (QWidget*)hf;
        connect((Sdr1000Frame*)hf, SIGNAL(hhcommand(QByteArray)), this, SLOT(sendHardwareCommand(QByteArray)));
        hf->radio_id = radio_index;
        hf->currentRxRfstream = currentRxRfstream;
        hf->currentTxRfstream = currentTxRfstream;
    }
} // end hardwareSet


void Radio::sendHardwareCommand(QByteArray command)
{
    emit send_command(command);
    fprintf(stderr, "Send hardware command...\n");
} // end sendHardwareCommand


void Radio::initializeRadio()
{
    if (hardwareType == "hermes")
    {
        radio_started = true;
        static_cast<HermesFrame*>(hww)->currentRxRfstream = currentRxRfstream;
        static_cast<HermesFrame*>(hww)->currentTxRfstream = currentTxRfstream;
        static_cast<HermesFrame*>(hww)->initializeRadio();
    }
    if (hardwareType == "sdr1000")
    {
        radio_started = true;
        static_cast<Sdr1000Frame*>(hww)->currentRxRfstream = currentRxRfstream;
        static_cast<Sdr1000Frame*>(hww)->currentTxRfstream = currentTxRfstream;
        static_cast<Sdr1000Frame*>(hww)->initializeRadio();
    }
} // end initializeRadio


void Radio::shutdownRadio()
{
    if (hardwareType == "hermes")
    {
        static_cast<HermesFrame*>(hww)->shutDown();
    }
} // end shutdownRadio


void Radio::initRigCtl(int port)
{
    rigCtl = new RigCtlServer(this, this, port);
}


long long Radio::rigctlGetFreq()
{
    return frequency[0];
}


QString Radio::rigctlGetMode()
{
    QString  m = mode[0].getStringMode();
    if (m == "CWU")
    {
        m = "CW";
    }
    if (m == "CWL")
    {
        m = "CWR";
    }
    return m;
}


QString Radio::rigctlGetFilter()
{
    QString fwidth;
    QString  m = mode[0].getStringMode();

    if (m == "CWU")
    {
        return fwidth.setNum(filters[0].getHigh() + filters[0].getLow());
    }
    else
        if (m == "CWL")
        {
            return fwidth.setNum(filters[0].getHigh() + filters[0].getLow());
        }
        else
            return fwidth.setNum(filters[0].getHigh() - filters[0].getLow());
}


QString Radio::rigctlGetVFO()
{
    return "VFOA"; // widget.vfoFrame->rigctlGetvfo();
}


void Radio::rigctlSetVFOA()
{
//    widget.vfoFrame->on_pBtnvfoA_clicked();
}


void Radio::rigctlSetVFOB()
{
//    widget.vfoFrame->on_pBtnvfoB_clicked();
}


void Radio::rigctlSetFreq(long long f)
{
    frequencyChanged(rfstream[currentRxRfstream].id, f);
    if (currentTxRfstream > -1)
        frequencyChanged(8, f);
}


void Radio::rigctlSetMode(int newmode)
{
    modeChanged(0, currentRxRfstream, mode[0].getMode(), newmode);
    if (currentTxRfstream > -1)
        modeChanged(8, currentTxRfstream, mode[8].getMode(), newmode);
    mode[0].setMode(0, 0, newmode);
}


void Radio::rigctlSetFilter(int newfilter)
{
    qDebug() << "UI.cpp: dl6kbg: wanted filter via hamlib: " << newfilter;
    filters[0].selectFilter(newfilter);
}


void Radio::rigSetPTT(int enabled)
{
    if (enabled)
    {
//        widget.ctlFrame->RigCtlTX(true);
        emit ctlSetPTT(enabled);
        txNow = true;
    }
    else
    {
//        widget.ctlFrame->RigCtlTX(false);
        emit ctlSetPTT(enabled);
        txNow = false;
    }
}


bool Radio::rigGetPTT(void)
{
    return txNow;
} // end rigGetPTT


void Radio::enableBandscope(SpectrumConnection *connection, bool enable)
{
    if (enable)
    {
        if (bandscope == NULL)
            bandscope = new Bandscope(connection);
        connect(bandscope, SIGNAL(closeBandScope()), this, SLOT(closeBandScope()));
        bandscope->setWindowTitle("QtRadioNG Bandscope");
        bandscope->rfstream = MAX_RFSTREAMS - 1 - radio_index;
        bandscope->radio_id = radio_index;
        bandscope->show();
        bandscope->connect();
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


void Radio::closeBandScope()
{
    emit bandScopeClosed();
} // end closeBandScope


void Radio::spectrumHighChanged(int8_t index, int high)
{
    //qDebug() << __FUNCTION__ << ": " << high;

    rxp[index]->setHigh(high);
    //    configure.setSpectrumHigh(high);
    band[index].setSpectrumHigh(high);
    settingsTimer[index].start(3000);
} // end spectrumHighChanged


void Radio::spectrumLowChanged(int8_t index, int low)
{
    //qDebug() << __FUNCTION__ << ": " << low;

    rxp[index]->setLow(low);
    //    configure.setSpectrumLow(low);
    band[index].setSpectrumLow(low);
    settingsTimer[index].start(3000);
} // end spectrumLowChanged


void Radio::waterfallHighChanged(int8_t index, int high)
{
    //qDebug() << __LINE__ << __FUNCTION__ << ": " << high;

    rxp[index]->panadapterScene->waterfallItem->setHigh(high);
    //    configure.setWaterfallHigh(high);
    band[index].setWaterfallHigh(high);
    settingsTimer[index].start(3000);
} // end waterfallHighChanged


void Radio::waterfallLowChanged(int8_t index, int low)
{
    //qDebug() << __FUNCTION__ << ": " << low;

    rxp[index]->panadapterScene->waterfallItem->setLow(low);
    //    configure.setWaterfallLow(low);
    band[index].setWaterfallLow(low);
    settingsTimer[index].start(3000);
} // end waterfallLowChanged


void Radio::waterfallAutomaticChanged(int8_t index, bool state)
{
    rxp[index]->panadapterScene->waterfallItem->setAutomatic(state);
} // end waterfallautomaticChanged


void Radio::resetbandedges(int8_t index, double offset)
{
//    loffset = offset;
    BandLimit limits = band[index].getBandLimits(band[index].getFrequency()-(rxp[index]->samplerate()/2),band[index].getFrequency()+(rxp[index]->samplerate()/2));
    rxp[index]->setBandLimits(limits.min() + offset, limits.max() + offset);
//    qDebug()<<"loffset = "<<loffset;
} // end resetbandedges


void Radio::squelchValueChanged(int8_t index, int val)
{
    squelchValue[index] = squelchValue[index] + val;
    if (squelch)
    {
        QByteArray command;
        command.clear();
        command.append((char)rfstream[index].id);
        command.append((char)SETSQUELCHVAL);
        command.append(QString("%1").arg(squelchValue[index]));
        emit sendCommand(command);
        rxp[index]->setSquelchVal(squelchValue[index]);
        settingsTimer[index].start(3000);
    }
}


void UI::tnfSetChecked(bool enabled)
{
    widget.tnfButton->setChecked(enabled);
} // end tnfSetChecked
