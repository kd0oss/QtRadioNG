#ifndef SMETER_H
#define SMETER_H

#include <QtCore>

#if QT_VERSION >= 0x050000
#include <QtWidgets/QFrame>
#else
#include <QFrame>
#endif

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
    int meter1;
    int meter2;

protected:
    void paintEvent(QPaintEvent*);

signals:

public slots:

};

#endif // SMETER_H
