#ifndef BANDSCOPE_H
#define BANDSCOPE_H

#include <QObject>

#include <QtCore>

#if QT_VERSION >= 0x050000
#include <QtWidgets/QFrame>
#else
#include <QFrame>
#endif

#include <QPainter>
#include <QPoint>
#include <QTimer>

#include "Connection.h"

#define BANDSCOPE_PORT 9000

namespace Ui {
    class Bandscope;
}

class Bandscope : public QFrame
{
    Q_OBJECT

public:
    explicit Bandscope(SpectrumConnection *pConn, QWidget *parent = 0);
    ~Bandscope();

    bool isConnected;
    void closeEvent(QCloseEvent* event);
    void resizeEvent(QResizeEvent *event);
    void connect();
    void disconnect();

    int8_t channel;
    int8_t radio_id;

public slots:
    void bandscopeBuffer(SPECTRUM);
    void connected();
    void disconnected();
    void updateBandscope();

protected:
    void paintEvent(QPaintEvent*);

signals:
    void closeBandScope();

private:
    Ui::Bandscope *ui;

    WidebandConnection *connection;

    //SpectrumConnection *connection;

    int bandscopeHigh;
    int bandscopeLow;
    QString host;
    int     port;
    QVector <QPoint> plot;
};

#endif // BANDSCOPE_H
