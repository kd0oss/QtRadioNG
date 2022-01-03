/*
 * File:   Configure.cpp
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


/* Copyright (C) 2012 - Alex Lee, 9V1Al
* modifications of the original program by John Melton
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
* Foundation, Inc., 59 Temple Pl
*/

#include <QSettings>
#if QT_VERSION >= 0x050000
#include <QtWidgets/QComboBox>
#else
#include <QComboBox>
#endif

#include "Xvtr.h"
#include "Configure.h"
#include "Mode.h"

Configure::Configure() {
    widget.setupUi(this);

    //Populate the combo boxes with defaults. The loadSettings may overwrite or add values.
    widget.MicEncodingComboBox->addItem("aLaw");

    widget.hostComboBox->addItem("127.0.0.1");
//    widget.hostComboBox->addItem("g0orx.homelinux.net");
    widget.waterfallHighSpinBox->setValue(-60);
    widget.waterfallLowSpinBox->setValue(-120);
    widget.fpsSpinBox->setValue(15);
    widget.encodingComboBox->addItem("aLaw");
    widget.encodingComboBox->addItem("16 bit pcm");

    widget.nrTapsSpinBox->setValue(64);
    widget.nrDelaySpinBox->setValue(8);
    widget.nrGainSpinBox->setValue(16);
    widget.nrLeakSpinBox->setValue(10);
    widget.anfTapsSpinBox->setValue(64);
    widget.anfDelaySpinBox->setValue(8);
    widget.anfGainSpinBox->setValue(32);
    widget.anfLeakSpinBox->setValue(1);

    widget.ifFrequencyLineEdit->setText("28000000");
    //set up userpass
    QStringList userlist;
    widget.userpass->setRowCount(10);
    widget.userpass->setColumnCount(3);
    userlist<<"Server" << "User" << "Password";
    widget.userpass->setHorizontalHeaderLabels(userlist);
    widget.userpass->setEditTriggers(QAbstractItemView::AllEditTriggers);
    //QTableWidgetItem *newitem = new QTableWidgetItem("Fill Item");
    //widget.userpass->setItem(0, 0, newitem);
    widget.windowComboBox->setCurrentIndex(1);  //KD0OSS
    widget.pttKeyEdit->setText("None");  // KD0OSS

    connect(widget.spectrumHighSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotSpectrumHighChanged(int)));
    connect(widget.spectrumLowSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotSpectrumLowChanged(int)));
    connect(widget.fpsSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotFpsChanged(int)));
    connect(widget.waterfallHighSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotWaterfallHighChanged(int)));
    connect(widget.waterfallLowSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotWaterfallLowChanged(int)));
    connect(widget.waterfallAutomatic,SIGNAL(toggled(bool)),this,SLOT(slotWaterfallAutomaticChanged(bool)));

    connect(widget.audioDeviceComboBox,SIGNAL(currentIndexChanged(int)),this,SLOT(slotAudioDeviceChanged(int)));

    connect(widget.MicComboBox,SIGNAL(currentIndexChanged(int)),this,SLOT(slotMicDeviceChanged(int)));

    connect(widget.hostComboBox,SIGNAL(currentIndexChanged(int)),this,SLOT(slotHostChanged(int)));
    connect(widget.rxSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotReceiverChanged(int)));

    connect(widget.rxAgcSlopeSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onRxAgcSlopeChanged(int))); //KD0OSS
    connect(widget.rxAgcMaxGainSpinBox,SIGNAL(valueChanged(double)),this,SLOT(onRxAgcMaxGainChanged(double))); //KD0OSS
    connect(widget.rxAgcAttackSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onRxAgcAttackChanged(int))); //KD0OSS
    connect(widget.rxAgcDecaySpinBox,SIGNAL(valueChanged(int)),this,SLOT(onRxAgcDecayChanged(int))); //KD0OSS
    connect(widget.rxAgcHangSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onRxAgcHangChanged(int))); //KD0OSS
    connect(widget.rxAgcFixedGainSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onRxAgcFixedGainChanged(int))); //KD0OSS
    connect(widget.rxAgcHangThreshSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onRxAgcHangThreshChanged(int))); //KD0OSS
    connect(widget.preAGCButton, SIGNAL(toggled(bool)), this, SLOT(slotPreAGCChanged(bool)));

    connect(widget.levelerEnabledCheckBox,SIGNAL(stateChanged(int)),this,SLOT(onLevelerStateChanged(int))); //KD0OSS
    connect(widget.levelerAttackSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onLevelerAttackChanged(int))); //KD0OSS
    connect(widget.levelerDecaySpinBox,SIGNAL(valueChanged(int)),this,SLOT(onLevelerDecayChanged(int))); //KD0OSS
    connect(widget.levelerHangSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onLevelerHangChanged(int))); //KD0OSS

//    connect(widget.alcEnabledCheckBox,SIGNAL(stateChanged(int)),this,SLOT(onAlcStateChanged(int))); //KD0OSS
    connect(widget.alcAttackSpinBox,SIGNAL(valueChanged(int)),this,SLOT(onAlcAttackChanged(int))); //KD0OSS
    connect(widget.alcDecaySpinBox,SIGNAL(valueChanged(int)),this,SLOT(onAlcDecayChanged(int))); //KD0OSS
    connect(widget.alcMaxGainSpinBox,SIGNAL(valueChanged(double)),this,SLOT(onAlcMaxGainChanged(double))); //KD0OSS

    connect(widget.pttKeySetButton, SIGNAL(toggled(bool)), this, SLOT(setPTTKey(bool))); // KD0OSS
/*
    connect(widget.nbTransitionSpinBox,SIGNAL(valueChanged(double)),this,SLOT(onNbTransitionChanged(double))); //KD0OSS
    connect(widget.nbLeadSpinBox,SIGNAL(valueChanged(double)),this,SLOT(onNbLeadChanged(double))); //KD0OSS
    connect(widget.nbLagSpinBox,SIGNAL(valueChanged(double)),this,SLOT(onNbLagChanged(double))); //KD0OSS
*/
    connect(widget.nrTapsSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotNrTapsChanged(int)));
    connect(widget.nrDelaySpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotNrDelayChanged(int)));
    connect(widget.nrGainSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotNrGainChanged(int)));
    connect(widget.nrLeakSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotNrLeakChanged(int)));
    connect(widget.nrGainGammaButton, SIGNAL(toggled(bool)), this, SLOT(slotNRgainMethodChanged(bool)));
    connect(widget.nrGainLogButton, SIGNAL(toggled(bool)), this, SLOT(slotNRgainMethodChanged(bool)));
    connect(widget.nrGainLinearButton, SIGNAL(toggled(bool)), this, SLOT(slotNRgainMethodChanged(bool)));
    connect(widget.nrNPEOsmsButton, SIGNAL(toggled(bool)), this, SLOT(slotNRnpeMethodChanged(bool)));
//    connect(widget.nrNPEMmseButton, SIGNAL(toggled(bool)), this, SLOT(slotNRnpeMethodChanged(bool)));

    connect(widget.anfTapsSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotAnfTapsChanged(int)));
    connect(widget.anfDelaySpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotAnfDelayChanged(int)));
    connect(widget.anfGainSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotAnfGainChanged(int)));
    connect(widget.anfLeakSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotAnfLeakChanged(int)));

    connect(widget.cessbOvershootCB,SIGNAL(toggled(bool)), this, SLOT(slotCESSBOvershoot(bool)));
    connect(widget.aeFilterCB, SIGNAL(toggled(bool)), this, SLOT(slotAEFilterChanged(bool)));
    connect(widget.nb2ModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(slotNB2ModeChanged(int)));

    connect(widget.windowComboBox,SIGNAL(currentIndexChanged(int)),this,SLOT(slotWindowType(int)));

//    connect(widget.nbThresholdSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotNbThresholdChanged(int)));
//    connect(widget.sdromThresholdSpinBox,SIGNAL(valueChanged(int)),this,SLOT(slotSdromThresholdChanged(int)));

    connect(widget.addPushButton,SIGNAL(clicked()),this,SLOT(slotXVTRAdd()));
    connect(widget.deletePushButton,SIGNAL(clicked()),this,SLOT(slotXVTRDelete()));
}

Configure::~Configure() {
}

void Configure::initAudioDevices(Audio* audio) {
    qDebug() << "Configure: initAudioDevices";
    audio->get_audio_devices(widget.audioDeviceComboBox); // TODO: how to change this to signal/slot
}

void Configure::initMicDevices(AudioInput* mic) {
    qDebug() << "Configure: initAudioDevices";
    mic->get_audioinput_devices(widget.MicComboBox); // TODO: how to change this to signal/slot
}

void Configure::updateXvtrList(Xvtr* xvtr) {
    // update the list of XVTR entries
    XvtrEntry* entry;
    QStringList headings;
    //    QString title;
    QString minFrequency;
    QString maxFrequency;
    QString ifFrequency;

    widget.XVTRTableWidget->clear();
    widget.XVTRTableWidget->setRowCount(xvtr->count());
    widget.XVTRTableWidget->setColumnCount(4);
    headings<< "Title" << "Min Frequency" << "Max Frequency" << "IF Frequency";
    widget.XVTRTableWidget->setHorizontalHeaderLabels(headings);

    for(int i=0;i<xvtr->count();i++) {
        entry=xvtr->getXvtrAt(i);
        QTableWidgetItem *titleItem=new QTableWidgetItem(entry->getTitle());
        titleItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        widget.XVTRTableWidget->setItem(i, 0, titleItem);
        minFrequency.sprintf("%lld",entry->getMinFrequency());
        QTableWidgetItem *minFrequencyItem=new QTableWidgetItem(minFrequency);
        minFrequencyItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        widget.XVTRTableWidget->setItem(i, 1, minFrequencyItem);
        maxFrequency.sprintf("%lld",entry->getMaxFrequency());
        QTableWidgetItem *maxFrequencyItem=new QTableWidgetItem(maxFrequency);
        maxFrequencyItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        widget.XVTRTableWidget->setItem(i, 2, maxFrequencyItem);
        ifFrequency.sprintf("%lld",entry->getIFFrequency());
        QTableWidgetItem *ifFrequencyItem=new QTableWidgetItem(ifFrequency);
        ifFrequencyItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        widget.XVTRTableWidget->setItem(i, 3, ifFrequencyItem);
    }
    widget.XVTRTableWidget->resizeColumnsToContents();
}

void Configure::connected(bool state) {
    // set configuration options enabled/disabled based on connection state
    widget.audioDeviceComboBox->setDisabled(state);
    widget.encodingComboBox->setDisabled(state);

    widget.MicComboBox->setDisabled(state);
    widget.MicEncodingComboBox->setDisabled(state);

    widget.hostComboBox->setDisabled(state);
    widget.rxSpinBox->setDisabled(state);
}

void Configure::loadSettings(QSettings* settings)
{
    int i;

    settings->beginGroup("Servers");
    if (settings->contains("entries"))
    {
        widget.hostComboBox->clear();
        int entries=settings->value("entries").toInt();
        for(i=0;i<entries;i++)
        {
            widget.hostComboBox->addItem(settings->value(QString::number(i)).toString());
        }
        widget.hostComboBox->setCurrentIndex(settings->value("selected").toInt());
    }
    if (settings->contains("rx")) widget.rxSpinBox->setValue(settings->value("rx").toInt());
    settings->endGroup();

    settings->beginGroup("Display");
    if (settings->contains("spectrumHigh"))
    {
        qDebug() << "The value of spectrumHigh is ... " << settings->value("spectrumHigh").toInt();
    }
    widget.spectrumHighSpinBox->setValue(settings->value("spectrumHigh",-40).toInt());
    widget.spectrumHighSpinBox->setValue(settings->value("spectrumHigh",-40).toInt());
    //    if (settings->contains("spectrumHigh"))widget.spectrumHighSpinBox->setValue(settings->value("spectrumHigh",-40).toInt());
    //    if (settings->contains("spectrumLow"))widget.spectrumLowSpinBox->setValue(settings->value("spectrumLow",-160).toInt());
    if (settings->contains("fps"))widget.fpsSpinBox->setValue(settings->value("fps").toInt());
    if (settings->contains("avg"))widget.avgSpinBox->setValue(settings->value("avg").toInt());
    if (settings->contains("waterfallHigh"))widget.waterfallHighSpinBox->setValue(settings->value("waterfallHigh").toInt());
    if (settings->contains("waterfallLow"))widget.waterfallLowSpinBox->setValue(settings->value("waterfallLow").toInt());
    if (settings->contains("waterfallAutomatic"))widget.waterfallAutomatic->setChecked(settings->value("waterfallAutomatic").toBool());
    if (settings->contains("WindowGeometryFlag"))widget.checkBoxWindowPosn->setChecked(settings->value("WindowGeometryFlag").toBool());
    if (settings->contains("windowMode")) widget.windowComboBox->setCurrentIndex(settings->value("windowMode").toInt());  //KD0OSS
    settings->endGroup();

    settings->beginGroup("Audio");
    if (settings->contains("device")) widget.audioDeviceComboBox->setCurrentIndex(settings->value("device").toInt());
 //   if (settings->contains("mic")) widget.MicComboBox->setCurrentIndex(settings->value("mic").toInt());
//    if (settings->contains("encoding")) widget.encodingComboBox->setCurrentIndex(settings->value("encoding").toInt());
//    if (settings->contains("micEncoding")) widget.MicEncodingComboBox->setCurrentIndex(settings->value("micEncoding").toInt());
    widget.spinBox_cwPitch->setValue(settings->value("cwPitch",600).toInt());
    settings->endGroup();

    settings->beginGroup("NR");
    if (settings->contains("taps")) widget.nrGainSpinBox->setValue(settings->value("taps").toInt());
    if (settings->contains("delay"))widget.nrDelaySpinBox->setValue(settings->value("delay").toInt());
    if (settings->contains("gain")) widget.nrGainSpinBox->setValue(settings->value("gain").toInt());
    if (settings->contains("leak")) widget.nrLeakSpinBox->setValue(settings->value("leak").toInt());
    if (settings->contains("txfilterwin")) widget.txFilterWindowCombo->setCurrentIndex(settings->value("txfilterwin").toInt());
    if (settings->contains("rxfilterwin")) widget.rxFilterWindowCombo->setCurrentIndex(settings->value("rxfilterwin").toInt());
    settings->endGroup();

    settings->beginGroup("ANF");
    if (settings->contains("taps")) widget.anfGainSpinBox->setValue(settings->value("taps").toInt());
    if (settings->contains("delay")) widget.anfDelaySpinBox->setValue(settings->value("delay").toInt());
    if (settings->contains("gain")) widget.anfGainSpinBox->setValue(settings->value("gain").toInt());
    if (settings->contains("leak")) widget.anfLeakSpinBox->setValue(settings->value("leak").toInt());
    if (settings->contains("cessb")) widget.cessbOvershootCB->setChecked(settings->value("cessb").toBool());
    if (settings->contains("preagc")) widget.preAGCButton->setChecked(settings->value("preagc").toBool());
    if (settings->contains("nrgaingamma")) widget.nrGainGammaButton->setChecked(settings->value("nrgaingamma").toBool());
    if (settings->contains("nrgainlog")) widget.nrGainLogButton->setChecked(settings->value("nrgainlog").toBool());
    if (settings->contains("nrgainlinear")) widget.nrGainLinearButton->setChecked(settings->value("nrgainlinear").toBool());
    if (settings->contains("nrnpeosms")) widget.nrNPEOsmsButton->setChecked(settings->value("nrnpeosms").toBool());
    if (settings->contains("nrnpemmse")) widget.nrNPEMmseButton->setChecked(settings->value("nrnpemmse").toBool());
    if (settings->contains("nb2mode")) widget.nb2ModeCombo->setCurrentIndex(settings->value("nb2mode").toInt());
    settings->endGroup();

    settings->beginGroup("AGC"); // KD0OSS
    if (settings->contains("slope")) widget.rxAgcSlopeSpinBox->setValue(settings->value("slope").toInt());
    if (settings->contains("maxgain")) widget.rxAgcMaxGainSpinBox->setValue(settings->value("maxgain").toInt());
    if (settings->contains("attack")) widget.rxAgcAttackSpinBox->setValue(settings->value("attack").toInt());
    if (settings->contains("decay")) widget.rxAgcDecaySpinBox->setValue(settings->value("decay").toInt());
    if (settings->contains("hang")) widget.rxAgcHangSpinBox->setValue(settings->value("hang").toInt());
    if (settings->contains("fixedgain")) widget.rxAgcFixedGainSpinBox->setValue(settings->value("fixedgain").toInt());
    if (settings->contains("hangthreshold")) widget.rxAgcHangThreshSpinBox->setValue(settings->value("hangthreshold").toInt());
    settings->endGroup();

    settings->beginGroup("Leveler"); // KD0OSS
    if (settings->contains("enabled")) widget.levelerEnabledCheckBox->setChecked(settings->value("enabled",false).toBool());
    if (settings->contains("attack")) widget.levelerAttackSpinBox->setValue(settings->value("attack").toInt());
    if (settings->contains("decay")) widget.levelerDecaySpinBox->setValue(settings->value("decay").toInt());
    if (settings->contains("hang")) widget.levelerHangSpinBox->setValue(settings->value("hang").toInt());
    settings->endGroup();

    settings->beginGroup("ALC"); // KD0OSS
//    if (settings->contains("enabled")) widget.alcEnabledCheckBox->setChecked(settings->value("enabled",false).toBool());
    if (settings->contains("attack")) widget.alcAttackSpinBox->setValue(settings->value("attack").toInt());
    if (settings->contains("decay")) widget.alcDecaySpinBox->setValue(settings->value("decay").toInt());
    if (settings->contains("maxgain")) widget.alcMaxGainSpinBox->setValue(settings->value("maxgain").toDouble());
    settings->endGroup();

    settings->beginGroup("NB");
 //   if (settings->contains("threshold")) widget.nbThresholdSpinBox->setValue(settings->value("threshold").toInt());
    settings->endGroup();

    settings->beginGroup("SDROM");
 //   if (settings->contains("threshold")) widget.nbThresholdSpinBox->setValue(settings->value("threshold").toInt());
    settings->endGroup();

    settings->beginGroup("RXDCBlock"); // KD0OSS
//    if (settings->contains("rxDCBlock")) widget.rxDCBlockCheckBox->setChecked(settings->value("rxDCBlock",false).toBool());
//    if (settings->contains("rxDCBlockGain")) widget.rxDCBlkGainSpinBox->setValue(settings->value("rxDCBlockGain",0).toInt());
    settings->endGroup();

    settings->beginGroup("TxSettings");
    widget.allowTx->setChecked(settings->value("allowTx",false).toBool());
//    widget.txDCBlockCheckBox->setChecked(settings->value("txDCBlock",false).toBool());  //KD0OSS
    widget.pttKeyEdit->setText(settings->value("pttKeyText", "None").toString());
    pttKeyId = settings->value("pttKeyId", 0).toUInt();
    settings->endGroup();

//    settings->beginGroup("TxIQimage");  //KD0OSS
//    if (settings->contains("TxIQPhaseCorrect")) widget.txIQPhaseSpinBox->setValue(settings->value("TxIQPhaseCorrect",0).toInt());  //KD0OSS
//    if (settings->contains("TxIQGainCorrect")) widget.txIQGainSpinBox->setValue(settings->value("TxIQGainCorrect",0).toInt());  //KD0OSS
//    settings->endGroup();

//    settings->beginGroup("RxIQimage");
//    widget.RxIQcheckBox->setChecked(settings->value("RxIQon/off",true).toBool());
//    widget.RxIQspinBox->setValue(settings->value("RxIQmu",25).toInt());
//    widget.rxIQPhaseSpinBox->setValue(settings->value("RxIQPhaseCorrect",0).toInt());  //KD0OSS
//    widget.rxIQGainSpinBox->setValue(settings->value("RxIQGainCorrect",0).toInt());  //KD0OSS
//    settings->endGroup();

    settings->beginGroup("UserPass");
    //QTableWidgetItem *server, *user, *pass;
    for (int i=0;i<widget.userpass->rowCount();i++)
    {
        QString s,u,p;
        s.setNum(i);
        u.setNum(i);
        p.setNum(i);
        s.append("_Server");
        u.append("_User");
        p.append("_Pass");
        if (settings->contains(s) && settings->contains(u) && settings->contains(p))
        {
            QTableWidgetItem* Server= new QTableWidgetItem(settings->value(s).toString());
            QTableWidgetItem* User= new QTableWidgetItem(settings->value(u).toString());
            QTableWidgetItem* Pass= new QTableWidgetItem(settings->value(p).toString());
            widget.userpass->setItem(i,0,Server);
            widget.userpass->setItem(i,1,User);
            widget.userpass->setItem(i,2,Pass);
        }
    }
    settings->endGroup();
}


void Configure::saveSettings(QSettings* settings) {
    int i;
    settings->beginGroup("Servers");
    qDebug() << "server count=" << widget.hostComboBox->count();
    settings->setValue("entries",widget.hostComboBox->count());
    for (i=0;i<widget.hostComboBox->count();i++) {
        qDebug() << "server: " << widget.hostComboBox->itemText(i);
        settings->setValue(QString::number(i),widget.hostComboBox->itemText(i));
    }
    settings->setValue("selected",widget.hostComboBox->currentIndex());
    qDebug() << "server selected: " << widget.hostComboBox->currentIndex();
    settings->setValue("rx",widget.rxSpinBox->value());
    settings->endGroup();

    settings->beginGroup("Display");
    settings->setValue("spectrumHigh",widget.spectrumHighSpinBox->value());
    settings->setValue("spectrumLow",widget.spectrumLowSpinBox->value());
    settings->setValue("fps",widget.fpsSpinBox->value());
    settings->setValue("avg",widget.avgSpinBox->value());
    settings->setValue("waterfallHigh",widget.waterfallHighSpinBox->value());
    settings->setValue("waterfallLow",widget.waterfallLowSpinBox->value());
    settings->setValue("waterfallAutomatic",widget.waterfallAutomatic->checkState());
    settings->setValue("WindowGeometryFlag", widget.checkBoxWindowPosn->checkState());
    settings->setValue("windowMode", widget.windowComboBox->currentIndex());
    settings->endGroup();

    settings->beginGroup("Audio");
    settings->setValue("device",widget.audioDeviceComboBox->currentIndex());
  //  settings->setValue("encoding",widget.encodingComboBox->currentIndex());
    settings->setValue("mic",widget.MicComboBox->currentIndex());
//    settings->setValue("micEncoding",widget.MicEncodingComboBox->currentIndex());
    settings->setValue("cwPitch",widget.spinBox_cwPitch->value());
    settings->endGroup();

    settings->beginGroup("NR");
    settings->setValue("taps",widget.nrTapsSpinBox->value());
    settings->setValue("delay",widget.nrDelaySpinBox->value());
    settings->setValue("gain",widget.nrGainSpinBox->value());
    settings->setValue("leak",widget.nrLeakSpinBox->value());
    settings->setValue("txfilterwin", widget.txFilterWindowCombo->currentIndex());
    settings->setValue("rxfilterwin", widget.rxFilterWindowCombo->currentIndex());
    settings->endGroup();

    settings->beginGroup("ANF");
    settings->setValue("taps",widget.anfTapsSpinBox->value());
    settings->setValue("delay",widget.anfDelaySpinBox->value());
    settings->setValue("gain",widget.anfGainSpinBox->value());
    settings->setValue("leak",widget.anfLeakSpinBox->value());
    settings->setValue("cessb", widget.cessbOvershootCB->isChecked());
    settings->setValue("preagc", widget.preAGCButton->isChecked());
    settings->setValue("nrgaingamma", widget.nrGainGammaButton->isChecked());
    settings->setValue("nrgainlog", widget.nrGainLogButton->isChecked());
    settings->setValue("nrgainlinear", widget.nrGainLinearButton->isChecked());
    settings->setValue("nrnpeosms", widget.nrNPEOsmsButton->isChecked());
    settings->setValue("nrnpemmse", widget.nrNPEMmseButton->isChecked());
    settings->setValue("nb2mode", widget.nb2ModeCombo->currentIndex());
    settings->endGroup();

    settings->beginGroup("AGC"); // KD0OSS
    settings->setValue("slope",widget.rxAgcSlopeSpinBox->value());
    settings->setValue("maxgain",widget.rxAgcMaxGainSpinBox->value());
    settings->setValue("attack",widget.rxAgcAttackSpinBox->value());
    settings->setValue("decay",widget.rxAgcDecaySpinBox->value());
    settings->setValue("hang",widget.rxAgcHangSpinBox->value());
    settings->setValue("fixedgain",widget.rxAgcFixedGainSpinBox->value());
    settings->setValue("hangthreshold",widget.rxAgcHangThreshSpinBox->value());
    settings->endGroup();

    settings->beginGroup("Leveler"); // KD0OSS
    settings->setValue("enabled",widget.levelerEnabledCheckBox->checkState());
    settings->setValue("attack",widget.levelerAttackSpinBox->value());
    settings->setValue("decay",widget.levelerDecaySpinBox->value());
    settings->setValue("hang",widget.levelerHangSpinBox->value());
    settings->endGroup();

    settings->beginGroup("ALC");  // KD0OSS
//    settings->setValue("enabled",widget.alcEnabledCheckBox->checkState());
    settings->setValue("attack",widget.alcAttackSpinBox->value());
    settings->setValue("decay",widget.alcDecaySpinBox->value());
    settings->setValue("maxgain",widget.alcMaxGainSpinBox->value());
    settings->endGroup();

    settings->beginGroup("NB");
//    settings->setValue("threshold",widget.nbThresholdSpinBox->value());
    settings->endGroup();

    settings->beginGroup("SDROM");
//    settings->setValue("threshold",widget.nbThresholdSpinBox->value());
    settings->endGroup();

    settings->beginGroup("RXDCBlock");  // KD0OSS
//    settings->setValue("rxDCBlock",widget.rxDCBlockCheckBox->checkState());
//    settings->setValue("rxDCBlockGain",widget.rxDCBlkGainSpinBox->value());
    settings->endGroup();

    settings->beginGroup("TxSettings");
    settings->setValue("allowTx",widget.allowTx->checkState());
//    settings->setValue("txDCBlock",widget.txDCBlockCheckBox->checkState());  //KD0OSS
    settings->setValue("pttKeyId", pttKeyId); //KD0OSS
    settings->setValue("pttKeyText", widget.pttKeyEdit->text());
    settings->endGroup();

//    settings->beginGroup("TxIQimage"); // KD0OSS
//    settings->setValue("TxIQPhaseCorrect",widget.txIQPhaseSpinBox->value());
//    settings->setValue("TxIQGainCorrect",widget.txIQGainSpinBox->value());
//    settings->endGroup();
//    settings->beginGroup("RxIQimage");
//    settings->setValue("RxIQon/off",widget.RxIQcheckBox->checkState());
 //   settings->setValue("RxIQmu",widget.RxIQspinBox->value());
//    settings->setValue("RxIQPhaseCorrect",widget.rxIQPhaseSpinBox->value());  //KD0OSS
//    settings->setValue("RxIQGainCorrect",widget.rxIQGainSpinBox->value());  //KD0OSS
//    settings->endGroup();

    settings->beginGroup("UserPass");
    QTableWidgetItem *server, *user, *pass;
    for (int i=0;i<widget.userpass->rowCount();i++){
        server = widget.userpass->item(i,0);
        user = widget.userpass->item(i,1);
        pass = widget.userpass->item(i,2);
        if (server && user && pass ){
            QString s,u,p;
            s.setNum(i);
            u.setNum(i);
            p.setNum(i);
            qDebug() << s;
            s.append("_Server");
            qDebug() <<s;
            u.append("_User");
            p.append("_Pass");
            settings->setValue(s,server->text());
            settings->setValue(u,user->text());
            settings->setValue(p,pass->text());
        }
    }
    settings->endGroup();

}

void Configure::slotHostChanged(int selection) {
    emit hostChanged(widget.hostComboBox->currentText());
}

void Configure::slotReceiverChanged(int receiver) {
    emit receiverChanged(receiver);
}

void Configure::slotSpectrumHighChanged(int high) {
    emit spectrumHighChanged(high);
}

void Configure::slotSpectrumLowChanged(int low) {
    emit spectrumLowChanged(low);
}

void Configure::slotFpsChanged(int fps) {
    emit fpsChanged(fps);
}

void Configure::slotWaterfallHighChanged(int high) {
    emit waterfallHighChanged(high);
}

void Configure::slotWaterfallLowChanged(int low) {
    emit waterfallLowChanged(low);
}

void Configure::slotWaterfallAutomaticChanged(bool state) {
    emit waterfallAutomaticChanged(state);
}

void Configure::slotAudioDeviceChanged(int selection) {
    emit audioDeviceChanged(widget.audioDeviceComboBox->itemData(selection).value<QAudioDeviceInfo >(),
                            8000,
                            1,
                            QAudioFormat::LittleEndian);
}


void Configure::slotMicDeviceChanged(int selection) {
    qDebug() << "Mic selected is device number: " << selection;
    emit micDeviceChanged(widget.MicComboBox->itemData(selection).value<QAudioDeviceInfo >(),
                          48000,
                          1,
                          QAudioFormat::LittleEndian);
}

void Configure::slotRxFilterWindowChanged(int type)
{
    emit rxFilterWindowChanged(type);
}

void Configure::slotTxFilterWindowChanged(int type)
{
    emit txFilterWindowChanged(type);
}

void Configure::slotNB2ModeChanged(int mode)
{
    emit nb2ModeChanged(mode);
}

void Configure::slotPreAGCChanged(bool enable)
{
    emit preAGCChanged(enable);
}

void Configure::slotCESSBOvershoot(bool enable)
{
    emit cessbOvershootChanged(enable);
}

void Configure::slotNRgainMethodChanged(bool tmp)
{
    int method = 2;

    if (widget.nrGainGammaButton->isChecked()) method = 2;
    if (widget.nrGainLogButton->isChecked()) method = 1;
    if (widget.nrGainLinearButton->isChecked()) method = 0;
    emit nrGainMethodChanged(method);
}

void Configure::slotNRnpeMethodChanged(bool tmp)
{
    int method = 0;

    if (widget.nrNPEMmseButton->isChecked()) method = 1;
    if (widget.nrNPEOsmsButton->isChecked()) method = 0;
    emit nrNpeMethodChanged(method);
}

void Configure::slotNrTapsChanged(int taps) {
    emit nrValuesChanged(widget.nrTapsSpinBox->value(),widget.nrDelaySpinBox->value(),(double)widget.nrGainSpinBox->value()*0.00001,(double)widget.nrLeakSpinBox->value()*0.001);
}

void Configure::slotNrDelayChanged(int delay) {
    emit nrValuesChanged(widget.nrTapsSpinBox->value(),widget.nrDelaySpinBox->value(),(double)widget.nrGainSpinBox->value()*0.00001,(double)widget.nrLeakSpinBox->value()*0.001);
}

void Configure::slotNrGainChanged(int gain) {
    emit nrValuesChanged(widget.nrTapsSpinBox->value(),widget.nrDelaySpinBox->value(),(double)widget.nrGainSpinBox->value()*0.00001,(double)widget.nrLeakSpinBox->value()*0.001);
}

void Configure::slotNrLeakChanged(int leak) {
    emit nrValuesChanged(widget.nrTapsSpinBox->value(),widget.nrDelaySpinBox->value(),(double)widget.nrGainSpinBox->value()*0.00001,(double)widget.nrLeakSpinBox->value()*0.001);
}

void Configure::slotAnfTapsChanged(int taps) {
    emit anfValuesChanged(widget.anfTapsSpinBox->value(),widget.anfDelaySpinBox->value(),(double)widget.anfGainSpinBox->value()*0.00001,(double)widget.anfLeakSpinBox->value()*0.001);
}

void Configure::slotAnfDelayChanged(int delay) {
    emit anfValuesChanged(widget.anfTapsSpinBox->value(),widget.anfDelaySpinBox->value(),(double)widget.anfGainSpinBox->value()*0.00001,(double)widget.anfLeakSpinBox->value()*0.001);
}

void Configure::slotAnfGainChanged(int gain) {
    emit anfValuesChanged(widget.anfTapsSpinBox->value(),widget.anfDelaySpinBox->value(),(double)widget.anfGainSpinBox->value()*0.00001,(double)widget.anfLeakSpinBox->value()*0.001);
}

void Configure::slotAnfLeakChanged(int leak) {
    emit anfValuesChanged(widget.anfTapsSpinBox->value(),widget.anfDelaySpinBox->value(),(double)widget.anfGainSpinBox->value()*0.00001,(double)widget.anfLeakSpinBox->value()*0.001);
}

void Configure::slotNbThresholdChanged(int threshold) {
//    emit nbThresholdChanged((double)widget.nbThresholdSpinBox->value()*0.165);
}

void Configure::slotSdromThresholdChanged(int threshold) {
 //   emit sdromThresholdChanged((double)widget.sdromThresholdSpinBox->value()*0.165);
}

void Configure::slotAEFilterChanged(bool enable)
{
    emit aeFilterChanged(enable);
}

void Configure::slotWindowType(int type) {
    emit windowTypeChanged(type);
}

QString Configure::getHost() {
    return widget.hostComboBox->currentText();
}


int Configure::getRxAGCSlopeValue() {  //KD0OSS
    return widget.rxAgcSlopeSpinBox->value();
}

int Configure::getRxAGCMaxGainValue() {  //KD0OSS
    return widget.rxAgcMaxGainSpinBox->value();
}

int Configure::getRxAGCAttackValue() {  //KD0OSS
    return widget.rxAgcAttackSpinBox->value();
}

int Configure::getRxAGCDecayValue() {  //KD0OSS
    return widget.rxAgcDecaySpinBox->value();
}

int Configure::getRxAGCHangValue() {  //KD0OSS
    return widget.rxAgcHangSpinBox->value();
}

int Configure::getRxAGCFixedGainValue() {  //KD0OSS
    return widget.rxAgcFixedGainSpinBox->value();
}

int Configure::getRxAGCHangThreshValue() {  //KD0OSS
    return widget.rxAgcHangThreshSpinBox->value();
}

bool Configure::getLevelerEnabledValue() {  //KD0OSS
    return widget.levelerEnabledCheckBox->isChecked();
}

int Configure::getLevelerAttackValue() {  //KD0OSS
    return widget.levelerAttackSpinBox->value();
}

int Configure::getLevelerDecayValue() {  //KD0OSS
    return widget.levelerDecaySpinBox->value();
}

int Configure::getLevelerHangValue() {  //KD0OSS
    return widget.levelerHangSpinBox->value();
}

bool Configure::getALCEnabledValue() {  //KD0OSS
 //   return widget.alcEnabledCheckBox->isChecked();
}

int Configure::getALCAttackValue() {  //KD0OSS
    return widget.alcAttackSpinBox->value();
}

int Configure::getALCDecayValue() {  //KD0OSS
    return widget.alcDecaySpinBox->value();
}

int Configure::getALCMaxGainValue() {  //KD0OSS
    return widget.alcMaxGainSpinBox->value();
}

int Configure::getReceiver() {
    return widget.rxSpinBox->value();
}

int Configure::getSpectrumHigh() {
    return widget.spectrumHighSpinBox->value();
}

int Configure::getSpectrumLow() {
    return widget.spectrumLowSpinBox->value();
}

int Configure::getFps() {
    return widget.fpsSpinBox->value();
}

int Configure::getAvg(){
    return widget.avgSpinBox->value();
}

int Configure::getWaterfallHigh() {
    return widget.waterfallHighSpinBox->value();
}

int Configure::getWaterfallLow() {
    return widget.waterfallLowSpinBox->value();
}

void Configure::setSpectrumLow(int low) {
    widget.spectrumLowSpinBox->setValue(low);
}

void Configure::setSpectrumHigh(int high) {
    widget.spectrumHighSpinBox->setValue(high);
}

void Configure::setWaterfallLow(int low) {
    widget.waterfallLowSpinBox->setValue(low);
}

void Configure::setWaterfallHigh(int high) {
    widget.waterfallHighSpinBox->setValue(high);
}

int Configure::getNrTaps() {
    return widget.nrTapsSpinBox->value();
}

int Configure::getNrDelay(){
    return widget.nrDelaySpinBox->value();
}

double Configure::getNrGain() {
    return (double)widget.nrGainSpinBox->value()*0.00001;
}

double Configure::getNrLeak() {
    return (double)widget.nrLeakSpinBox->value()*0.001;
}

int Configure::getAnfTaps() {
    return widget.anfTapsSpinBox->value();
}

int Configure::getAnfDelay() {
    return widget.anfDelaySpinBox->value();
}

double Configure::getAnfGain() {
    return (double)widget.anfGainSpinBox->value()*0.00001;
}

double Configure::getAnfLeak() {
    return (double)widget.anfLeakSpinBox->value()*0.001;
}

double Configure::getNbThreshold() {
 //   return (double)widget.nbThresholdSpinBox->value()*0.165;
}


void Configure::slotXVTRAdd() {

    QString title;
    long long minFrequency;
    long long maxFrequency;
    long long ifFrequency;

    // check name is present
    title=widget.titleLineEdit->text();

    // check min frequency
    minFrequency=widget.minFrequencyLineEdit->text().toLongLong();
    maxFrequency=widget.maxFrequencyLineEdit->text().toLongLong();
    ifFrequency=widget.ifFrequencyLineEdit->text().toLongLong();

    if (title==QString("")) {
        // must have a title
        qDebug()<<"XVTR entry must hava a title";
        return;
    }

    if (minFrequency<=0LL) {
        // must not be zero or negative
        qDebug()<<"XVTR min frequency must be > 0";
        return;
    }

    if (maxFrequency<=0LL) {
        // must not be zero or negative
        qDebug()<<"XVTR max frequency must be > 0";
        return;
    }

    if (minFrequency>=maxFrequency) {
        // max must be greater than min
        qDebug()<<"XVTR min frequency must be < max frequency";
        return;
    }

    // all looks OK so save it
    emit addXVTR(title,minFrequency,maxFrequency,ifFrequency,minFrequency,MODE_USB,5);

    // update the list
}

void Configure::slotXVTRDelete() {

    int index=widget.XVTRTableWidget->currentRow();
    if (index==-1) {
        qDebug()<<"XVTR Delete but nothing selected";
        return;
    }

    qDebug()<<"Configure::slotXVTRDelete"<<index;
    emit deleteXVTR(index);
}

void Configure::on_pBtnAddHost_clicked()
{
    widget.hostComboBox->addItem(widget.hostComboBox->currentText());
}

void Configure::addHost(QString host){
    int current_index;
    if ((current_index = widget.hostComboBox->findText(host)) == -1){      // not currently on ComboBox
        widget.hostComboBox->addItem(host);
        current_index = widget.hostComboBox->findText(host);
    }
    widget.hostComboBox->setCurrentIndex(current_index);

}

void Configure::removeHost(QString host){
    int current_index;
    if ((current_index = widget.hostComboBox->findText(host)) == -1){    // not currently on ComboBox
    }else{
        current_index = widget.hostComboBox->findText(host);
        widget.hostComboBox->setCurrentIndex(current_index);
        on_pBtnRemHost_clicked();
    }
}

void Configure::on_pBtnRemHost_clicked()
{
    widget.hostComboBox->removeItem(widget.hostComboBox->currentIndex());
}


bool Configure::getGeometryState()
{
    return widget.checkBoxWindowPosn->checkState();
}

bool Configure::getTxAllowed()
{
    return widget.allowTx->checkState();
}

void Configure::setTxAllowed(bool newstate)
{
    widget.allowTx->setChecked(newstate);
}

int Configure::getWindowType()
{
    return widget.windowComboBox->currentIndex();
}

int Configure::getRxFilterWindow()
{
    return widget.rxFilterWindowCombo->currentIndex();
}

int Configure::getTxFilterWindow()
{
    return widget.txFilterWindowCombo->currentIndex();
}

void Configure::on_userpasssave_clicked()
{
    QSettings settings("G0ORX","QtRadio");
    saveSettings(&settings);

}

bool  Configure::setPasswd(QString ServerName){
    QTableWidgetItem *server, *user, *pass;
    for (int i=0;i<widget.userpass->rowCount();i++){
        server = widget.userpass->item(i,0);
        user = widget.userpass->item(i,1);
        pass = widget.userpass->item(i,2);
        if (server && user && pass ){
            if (ServerName.compare(server->text()) == 0){
                thisuser = user->text();
                thispass = pass->text();
                return true;
            }
        }
    }
    return false;
}

int Configure::getCwPitch()
{
    return widget.spinBox_cwPitch->value();
}


void Configure::on_spinBox_cwPitch_valueChanged(int arg1)
{
    qDebug()<<Q_FUNC_INFO<<": The cw pitch is now "<<widget.spinBox_cwPitch->value()<<" Hz and arg1 = "<< arg1;
    emit spinBox_cwPitchChanged(arg1);
}


void Configure::on_avgSpinBox_valueChanged(int arg1)
{
    emit avgSpinChanged(arg1);
}

/*
void Configure::onNbTransitionChanged(double arg1) // KD0OSS
{
    emit nbTransitionChanged(arg1);
}

void Configure::onNbLeadChanged(double arg1) // KD0OSS
{
    emit nbLeadChanged(arg1);
}

void Configure::onNbLagChanged(double arg1) // KD0OSS
{
    emit nbLagChanged(arg1);
}
*/
void Configure::onRxAgcSlopeChanged(int arg1) // KD0OSS
{
    emit agcSlopeChanged(arg1);
}

void Configure::onRxAgcMaxGainChanged(double arg1) // KD0OSS
{
    emit agcMaxGainChanged(arg1);
}

void Configure::onRxAgcAttackChanged(int arg1) // KD0OSS
{
    emit agcAttackChanged(arg1);
}

void Configure::onRxAgcDecayChanged(int arg1) // KD0OSS
{
    emit agcDecayChanged(arg1);
}

void Configure::onRxAgcHangChanged(int arg1) // KD0OSS
{
    emit agcHangChanged(arg1);
}

void Configure::onRxAgcFixedGainChanged(int arg1) // KD0OSS
{
    emit agcFixedGainChanged(arg1);
}

void Configure::onRxAgcHangThreshChanged(int arg1) // KD0OSS
{
    emit agcHangThreshChanged(arg1);
}

void Configure::onLevelerStateChanged(int arg1) // KD0OSS
{
    emit levelerStateChanged(arg1);
}

void Configure::onLevelerAttackChanged(int arg1) // KD0OSS
{
    emit levelerAttackChanged(arg1);
}

void Configure::onLevelerDecayChanged(int arg1) // KD0OSS
{
    emit levelerDecayChanged(arg1);
}

void Configure::onLevelerHangChanged(int arg1) // KD0OSS
{
    emit levelerHangChanged(arg1);
}

void Configure::onAlcStateChanged(int arg1) // KD0OSS
{
    emit alcStateChanged(arg1);
}

void Configure::onAlcAttackChanged(int arg1) // KD0OSS
{
    emit alcAttackChanged(arg1);
}

void Configure::onAlcDecayChanged(int arg1) // KD0OSS
{
    emit alcDecayChanged(arg1);
}

void Configure::onAlcMaxGainChanged(double arg1) // KD0OSS
{
    emit alcMaxGainChanged(arg1);
}

void Configure::setPTTKey(bool enabled) // KD0OSS
{
    capturePTTKey = enabled;
}

void Configure::keyReleaseEvent(QKeyEvent *event) // KD0OSS
{
    QString key;

    if (capturePTTKey)
    {
        pttKeyId = event->nativeVirtualKey();
        key = event->text();
        if (pttKeyId == 0xffe4) key = "R-CTRL";
        if (pttKeyId == 0xffea) key = "R-ALT";
        if (pttKeyId == 0xffe3) key = "L-CTRL";
        if (pttKeyId == 0xffe9) key = "L-ALT";
        if (pttKeyId == 0xffe2) key = "R-SHFT";
        if (pttKeyId == 0xffe1) key = "L-SHFT";
        if (pttKeyId == 0xff67) key = "MENU";
        if (pttKeyId == 0xffeb) key = "START";
        if (pttKeyId == 0xff08) key = "BKSP";
        if (pttKeyId == 0xff09) key = "TAB";
        if (pttKeyId == 0xff50) key = "HOME";
        if (pttKeyId == 0xff57) key = "END";
        if (pttKeyId == 0xffff) key = "DEL";
        if (pttKeyId == 0xff63) key = "INSRT";
        if (pttKeyId == 0xff55) key = "PGUP";
        if (pttKeyId == 0xff56) key = "PGDN";
        if (pttKeyId == 0xff52) key = "UP";
        if (pttKeyId == 0xff54) key = "DOWN";
        if (pttKeyId == 0xff51) key = "LEFT";
        if (pttKeyId == 0xff53) key = "RIGHT";
        if (pttKeyId == 0xffbe) key = "F1";
        if (pttKeyId == 0xffbf) key = "F2";
        if (pttKeyId == 0xffc0) key = "F3";
        if (pttKeyId == 0xffc1) key = "F4";
        if (pttKeyId == 0xffc2) key = "F5";
        if (pttKeyId == 0xffc3) key = "F6";
        if (pttKeyId == 0xffc4) key = "F7";
        if (pttKeyId == 0xffc5) key = "F8";
        if (pttKeyId == 0xffc6) key = "F9";
        if (pttKeyId == 0xffc7) key = "F10";
        if (pttKeyId == 0xffc8) key = "F11";
        if (pttKeyId == 0xffc9) key = "F12";
        if (key.isEmpty()) key = "NA";
        widget.pttKeyEdit->setText(QString("0x%1 [%2]").arg(pttKeyId, 8, 16, QChar('0')).arg(key));
        widget.pttKeySetButton->setChecked(false);
        capturePTTKey = false;
    }
}
