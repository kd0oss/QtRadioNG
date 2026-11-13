#ifndef SMETER_H
#define SMETER_H

#include <QtCore>

#include <QFrame>

#include <QPainter>
#include "Meter.h"


// class Spectrum: public QFrame {
//    Q_OBJECT
class sMeter: public QFrame {
    Q_OBJECT

public:
    sMeter();
    sMeter(QWidget* parent=0);
    virtual ~sMeter();
    Meter* sMeterMain;
    int meter0;
    int meter1;
    int meter2;

protected:
    void paintEvent(QPaintEvent*);

signals:

public slots:

};

#endif // SMETER_H
