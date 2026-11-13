#include "hackrfoneframe.h"
#include "ui_hackrfoneframe.h"
#include "../hackrf_one/hackrf.h"


hackrfOneframe::hackrfOneframe(Radio *pUI, QWidget *parent) : QDialog(parent), ui(new Ui::hackrfOneframe)
{
    ui->setupUi(this);
    pui = pUI;
    radio_id = -1;
    currentRxRfstream = -1;
    currentTxRfstream = -1;
    sample_rate[0] = HACKRF_RX_RATE_DEFAULT;
    hw_rate = HACKRF_HW_RATE_DEFAULT;
    settings = new QSettings("FreeSDR", "QtRadioII");

    // UI.cpp asks for sample_rate[0] as soon as the receiver attaches,
    // so the saved settings are loaded now rather than in initialize().
    loadSettings();

    connect(ui->radio8000000Chk, &QRadioButton::released, this, &hackrfOneframe::hwRateChanged);
    connect(ui->radio10000000Chk, &QRadioButton::released, this, &hackrfOneframe::hwRateChanged);
    connect(ui->radio12500000Chk, &QRadioButton::released, this, &hackrfOneframe::hwRateChanged);
    connect(ui->radio16000000Chk, &QRadioButton::released, this, &hackrfOneframe::hwRateChanged);
    connect(ui->radio20000000Chk, &QRadioButton::released, this, &hackrfOneframe::hwRateChanged);
    connect(ui->radio48000Chk, &QRadioButton::released, this, &hackrfOneframe::sampleRateChanged);
    connect(ui->radio96000Chk, &QRadioButton::released, this, &hackrfOneframe::sampleRateChanged);
    connect(ui->radio192000Chk, &QRadioButton::released, this, &hackrfOneframe::sampleRateChanged);
    connect(ui->radio384000Chk, &QRadioButton::released, this, &hackrfOneframe::sampleRateChanged);
    connect(ui->lnaGainSlider, &QSlider::valueChanged, this, &hackrfOneframe::lnaGainChanged);
    connect(ui->vgaGainSlider, &QSlider::valueChanged, this, &hackrfOneframe::vgaGainChanged);
    connect(ui->rfAmpChk, &QCheckBox::toggled, this, &hackrfOneframe::rfAmpToggled);
    connect(ui->antPowerChk, &QCheckBox::toggled, this, &hackrfOneframe::antPowerToggled);
    connect(ui->ppmSpin, &QDoubleSpinBox::valueChanged, this, &hackrfOneframe::ppmChanged);
    connect(ui->txGainSlider, &QSlider::valueChanged, this, &hackrfOneframe::txGainChanged);
    connect(ui->txAmpChk, &QCheckBox::toggled, this, &hackrfOneframe::txAmpToggled);
    connect(ui->micGainSlider, &QSlider::valueChanged, this, &hackrfOneframe::micGainChanged);

    itimer = new QTimer(this);
    itimer->setSingleShot(true);
    connect(itimer, &QTimer::timeout, this, &hackrfOneframe::initialize);
} // end constructor


hackrfOneframe::~hackrfOneframe()
{
    delete settings;
    delete ui;
} // end destructor


void hackrfOneframe::loadSettings(void)
{
    settings->beginGroup("HackrfOne");
    long rx = settings->value("rx_rate", (int)HACKRF_RX_RATE_DEFAULT).toInt();
    long hw = settings->value("hw_rate", (int)HACKRF_HW_RATE_DEFAULT).toInt();
    int lna = settings->value("lna_gain", HACKRF_LNA_GAIN_DEFAULT).toInt();
    int vga = settings->value("vga_gain", HACKRF_VGA_GAIN_DEFAULT).toInt();
    bool amp = settings->value("rf_amp", false).toBool();
    bool ant = settings->value("antenna_power", false).toBool();
    double ppm = settings->value("ppm", 0.0).toDouble();
    int txGain = settings->value("tx_gain", HACKRF_TX_GAIN_DEFAULT).toInt();
    bool txAmp = settings->value("tx_amp", false).toBool();
    int micGain = settings->value("mic_gain", 0).toInt();
    settings->endGroup();

    // set the widgets without sending anything: initialize() does that
    const QSignalBlocker b1(ui->lnaGainSlider);
    const QSignalBlocker b2(ui->vgaGainSlider);
    const QSignalBlocker b3(ui->rfAmpChk);
    const QSignalBlocker b4(ui->antPowerChk);
    const QSignalBlocker b5(ui->ppmSpin);
    const QSignalBlocker b6(ui->txGainSlider);
    const QSignalBlocker b7(ui->txAmpChk);
    const QSignalBlocker b8(ui->micGainSlider);

    ui->radio48000Chk->setChecked(rx == 48000);
    ui->radio96000Chk->setChecked(rx == 96000);
    ui->radio192000Chk->setChecked(rx == 192000);
    ui->radio384000Chk->setChecked(rx == 384000);
    ui->radio8000000Chk->setChecked(hw == 8000000);
    ui->radio10000000Chk->setChecked(hw == 10000000);
    ui->radio12500000Chk->setChecked(hw == 12500000);
    ui->radio16000000Chk->setChecked(hw == 16000000);
    ui->radio20000000Chk->setChecked(hw == 20000000);
    ui->lnaGainSlider->setValue(lna / 8);
    ui->lnaGainLabel->setText(QString("LNA %1 dB").arg(ui->lnaGainSlider->value() * 8));
    ui->vgaGainSlider->setValue(vga / 2);
    ui->vgaGainLabel->setText(QString("VGA %1 dB").arg(ui->vgaGainSlider->value() * 2));
    ui->rfAmpChk->setChecked(amp);
    ui->antPowerChk->setChecked(ant);
    ui->ppmSpin->setValue(ppm);
    ui->txGainSlider->setValue(txGain);
    ui->txGainLabel->setText(QString("TX %1 dB").arg(ui->txGainSlider->value()));
    ui->txAmpChk->setChecked(txAmp);
    ui->micGainSlider->setValue(micGain);
    ui->micGainLabel->setText(QString("Mic %1 dB").arg(ui->micGainSlider->value()));

    sample_rate[0] = checkedRxRate();
    hw_rate = checkedHwRate();
} // end loadSettings


void hackrfOneframe::saveSetting(const QString &key, const QVariant &value)
{
    settings->beginGroup("HackrfOne");
    settings->setValue(key, value);
    settings->endGroup();
} // end saveSetting


long hackrfOneframe::checkedRxRate(void) const
{
    if (ui->radio48000Chk->isChecked())
        return 48000;
    if (ui->radio96000Chk->isChecked())
        return 96000;
    if (ui->radio384000Chk->isChecked())
        return 384000;
    return 192000;
} // end checkedRxRate


long hackrfOneframe::checkedHwRate(void) const
{
    if (ui->radio8000000Chk->isChecked())
        return 8000000;
    if (ui->radio12500000Chk->isChecked())
        return 12500000;
    if (ui->radio16000000Chk->isChecked())
        return 16000000;
    if (ui->radio20000000Chk->isChecked())
        return 20000000;
    return 10000000;
} // end checkedHwRate


double hackrfOneframe::bandscopeCentre(long long rxFrequency) const
{
    return (double)(rxFrequency - HACKRF_IF_OFFSET);
} // end bandscopeCentre


/* [rx stream][STARCOMMAND][command][argument] is passed on to hackrf_one by dspserver. */
void hackrfOneframe::sendStarCommand(char command, const QByteArray &argument)
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


void hackrfOneframe::initializeRadio(void)
{
    QByteArray command;

    command.clear();
    command.append((char)0);
    command.append((char)STARCOMMAND);
    command.append((char)STARTRADIO);
    command.append((char)radio_id);
    emit hhcommand(command);
    itimer->start(500);
} // end initializeRadio


void hackrfOneframe::initialize(void)
{
    // send all hardware settings once the radio is running
    sendStarCommand((char)SETHWSAMPLERATE, QString::number(hw_rate).toUtf8());
    lnaGainChanged(ui->lnaGainSlider->value());
    vgaGainChanged(ui->vgaGainSlider->value());
    rfAmpToggled(ui->rfAmpChk->isChecked());
    antPowerToggled(ui->antPowerChk->isChecked());
    ppmChanged(ui->ppmSpin->value());
    txGainChanged(ui->txGainSlider->value());
    txAmpToggled(ui->txAmpChk->isChecked());
    micGainChanged(ui->micGainSlider->value());
    emit hwRateUpdated(hw_rate);
} // end initialize


void hackrfOneframe::shutDown(void)
{
    QByteArray command;

    command.clear();
    command.append((char)0);
    command.append((char)STARCOMMAND);
    command.append((char)STOPRADIO);
    command.append((char)radio_id);
    emit hhcommand(command);
} // end shutDown


void hackrfOneframe::sampleRateChanged(void)
{
    QByteArray command;

    sample_rate[0] = checkedRxRate();
    saveSetting("rx_rate", (int)sample_rate[0]);
    if (currentRxRfstream < 0)
        return;

    // the receiver rate is a dspserver command: it also retunes WDSP
    command.clear();
    command.append((char)currentRxRfstream);
    command.append((char)SETSAMPLERATE);
    command.append(QString::number(sample_rate[0]).toUtf8());
    emit hhcommand(command);
    emit sampleRateUpdated(sample_rate[0]);
} // end sampleRateChanged


void hackrfOneframe::hwRateChanged(void)
{
    hw_rate = checkedHwRate();
    saveSetting("hw_rate", (int)hw_rate);
    sendStarCommand((char)SETHWSAMPLERATE, QString::number(hw_rate).toUtf8());
    emit hwRateUpdated(hw_rate);
} // end hwRateChanged


void hackrfOneframe::lnaGainChanged(int step)
{
    int db = step * 8;

    ui->lnaGainLabel->setText(QString("LNA %1 dB").arg(db));
    saveSetting("lna_gain", db);
    sendStarCommand((char)SETLNAGAIN, QString::number(db).toUtf8());
} // end lnaGainChanged


void hackrfOneframe::vgaGainChanged(int step)
{
    int db = step * 2;

    ui->vgaGainLabel->setText(QString("VGA %1 dB").arg(db));
    saveSetting("vga_gain", db);
    sendStarCommand((char)SETVGAGAIN, QString::number(db).toUtf8());
} // end vgaGainChanged


void hackrfOneframe::rfAmpToggled(bool on)
{
    saveSetting("rf_amp", on);
    sendStarCommand((char)SETRFAMP, QByteArray(1, on ? 1 : 0));
} // end rfAmpToggled


void hackrfOneframe::antPowerToggled(bool on)
{
    saveSetting("antenna_power", on);
    sendStarCommand((char)SETANTPOWER, QByteArray(1, on ? 1 : 0));
} // end antPowerToggled


void hackrfOneframe::ppmChanged(double ppm)
{
    saveSetting("ppm", ppm);
    sendStarCommand((char)SETFREQCORR, QString::number(ppm, 'f', 1).toUtf8());
} // end ppmChanged


void hackrfOneframe::txGainChanged(int db)
{
    ui->txGainLabel->setText(QString("TX %1 dB").arg(db));
    saveSetting("tx_gain", db);
    sendStarCommand((char)SETTXVGAGAIN, QString::number(db).toUtf8());
} // end txGainChanged


void hackrfOneframe::txAmpToggled(bool on)
{
    saveSetting("tx_amp", on);
    sendStarCommand((char)SETTXRFAMP, QByteArray(1, on ? 1 : 0));
} // end txAmpToggled


/* Mic gain is a dspserver command for the transmit stream (WDSP TX panel gain, in dB). */
void hackrfOneframe::micGainChanged(int db)
{
    QByteArray command;

    ui->micGainLabel->setText(QString("Mic %1 dB").arg(db));
    saveSetting("mic_gain", db);
    if (currentTxRfstream < 0)
        return;
    command.append((char)currentTxRfstream);
    command.append((char)SETMICGAIN);
    command.append(QString::number(db).toUtf8());
    emit hhcommand(command);
} // end micGainChanged
