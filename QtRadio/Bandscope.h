#ifndef BANDSCOPE_H
#define BANDSCOPE_H

#include <QObject>

#include <QtCore>

#include <QFrame>

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
    void setSpan(double startHz, double spanHz);
    void setMarker(double hz);

    int8_t rfstream;
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
    double  spanStart;      // frequency at the left edge, Hz
    double  spanWidth;      // frequency span of the window, Hz
    double  markerHz;       // receiver frequency, < 0 for none
};

#endif // BANDSCOPE_H
