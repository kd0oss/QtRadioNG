#include <math.h>
#include "Meter.h"
#include "calc.h"

#define CENTER_X 75
#define CENTER_Y 85

Meter::Meter(QString title, short mtype)
{
    image=new QImage(150, 105, QImage::Format_ARGB32);
    image->fill(0xFFFFFFFF);

    type = mtype;

    QPainter painter(image);

    painter.setFont(QFont("Arial", 8));

    if (type == SIGMETER)
    {
        painter.setPen(QPen(Qt::red, 1));
        painter.drawArc(5, 15, 140, 140, -260*16, -60*16);

        painter.setPen(QPen(Qt::black, 1));
        painter.drawArc(5, 15, 140, 140, -212*16, -48*16);

        // put in the markers
        painter.setPen(Qt::black);
        calculateLine(-121, 60, 75); // 1
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"1");
        calculateLine(-115, 65, 70); // 2
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(-109, 60, 75); // 3
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"3");
        calculateLine(-103, 65, 70); // 4
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(-97, 65, 70); // 5
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(-91, 60, 75); // 6
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"6");
        calculateLine(-85, 65, 70); // 7
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(-79, 65, 70); // 8
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(-73, 60, 75); // 9
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"9");

        painter.setPen(Qt::red);
        calculateLine(-53, 60, 75); // +20
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"+20");
        calculateLine(-33, 60, 75); // +40
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"+40");
        calculateLine(-13, 60, 75); // +60
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"+60");
    }

    if (type == POWMETER)
    {
        Calc calc;

        painter.setPen(QPen(Qt::red, 1));
        painter.drawArc(5, 15, 140, 140, -300*16, -20*16);

        painter.setPen(QPen(Qt::black, 1));
        painter.drawArc(5, 15, 140, 140, -212*16, -88*16);


        // put in the markers
        painter.setPen(Qt::black);
        calculateLine(-121, 60, 75); // 0
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"0");
        calculateLine(calc.WattsTodBm(.025)*2.5-121, 60, 75); // 0.25
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,".25");
        calculateLine(calc.WattsTodBm(.1)*2.5-121, 60, 75); // 1
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"1");
        calculateLine(calc.WattsTodBm(.2)*2.5-121, 60, 75); // 2
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"2");
        calculateLine(calc.WattsTodBm(.3)*2.5-121, 65, 70); // 3
        painter.drawLine(dxmin, dymin, dxmax, dymax);
    //    painter.drawText(dxmax-5, dymax,"3");
        calculateLine(calc.WattsTodBm(.5)*2.5-121, 60, 75); // 5
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"5");
        calculateLine(calc.WattsTodBm(1.0)*2.5-121, 60, 75); // 10
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"10");
        calculateLine(calc.WattsTodBm(2.0)*2.5-121, 60, 75); // 20
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"20");
        calculateLine(calc.WattsTodBm(3.0)*2.5-121, 65, 70); // 30
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(calc.WattsTodBm(4.0)*2.5-121, 65, 70); // 40
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(calc.WattsTodBm(5.0)*2.5-121, 60, 75); // 50
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"50");
        calculateLine(calc.WattsTodBm(6.0)*2.5-121, 65, 70); // 60
        painter.drawLine(dxmin, dymin, dxmax, dymax);

        calculateLine(calc.WattsTodBm(7.0)*2.5-121, 65, 70); // 70
        painter.drawLine(dxmin, dymin, dxmax, dymax);

        painter.setPen(Qt::red);
        calculateLine(calc.WattsTodBm(8.0)*2.5-121, 60, 75); // 80
   //     painter.drawLine(dxmin, dymin, dxmax, dymax);
   //     painter.drawText(dxmax-5, dymax,"80");
        calculateLine(calc.WattsTodBm(9.0)*2.5-121, 65, 70); // 90
   //     painter.drawLine(dxmin, dymin, dxmax, dymax);
        calculateLine(calc.WattsTodBm(10.0)*2.5-121, 60, 75); // 100
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"100");
        calculateLine(calc.WattsTodBm(18.0)*2.5-121, 60, 75); // 180
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.drawText(dxmax-5, dymax,"180");
    }

    painter.drawText(0, 0, image->width(), image->height(), Qt::AlignTop, title);
}


void Meter::calculateLine(int dbm, double minRadius, double maxRadius)
{
    double degrees = -121-(dbm+121);
    double radians = degrees*(3.14159265358979323846/180.0);
    double sine   = sin(radians);
    double cosine = cos(radians);

    dxmin = CENTER_X + (int)(minRadius * sine);
    dymin = CENTER_Y + (int)(minRadius * cosine);

    dxmax = CENTER_X + (int)(maxRadius * sine);
    dymax = CENTER_Y + (int)(maxRadius * cosine);

}


void Meter::calculateNeedle(int dbm, double minRadius, double maxRadius)
{
    Calc calc;
    double degrees;

    if (type == POWMETER)
        degrees = -121-(((calc.WattsTodBm((float)(dbm/10.0)))*2.5-121)+121);
    else
        degrees = -121-(dbm+121);
//    qDebug("dbm: %2.2f", ((calc.WattsTodBm((float)(dbm/10.0)))*2.5-121)+121);
    if (degrees > -121) degrees = -121;
    double radians = degrees*(3.14159265358979323846/180.0);
    double sine   = sin(radians);
    double cosine = cos(radians);

    dxmin = CENTER_X + (int)(minRadius * sine);
    dymin = CENTER_Y + (int)(minRadius * cosine);

    dxmax = CENTER_X + (int)(maxRadius * sine);
    dymax = CENTER_Y + (int)(maxRadius * cosine);

}


QImage Meter::getImage(float meter0, float meter1, float meter2)
{
    Calc calc;
    QImage qImage(*image);
    QPainter painter(&qImage);
  //  Calc calc;

    painter.setPen(Qt::blue);

    if (type == POWMETER)
    {
//        qDebug("F: %2.2f  R: %2.2f", meter1, meter2);
//        calculateNeedle(calc.PAPower((meter0 & 0xff00) >> 8),0,75);
        calculateNeedle((int)(meter1), 0, 75);
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.setPen(Qt::black);
        // meter1 = forward, meter2 = reverse power (watts). SWR comes from the
        // reflection coefficient rho = sqrt(Pr/Pf): SWR = (1 + rho) / (1 - rho).
        // With no RF show 0.0; reverse >= forward (rho >= 1) is shown as 99.9.
        float swr = 0.0f;
        if (meter1 > 0.0f)
        {
            float rho = (meter2 > 0.0f) ? sqrtf(meter2 / meter1) : 0.0f;
            swr = (rho < 1.0f) ? (1.0f + rho) / (1.0f - rho) : 99.9f;
            if (swr > 99.9f)
                swr = 99.9f;
        }
        strDbm = QString("%1 SWR  %2 W").arg(swr, 0, 'f', 1).arg(meter1, 2, 'f', 1);
        QRectF r1(image->width()-133, image->height()-15, 105, 20);
        painter.drawText(r1, Qt::AlignRight, strDbm);
    }
    else
    {
        calculateNeedle((int)meter0, 0, 75);
        painter.drawLine(dxmin, dymin, dxmax, dymax);
        painter.setPen(Qt::black);
        painter.setBackground(QBrush(QColor(Qt::white)));
        strDbm = QString("%1 dBm").arg((int)meter0);
        QRectF r1(image->width()-150, image->height()-15, 105, 20);
        painter.drawText(r1, Qt::AlignRight, strDbm);
    }

    return qImage;
}
