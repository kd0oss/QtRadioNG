#include "flexframe.h"
#include "ui_flexframe.h"
#include "../flexradio/flex.h"


FlexFrame::FlexFrame(Radio *pUI, QWidget *parent) : QDialog(parent), ui(new Ui::FlexFrame)
{
    ui->setupUi(this);
    pui = pUI;
    radio_id = -1;
    currentRxRfstream = -1;
    currentTxRfstream = -1;
    settings = new QSettings("FreeSDR", "QtRadioII");

    // UI.cpp asks for sample_rate[0] as soon as the receiver attaches,
    // so the saved settings are loaded now.
    settings->beginGroup("Flex");
    long rate = settings->value("rx_rate", FLEX_RX_RATE_DEFAULT).toInt();
    QString ant = settings->value("antenna", "ANT1").toString();
    int gain = settings->value("rf_gain", 0).toInt();
    int power = settings->value("rf_power", 50).toInt();
    settings->endGroup();

    ui->radio48000Chk->setChecked(rate == 48000);
    ui->radio96000Chk->setChecked(rate == 96000);
    ui->radio192000Chk->setChecked(rate != 48000 && rate != 96000);
    sample_rate[0] = checkedRate();
    int a = ui->antCombo->findText(ant);
    ui->antCombo->setCurrentIndex(a >= 0 ? a : 0);
    ui->rfGainSlider->setValue(gain);
    ui->rfGainLabel->setText(QString("RF gain %1 dB").arg(gain));
    ui->rfPowerSlider->setValue(power);
    ui->rfPowerLabel->setText(QString("RF power %1 %").arg(power));

    connect(ui->radio48000Chk, &QRadioButton::released, this, &FlexFrame::sampleRateChanged);
    connect(ui->radio96000Chk, &QRadioButton::released, this, &FlexFrame::sampleRateChanged);
    connect(ui->radio192000Chk, &QRadioButton::released, this, &FlexFrame::sampleRateChanged);
    connect(ui->antCombo, &QComboBox::currentIndexChanged, this, &FlexFrame::antennaChanged);
    connect(ui->rfGainSlider, &QSlider::valueChanged, this, &FlexFrame::rfGainChanged);
    connect(ui->rfPowerSlider, &QSlider::valueChanged, this, &FlexFrame::rfPowerChanged);

    itimer = new QTimer(this);
    itimer->setSingleShot(true);
    connect(itimer, &QTimer::timeout, this, &FlexFrame::initialize);
} // end constructor


FlexFrame::~FlexFrame()
{
    delete settings;
    delete ui;
} // end destructor


void FlexFrame::saveSetting(const QString &key, const QVariant &value)
{
    settings->beginGroup("Flex");
    settings->setValue(key, value);
    settings->endGroup();
} // end saveSetting


long FlexFrame::checkedRate(void) const
{
    if (ui->radio48000Chk->isChecked())
        return 48000;
    if (ui->radio96000Chk->isChecked())
        return 96000;
    return 192000;
} // end checkedRate


/* [rx stream][STARCOMMAND][command][argument] is passed on to flexradio by dspserver. */
void FlexFrame::sendStarCommand(char command, const QByteArray &argument)
{
    QByteArray buffer;

    if (currentRxRfstream < 0)
        return;
    buffer.append((char)currentRxRfstream);
    buffer.append((char)STARCOMMAND);
    buffer.append(command);
    buffer.append(argument);
    emit hhcommand(buffer);
} // end sendStarCommand


void FlexFrame::initializeRadio(void)
{
    QByteArray command;

    command.append((char)0);
    command.append((char)STARCOMMAND);
    command.append((char)FLEXSTARTRADIO);
    command.append((char)radio_id);
    emit hhcommand(command);
    itimer->start(500);
} // end initializeRadio


void FlexFrame::initialize(void)
{
    antennaChanged(ui->antCombo->currentIndex());
    rfGainChanged(ui->rfGainSlider->value());
    rfPowerChanged(ui->rfPowerSlider->value());
    if (!currentMode.isEmpty())
        sendStarCommand((char)FLEXSETMODE, currentMode.toUtf8());
} // end initialize


void FlexFrame::shutDown(void)
{
    QByteArray command;

    command.append((char)0);
    command.append((char)STARCOMMAND);
    command.append((char)FLEXSTOPRADIO);
    command.append((char)radio_id);
    emit hhcommand(command);
} // end shutDown


void FlexFrame::sampleRateChanged(void)
{
    QByteArray command;

    sample_rate[0] = checkedRate();
    saveSetting("rx_rate", (int)sample_rate[0]);
    if (currentRxRfstream < 0)
        return;
    command.append((char)currentRxRfstream);
    command.append((char)SETSAMPLERATE);
    command.append(QString::number(sample_rate[0]).toUtf8());
    emit hhcommand(command);
    emit sampleRateUpdated(sample_rate[0]);
} // end sampleRateChanged


void FlexFrame::antennaChanged(int)
{
    QString ant = ui->antCombo->currentText();

    saveSetting("antenna", ant);
    sendStarCommand((char)FLEXSETRXANT, ant.toUtf8());
} // end antennaChanged


void FlexFrame::rfGainChanged(int db)
{
    // snap to 10 dB steps; flexradio rounds again to the radio's own steps
    int snapped = (db >= 0 ? (db + 5) / 10 : (db - 5) / 10) * 10;
    if (snapped != db)
    {
        ui->rfGainSlider->setValue(snapped);
        return;
    }
    ui->rfGainLabel->setText(QString("RF gain %1 dB").arg(db));
    saveSetting("rf_gain", db);
    sendStarCommand((char)FLEXSETRFGAIN, QString::number(db).toUtf8());
} // end rfGainChanged


void FlexFrame::rfPowerChanged(int percent)
{
    ui->rfPowerLabel->setText(QString("RF power %1 %").arg(percent));
    saveSetting("rf_power", percent);
    sendStarCommand((char)FLEXSETRFPOWER, QString::number(percent).toUtf8());
} // end rfPowerChanged


void FlexFrame::setMode(const QString &mode)
{
    if (mode == currentMode)
        return;
    currentMode = mode;
    sendStarCommand((char)FLEXSETMODE, mode.toUtf8());
} // end setMode
