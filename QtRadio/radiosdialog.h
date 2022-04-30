#ifndef RADIOSDIALOG_H
#define RADIOSDIALOG_H

#include <QDialog>
#include "Connection.h"

namespace Ui {
class RadiosDialog;
}

class RadiosDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RadiosDialog(QWidget *parent = 0);
    ~RadiosDialog();

    QString manifest_xml[4];
    QString radio_type;
    int     available_xcvrs[4];
    int     servers;
    int8_t  selected_channel;
    bool    receivers_active[8];
    int8_t  receiver_channel[8];
    bool    remote_audio;
    bool    remote_mic_audio;
    long    sample_rate[8];
    int     active_radios;
    int     active_channels;
    int8_t  txrxPair[2];
    CHANNEL *channels;

    void fillRadioList();

private:
    Ui::RadiosDialog *ui;

    QString getXcvrProperty(int server, int xcvr, const QString property);

private slots:
    void getRadioDetails();
    void startRadio();
};

#endif // RADIOSDIALOG_H
