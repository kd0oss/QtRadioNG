#include "radiosdialog.h"
#include "ui_radiosdialog.h"

RadiosDialog::RadiosDialog(QWidget *parent): QDialog(parent), ui(new Ui::RadiosDialog)
{
    ui->setupUi(this);

    servers = 0;
    selected_rfstream = -1;
    active_rfstreams = 0;
    txrxPair[0] = -1;
    txrxPair[1] = -1;
    local_audio = false;
    local_mic_audio = false;

    for (int i=0;i<4;i++)
    {
        manifest_xml[i].clear();
        available_xcvrs[i] = 0;
    }

    for (int i=0;i<8;i++)
    {
        receivers_active[i] = false;
        receiver_rfstream[i] = -1;
    }

    ui->receiver1Ckb->setEnabled(false);
    ui->receiver2Ckb->setChecked(false);
    ui->receiver3Ckb->setChecked(false);
    ui->receiver4Ckb->setChecked(false);
    ui->receiver5Ckb->setChecked(false);
    ui->receiver6Ckb->setChecked(false);
    ui->receiver7Ckb->setChecked(false);
    ui->receiver8Ckb->setChecked(false);

    ui->receiver2Ckb->setEnabled(false);
    ui->receiver3Ckb->setEnabled(false);
    ui->receiver4Ckb->setEnabled(false);
    ui->receiver5Ckb->setEnabled(false);
    ui->receiver6Ckb->setEnabled(false);
    ui->receiver7Ckb->setEnabled(false);
    ui->receiver8Ckb->setEnabled(false);

    connect(ui->radioList, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(getRadioDetails()));
    connect(ui->radioStartButton, SIGNAL(released()), this, SLOT(startRadio()));
}


RadiosDialog::~RadiosDialog()
{
    delete ui;
}


void RadiosDialog::fillRadioList()
{
    for (int i=0;i<active_radios;i++)
    {
        for (int x=0;x<active_rfstreams;x++)
        {
            if (rfstream[x].radio.radio_id == i)
            {
                ui->radioList->addItem(QString("%1   %2   %3   %4").arg(i+1).arg(rfstream[x].radio.radio_name).arg(rfstream[x].radio.mac_address).arg(rfstream[x].radio.ip_address));
                break;
            }
        }
    }
} // end fillRadioList


void RadiosDialog::startRadio()
{
    if (selected_rfstream < 0) return;

    receivers_active[0] = ui->receiver1Ckb->isChecked();
    receivers_active[1] = ui->receiver2Ckb->isChecked();
    receivers_active[2] = ui->receiver3Ckb->isChecked();
    receivers_active[3] = ui->receiver4Ckb->isChecked();
    receivers_active[4] = ui->receiver5Ckb->isChecked();
    receivers_active[5] = ui->receiver6Ckb->isChecked();
    receivers_active[6] = ui->receiver7Ckb->isChecked();
    receivers_active[7] = ui->receiver8Ckb->isChecked();

    if (ui->transPairedCB->currentIndex() > 0 && txrxPair[0] > -1)
        txrxPair[1] = receiver_rfstream[ui->transPairedCB->currentIndex()-1];

    local_audio = ui->radioAudioLocalChk->isChecked();
    local_mic_audio = ui->radioMicLocalChk->isChecked();

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
    int index = 0;
    int radio_id = ui->radioList->currentItem()->text().split(" ").at(0).toInt() - 1;

    txrxPair[0] = -1;
    txrxPair[1] = -1;

    selected_rfstream = radio_id;
    for (int x=0;x<active_rfstreams;x++)
    {
        if (rfstream[x].radio.radio_id == radio_id)
        {
            radio_type = rfstream[x].radio.radio_type;
            break;
        }
    }
    //radio_type = ui->radioList->currentItem()->text().split(" ").at(1);

    ui->receiver1Ckb->setChecked(false);
    ui->receiver2Ckb->setChecked(false);
    ui->receiver3Ckb->setChecked(false);
    ui->receiver4Ckb->setChecked(false);
    ui->receiver5Ckb->setChecked(false);
    ui->receiver6Ckb->setChecked(false);
    ui->receiver7Ckb->setChecked(false);
    ui->receiver8Ckb->setChecked(false);

    ui->receiver2Ckb->setEnabled(false);
    ui->receiver3Ckb->setEnabled(false);
    ui->receiver4Ckb->setEnabled(false);
    ui->receiver5Ckb->setEnabled(false);
    ui->receiver6Ckb->setEnabled(false);
    ui->receiver7Ckb->setEnabled(false);
    ui->receiver8Ckb->setEnabled(false);

    ui->radioAudioLocalChk->setEnabled(false);
    ui->radioMicLocalChk->setEnabled(false);

    ui->transPairedCB->clear();
    ui->transPairedCB->addItem("None");

    for (int x=0;x<active_rfstreams;x++)
    {
        if (rfstream[x].radio.radio_id == radio_id)
        {
            if (rfstream[x].radio.local_audio)
                ui->radioAudioLocalChk->setEnabled(true);
            else
            {
                ui->radioAudioRemoteChk->setChecked(true);
                ui->radioAudioLocalChk->setChecked(false);
                ui->radioAudioLocalChk->setEnabled(false);
            }

            if (rfstream[x].radio.local_mic)
                ui->radioMicLocalChk->setEnabled(true);
            else
            {
                ui->radioMicRemoteChk->setChecked(true);
                ui->radioMicLocalChk->setChecked(false);
                ui->radioMicLocalChk->setEnabled(false);
            }
        }

        if (index >= 8) break;
        if (rfstream[x].radio.radio_id == radio_id)
        {
            if (rfstream[x].isTX)
                txrxPair[0] = x;
            else
                switch (index)
                {
                case 0:
                    ui->receiver1Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 1");
                    receiver_rfstream[index] = x;
                    break;

                case 1:
                    ui->receiver2Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 2");
                    receiver_rfstream[index] = x;
                    break;

                case 2:
                    ui->receiver3Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 3");
                    receiver_rfstream[index] = x;
                    break;

                case 3:
                    ui->receiver4Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 4");
                    receiver_rfstream[index] = x;
                    break;

                case 4:
                    ui->receiver5Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 5");
                    receiver_rfstream[index] = x;
                    break;

                case 5:
                    ui->receiver6Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 6");
                    receiver_rfstream[index] = x;
                    break;

                case 6:
                    ui->receiver7Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 7");
                    receiver_rfstream[index] = x;
                    break;

                case 7:
                    ui->receiver8Ckb->setEnabled(true);
                    ui->transPairedCB->addItem("Recv 8");
                    receiver_rfstream[index] = x;
                    break;

                default:
                    break;
                }
            index++;
        }
    }
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
