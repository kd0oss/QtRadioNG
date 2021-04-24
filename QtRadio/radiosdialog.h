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
    int     selected_channel;
    bool    receivers_active[7];
    bool    remote_audio;
    bool    remote_mic_audio;
    long    sample_rate[7];
    int     active_channels;
    CHANNEL *channel;

    void fillRadioTable();

private:
    Ui::RadiosDialog *ui;

    QString getXcvrProperty(int server, int xcvr, const QString property);

private slots:
    void getRadioDetails();
    void startRadio();
};

#endif // RADIOSDIALOG_H
