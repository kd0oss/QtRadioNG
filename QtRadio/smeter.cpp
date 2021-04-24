#include "smeter.h"
#include "Panadapter.h"
#include "UI.h"


sMeter::sMeter(QWidget* parent) : QFrame(parent)
{
    sMeterMain=new Meter("Main Rx", SIGMETER);
    meter1 = -121;
}

sMeter::~sMeter()
{

}

void sMeter::paintEvent(QPaintEvent*)
{
//qDebug() << "smeter.cpp - Meter value is equal to " << meter_dbm;
//return;
    // Draw the Main Rx S-Meter
    QPainter painter(this);
    QImage image = sMeterMain->getImage(meter1, meter2);  ////////// remove submeter
    painter.drawImage(4, 3, image);
}
