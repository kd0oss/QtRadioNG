#include "hermesframe.h"
#include "ui_hermesframe.h"
#include "Mode.h"

#define STARCOMMAND 9

HermesFrame::HermesFrame(UI *pUI, QWidget *parent) : QFrame(parent), ui(new Ui::HermesFrame)
{
    ui->setupUi(this);
    tuning = false;
    pui = pUI;

    setAttenuation(true);
    setRxAntenna();
    setTxRelay();

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
    connect(pUI, SIGNAL(tuningEnable(bool)), this, SLOT(tuningEnabled(bool)));
    connect(ui->hhRxAnt0Radio, SIGNAL(released()), this, SLOT(setRxAntenna()));
    connect(ui->hhRxAnt1Radio, SIGNAL(released()), this, SLOT(setRxAntenna()));
    connect(ui->hhRxAnt2Radio, SIGNAL(released()), this, SLOT(setRxAntenna()));
    connect(ui->hhTxAnt0Radio, SIGNAL(released()), this, SLOT(setTxRelay()));
    connect(ui->hhTxAnt1Radio, SIGNAL(released()), this, SLOT(setTxRelay()));
    connect(ui->hhTxAnt2Radio, SIGNAL(released()), this, SLOT(setTxRelay()));
} // end constructor


HermesFrame::~HermesFrame()
{
    delete ui;
} // end destructor


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
 //   else
 //       atn = -ui->hhRxAttSlider->value();

    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETATTENUATOR);
    command.append(QString("%1").arg(atn));
    emit hhcommand(command);
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
    command.append((char)STARCOMMAND);
    command.append((char)SETOCOUTPUT);
    command.append((char)io);
    emit hhcommand(command);
} // end setOCOutputs


void HermesFrame::adjustPower(int pwr)
{
    QByteArray command;

    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETPOWEROUT);
    command.append((char)pwr);
    emit hhcommand(command);
} // end adjustPower


void HermesFrame::adjustMicBoost(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETMICBOOST);
    command.append((char)enable);
    emit hhcommand(command);
} // end adjustMicBoost


void HermesFrame::enablePreamp(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETPREAMP);
    command.append((char)enable);
    emit hhcommand(command);
} // end enablePreamp


void HermesFrame::enableDither(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETDITHER);
    command.append((char)enable);
    emit hhcommand(command);
} // end enableDither


void HermesFrame::enableRandom(bool enable)
{
    QByteArray command;

    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETRANDOM);
    command.append((char)enable);
    emit hhcommand(command);
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
    command.append((char)STARCOMMAND);
    command.append((char)SETRXANT);
    command.append((char)ant);
    emit hhcommand(command);
} // end setRxAntenna


void HermesFrame::setTxRelay(void)
{
    QByteArray command;
    char ant;

    if (ui->hhTxAnt0Radio->isChecked())
        ant = 0;
    if (ui->hhTxAnt1Radio->isChecked())
        ant = 1;
    if (ui->hhTxAnt2Radio->isChecked())
        ant = 2;
    command.clear();
    command.append((char)STARCOMMAND);
    command.append((char)SETTXRELAY);
    command.append((char)ant);
    emit hhcommand(command);
} // end setTxRelay


void HermesFrame::pwrSliderValueChanged(int pwr)
{
    if (tuning) return;
    QByteArray command;

    if (pui->mode.getMode() == MODE_AM || pui->mode.getMode() == MODE_SAM)
    {
        fprintf(stderr, "TX Gain slider: %d\n", pwr);
        command.clear();
        command.append((char)SETTXAMCARLEV);
        command.append(QString("%1").arg(pwr/1000.0));
    }
    else
    {
        command.clear();
        command.append((char)SETMICGAIN);
        command.append(QString("%1").arg(pwr/1000.0));
    }
//    qDebug("Message: %s", QString("%1").arg(pwr/1000.0, 0, 'f', 3).toLatin1().data());
    emit hhcommand(command);
} // end pwrSliderValueChanged


void HermesFrame::tunePwrSliderValueChanged(int pwr)
{
    if (!tuning) return;
    QByteArray command;
    fprintf(stderr, "Tune slider: %d", pwr);
    command.clear();
    command.append((char)SETTXAMCARLEV);
    command.append(QString("%1").arg(pwr/1000.0));
    emit hhcommand(command);
} // end tunePwrSliderValueChanged


void HermesFrame::getSerial(void)
{
    QByteArray command;

    command.clear();
    command.append((char)STARGETSERIAL);
    emit hhcommand(command);
} // end getSerial
