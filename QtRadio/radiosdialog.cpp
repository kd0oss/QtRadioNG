#include "radiosdialog.h"
#include "ui_radiosdialog.h"

RadiosDialog::RadiosDialog(QWidget *parent): QDialog(parent), ui(new Ui::RadiosDialog)
{
    ui->setupUi(this);

    servers = 0;
    selected_channel = -1;
    active_channels = 0;
    for (int i=0;i<4;i++)
    {
        manifest_xml[i].clear();
        available_xcvrs[i] = 0;
    }
/*
    ui->radioActRxChk_1->setCheckable(true);
    ui->radioActRxChk_2->setChecked(false);
    ui->radioActRxChk_3->setChecked(false);
    ui->radioActRxChk_4->setChecked(false);
    ui->radioActRxChk_5->setChecked(false);
    ui->radioActRxChk_6->setChecked(false);
    ui->radioActRxChk_7->setChecked(false);

    ui->radioActRxChk_2->setCheckable(false);
    ui->radioActRxChk_3->setCheckable(false);
    ui->radioActRxChk_4->setCheckable(false);
    ui->radioActRxChk_5->setCheckable(false);
    ui->radioActRxChk_6->setCheckable(false);
    ui->radioActRxChk_7->setCheckable(false);
*/
    connect(ui->radioListTable, SIGNAL(itemClicked(QTableWidgetItem*)), this, SLOT(getRadioDetails()));
    connect(ui->radioStartButton, SIGNAL(released()), this, SLOT(startRadio()));
}


RadiosDialog::~RadiosDialog()
{
    delete ui;
}

void RadiosDialog::fillRadioTable()
{
    QTableWidgetItem *newItem;
    QString           value;
    int               row=0;

    for (int i=0; i<active_channels; i++)
    {
      //  for (int x=0;x<available_xcvrs[i];x++)
        {
            ui->radioListTable->insertRow(row);
            //value = getXcvrProperty(i, x, "name");
            newItem = new QTableWidgetItem(QString("%1").arg(i+1));
            ui->radioListTable->setItem(row, 0, newItem);
//            value = getXcvrProperty(i, x, "radio_type");
            newItem = new QTableWidgetItem(QString("%1").arg(channel[i].radio_type));
            ui->radioListTable->setItem(row, 1, newItem);
//            value = getXcvrProperty(i, x, "supported_receivers");
            newItem = new QTableWidgetItem(QString("%1").arg(channel[i].receiver));
            newItem->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            ui->radioListTable->setItem(row, 2, newItem);
//            value = getXcvrProperty(i, x, "supported_transmitters");
            newItem = new QTableWidgetItem(QString("%1").arg(channel[i].transmitter));
            newItem->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            ui->radioListTable->setItem(row, 3, newItem);
            newItem = new QTableWidgetItem(QString("%1").arg(channel[i].enabled));
//            newItem = new QTableWidgetItem("No");
            ui->radioListTable->setItem(row, 4, newItem);
            row++;
        }
    }
} // end fillRadiotable


void RadiosDialog::startRadio()
{
    if (selected_channel < 0) return;
    /*
    receivers_active[0] = ui->radioActRxChk_1->isChecked();
    receivers_active[1] = ui->radioActRxChk_2->isChecked();
    receivers_active[2] = ui->radioActRxChk_3->isChecked();
    receivers_active[3] = ui->radioActRxChk_4->isChecked();
    receivers_active[4] = ui->radioActRxChk_5->isChecked();
    receivers_active[5] = ui->radioActRxChk_6->isChecked();
    receivers_active[6] = ui->radioActRxChk_7->isChecked();
*/
    remote_audio = ui->radioAudioRemoteChk->isChecked();
    remote_mic_audio = ui->radioMicRemoteChk->isChecked();

    if (ui->radio48000Chk->isChecked())
        sample_rate[0] = 48000;
    else
        if (ui->radio96000Chk->isChecked())
            sample_rate[0] = 96000;
        else
            if (ui->radio192000Chk->isChecked())
                sample_rate[0] = 192000;
            else
                if (ui->radio384000Chk->isChecked())
                    sample_rate[0] = 384000;
                else
                    if (ui->radio768000Chk->isChecked())
                        sample_rate[0] = 768000;
                    else
                        if (ui->radio1536000Chk->isChecked())
                            sample_rate[0] = 1536000;
   this->accept();
} // end startRadio


void RadiosDialog::getRadioDetails()
{
    int row = ui->radioListTable->currentRow();
    qDebug("Row: %d\n", row);
    selected_channel = row;
    radio_type = ui->radioListTable->item(row, 1)->text();
/*
    switch (ui->radioListTable->item(row, 2)->text().toInt())
    {
    case 1:
        ui->radioActRxChk_1->setCheckable(true);
    break;

    case 2:
        ui->radioActRxChk_1->setCheckable(true);
        ui->radioActRxChk_2->setCheckable(true);
        break;

    case 3:
        ui->radioActRxChk_1->setCheckable(true);
        ui->radioActRxChk_2->setCheckable(true);
        ui->radioActRxChk_3->setCheckable(true);
        break;

    case 4:
        ui->radioActRxChk_1->setCheckable(true);
        ui->radioActRxChk_2->setCheckable(true);
        ui->radioActRxChk_3->setCheckable(true);
        ui->radioActRxChk_4->setCheckable(true);
        break;

    case 5:
        ui->radioActRxChk_1->setCheckable(true);
        ui->radioActRxChk_2->setCheckable(true);
        ui->radioActRxChk_3->setCheckable(true);
        ui->radioActRxChk_4->setCheckable(true);
        ui->radioActRxChk_5->setCheckable(true);
        break;

    case 6:
        ui->radioActRxChk_1->setCheckable(true);
        ui->radioActRxChk_2->setCheckable(true);
        ui->radioActRxChk_3->setCheckable(true);
        ui->radioActRxChk_4->setCheckable(true);
        ui->radioActRxChk_5->setCheckable(true);
        ui->radioActRxChk_6->setCheckable(true);
        break;

    case 7:
        ui->radioActRxChk_1->setCheckable(true);
        ui->radioActRxChk_2->setCheckable(true);
        ui->radioActRxChk_3->setCheckable(true);
        ui->radioActRxChk_4->setCheckable(true);
        ui->radioActRxChk_5->setCheckable(true);
        ui->radioActRxChk_6->setCheckable(true);
        ui->radioActRxChk_7->setCheckable(true);
    }
    */
} // end getRadioDetails


QString RadiosDialog::getXcvrProperty(int server, int xcvr, const QString property)
{
    QStringList list;
    QString     result;
    bool        found = false;

    list = manifest_xml[server].split("\n", QString::SkipEmptyParts);
    for (int i=0; i<list.count(); i++)
    {
        if (list[i].contains("radio="))
        {
            QStringList l = list[i].split(">");
            if (l[0].split("=").at(1).toInt() == xcvr)
                found = true;
            else
                continue;
        }
        else
            if (list[i].toLower().contains(property.toLower()) && found)
            {
                QStringList l = list[i].split(">");
                result = l[1].split("<").at(0);
                break;
            }
    }
    return result;
} // end getXcvrProperty
