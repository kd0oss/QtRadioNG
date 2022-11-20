#include "sdr1000frame.h"
#include "ui_sdr1000frame.h"
#include "Mode.h"

#define STARCOMMAND 9
#define SETATTENUATOR 0

Sdr1000Frame::Sdr1000Frame(Radio *pUI, QWidget *parent) : QFrame(parent), ui(new Ui::Sdr1000Frame)
{
    ui->setupUi(this);
    tuning = false;
    pui = pUI;

    radio_id = -1;
    currentRxRfstream = -1;
    currentTxRfstream = -1;

    // Load hardware settings
    settings = new QSettings("FreeSDR", "QtRadioII");
    settings->beginGroup("Sdr1000");
    int pwr = settings->value("tx_gain", 30).toInt();
    ui->hhTxGainSlider->setValue(pwr);
 //   pwrSliderValueChanged(pwr);
    pui->currentPwr = pwr/1000.0f;
    pwr = settings->value("tune_pwr", 30).toInt();
    ui->hhTxTunePwrSlider->setValue(pwr);
    pwr = settings->value("tx_pwr", 30).toInt();
    ui->hhTxPowerSlider->setValue(pwr);
 //   adjustPower(pwr);
    pwr = settings->value("tx_line_gain", 50).toInt();
    ui->hhTxLineGainSlider->setValue(pwr);
    switch (settings->value("attenuation").toInt())
    {
    case 0:
        ui->hhRxAtt0Radio->setChecked(true);
        break;
    case 1:
        ui->hhRxAtt10Radio->setChecked(true);
        break;
    case 2:
        ui->hhRxAtt20Radio->setChecked(true);
        break;
    case 3:
        ui->hhRxAtt30Radio->setChecked(true);
        break;
    default:
        break;
    };
    settings->endGroup();

    connect(ui->hhRxAtt0Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAtt10Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAtt20Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAtt30Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));

    itimer = new QTimer(this);
    itimer->setSingleShot(true);
    connect(itimer, SIGNAL(timeout()), this, SLOT(initialize()));
} // end constructor


Sdr1000Frame::~Sdr1000Frame()
{
    delete itimer;
    delete settings;
    delete ui;
} // end destructor


void Sdr1000Frame::initializeRadio(void)
{
    QByteArray command;

    command.clear();
    command.append((char)0);
    command.append((char)STARCOMMAND);
//    command.append((char)STARTRADIO);
    command.append((char)radio_id);
//    emit hhcommand(command);
    itimer->start(500);
} // end initializeRadio


void Sdr1000Frame::initialize(void)
{
    setAttenuation(true);
} // end initialize


void Sdr1000Frame::shutDown(void)
{
    QByteArray command;

    command.clear();
    command.append((char)0);
    command.append((char)STARCOMMAND);
//    command.append((char)STOPRADIO);
    command.append((char)radio_id);
//    emit hhcommand(command);
} // end shutDown


void Sdr1000Frame::tuningEnabled(bool enabled)
{
    tuning = enabled;
} // end tuningEnabled


void Sdr1000Frame::setAttenuation(bool button)
{
    QByteArray command;
    int atn = 0;

    if (button)
    {
        if (ui->hhRxAtt0Radio->isChecked())
            atn = 0;
        if (ui->hhRxAtt10Radio->isChecked())
            atn = 10;
        if (ui->hhRxAtt20Radio->isChecked())
            atn = 20;
        if (ui->hhRxAtt30Radio->isChecked())
            atn = 30;
        ui->hhRxAttSlider->setValue(-atn*10);
    }
    else
        atn = -ui->hhRxAttSlider->value();

    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)STARCOMMAND);
    command.append((char)SETATTENUATOR);
    command.append(QString("%1").arg(atn));
    emit hhcommand(command);
    settings->beginGroup("Sdr1000");
    settings->setValue("attenuation", atn / 10);
    settings->endGroup();
} // end setAttenuation


