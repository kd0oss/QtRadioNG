#include "EqualizerDialog.h"
#include "ui_EqualizerDialog.h"
#include "UI.h"

EqualizerDialog::EqualizerDialog(ServerConnection *pConn, QWidget *parent) : QDialog(parent), ui(new Ui::EqualizerDialog)
{
    ui->setupUi(this);

    connection = pConn;
    currentRxRfstream = -1;
    currentTxRfstream = -1;

    connect(ui->rxEqPreampSlider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq32Slider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq63Slider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq125Slider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq250Slider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq500Slider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq1KSlider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq2KSlider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq4KSlider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq8KSlider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);
    connect(ui->rxEq16KSlider, &QSlider::valueChanged, this, &EqualizerDialog::rxSliderChanged);


    connect(ui->txEqPreampSlider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq32Slider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq63Slider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq125Slider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq250Slider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq500Slider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq1KSlider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq2KSlider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq4KSlider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq8KSlider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);
    connect(ui->txEq16KSlider, &QSlider::valueChanged, this, &EqualizerDialog::txSliderChanged);

    connect(ui->rxEqResetButton, &QPushButton::clicked, this, &EqualizerDialog::resetRx);
    connect(ui->txEqResetButton, &QPushButton::clicked, this, &EqualizerDialog::resetTx);

    connect(ui->eq3BandButton, &QRadioButton::clicked, this, &EqualizerDialog::set3BandEqualizer);
    connect(ui->eq10BandButton, &QRadioButton::clicked, this, &EqualizerDialog::set10BandEqualizer);

    connect(ui->eqSaveButton, &QPushButton::clicked, this, &EqualizerDialog::saveSettings);
}

EqualizerDialog::~EqualizerDialog()
{
    delete ui;
}

void EqualizerDialog::resetRx(void)
{
    ui->rxEqPreampSlider->setValue(0);
    ui->rxEq32Slider->setValue(0);
    ui->rxEq63Slider->setValue(0);
    ui->rxEq125Slider->setValue(0);
    ui->rxEq250Slider->setValue(0);
    ui->rxEq500Slider->setValue(0);
    ui->rxEq1KSlider->setValue(0);
    ui->rxEq2KSlider->setValue(0);
    ui->rxEq4KSlider->setValue(0);
    ui->rxEq8KSlider->setValue(0);
    ui->rxEq16KSlider->setValue(0);
} // end resetRx


void EqualizerDialog::resetTx(void)
{
    ui->txEqPreampSlider->setValue(0);
    ui->txEq32Slider->setValue(0);
    ui->txEq63Slider->setValue(0);
    ui->txEq125Slider->setValue(0);
    ui->txEq250Slider->setValue(0);
    ui->txEq500Slider->setValue(0);
    ui->txEq1KSlider->setValue(0);
    ui->txEq2KSlider->setValue(0);
    ui->txEq4KSlider->setValue(0);
    ui->txEq8KSlider->setValue(0);
    ui->txEq16KSlider->setValue(0);
} // end resetTx


void EqualizerDialog::set3BandEqualizer(void)
{
    QSettings settings("FreeSDR", "QtRadioII");

    ui->rxEq32Label->setText("Low");
    ui->rxEq63Label->setText("Med");
    ui->rxEq125Label->setText("High");
    ui->rxEq250Label->setEnabled(false);
    ui->rxEq500Label->setEnabled(false);
    ui->rxEq1kLabel->setEnabled(false);
    ui->rxEq2kLabel->setEnabled(false);
    ui->rxEq4kLabel->setEnabled(false);
    ui->rxEq8kLabel->setEnabled(false);
    ui->rxEq16kLabel->setEnabled(false);

    ui->rxEq250Slider->setEnabled(false);
    ui->rxEq500Slider->setEnabled(false);
    ui->rxEq1KSlider->setEnabled(false);
    ui->rxEq2KSlider->setEnabled(false);
    ui->rxEq4KSlider->setEnabled(false);
    ui->rxEq8KSlider->setEnabled(false);
    ui->rxEq16KSlider->setEnabled(false);

    ui->txEq32Label->setText("Low");
    ui->txEq63Label->setText("Med");
    ui->txEq125Label->setText("High");
    ui->txEq250Label->setEnabled(false);
    ui->txEq500Label->setEnabled(false);
    ui->txEq1kLabel->setEnabled(false);
    ui->txEq2kLabel->setEnabled(false);
    ui->txEq4kLabel->setEnabled(false);
    ui->txEq8kLabel->setEnabled(false);
    ui->txEq16kLabel->setEnabled(false);

    ui->txEq250Slider->setEnabled(false);
    ui->txEq500Slider->setEnabled(false);
    ui->txEq1KSlider->setEnabled(false);
    ui->txEq2KSlider->setEnabled(false);
    ui->txEq4KSlider->setEnabled(false);
    ui->txEq8KSlider->setEnabled(false);
    ui->txEq16KSlider->setEnabled(false);

    resetRx();
    resetTx();
    loadSettings3Band();
    settings.beginGroup("AudioEqualizer");
    settings.setValue("eqMode", 3);
    settings.endGroup();
    ui->eq3BandButton->setCheckable(true);
} // end set3BandEqualizer


void EqualizerDialog::set10BandEqualizer(void)
{
    QSettings settings("FreeSDR", "QtRadioII");

    ui->rxEq32Label->setText("32");
    ui->rxEq63Label->setText("63");
    ui->rxEq125Label->setText("125");
    ui->rxEq250Label->setEnabled(true);
    ui->rxEq500Label->setEnabled(true);
    ui->rxEq1kLabel->setEnabled(true);
    ui->rxEq2kLabel->setEnabled(true);
    ui->rxEq4kLabel->setEnabled(true);
    ui->rxEq8kLabel->setEnabled(true);
    ui->rxEq16kLabel->setEnabled(true);

    ui->rxEq250Slider->setEnabled(true);
    ui->rxEq500Slider->setEnabled(true);
    ui->rxEq1KSlider->setEnabled(true);
    ui->rxEq2KSlider->setEnabled(true);
    ui->rxEq4KSlider->setEnabled(true);
    ui->rxEq8KSlider->setEnabled(true);
    ui->rxEq16KSlider->setEnabled(true);

    ui->txEq32Label->setText("32");
    ui->txEq63Label->setText("63");
    ui->txEq125Label->setText("125");
    ui->txEq250Label->setEnabled(true);
    ui->txEq500Label->setEnabled(true);
    ui->txEq1kLabel->setEnabled(true);
    ui->txEq2kLabel->setEnabled(true);
    ui->txEq4kLabel->setEnabled(true);
    ui->txEq8kLabel->setEnabled(true);
    ui->txEq16kLabel->setEnabled(true);

    ui->txEq250Slider->setEnabled(true);
    ui->txEq500Slider->setEnabled(true);
    ui->txEq1KSlider->setEnabled(true);
    ui->txEq2KSlider->setEnabled(true);
    ui->txEq4KSlider->setEnabled(true);
    ui->txEq8KSlider->setEnabled(true);
    ui->txEq16KSlider->setEnabled(true);

    resetRx();
    resetTx();
    loadSettings10Band();
    settings.beginGroup("AudioEqualizer");
    settings.setValue("eqMode", 10);
    settings.endGroup();
    ui->eq10BandButton->setChecked(true);
} // end set10BandEqualizer


void EqualizerDialog::rxSliderChanged(void)
{
    QByteArray command;
    QString line;
    int values[11];
    int bands = 3;

    if (currentRxRfstream < 0) return;

    values[0] = ui->rxEqPreampSlider->value();
    values[1] = ui->rxEq32Slider->value();
    values[2] = ui->rxEq63Slider->value();
    values[3] = ui->rxEq125Slider->value();
    if (ui->eq10BandButton->isChecked())
    {
        bands = 10;
        values[4] = ui->rxEq250Slider->value();
        values[5] = ui->rxEq500Slider->value();
        values[6] = ui->rxEq1KSlider->value();
        values[7] = ui->rxEq2KSlider->value();
        values[8] = ui->rxEq4KSlider->value();
        values[9] = ui->rxEq8KSlider->value();
        values[10] = ui->rxEq16KSlider->value();
        command.clear();
        line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11").arg(values[0]).arg(values[1]).arg(values[2]).
               arg(values[3]).arg(values[4]).arg(values[5]).arg(values[6]).arg(values[7]).arg(values[8]).arg(values[9]).arg(values[10]);
    }
    else
    {
        command.clear();
        line = QString("%1 %2 %3 %4").arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]);
    }
    command.append((char)currentRxRfstream);
    command.append((char)SETRXEQPRO);
    command.append((char)bands);
    command.append(line.toUtf8());
    connection->sendCommand(command);
    qDebug() << Q_FUNC_INFO << ":   The command sent is is " << command;
} // end rxSliderChanged


void EqualizerDialog::txSliderChanged(void)
{
    QByteArray command;
    QString line;
    int values[11];
    int bands = 3;

    if (currentTxRfstream < 0) return;

    values[0] = ui->txEqPreampSlider->value();
    values[1] = ui->txEq32Slider->value();
    values[2] = ui->txEq63Slider->value();
    values[3] = ui->txEq125Slider->value();
    if (ui->eq10BandButton->isChecked())
    {
        bands = 10;
        values[4] = ui->txEq250Slider->value();
        values[5] = ui->txEq500Slider->value();
        values[6] = ui->txEq1KSlider->value();
        values[7] = ui->txEq2KSlider->value();
        values[8] = ui->txEq4KSlider->value();
        values[9] = ui->txEq8KSlider->value();
        values[10] = ui->txEq16KSlider->value();
        command.clear();
        line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11").arg(values[0]).arg(values[1]).arg(values[2]).
               arg(values[3]).arg(values[4]).arg(values[5]).arg(values[6]).arg(values[7]).arg(values[8]).arg(values[9]).arg(values[10]);
    }
    else
    {
        command.clear();
        line = QString("%1 %2 %3 %4").arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]);
    }
    command.append((char)currentTxRfstream);
    command.append((char)SETTXEQPRO);
    command.append((char)bands);
    command.append(line.toUtf8());
    connection->sendCommand(command);
    qDebug() << Q_FUNC_INFO << ":   The command sent is is " << command;
} // end txSliderChanged


void EqualizerDialog::saveSettings(void)
{
    if (ui->eq3BandButton->isChecked())
        saveSettings3Band();
    else
        saveSettings10Band();
} // end saveSettings


void EqualizerDialog::saveSettings3Band(void)
{
    QSettings settings("G0ORX", "QtRadio");

    settings.beginGroup("AudioEqualizer3Band");
    settings.setValue("rxPreamp", ui->rxEqPreampSlider->value());
    settings.setValue("rxEqLow", ui->rxEq32Slider->value());
    settings.setValue("rxEqMed", ui->rxEq63Slider->value());
    settings.setValue("rxEqHi", ui->rxEq125Slider->value());
    settings.setValue("txPreamp", ui->txEqPreampSlider->value());
    settings.setValue("txEqLow", ui->txEq32Slider->value());
    settings.setValue("txEqMed", ui->txEq63Slider->value());
    settings.setValue("txEqHi", ui->txEq125Slider->value());
    settings.endGroup();
} // end saveSettings3Band


void EqualizerDialog::loadSettings3Band(void)
{
    QSettings settings("G0ORX", "QtRadio");

    settings.beginGroup("AudioEqualizer3Band");
    ui->rxEqPreampSlider->setValue(settings.value("rxPreamp").toInt());
    ui->rxEq32Slider->setValue(settings.value("rxEqLow").toInt());
    ui->rxEq63Slider->setValue(settings.value("rxEqMed").toInt());
    ui->rxEq125Slider->setValue(settings.value("rxEqHi").toInt());
    ui->txEqPreampSlider->setValue(settings.value("txPreamp").toInt());
    ui->txEq32Slider->setValue(settings.value("txEqLow").toInt());
    ui->txEq63Slider->setValue(settings.value("txEqMed").toInt());
    ui->txEq125Slider->setValue(settings.value("txEqHi").toInt());
    settings.endGroup();
} // end loadSettings3Band


void EqualizerDialog::saveSettings10Band(void)
{
    QSettings settings("FreeSDR", "QtRadioII");

    settings.beginGroup("AudioEqualizer10Band");
    settings.setValue("rxPreamp", ui->rxEqPreampSlider->value());
    settings.setValue("rxEq32", ui->rxEq32Slider->value());
    settings.setValue("rxEq63", ui->rxEq63Slider->value());
    settings.setValue("rxEq125", ui->rxEq125Slider->value());
    settings.setValue("rxEq250", ui->rxEq250Slider->value());
    settings.setValue("rxEq500", ui->rxEq500Slider->value());
    settings.setValue("rxEq1K", ui->rxEq1KSlider->value());
    settings.setValue("rxEq2K", ui->rxEq2KSlider->value());
    settings.setValue("rxEq4K", ui->rxEq4KSlider->value());
    settings.setValue("rxEq8K", ui->rxEq8KSlider->value());
    settings.setValue("rxEq16K", ui->rxEq16KSlider->value());

    settings.setValue("txPreamp", ui->txEqPreampSlider->value());
    settings.setValue("txEq32", ui->txEq32Slider->value());
    settings.setValue("txEq63", ui->txEq63Slider->value());
    settings.setValue("txEq125", ui->txEq125Slider->value());
    settings.setValue("txEq250", ui->txEq250Slider->value());
    settings.setValue("txEq500", ui->txEq500Slider->value());
    settings.setValue("txEq1K", ui->txEq1KSlider->value());
    settings.setValue("txEq2K", ui->txEq2KSlider->value());
    settings.setValue("txEq4K", ui->txEq4KSlider->value());
    settings.setValue("txEq8K", ui->txEq8KSlider->value());
    settings.setValue("txEq16K", ui->txEq16KSlider->value());
    settings.endGroup();

    rxSliderChanged();
    txSliderChanged();
} // end saveSettings10Band


void EqualizerDialog::loadSettings10Band(void)
{
    QSettings settings("FreeSDR", "QtRadioII");

    settings.beginGroup("AudioEqualizer10Band");
    ui->rxEqPreampSlider->setValue(settings.value("rxPreamp").toInt());
    ui->rxEq32Slider->setValue(settings.value("rxEq32").toInt());
    ui->rxEq63Slider->setValue(settings.value("rxEq63").toInt());
    ui->rxEq125Slider->setValue(settings.value("rxEq125").toInt());
    ui->rxEq250Slider->setValue(settings.value("rxEq250").toInt());
    ui->rxEq500Slider->setValue(settings.value("rxEq500").toInt());
    ui->rxEq1KSlider->setValue(settings.value("rxEq1K").toInt());
    ui->rxEq2KSlider->setValue(settings.value("rxEq2K").toInt());
    ui->rxEq4KSlider->setValue(settings.value("rxEq4K").toInt());
    ui->rxEq8KSlider->setValue(settings.value("rxEq8K").toInt());
    ui->rxEq16KSlider->setValue(settings.value("rxEq16K").toInt());

    ui->txEqPreampSlider->setValue(settings.value("txPreamp").toInt());
    ui->txEq32Slider->setValue(settings.value("txEq32").toInt());
    ui->txEq63Slider->setValue(settings.value("txEq63").toInt());
    ui->txEq125Slider->setValue(settings.value("txEq125").toInt());
    ui->txEq250Slider->setValue(settings.value("txEq250").toInt());
    ui->txEq500Slider->setValue(settings.value("txEq500").toInt());
    ui->txEq1KSlider->setValue(settings.value("txEq1K").toInt());
    ui->txEq2KSlider->setValue(settings.value("txEq2K").toInt());
    ui->txEq4KSlider->setValue(settings.value("txEq4K").toInt());
    ui->txEq8KSlider->setValue(settings.value("txEq8K").toInt());
    ui->txEq16KSlider->setValue(settings.value("txEq16K").toInt());
    settings.endGroup();

    rxSliderChanged();
    txSliderChanged();
} // end loadSettings10Band

