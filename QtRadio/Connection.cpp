/* 
 * File:   Connection.cpp
 * Author: John Melton, G0ORX/N6LYT
 *
 * Created on 16 August 2010, 07:40
 */

/* Copyright (C)
* 2009 - John Melton, G0ORX/N6LYT
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/


/* Copyright (C) 2012 - Alex Lee, 9V1Al
* modifications of the original program by John Melton
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Pl
*/


/*  2019 - Rick Schnicker, KD0OSS
 *  Rewrite to QTRadioNG.
 */

#include "Connection.h"
#include "radiosdialog.h"
#include <QDebug>
#include <QRegExp>

ServerConnection::ServerConnection()
{
    qDebug() << "ServerConnection::Connection";
    tcpSocket = NULL;
    state = READ_HEADER;
    bytes = 0;
    active_radios = 0;
    active_channels = 0;
    hdr = (char*)malloc(HEADER_SIZE_2_1);  // HEADER_SIZE is larger than AUTIO_HEADER_SIZE so it is OK for both
    SemSpectrum.release();
    muted = false;
    serverver = 0;
    amSlave = false;
    xml = NULL;
    available_xcvrs[0] = 0;
    available_xcvrs[1] = 0;
    available_xcvrs[2] = 0;
    available_xcvrs[3] = 0;
    QObject::connect(this, SIGNAL(activateRadioSig()), this, SLOT(activateRadio()));
    QObject::connect(this, SIGNAL(send_command(QByteArray)), this, SLOT(sendCommand(QByteArray)));
} // end constructor


ServerConnection::~ServerConnection()
{
    qDebug() << "ServerConnection::~Connection";
} // end destructor


QString ServerConnection::getHost()
{
    qDebug() << "ServerConnection::getHost: " << host;
    return host;
} // end getHost


void ServerConnection::connect(QString h, int p)
{
    host = h;
    port = p;

    // cleanup previous object, if any
    if (tcpSocket)
    {
        delete tcpSocket;
    }

    tcpSocket = new QTcpSocket(this);

    QObject::connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(socketError(QAbstractSocket::SocketError)));

    QObject::connect(tcpSocket, SIGNAL(connected()),
                     this, SLOT(connected()));

    QObject::connect(tcpSocket, SIGNAL(disconnected()),
                     this, SLOT(disconnected()));

    QObject::connect(tcpSocket, SIGNAL(readyRead()),
                     this, SLOT(cmdSocketData()));

    // set the initial state
    state = READ_HEADER;
    // cleanup dirty value eventually left from previous usage
    bytes = 0;
    qDebug() << "ServerConnection::connect: connectToHost: " << host << ":" << port;
    tcpSocket->connectToHost(host, port);
} // end connect


void ServerConnection::disconnected()
{
    qDebug() << "ServerConnection::disconnected: emits: " << "Remote disconnected";
    emit disconnected("Remote disconnected");

    if (tcpSocket != NULL)
    {
        QObject::disconnect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                            this, SLOT(socketError(QAbstractSocket::SocketError)));

        QObject::disconnect(tcpSocket, SIGNAL(connected()),
                            this, SLOT(connected()));

        QObject::disconnect(tcpSocket, SIGNAL(disconnected()),
                            this, SLOT(disconnected()));

        QObject::disconnect(tcpSocket, SIGNAL(readyRead()),
                            this, SLOT(cmdSocketData()));
    }
} // end disconnected


void ServerConnection::disconnect()
{
    qDebug() << "ServerConnection::disconnect Line " << __LINE__;

    if (tcpSocket != NULL)
    {
        tcpSocket->close();
        // object deletion moved in connect method
        // tcpSocket=NULL;

    }
    // close the hardware panel, if any

    emit hardware(QString(""));
} // end disconnnect


void ServerConnection::socketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError)
    {
    case QAbstractSocket::RemoteHostClosedError:
        qDebug() << "Remote closed connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        qDebug() << "Host not found";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        qDebug() << "Remote host refused connection";
        break;
    default:
        qDebug() << "Socket Error: " << tcpSocket->errorString();
    }

    emit disconnected(tcpSocket->errorString());
    // memory leakeage !!
    // tcpSocket=NULL;
} // end socketError


void ServerConnection::connected()
{
    qDebug() << "ServerConnection::Connected" << tcpSocket->isValid();

    for (int i=0;i<MAX_CHANNELS;i++)
    {
        channels[i].id = i;
        channels[i].radio.radio_id = -1;
        channels[i].index = -1;
        channels[i].dsp_channel = -1;
        channels[i].enabled = false;
        channels[i].isTX = false;
    }
    ////    emit isConnected();
    state = READ_HEADER;
    lastFreq = 0;
    lastMode = 99;
    lastSlave = 1;
    QByteArray qbyte;
    qbyte.append((char)0);
    qbyte.append((char)QUESTION);
    qbyte.append((char)STARHARDWARE);
    sendCommand(qbyte);
    qbyte.clear();
    qbyte.append((char)0);
    qbyte.append((char)QUESTION);
    qbyte.append((char)QINFO);
    //   sendCommand(qbyte);
    /*
    qbyte.append((char)QUESTION);
    qbyte.append((char)QDSPVERSION);
    sendCommand(qbyte);
    qbyte.clear();
    qbyte.append((char)QUESTION);
    qbyte.append((char)QLOFFSET);
    sendCommand(qbyte);
    qbyte.clear();
    qbyte.append((char)QUESTION);
    qbyte.append((char)QCOMMPROTOCOL1);
    sendCommand(qbyte);
    qbyte.clear();
    amSlave = true;
    serverver = 0;
    */
} // end connected


void ServerConnection::sendCommand(QByteArray command)
{
    int  i;
    char buffer[SEND_BUFFER_SIZE];
    int  bytesWritten;

    //  qDebug() << "ServerConnection::sendCommand: "<< command.toStdString().c_str();
    mutex.lock();
    for (i=0; i < SEND_BUFFER_SIZE; i++)
        buffer[i] = 0;

    if (tcpSocket != NULL && tcpSocket->isValid() && tcpSocket->isWritable())
    {
        qDebug("Server: ch: %d  Comm: %u\n", (char)command[0], (unsigned char)command[1]);
        memcpy(buffer, command.constData(), command.size());
        bytesWritten = tcpSocket->write(buffer, SEND_BUFFER_SIZE);
        if (bytesWritten != SEND_BUFFER_SIZE)
            qDebug() << "sendCommand: write error";
        tcpSocket->flush();
    }
    mutex.unlock();
} // end sendCommand


void ServerConnection::cmdSocketData()
{
    int     toRead;
    int     bytesRead=0;
    int     thisRead=0;
    static int radio_index;

    if (bytes < 0)
    {
        fprintf(stderr,"QtRadio: FATAL: INVALID byte counter: %d\n", bytes);
        //tcpSocket->close();
        return;
    }

    toRead = tcpSocket->bytesAvailable();
    if (toRead <= 0)
    {
        return;
    }

    while (bytesRead < toRead)
    {
        switch (state)
        {
        case READ_HEADER:
            thisRead = tcpSocket->read(&hdr[bytes], 4 - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr,"QtRadio: FATAL: READ_HEADER_TYPE: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            if (bytes == 4)
            {
                length = ((hdr[0] & 0xFF) << 8) + (hdr[1] & 0xFF);
                if (length > 0)
                {
                    switch (hdr[2])
                    {
                    case READ_MANIFEST:
                        if (xml) free(xml);
                        xml = (char*)malloc(length);
                        memset((char*)xml, 0, length);
                        qDebug("Manifest header...\n");
                        active_radios = hdr[3];
                        bytes = 0;
                        radio_index = 0;
                        active_channels = 0;
                        state = READ_MANIFEST;
                        break;

                    case QINFO:
                        buffer = (char*)malloc(length);
                        memset((char*)buffer, 0, length);
                        bytes = 0;
                        state = READ_QINFO;
                        break;

                    default:
                        bytes = 0;
                        break;
                    }
                }
            }
            break;

        case READ_MANIFEST:
            thisRead = tcpSocket->read(&xml[bytes], length - bytes);
            bytes += thisRead;
            if (bytes == length)
            {
                qDebug("XML: %s", xml);
                manifest_xml[radio_index].clear();
                manifest_xml[radio_index].append(xml);
                QStringList list = manifest_xml[active_radios].split(">");
                available_xcvrs[radio_index++] = list[0].split("=").at(1).toInt();
                qDebug("Radio Type: %s\n", getXcvrProperty(0, 0, "radio_type").toLatin1().data());
                bytes = 0;
                state = READ_HEADER;
                createChannels(radio_index);
                emit activateRadioSig();
            }
            break;

        case READ_QINFO:
            thisRead = tcpSocket->read(&buffer[bytes], length - bytes);
            bytes += thisRead;
            if (bytes == length)
            {
                /********* FIXME: This is a mess. Need better way to get server status. ***********/
                QString ans = buffer;
                QStringList tmp = ans.split("^");
                emit setdspversion(tmp[0].split(";").at(0).toLong(), (QString)(tmp[0].split(";").at(1)));
                serverver = tmp[0].split(";").at(0).toLong();
                QString f = tmp[1].split(";").at(3);
                QString m = tmp[1].split(";").at(5);
                QString z = tmp[1].split(";").at(7);
                QString l = tmp[1].split(";").at(9);
                QString r = tmp[1].split(";").at(11);
                QString c = tmp[1].split(";").at(12);
                long long newf = f.toLongLong();
                int newmode = m.toInt();
                int zoom = z.toInt();
                int left = l.toInt();
                int right = r.toInt();
                int current_channel = c.toInt();
                //             emit setCurrentChannel(current_channel);
                emit slaveSetFreq(newf);
                //          emit slaveSetFilter(left, right);
                //            emit slaveSetZoom(zoom);
                if (newmode != lastMode)
                {
                    //               emit slaveSetMode(newmode);
                }

                emit printStatusBar(" .. Info. ");    //added by gvj
                lastFreq = newf;
                lastMode = newmode;
                emit setservername("local");
                double loffset = 0.0f;
                emit resetbandedges(loffset);
                emit setCanTX(true);
                emit setChkTX(false);
                bytes = 0;
                state = READ_HEADER;
            }
            break;

        default:
            fprintf (stderr, "FATAL: WRONG STATUS !!!!!\n");
        }
        bytesRead += thisRead;
    }
} // end cmdSocketData

#ifdef old
void Connection::socketData()
{
    int     toRead;
    int     bytesRead=0;
    int     thisRead=0;
    int     version;
    int     header_size=0;
    int     answer_size=0;
    char   *ans;
    QString answer;

    if (bytes < 0)
    {
        fprintf(stderr,"QtRadio: FATAL: INVALID byte counter: %d\n", bytes);
        //tcpSocket->close();
        return;
    }

    toRead = tcpSocket->bytesAvailable();
    if (toRead <= 0)
    {
        return;
    }

    while (bytesRead < toRead)
    {
        //       fprintf (stderr, "%d of %d [%d]\n", bytesRead, toRead, state);
        switch (state)
        {
        case READ_HEADER_TYPE:
            thisRead = tcpSocket->read(&hdr[bytes], 3 - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr,"QtRadioII: FATAL: READ_HEADER_TYPE: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            //        if (thisRead == 0)
            //          return;

            bytes += thisRead;
            if (bytes == 3)
            {
                switch (hdr[0])
                {
                case AUDIO_BUFFER:
                    state = READ_AUDIO_HEADER;
                    break;
                case SPECTRUM_BUFFER:
                    version = hdr[1];
                    header_size = 15;
                    state = READ_HEADER;
                    break;
                case BANDSCOPE_BUFFER:
                    bytes = 0;
                    break;
                case 52: //ANSWER_BUFFER
                    // answer size is in hdr index 1 max 255
                    state = READ_ANSWER;
                    bytes = 0;
                    answer_size = hdr[1];
                    ans = (char*)malloc(answer_size + 1);
                    memset(ans, 0, answer_size + 1);
                    break;
                default:
                    bytes = 0;
                }
            }
            break;

        case READ_AUDIO_HEADER:
            //fprintf (stderr, "READ_AUDIO_HEADER: hdr size: %d bytes: %d\n", AUDIO_HEADER_SIZE, bytes);
            thisRead = tcpSocket->read(&hdr[bytes], AUDIO_HEADER_SIZE - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadioII: FATAL: READ_AUDIO_HEADER: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            if (bytes == AUDIO_HEADER_SIZE)
            {
                length = ((hdr[3] & 0xFF) << 8) + (hdr[4] & 0xFF);
                if (length >= 0)
                {
                    buffer = (char*)malloc(length);
                    bytes = 0;
                    state = READ_BUFFER;
                }
                else
                {
                    state = READ_HEADER_TYPE;
                    bytes = 0;
                }
            }
            break;

        case READ_HEADER:
            //fprintf (stderr, "READ_HEADER: hdr size: %d bytes: %d\n", header_size, bytes);
            thisRead = tcpSocket->read(&hdr[bytes], header_size - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadioII: FATAL: READ_HEADER: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            if (bytes == header_size)
            {
                length = ((hdr[3] & 0xFF) << 8) + (hdr[4] & 0xFF);
                //     if ((length < 0) || (length > 4096))
                if (length != 2000)
                {
                    state = READ_HEADER_TYPE;
                    bytes = 0;
                }
                else
                {
                    buffer = (char*)malloc(length);
                    bytes = 0;
                    state = READ_BUFFER;
                }
            }
            break;

        case READ_BUFFER:
            //      fprintf (stderr, "READ_BUFFER: length: %d bytes: %d\n", length, bytes);
            thisRead = tcpSocket->read(&buffer[bytes], length - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadioII: FATAL: READ_BUFFER: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            //qDebug() << "READ_BUFFER: read " << bytes << " of " << length;
            if (bytes == length)
            {
                version = hdr[1];
                //    subversion = hdr[2];
                queue.enqueue(new Buffer(hdr, buffer));
                QTimer::singleShot(0, this, SLOT(processBuffer()));
                hdr = (char*)malloc(15);
                bytes = 0;
                state = READ_HEADER_TYPE;
            }
            break;

        case READ_ANSWER:
            //qDebug() << "Connection READ ANSWER";
            thisRead = tcpSocket->read(&ans[bytes], answer_size - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadioII: FATAL: READ_ANSWER: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            if (bytes == answer_size)
            {
                //fprintf(stderr,"ans length = %lu\n",strlen(ans));
                ans[answer_size] = '\0';
                if (hdr[2] == QDSPVERSION)
                {
                    //"20120107;-rxtx"; YYYYMMDD; text desc
                    answer = ans;
                    QString tmp = (QString)(answer.split(";").at(0));
                    emit setdspversion(tmp.toLong(), (QString)(answer.split(";").at(1)));
                    serverver = tmp.toLong();
                    QByteArray qbyte;
                    qbyte.append((char)QUESTION);
                    qbyte.append((char)QMASTER);
                    sendCommand(qbyte);
                }
                else
                    if (hdr[2] == QSERVER)
                    {
                        answer = ans;
                        QString servername = (QString)(answer.split(" ").at(0));
                        emit setservername(servername);
                        QString hasTX = (QString)(answer.split(" ").at(1));
                        if (hasTX.compare("N") == 0)
                        {
                            emit setCanTX(false);
                        }
                        else
                            if (hasTX.compare("P") == 0)
                            {
                                emit setCanTX(false);
                                emit setChkTX(true);
                            }
                            else
                            {  // must be yes
                                //qDebug() <<"Yes Master";
                                if (amSlave)
                                {
                                    emit setCanTX(false);
                                    emit setChkTX(false);
                                }
                                else
                                {
                                    emit setCanTX(true);
                                    emit setChkTX(false);
                                }
                            }
                    }
                    else
                        if (hdr[2] == QMASTER)
                        {
                            //qDebug() << "q-master:" << answer;
                            answer = ans;
                            if (answer.contains("slave"))
                            {
                                amSlave = true;
                                emit printStatusBar("  ...Slave Mode. ");
                            }
                            else
                            {
                                amSlave = false;
                                emit printStatusBar("  ...Master Mode. ");
                                emit setCanTX(true); /***** remove this after test ***************************/
                            }
                        }
                        else
                            if (hdr[2] == QCANTX)
                            {
                                answer = ans;
                                QString TXNow = (QString)(answer.split(":").at(1));
                                if (TXNow.compare("Y") == 0)
                                {
                                    emit setCanTX(true);
                                }
                                else
                                {
                                    emit setCanTX(false);
                                }
                            }
                            else
                                if (hdr[2] == QLOFFSET)
                                {
                                    answer = ans;
                                    QString tmp = (QString)(answer.split(";").at(0));
                                    double loffset = tmp.toDouble();
                                    emit resetbandedges(loffset);
                                }
                                else
                                    if (hdr[2] == QINFO)
                                    {
                                        answer = ans;
                                        QString f = (QString)(answer.split(";").at(4));
                                        QString m = (QString)(answer.split(";").at(6));
                                        QString z = (QString)(answer.split(";").at(8));
                                        QString l = (QString)(answer.split(";").at(10));
                                        QString r = (QString)(answer.split(";").at(12));
                                        long long newf = f.toLongLong();
                                        int newmode = m.toInt();
                                        int zoom = z.toInt();
                                        int left = l.toInt();
                                        int right = r.toInt();
                                        emit slaveSetFreq(newf);
                                        emit slaveSetFilter(left, right);
                                        emit slaveSetZoom(zoom);
                                        if (newmode != lastMode)
                                        {
                                            emit slaveSetMode(newmode);
                                        }

                                        lastFreq = newf;
                                        lastMode = newmode;
                                    }
                                    else
                                        if (hdr[2] == QCOMMPROTOCOL1)
                                        {
                                            answer = ans;
                                            emit setFPS();
                                        }
                                        else
                                            if (hdr[2] == STARHARDWARE)
                                            {
                                                answer = ans+2;
                                                qDebug() << "--------------->" << answer;

                                                emit hardware(QString(answer));
                                            }

                //answer.prepend("  Question/Answer ");
                //emit printStatusBar(answer);
                //       qDebug() << "ANSWER bytes " << bytes << " answer " << ans;
                free(ans);
                bytes = 0;
                state = READ_HEADER_TYPE;
            }
            break;

        default:
            fprintf (stderr, "FATAL: WRONG STATUS !!!!!\n");
        }
        bytesRead += thisRead;
        //      QCoreApplication::processEvents();
    }
} // end socketData
#endif

void ServerConnection::processBuffer()
{
    Buffer *buffer;
    char   *nextHeader;
    char   *nextBuffer;

    while (!queue.isEmpty())
    {
        buffer = queue.dequeue();
        nextHeader = buffer->getHeader();
        nextBuffer = buffer->getBuffer();
        // emit a signal to show what buffer we have
        //   qDebug() << "processBuffer " << nextHeader[0];
        if (nextHeader[0] == AUDIO_BUFFER)
        {
            // need to add a duplex state
            if (!muted)
                emit audioBuffer(nextHeader, nextBuffer);
        }
        else
        {
            qDebug() << "Connection::socketData: invalid header: " << nextHeader[0];
            queue.clear();
        }
    }
} // end processBuffer


void ServerConnection::freeBuffers(char* header,char* buffer)
{
    if (header != NULL) free(header);
    if (buffer != NULL) free(buffer);
} // end freeBuffers


bool ServerConnection::getSlave()
{
    return amSlave;
} // end getSlave


// added by gvj
void ServerConnection::setMuted(bool muteState)
{
    muted = muteState;
} // end setMuted


QString ServerConnection::getXcvrProperty(int server, int xcvr, const QString property)
{
    QStringList list;
    QString     result;
    bool        found = false;

    list = manifest_xml[server].split("\n", QString::SkipEmptyParts);
    for (int i=0;i<list.count();i++)
    {
        if (list[i].contains("radio="))
        {
            QStringList l = list[i].split(">");
            if (l[0].split("=").at(1).toInt() == xcvr)
                found = true;
            else
                continue;
        }
        else
            if (list[i].toLower().contains(property.toLower()) && found)
            {
                QStringList l = list[i].split(">");
                result = l[1].split("<").at(0);
                break;
            }
    }
    return result;
} // end getXcvrProperty


void ServerConnection::createChannels(int radios)
{
    char line[80];
    char *manifest = NULL;
    char ip_addr[16] = "";
    char mac_addr[18] = "";
    bool local_audio = false;
    bool local_mic = false;
    int  index = 0;
    int  t = 0;
    int  r = 0;
    int  radio_id = 0;
    char radio_type[25];
    char radio_name[25];
    int  num_chs = 0;
    int  num_rcvrs = 0;
    bool bs_capable = false;

    active_radios = 0;
    for (int x=0;x<radios;x++)
    {
        manifest = (char*)malloc(manifest_xml[x].length()+1);
        strcpy(manifest, manifest_xml[x].toLatin1().data());

        for (int i=0;i<(int)strlen(manifest);i++)
        {
            if (manifest[i] != 10)
            {
                if (manifest[i] == '=' || manifest[i] == '<' || manifest[i] == '>')
                {
                    if (index != 0 && index < 79)
                        line[index++] = ' ';
                }
                else
                {
                    if (manifest[i] == ' ' && index == 0)
                        ; // do nothing
                    else
                        if (index < 79)
                            line[index++] = manifest[i];
                }
            }
            else
            {
                line[index] = 0;
                index = 0;
                fprintf(stderr, "%s\n", line);
                if (strstr((const char*)QString(line).trimmed().toLatin1().data(), "radio "))
                {
                    active_radios++;
                    sscanf(line, "%*s %d", &radio_id);
                }
                else
                    if (strstr(line, "radio_type"))
                    {
                        sscanf(line, "%*s %s %*s", radio_type);
                    }
                    else
                        if (strstr(line, "name"))
                        {
                            sscanf(line, "%*s %s %*s", radio_name);
                        }
                        else
                            if (strstr(line, "supported_receivers"))
                            {
                                r = 0;
                                sscanf(line, "%*s %d %*s", &num_rcvrs);
                            }
                            else
                                if (strstr(line, "bandscope"))
                                {
                                    int tmp=0;
                                    sscanf(line, "%*s %d %*s", &tmp);
                                    if (tmp == 1)
                                        bs_capable = true;
                                    else
                                        bs_capable = false;
                                }
                                else
                                    if (strstr(line, "supported_transmitters"))
                                    {
                                        sscanf(line, "%*s %d %*s", &t);
                                    }
                                    else
                                        if (strstr(line, "mac_address"))
                                            sscanf(line, "%*s %s %*s", mac_addr);
                                        else
                                            if (strstr(line, "ip_address"))
                                                sscanf(line, "%*s %s %*s", ip_addr);
                                            else
                                                if (strstr(line, "local_audio"))
                                                {
                                                    int tmp = 0;
                                                    sscanf(line, "%*s %d %*s", &tmp);
                                                    if (tmp == 1)
                                                        local_audio = true;
                                                    else
                                                        local_audio = false;
                                                }
                                                else
                                                    if (strstr(line, "local_mic"))
                                                    {
                                                        int tmp = 0;
                                                        sscanf(line, "%*s %d %*s", &tmp);
                                                        if (tmp == 1)
                                                            local_mic = true;
                                                        else
                                                            local_mic = false;
                                                    }
                                                    else
                                                        if (strstr(line, "ant_switch"))
                                                        {
                                                            int ant_switch = 0;
                                                            sscanf(line, "%*s %d %*s", &ant_switch);
                                                            for (;r<num_rcvrs;r++)
                                                            {   // setup receive channels
                                                                channels[active_channels].radio.radio_id = active_radios-1;
                                                                strcpy(channels[active_channels].radio.ip_address, ip_addr);
                                                                strcpy(channels[active_channels].radio.mac_address, mac_addr);
                                                                channels[active_channels].radio.local_audio = local_audio;
                                                                channels[active_channels].radio.local_mic = local_mic;
                                                                channels[active_channels].radio.ant_switch = ant_switch;
                                                                channels[active_channels].dsp_channel = num_chs++;
                                                                channels[active_channels].index = r;
                                                                channels[active_channels].id = active_channels;
                                                                channels[active_channels].radio.bandscope_capable = bs_capable;
                                                                strcpy(channels[active_channels].radio.radio_type, radio_type);
                                                                strcpy(channels[active_channels].radio.radio_name, radio_name);
                                                                channels[active_channels].isTX = false;
                                                                channels[active_channels].enabled = false;
                                                                channels[active_channels].spectrum.samples = NULL;
                                                                active_channels++;
                                                            }
                                                            for (;r<t+num_rcvrs;r++)
                                                            {   // setup transmit channels
                                                                channels[active_channels].radio.radio_id = active_radios-1;
                                                                strcpy(channels[active_channels].radio.ip_address, ip_addr);
                                                                strcpy(channels[active_channels].radio.mac_address, mac_addr);
                                                                channels[active_channels].radio.local_audio = local_audio;
                                                                channels[active_channels].radio.local_mic = local_mic;
                                                                channels[active_channels].radio.ant_switch = ant_switch;
                                                                channels[active_channels].dsp_channel = num_chs++;
                                                                channels[active_channels].index = r;
                                                                channels[active_channels].id = active_channels;
                                                                channels[active_channels].radio.bandscope_capable = bs_capable;
                                                                strcpy(channels[active_channels].radio.radio_type, radio_type);
                                                                strcpy(channels[active_channels].radio.radio_name, radio_name);
                                                                channels[active_channels].isTX = true;
                                                                channels[active_channels].enabled = false;
                                                                channels[active_channels].spectrum.samples = NULL;
                                                                active_channels++;
                                                            }
                                                        }
            } // end if
        } // end for
        free(manifest);
    }
    fprintf(stderr, "Active channels = %d\n", active_channels);
} // end createChannels


void ServerConnection::activateRadio()
{
    bool started = false;

    RadiosDialog *rd = new RadiosDialog();
    rd->manifest_xml[0] = manifest_xml[0];
    rd->manifest_xml[1] = manifest_xml[1];
    rd->manifest_xml[2] = manifest_xml[2];
    rd->manifest_xml[3] = manifest_xml[3];
    rd->available_xcvrs[0] = available_xcvrs[0];
    rd->available_xcvrs[1] = available_xcvrs[1];
    rd->available_xcvrs[2] = available_xcvrs[2];
    rd->available_xcvrs[3] = available_xcvrs[3];
    rd->active_radios = active_radios;
    rd->active_channels = active_channels;
    rd->channels = (CHANNEL*)&channels;
    rd->fillRadioList();
    if (rd->exec() == QDialog::Accepted)
    {
        for (int i=0;i<8;i++)
        {
            receivers_active[i] = rd->receivers_active[i];
            receiver_channel[i] = rd->receiver_channel[i];
        }
        memcpy((int8_t*)&txrxPair, (int8_t*)&rd->txrxPair, 2);
        sample_rate = rd->sample_rate[0];
        started = true;
    }
    delete rd;
    if (started)
        emit isConnected((bool*)&receivers_active, (int8_t*)&receiver_channel, (int8_t*)&txrxPair);
} // end activateRadio


/*************************************************************************************************************
 * ****************************************************************************************************
 * ***********************************************************************************************************/

SpectrumConnection::SpectrumConnection()
{
    qDebug() << "Spectrum connection::Connection";
    tcpSocket = NULL;
    state = READ_HEADER;
    bytes = 0;

    QObject::connect(this, SIGNAL(send_spectrum_command(QByteArray)), this, SLOT(sendCommand(QByteArray)));
}


void SpectrumConnection::connect(QString h, int p)
{
    server = h;
    port = p;

    // cleanup previous object, if any
    if (tcpSocket)
    {
        delete tcpSocket;
    }

    tcpSocket = new QTcpSocket(this);

    QObject::connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(socketError(QAbstractSocket::SocketError)));

    QObject::connect(tcpSocket, SIGNAL(connected()),
                     this, SLOT(connected()));

    QObject::connect(tcpSocket, SIGNAL(disconnected()),
                     this, SLOT(disconnected()));

    QObject::connect(tcpSocket, SIGNAL(readyRead()),
                     this, SLOT(spectrumSocketData()));

    // set the initial state
    state = READ_HEADER;
    // cleanup dirty value eventually left from previous usage
    bytes = 0;
    qDebug() << "Spectrum connectToHost: " << server << ":" << port;
    tcpSocket->connectToHost(server, port);
} // end connect


void SpectrumConnection::disconnected()
{
    qDebug() << "Spectrum connection::disconnected: emits: " << "Remote disconnected";
    emit disconnected("Remote disconnected");

    if (tcpSocket != NULL)
    {
        QObject::disconnect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                            this, SLOT(socketError(QAbstractSocket::SocketError)));

        QObject::disconnect(tcpSocket, SIGNAL(connected()),
                            this, SLOT(connected()));

        QObject::disconnect(tcpSocket, SIGNAL(disconnected()),
                            this, SLOT(disconnected()));

        QObject::disconnect(tcpSocket, SIGNAL(readyRead()),
                            this, SLOT(spectrumSocketData()));
    }
} // end disconnected


void SpectrumConnection::disconnect()
{
    qDebug() << "Spectrum connection::disconnect Line " << __LINE__;

    if (tcpSocket != NULL)
    {
        tcpSocket->close();
        // object deletion moved in connect method
        // tcpSocket=NULL;

    }
} // end disconnnect


void SpectrumConnection::socketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError)
    {
    case QAbstractSocket::RemoteHostClosedError:
        qDebug() << "Remote closed connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        qDebug() << "Host not found";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        qDebug() << "Remote host refused connection";
        break;
    default:
        qDebug() << "Socket Error: " << tcpSocket->errorString();
    }

    emit disconnected(tcpSocket->errorString());
    // memory leakeage !!
    // tcpSocket=NULL;
} // end socketError


void SpectrumConnection::connected()
{
    qDebug() << "Spectrum connection::Connected" << tcpSocket->isValid();
}


SpectrumConnection::~SpectrumConnection()
{

}


void SpectrumConnection::spectrumSocketData()
{
    int     toRead;
    int     bytesRead = 0;
    int     thisRead = 0;
    static  CHANNEL channel;
    int     header_size = sizeof(CHANNEL);

    if (bytes < 0)
    {
        fprintf(stderr,"QtRadio: FATAL: INVALID byte counter: %d\n", bytes);
        //tcpSocket->close();
        return;
    }

    recv_mutex.lock();
    toRead = tcpSocket->bytesAvailable();
    if (toRead <= 0)
    {
        recv_mutex.unlock();
        return;
    }

    while (bytesRead < toRead)
    {
        switch (state)
        {
        case READ_HEADER:
            //fprintf (stderr, "READ_HEADER: hdr size: %d bytes: %d\n", header_size, bytes);
            thisRead = tcpSocket->read((char*)&channel+bytes, header_size - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadio: FATAL: READ_HEADER: error in read: %d\n", thisRead);
                tcpSocket->close();
                continue;
            }
            bytes += thisRead;
            if (bytes == header_size)
            {
                length = channel.spectrum.length;
                //     if ((length < 0) || (length > 4096))
                if (length != 2000)
                {
                    fprintf(stderr, "Spec Len: %d\n", length);
                    state = READ_HEADER;
                    bytes = 0;
                }
                else
                {
                    channel.spectrum.samples = (char*)malloc(length);
                    bytes = 0;
                    state = READ_SPECTRUM;
                }
            }
            break;

        case READ_SPECTRUM:
            thisRead = tcpSocket->read(&channel.spectrum.samples[bytes], length - bytes);
            bytes += thisRead;
            if (bytes == length)
            {
                bytes = 0;
                state = READ_HEADER;
                emit spectrumBuffer(channel);
            }
            break;
        default:
            fprintf (stderr, "FATAL: WRONG STATUS !!!!!\n");
        } // switch
        bytesRead += thisRead;
    } // while
    recv_mutex.unlock();
} // end spectrumSocketData


void SpectrumConnection::sendCommand(QByteArray command)
{
    int  i;
    char buffer[SEND_BUFFER_SIZE];
    int  bytesWritten;

    trans_mutex.lock();
    for (i=0; i < SEND_BUFFER_SIZE; i++)
        buffer[i] = 0;

    if (tcpSocket != NULL && tcpSocket->isValid() && tcpSocket->isWritable())
    {
        qDebug("Spec: ch: %d  Comm: %d\n", (char)command[0], (char)command[1]);
        memcpy(buffer, command.constData(), SEND_BUFFER_SIZE);
        bytesWritten = tcpSocket->write(buffer, SEND_BUFFER_SIZE);
        if (bytesWritten != SEND_BUFFER_SIZE)
            qDebug() << "spectrum sendCommand: write error";
        //tcpSocket->flush();
    }
    trans_mutex.unlock();
} // end sendCommand


void SpectrumConnection::freeBuffers(SPECTRUM spec)
{
    if (spec.samples != NULL) free(spec.samples);
} // end freeBuffers


/*************************************************************************************************************
 * ****************************************************************************************************
 * ***********************************************************************************************************/

WidebandConnection::WidebandConnection()
{
    qDebug() << "Wideband connection::Connection";
    tcpSocket = NULL;
    state = READ_HEADER;
    bytes = 0;
}


void WidebandConnection::connect(QString h, int p)
{
    server = h;
    port = p;

    // cleanup previous object, if any
    if (tcpSocket)
    {
        delete tcpSocket;
    }

    tcpSocket = new QTcpSocket(this);

    QObject::connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(socketError(QAbstractSocket::SocketError)));

    QObject::connect(tcpSocket, SIGNAL(connected()),
                     this, SLOT(connected()));

    QObject::connect(tcpSocket, SIGNAL(disconnected()),
                     this, SLOT(disconnected()));

    QObject::connect(tcpSocket, SIGNAL(readyRead()),
                     this, SLOT(widebandSocketData()));

    // set the initial state
    state = READ_HEADER;
    // cleanup dirty value eventually left from previous usage
    bytes = 0;
    qDebug() << "Wideband connectToHost: " << server << ":" << port;
    tcpSocket->connectToHost(server, port);
} // end connect


void WidebandConnection::disconnected()
{
    qDebug() << "Wideband connection::disconnected: emits: " << "Remote disconnected";
    emit disconnected("Remote disconnected");

    if (tcpSocket != NULL)
    {
        QObject::disconnect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                            this, SLOT(socketError(QAbstractSocket::SocketError)));

        QObject::disconnect(tcpSocket, SIGNAL(connected()),
                            this, SLOT(connected()));

        QObject::disconnect(tcpSocket, SIGNAL(disconnected()),
                            this, SLOT(disconnected()));

        QObject::disconnect(tcpSocket, SIGNAL(readyRead()),
                            this, SLOT(widebandSocketData()));
    }
} // end disconnected


void WidebandConnection::disconnect()
{
    qDebug() << "Wideband connection::disconnect Line " << __LINE__;

    if (tcpSocket != NULL)
    {
        tcpSocket->close();
        // object deletion moved in connect method
        // tcpSocket=NULL;

    }
} // end disconnnect


void WidebandConnection::socketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError)
    {
    case QAbstractSocket::RemoteHostClosedError:
        qDebug() << "Remote closed connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        qDebug() << "Host not found";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        qDebug() << "Remote host refused connection";
        break;
    default:
        qDebug() << "Socket Error: " << tcpSocket->errorString();
    }

    emit disconnected(tcpSocket->errorString());
    // memory leakeage !!
    // tcpSocket=NULL;
} // end socketError


void WidebandConnection::connected()
{
    QByteArray command;

    emit bsConnected();
    qDebug() << "Wideband connection::Connected" << tcpSocket->isValid();
}


WidebandConnection::~WidebandConnection()
{

}


void WidebandConnection::widebandSocketData()
{
    int     toRead;
    int     bytesRead=0;
    int     thisRead=0;
    static  CHANNEL channel;
    int     header_size=sizeof(CHANNEL);

    if (bytes < 0)
    {
        fprintf(stderr,"QtRadio: FATAL: INVALID byte counter: %d\n", bytes);
        //tcpSocket->close();
        return;
    }

    toRead = tcpSocket->bytesAvailable();
    if (toRead <= 0)
    {
        return;
    }

    while (bytesRead < toRead)
    {
        switch (state)
        {
        case READ_HEADER:
            //     fprintf(stderr, "READ_HEADER: hdr size: %d bytes: %d\n", header_size, bytes);
            thisRead = tcpSocket->read((char*)&channel+bytes, header_size - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadio: FATAL: READ_HEADER: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            if (bytes == header_size)
            {
                length = channel.spectrum.length;
                if ((length < 512) || (length > 1920))
                    //         if (length != 512)
                {
                    fprintf(stderr, "Wb spec Len: %u\n", (int)length);
                    state = READ_HEADER;
                    bytes = 0;
                }
                else
                {
                    channel.spectrum.samples = (char*)malloc(length);
                    bytes = 0;
                    state = READ_WIDEBAND;
                }
            }
            break;

        case READ_WIDEBAND:
            thisRead = tcpSocket->read(&channel.spectrum.samples[bytes], length - bytes);
            bytes += thisRead;
            if (bytes == length)
            {
                bytes = 0;
                state = READ_HEADER;
                emit bandscopeBuffer(channel.spectrum);
            }
            break;

        default:
            fprintf (stderr, "FATAL: WRONG STATUS !!!!!\n");
        }
        bytesRead += thisRead;
    }
} // end widebandSocketData


void WidebandConnection::sendCommand(QByteArray command)
{
    int  i;
    char buffer[SEND_BUFFER_SIZE];
    int  bytesWritten;

    for (i=0; i < SEND_BUFFER_SIZE; i++)
        buffer[i] = 0;

    if (tcpSocket != NULL && tcpSocket->isValid() && tcpSocket->isWritable())
    {
        mutex.lock();
        memcpy(buffer, command.constData(), SEND_BUFFER_SIZE);
        bytesWritten = tcpSocket->write(buffer, SEND_BUFFER_SIZE);
        if (bytesWritten != SEND_BUFFER_SIZE)
            qDebug() << "wideband sendCommand: write error";
        else
            qDebug() << "wideband sendCommand: successful";
        //tcpSocket->flush();
        mutex.unlock();
    }
} // end sendCommand


void WidebandConnection::freeBuffers(SPECTRUM spec)
{
    if (spec.samples != NULL) free(spec.samples);
} // end freeBuffers


/*************************************************************************************************************
 * ****************************************************************************************************
 * ***********************************************************************************************************/

AudioConnection::AudioConnection()
{
    qDebug() << "AudioConnection::Connection";
    tcpSocket = NULL;
    state = READ_HEADER;
    bytes = 0;
    hdr = (char*)malloc(HEADER_SIZE_2_1);  // HEADER_SIZE is larger than AUTIO_HEADER_SIZE so it is OK for both
}


void AudioConnection::connect(QString h, int p)
{
    server = h;
    port = p;

    // cleanup previous object, if any
    if (tcpSocket)
    {
        delete tcpSocket;
    }

    tcpSocket = new QTcpSocket(this);

    QObject::connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(socketError(QAbstractSocket::SocketError)));

    QObject::connect(tcpSocket, SIGNAL(connected()),
                     this, SLOT(connected()));

    QObject::connect(tcpSocket, SIGNAL(disconnected()),
                     this, SLOT(disconnected()));

    QObject::connect(tcpSocket, SIGNAL(readyRead()),
                     this, SLOT(socketData()));

    // set the initial state
    state = READ_HEADER;
    // cleanup dirty value eventually left from previous usage
    bytes = 0;
    qDebug() << "AudioConnection::connect: connectToHost: " << server << ":" << port;
    tcpSocket->connectToHost(server, port);
} // end connect


void AudioConnection::disconnected()
{
    qDebug() << "AudioConnection::disconnected: emits: " << "Remote disconnected";
    emit disconnected("Remote disconnected");

    if (tcpSocket != NULL)
    {
        QObject::disconnect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                            this, SLOT(socketError(QAbstractSocket::SocketError)));

        QObject::disconnect(tcpSocket, SIGNAL(connected()),
                            this, SLOT(connected()));

        QObject::disconnect(tcpSocket, SIGNAL(disconnected()),
                            this, SLOT(disconnected()));

        QObject::disconnect(tcpSocket, SIGNAL(readyRead()),
                            this, SLOT(socketData()));
    }
} // end disconnected


void AudioConnection::disconnect()
{
    qDebug() << "AudioConnection::disconnect Line " << __LINE__;

    if (tcpSocket != NULL)
    {
        tcpSocket->close();
        // object deletion moved in connect method
        // tcpSocket=NULL;

    }
} // end disconnnect


void AudioConnection::socketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError)
    {
    case QAbstractSocket::RemoteHostClosedError:
        qDebug() << "Remote closed connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        qDebug() << "Host not found";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        qDebug() << "Remote host refused connection";
        break;
    default:
        qDebug() << "Socket Error: " << tcpSocket->errorString();
    }

    emit disconnected(tcpSocket->errorString());
    // memory leakeage !!
    // tcpSocket=NULL;
} // end socketError


void AudioConnection::connected()
{
    qDebug() << "AudioConnection::Connected" << tcpSocket->isValid();
}


AudioConnection::~AudioConnection()
{

}


void AudioConnection::socketData()
{
    int     toRead;
    int     bytesRead=0;
    int     thisRead=0;
    int     header_size=5;

    if (bytes < 0)
    {
        fprintf(stderr,"QtRadio: FATAL: INVALID byte counter: %d\n", bytes);
        //tcpSocket->close();
        return;
    }

    toRead = tcpSocket->bytesAvailable();
    if (toRead <= 0)
    {
        return;
    }

    while (bytesRead < toRead)
    {
        switch (state)
        {
        case READ_HEADER:
            //fprintf (stderr, "READ_HEADER: hdr size: %d bytes: %d\n", header_size, bytes);
            thisRead = tcpSocket->read(&hdr[bytes], header_size - bytes);
            if (thisRead < 0)
            {
                fprintf(stderr, "QtRadio: FATAL: READ_HEADER: error in read: %d\n", thisRead);
                tcpSocket->close();
                return;
            }
            bytes += thisRead;
            if (bytes == header_size)
            {
                length = ((hdr[3] & 0xFF) << 8) + (hdr[4] & 0xFF);
                //     if ((length < 0) || (length > 4096))
                if (length < 1)
                {
                    state = READ_HEADER;
                    bytes = 0;
                }
                else
                {
                    buffer = (char*)malloc(length);
                    bytes = 0;
                    state = READ_BUFFER;
                }
            }
            break;

        case READ_BUFFER:
            thisRead = tcpSocket->read(&buffer[bytes], length - bytes);
            bytes += thisRead;
            if (bytes == length)
            {
                bytes = 0;
                state = READ_HEADER;
                emit audioBuffer(hdr, buffer);
            }
            break;

        default:
            fprintf (stderr, "FATAL: WRONG STATUS !!!!!\n");
        }
        bytesRead += thisRead;
    }
} // end socketData


void AudioConnection::freeBuffers(char* header, char* buffer)
{
    if (buffer != NULL) free(buffer);
} // end freeBuffers


/*************************************************************************************************************
 * ****************************************************************************************************
 * ***********************************************************************************************************/

MicAudioConnection::MicAudioConnection()
{
    qDebug() << "Mic audio::Connection";
    tcpSocket = NULL;

}


void MicAudioConnection::connect(QString h, int p)
{
    server = h;
    port = p;

    // cleanup previous object, if any
    if (tcpSocket)
    {
        delete tcpSocket;
    }

    tcpSocket = new QTcpSocket(this);

    QObject::connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(socketError(QAbstractSocket::SocketError)));

    QObject::connect(tcpSocket, SIGNAL(connected()),
                     this, SLOT(connected()));

    QObject::connect(tcpSocket, SIGNAL(disconnected()),
                     this, SLOT(disconnected()));

    QObject::connect(tcpSocket, SIGNAL(readyRead()),
                     this, SLOT(socketData()));

    // set the initial state
    state = READ_HEADER;
    // cleanup dirty value eventually left from previous usage
    bytes = 0;
    qDebug() << "Mic audio::connect: connectToHost: " << server << ":" << port;
    tcpSocket->connectToHost(server, port);
} // end connect


void MicAudioConnection::disconnected()
{
    qDebug() << "Mic audio::disconnected: emits: " << "Mic auido remote disconnected";
    emit disconnected("Mic audio remote disconnected");

    if (tcpSocket != NULL)
    {
        QObject::disconnect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
                            this, SLOT(socketError(QAbstractSocket::SocketError)));

        QObject::disconnect(tcpSocket, SIGNAL(connected()),
                            this, SLOT(connected()));

        QObject::disconnect(tcpSocket, SIGNAL(disconnected()),
                            this, SLOT(disconnected()));

        QObject::disconnect(tcpSocket, SIGNAL(readyRead()),
                            this, SLOT(socketData()));
    }
} // end disconnected


void MicAudioConnection::disconnect()
{
    qDebug() << "Mic audio connection::disconnect Line " << __LINE__;

    if (tcpSocket != NULL)
    {
        tcpSocket->close();
        // object deletion moved in connect method
        // tcpSocket=NULL;

    }
} // end disconnnect


void MicAudioConnection::socketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError)
    {
    case QAbstractSocket::RemoteHostClosedError:
        qDebug() << "Mic audio remote closed connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        qDebug() << "Mic audio host not found";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        qDebug() << "Mic audio remote host refused connection";
        break;
    default:
        qDebug() << "Mic audio socket Error: " << tcpSocket->errorString();
    }

    emit disconnected(tcpSocket->errorString());
    // memory leakeage !!
    // tcpSocket=NULL;
} // end socketError


void MicAudioConnection::connected()
{
    qDebug() << "Mic Audio::Connected" << tcpSocket->isValid();
}


MicAudioConnection::~MicAudioConnection()
{

}


void MicAudioConnection::socketData()
{

}


void MicAudioConnection::sendAudio(int8_t channel, int length, unsigned char* data)
{
    char buffer[MICS_BUFFER_SIZE];
    //    int  i;
    int  bytesWritten;

    //    for (i=0; i < SEND_BUFFER_SIZE; i++)
    //      buffer[i] = 0;
    if (tcpSocket!=NULL && tcpSocket->isValid() && tcpSocket->isWritable())
    {
        buffer[0] = channel;
        buffer[1] = ISMIC;
        memcpy(&buffer[6], data, length);
        mutex.lock();
        bytesWritten = tcpSocket->write(buffer, MICS_BUFFER_SIZE);
        if (bytesWritten != MICS_BUFFER_SIZE)
            qDebug() << "mic audio: write error";
        //        qDebug("send mic....");
        //tcpSocket->flush();
        mutex.unlock();
    }
} // end sendAudio
