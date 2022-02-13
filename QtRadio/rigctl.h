#ifndef RIGCTL_H
#define RIGCTL_H

#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

class Radio;

class RigCtlSocket : public QObject {
        Q_OBJECT

        public:
                RigCtlSocket(QObject *parent = 0, Radio *main = 0, QTcpSocket *conn = 0);

        public slots:
                void disconnected(void);
                void readyRead(void);

        private:
                Radio *main;
                QTcpSocket *conn;
};

class RigCtlServer : public QObject {
        Q_OBJECT

        public:
                RigCtlServer(QObject *parent = 0, Radio *main = 0, int port = 19090);

        public slots:
                void newConnection(void);

        private:
                Radio *main;
                QTcpServer *server;
};
#endif // RIGCTL_H
