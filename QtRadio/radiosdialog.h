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
    int8_t  selected_rfstream;
    bool    receivers_active[8];
    int8_t  receiver_rfstream[8];
    bool    local_audio;
    bool    local_mic_audio;
    long    sample_rate[8];
    int     active_radios;
    int     active_rfstreams;
    int8_t  txrxPair[2];
    RFSTREAM *rfstream;

    void fillRadioList();

private:
    Ui::RadiosDialog *ui;

    QString getXcvrProperty(int server, int xcvr, const QString property);

private slots:
    void getRadioDetails();
    void startRadio();
};

#endif // RADIOSDIALOG_H
