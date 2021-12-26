#include "Bandscope.h"
#include "ui_Bandscope.h"
#include "Connection.h"

#include <QDebug>

Bandscope::Bandscope(SpectrumConnection *pConn, QWidget *parent):QFrame(parent), ui(new Ui::Bandscope)
{
    qDebug() << "Bandscope";
    ui->setupUi(this);

 //   resize(512, 100);
    isConnected = false;
    connection = new WidebandConnection();

    QObject::connect(connection, SIGNAL(bandscopeBuffer(spectrum)), this, SLOT(bandscopeBuffer(spectrum)));
    QObject::connect(connection, SIGNAL(bsConnected()), this, SLOT(connected()));

    bandscopeHigh = 0;
    bandscopeLow = -140;

    qDebug("Spec Port: %d", pConn->port);
    host = pConn->server;
    port = pConn->port;
}


Bandscope::~Bandscope()
{
    connection->disconnect();
    delete connection;
    delete ui;
}


void Bandscope::closeEvent(QCloseEvent* event)
{
    disconnect();
    emit closeBandScope();
}


void Bandscope::resizeEvent(QResizeEvent *event)
{
    updateBandscope();
}


void Bandscope::connect()
{
    qDebug("BS width: %d", width());
    if (width() < 512)
    {
        return;
    }
    connection->connect(host, port+1);
}


void Bandscope::disconnect()
{
    disconnected();
    connection->disconnect();
}


void Bandscope::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0, Qt::black);
    gradient.setColorAt(1, Qt::gray);
    painter.setBrush(gradient);
    painter.drawRect(0, 0, width(), height());

    // plot the vertical frequency lines
    float hzPerPixel = (float)61440000/(float)width();
    long long f = 0;

    for (int i=0;i<width();i++)
    {
        f = (long long)(hzPerPixel*(float)i);
        if (f > 0)
        {
            if ((f%10000000) < (long long)hzPerPixel)
            {
                painter.setOpacity(0.5);
                painter.setPen(QPen(Qt::white, 1, Qt::DotLine));
                painter.drawLine(i, 0, i, height());

                painter.setOpacity(1.0);
                painter.setPen(QPen(Qt::black, 1));
                painter.setFont(QFont("Arial", 10));
                painter.drawText(i, height(), QString::number(f/1000000));
            }
        }
    }

    // plot Spectrum
    painter.setOpacity(1.0);
    painter.setPen(QPen(Qt::yellow, 1));
    if (plot.count() == width())
    {
        painter.drawPolyline(plot.constData(),plot.count());
    }

}


void Bandscope::connected()
{
    QByteArray command;

    qDebug("Connected:  BS width: %d", width());
    command.clear();
    command.append((char)STARTBANDSCOPE);
    command.append(QString("%1").arg(width()));
    isConnected = true;
    connection->sendCommand(command);
}


void Bandscope::disconnected()
{
    QByteArray command;

    command.append((char)STOPBANDSCOPE);
    connection->sendCommand(command);
    qDebug() << "bandscope disabled";
}


void Bandscope::updateBandscope()
{
    QByteArray command;

    if (!isConnected) return;
    qDebug("Resize:  BS width: %d", width());
    command.clear();
    command.append((char)UPDATEBANDSCOPE);
    command.append(QString("%1").arg(width()));
    connection->sendCommand(command);
}


void Bandscope::bandscopeBuffer(spectrum spec)
{
    int i;

    if (spec.length == width())
    {
        float* samples = (float*) malloc(spec.length * sizeof (float));
        for (i=0;i<spec.length;i++)
        {
            samples[i] = -(spec.samples[i] & 0xFF);
        }
  //      qDebug() << "bandscope: create plot points";
        plot.clear();
        for (i=0;i<spec.length;i++)
        {
            plot << QPoint(i, (int)floor(((float)bandscopeHigh-samples[i])*(float) height()/(float)(bandscopeHigh - bandscopeLow)));
        }
        free(samples);
        repaint();
    }
    connection->freeBuffers(spec);
}
