/*
 * File:   Connection.h
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

/* Copyright (C) Rick Schnicker, KD0OSS
 * Rewrite for QtRadioII
 */

#ifndef CONNECTION_H
#define	CONNECTION_H

#include <QObject>
#include <QDebug>
#include <QTcpSocket>
#include <QTimer>
#include <QMutex>
#include <QQueue>
#include <QSemaphore>
#include <QCoreApplication>

#include "Buffer.h"
#include "../common.h"

#define DSPSERVER_BASE_PORT   8000

#define SPECTRUM_BUFFER       0
#define AUDIO_BUFFER          1
#define BANDSCOPE_BUFFER      2
#define RTP_REPLY_BUFFER      3
#define ANSWER_BUFFER         4

// minimum supported header version
#define HEADER_VERSION        2
#define HEADER_SUBVERSION     1

// g0orx binary header
#define HEADER_SIZE_2_1       15
#define AUDIO_HEADER_SIZE     5
#define AUDIO_LENGTH_POSITION 1

#define SEND_BUFFER_SIZE      64
#define MICS_BUFFER_SIZE      518

#define READ_HEADER           0
#define READ_BUFFER           1
#define READ_QINFO            2
#define READ_AUDIO_HEADER     4
#define READ_RTP_REPLY        5
#define READ_ANSWER           6
#define READ_MANIFEST         77
#define READ_SPECTRUM         8
#define READ_WIDEBAND         9

#define MAX_CHANNELS         35

typedef enum {
    RXS, TXS, BS
} SPECTRUM_TYPE;

typedef struct _xcvr
{
    int8_t    radio_id;
    int8_t    connection;
    char      radio_type[25];
    char      radio_name[25];
    char      ip_address[16];
    char      mac_address[18];
    bool      bandscope_capable;
    bool      mox;
    bool      local_audio;
    bool      local_mic;
    int       ant_switch;
} XCVR;

typedef struct _spectrum
{
    SPECTRUM_TYPE  type;
    unsigned short length;
    float          meter;
    float          fwd_pwr;
    float          rev_pwr;
    unsigned int   sample_rate;
    float          lo_offset;
    short int      fps;
    int            nsamples;
    int            frame_counter;
    char           *samples; // not used here, just a place holder for client side consistancy.
} SPECTRUM;

typedef struct _channel
{
    int8_t    id;
    XCVR      radio;
    SPECTRUM  spectrum;
    int8_t    dsp_channel;
    int8_t    protocol;
    long long frequency;
    int8_t    index;
    bool      isTX;
    bool      enabled;
} CHANNEL;

class ServerConnection : public QObject {
    Q_OBJECT

public:
    ServerConnection();
    virtual ~ServerConnection();

    QString  manifest_xml[4];
    int      available_xcvrs[4];
    bool     receivers_active[8];
    int8_t   receiver_channel[8];
    long     sample_rate;
    CHANNEL  channels[35];
    int8_t   txrxPair;
    int      active_channels;
    int      active_radios;
    int      selected_channel;

    void    connect(QString host, int receiver);
    void    freeBuffers(char* header, char* buffer);
    void    setMuted(bool);
    bool    getSlave();
    QString getHost();
    QString getXcvrProperty(int server, int xcvr, const QString property);

    QSemaphore SemSpectrum;

public slots:
    void connected();
    void disconnected();
    void disconnect();
    void socketError(QAbstractSocket::SocketError socketError);
    void cmdSocketData();
    void processBuffer();
    void activateRadio();
    void sendCommand(QByteArray command);

signals:
    void isConnected(bool*, int8_t*, int8_t*);
    void disconnected(QString message);
    void header(char* header);
    void audioBuffer(char* header, char* buffer);
    void printStatusBar(QString message);
    void slaveSetFreq(long long f);
    void slaveSetMode(int m);
    void slaveSetFilter(int l, int r);
    void slaveSetZoom(int z);
    void setdspversion(long, QString);
    void setservername( QString);
    void setCanTX(bool);
    void setChkTX(bool);  // password style server
    void resetbandedges(double loffset);
//    void setFPS();
    void hardware(QString);
    void activateRadioSig();
    void setSampleRate(long);
    void setCurrentChannel(int);
    void send_command(QByteArray command);

private:
    // really not used (and not even implemented)
    // defined as private in order to prevent unduly usage 
    ServerConnection(const ServerConnection& orig);
    void createChannels(int);

    QString      host;
    QTcpSocket  *tcpSocket;
    QMutex       mutex;
    char        *xml;
    int          port;
    int          state;
    char        *hdr;
    char        *buffer;
    short        length;   // int causes errors in converting 2 char bytes to integer
    int          bytes;
    bool         muted;
    bool         amSlave;
    long long    lastFreq;
    int          lastMode;
    int          lastSlave;
    long         serverver;
    bool         initialTxAllowedState;
    QQueue<Buffer*> queue;
};


//*****************************************************************************************
class SpectrumConnection : public QObject {
    Q_OBJECT
public:
    SpectrumConnection();
    virtual ~SpectrumConnection();
    void    connect(QString host, int receiver);
    void    freeBuffers(SPECTRUM);
    QString server;
    int     port;
    QMutex  trans_mutex;
    QMutex  recv_mutex;

private:
    QTcpSocket  *tcpSocket;
    int          state;
    char        *hdr;
    char        *buffer;
    unsigned short length;   // int causes errors in converting 2 char bytes to integer
    int          bytes;

public slots:
    void connected();
    void disconnected();
    void disconnect();
    void socketError(QAbstractSocket::SocketError socketError);
    void spectrumSocketData();
    void sendCommand(QByteArray command);

signals:
    void isConnected();
    void disconnected(QString message);
    void spectrumBuffer(CHANNEL);
    void send_spectrum_command(QByteArray);
};


//*****************************************************************************************
class WidebandConnection : public QObject {
    Q_OBJECT

public:
    WidebandConnection();
    virtual ~WidebandConnection();
    void    connect(QString host, int receiver);
    void    sendCommand(QByteArray command);
    void    freeBuffers(SPECTRUM);

private:
    QTcpSocket  *tcpSocket;
    QString      server;
    int          port;
    QMutex       mutex;
    int          state;
    char        *hdr;
    char        *buffer;
    unsigned short length;   // int causes errors in converting 2 char bytes to integer
    int          bytes;

public slots:
    void connected();
    void disconnected();
    void disconnect();
    void socketError(QAbstractSocket::SocketError socketError);
    void widebandSocketData();

signals:
    void isConnected();
    void disconnected(QString message);
    void bsConnected();
    void bandscopeBuffer(SPECTRUM);
};


//*****************************************************************************************
class AudioConnection : public QObject {
    Q_OBJECT
public:
    AudioConnection();
    virtual ~AudioConnection();
    void    connect(QString host, int receiver);
    void    freeBuffers(char* header, char* buffer);


private:
    QTcpSocket  *tcpSocket;
    QString      server;
    int          port;
    QMutex       mutex;
    int          state;
    char        *hdr;
    char        *buffer;
    short        length;   // int causes errors in converting 2 char bytes to integer
    int          bytes;

public slots:
    void connected();
    void disconnected();
    void disconnect();
    void socketError(QAbstractSocket::SocketError socketError);
    void socketData();

signals:
    void isConnected();
    void disconnected(QString message);
    void audioBuffer(char* header,char* buffer);
};

//*****************************************************************************************
class MicAudioConnection : public QObject {
    Q_OBJECT
public:
    MicAudioConnection();
    virtual ~MicAudioConnection();
    void    connect(QString host, int receiver);
    void    sendAudio(int8_t channel, int length, unsigned char* data);

private:
    QTcpSocket  *tcpSocket;
    QString      server;
    int          port;
    QMutex       mutex;
    int          state;
    char        *hdr;
    char        *buffer;
    short        length;   // int causes errors in converting 2 char bytes to integer
    int          bytes;

public slots:
    void connected();
    void disconnected();
    void disconnect();
    void socketError(QAbstractSocket::SocketError socketError);
    void socketData();

signals:
    void isConnected();
    void disconnected(QString message);
};
#endif	/* CONNECTION_H */

