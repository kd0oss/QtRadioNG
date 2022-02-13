#include "hermesframe.h"
#include "ui_hermesframe.h"
#include "Mode.h"

#define STARCOMMAND 9

HermesFrame::HermesFrame(Radio *pUI, QWidget *parent) : QFrame(parent), ui(new Ui::HermesFrame)
{
    ui->setupUi(this);
    tuning = false;
    pui = pUI;

    radio_id = -1;
    currentRxChannel = -1;
    currentTxChannel = -1;

    // Load hardware settings
    settings = new QSettings("FreeSDR", "QtRadioII");
    settings->beginGroup("Hermes");
    int pwr = settings->value("tx_gain", 30).toInt();
    ui->hhTxGainSlider->setValue(pwr);
    pwrSliderValueChanged(pwr);
    pui->currentPwr = pwr/1000.0f;
    pwr = settings->value("tune_pwr", 30).toInt();
    ui->hhTxTunePwrSlider->setValue(pwr);
    ui->hhTxMicBoostCB->setChecked(settings->value("mic_boost_on", false).toBool());
    adjustMicBoost(settings->value("mic_boost_on", false).toBool());
    pwr = settings->value("tx_pwr", 30).toInt();
    ui->hhTxPowerSlider->setValue(pwr);
    adjustPower(pwr);
    pwr = settings->value("tx_line_gain", 50).toInt();
    ui->hhTxLineGainSlider->setValue(pwr);
    ui->hhPreampCB->setChecked(settings->value("preamp_on", false).toBool());
    enablePreamp(settings->value("preamp_on", false).toBool());
    ui->hhDitherCB->setChecked(settings->value("dither_on", false).toBool());
    enableDither(settings->value("dither_on", false).toBool());
    ui->hhRandomCB->setChecked(settings->value("random_on", false).toBool());
    enableRandom(settings->value("random_on", false).toBool());
    switch (settings->value("rx_ant").toInt())
    {
    case 0:
        ui->hhRxAnt0Radio->setChecked(true);
        break;
    case 1:
        ui->hhRxAnt1Radio->setChecked(true);
        break;
    case 2:
        ui->hhRxAnt2Radio->setChecked(true);
        break;
    default:
        break;
    };
    switch (settings->value("tx_ant").toInt())
    {
    case 0:
        ui->hhTxAnt0Radio->setChecked(true);
        break;
    case 1:
        ui->hhTxAnt1Radio->setChecked(true);
        break;
    case 2:
        ui->hhTxAnt2Radio->setChecked(true);
        break;
    default:
        break;
    };
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

    connect(ui->hhTxPowerSlider, SIGNAL(valueChanged(int)), this, SLOT(adjustPower(int)));
    connect(ui->hhTxMicBoostCB, SIGNAL(toggled(bool)), this, SLOT(adjustMicBoost(bool)));
    connect(ui->hhPreampCB, SIGNAL(toggled(bool)), this, SLOT(enablePreamp(bool)));
    connect(ui->hhDitherCB, SIGNAL(toggled(bool)), this, SLOT(enableDither(bool)));
    connect(ui->hhRandomCB, SIGNAL(toggled(bool)), this, SLOT(enableRandom(bool)));
    connect(ui->hhTxGainSlider, SIGNAL(valueChanged(int)), this, SLOT(pwrSliderValueChanged(int)));
    connect(ui->hhTxTunePwrSlider, SIGNAL(valueChanged(int)), this, SLOT(tunePwrSliderValueChanged(int)));
    connect(ui->hhRxAtt0Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAtt10Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAtt20Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAtt30Radio, SIGNAL(toggled(bool)), this, SLOT(setAttenuation(bool)));
    connect(ui->hhRxAttSlider, SIGNAL(sliderReleased()), this, SLOT(attSliderChanged()));
    connect(ui->hhIO0CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhIO1CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhIO2CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhIO3CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhIO4CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhIO5CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhIO6CB, SIGNAL(released()), this, SLOT(setOCOutputs()));
    connect(ui->hhTxTuneButton, SIGNAL(released()), this, SLOT(tuneClicked()));
    connect(ui->hhRxAnt0Radio, SIGNAL(released()), this, SLOT(setRxAntenna()));
    connect(ui->hhRxAnt1Radio, SIGNAL(released()), this, SLOT(setRxAntenna()));
    connect(ui->hhRxAnt2Radio, SIGNAL(released()), this, SLOT(setRxAntenna()));
    connect(ui->hhTxAnt0Radio, SIGNAL(released()), this, SLOT(setTxRelay()));
    connect(ui->hhTxAnt1Radio, SIGNAL(released()), this, SLOT(setTxRelay()));
    connect(ui->hhTxAnt2Radio, SIGNAL(released()), this, SLOT(setTxRelay()));
    connect(pUI, SIGNAL(tuningEnable(bool)), this, SLOT(tuningEnabled(bool)));

    itimer = new QTimer(this);
    itimer->setSingleShot(true);
    connect(itimer, SIGNAL(timeout()), this, SLOT(initialize()));
} // end constructor


HermesFrame::~HermesFrame()
{
    delete itimer;
    delete settings;
    delete ui;
} // end destructor


void HermesFrame::initializeRadio(void)
{
    QByteArray command;

    command.clear();
    command.append((char)0);
    command.append((char)STARCOMMAND);
    command.append((char)STARTRADIO);
    command.append((char)radio_id);
    emit hhcommand(command);
    itimer->start(1000);
} // end initializeRadio


void HermesFrame::initialize(void)
{
    setAttenuation(true);
    setRxAntenna();
    setTxRelay();
} // end initialize


void HermesFrame::shutDown(void)
{
    QByteArray command;

    command.clear();
    command.append((char)0);
    command.append((char)STARCOMMAND);
    command.append((char)STOPRADIO);
    command.append((char)radio_id);
    emit hhcommand(command);
} // end shutDown


void HermesFrame::tuneClicked(void)
{
    if (tuning)
    {
        tuning = 0;
        emit pttTuneChange(1, 0);
    }
    else
    {
        tuning = 1;
        emit pttTuneChange(1, 1);
    }
} // end tuneclicked


void HermesFrame::tuningEnabled(bool enabled)
{
    tuning = enabled;
} // end tuningEnabled


void HermesFrame::setAttenuation(bool button)
{
    QByteArray command;
    int atn = 0;

    if (button)
    {
        if (ui->hhRxAtt0Radio->isChecked())
            atn = 0;
        if (ui->hhRxAtt10Radio->isChecked())
            atn = 1;
        if (ui->hhRxAtt20Radio->isChecked())
            atn = 2;
        if (ui->hhRxAtt30Radio->isChecked())
            atn = 3;
        ui->hhRxAttSlider->setValue(-atn*10);
    }
    else
        atn = -ui->hhRxAttSlider->value() / 10;

    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETATTENUATOR);
    command.append(QString("%1").arg(atn));
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("attenuation", atn);
    settings->endGroup();
} // end setAttenuation


void HermesFrame::attSliderChanged(void)
{
    setAttenuation(false);
} // end attSliderChanged


void HermesFrame::setOCOutputs(void)
{
    QByteArray command;
    int io = 0;

    if (ui->hhIO0CB->isChecked())
        io = 0;
    if (ui->hhIO1CB->isChecked())
        io = 1;
    if (ui->hhIO2CB->isChecked())
        io = 2;
    if (ui->hhIO3CB->isChecked())
        io = 3;
    if (ui->hhIO4CB->isChecked())
        io = 4;
    if (ui->hhIO5CB->isChecked())
        io = 5;
    if (ui->hhIO6CB->isChecked())
        io = 6;

    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETOCOUTPUT);
    command.append((char)io);
    emit hhcommand(command);
} // end setOCOutputs


void HermesFrame::adjustPower(int pwr)
{
    QByteArray command;

    if (currentTxChannel < 0)
        return;
    command.clear();
    command.append((char)currentTxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETPOWEROUT);
    command.append((char)pwr);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("tx_pwr", pwr);
    settings->endGroup();
} // end adjustPower


void HermesFrame::adjustMicBoost(bool enable)
{
    QByteArray command;

    if (currentTxChannel < 0)
        return;
    command.clear();
    command.append((char)currentTxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETMICBOOST);
    command.append((char)enable);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("mic_boost_on", enable);
    settings->endGroup();
} // end adjustMicBoost


void HermesFrame::enablePreamp(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETPREAMP);
    command.append((char)enable);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("preamp_on", enable);
    settings->endGroup();
} // end enablePreamp


void HermesFrame::enableDither(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETDITHER);
    command.append((char)enable);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("dither_on", enable);
    settings->endGroup();
} // end enableDither


void HermesFrame::enableRandom(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETRANDOM);
    command.append((char)enable);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("random_on", enable);
    settings->endGroup();
} // end enableRandom


void HermesFrame::setRxAntenna(void)
{
    QByteArray command;
    char ant;

    if (ui->hhRxAnt0Radio->isChecked())
        ant = 0;
    if (ui->hhRxAnt1Radio->isChecked())
        ant = 1;
    if (ui->hhRxAnt2Radio->isChecked())
        ant = 2;
    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETRXANT);
    command.append((char)ant);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("rx_ant", ant);
    settings->endGroup();
} // end setRxAntenna


void HermesFrame::setTxRelay(void)
{
    QByteArray command;
    char ant;

    if (currentTxChannel < 0)
        return;
    if (ui->hhTxAnt0Radio->isChecked())
        ant = 0;
    if (ui->hhTxAnt1Radio->isChecked())
        ant = 1;
    if (ui->hhTxAnt2Radio->isChecked())
        ant = 2;
    command.clear();
    command.append((char)currentTxChannel);
    command.append((char)STARCOMMAND);
    command.append((char)SETTXRELAY);
    command.append((char)ant);
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("tx_ant", ant);
    settings->endGroup();
} // end setTxRelay


void HermesFrame::pwrSliderValueChanged(int pwr)
{
//    if (tuning) return;
    QByteArray command;

    if (currentTxChannel < 0)
        return;
    if (pui->mode[pui->channels[currentRxChannel].index].getMode() == MODE_AM || pui->mode[pui->channels[currentRxChannel].index].getMode() == MODE_SAM)
    {
        fprintf(stderr, "TX Gain slider: %d\n", pwr);
        command.clear();
        command.append((char)currentTxChannel);
        command.append((char)SETTXAMCARLEV);
        command.append(QString("%1").arg(pwr/1000.0));
    }
    else
    {
        command.clear();
        command.append((char)currentTxChannel);
        command.append((char)SETMICGAIN);
        command.append(QString("%1").arg(pwr/1000.0));
    }
//    qDebug("Message: %s", QString("%1").arg(pwr/1000.0, 0, 'f', 3).toLatin1().data());
    pui->currentPwr = pwr/1000.0f;
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("tx_gain", pwr);
    settings->endGroup();
} // end pwrSliderValueChanged


void HermesFrame::tunePwrSliderValueChanged(int pwr)
{
    if (currentTxChannel < 0)
        return;
    if (!tuning) return;
    QByteArray command;
    fprintf(stderr, "Tune slider: %d", pwr);
    command.clear();
    command.append((char)currentTxChannel);
    command.append((char)SETTXAMCARLEV);
    command.append(QString("%1").arg(pwr/1000.0));
    emit hhcommand(command);
    settings->beginGroup("Hermes");
    settings->setValue("tune_pwr", pwr);
    settings->endGroup();
} // end tunePwrSliderValueChanged


void HermesFrame::getSerial(void)
{
    QByteArray command;

    command.clear();
    command.append((char)currentRxChannel);
    command.append((char)STARGETSERIAL);
    emit hhcommand(command);
} // end getSerial
