/* 
 * File:   Audio.h
 * Author: John Melton, G0ORX/N6LYT
 *
 * Created on 16 August 2010, 11:19
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
* Updated to Qt6 libraries. Rick Schnickr KD0OSS 2024
*/

#ifndef AUDIO_H
#define	AUDIO_H

#include <QtCore>
#include <QScopedPointer>
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>

#include <QComboBox>

#include <QMutex>
#include <samplerate.h>
#include <QThread>
#include "G711A.h"
#include "cusdr_queue.h"

#define AUDIO_BUFFER_SIZE 800
#define AUDIO_OUTPUT_BUFFER_SIZE (1024*2)
#define RESAMPLING_BUFFER_SIZE (32000*2)    // 2 channels

#define BIGENDIAN
// There are problems running at 8000 samples per second on Mac OS X
// The resolution is to run at 8011 samples persecond.
//
// Update: KD0NUZ - June 28, 2013, OSX 10.8.4 - Crashes with FUDGE > 0
//
//#define SAMPLE_RATE_FUDGE 11
#define SAMPLE_RATE_FUDGE 0

class Audio;

class Audio_playback : public QIODevice
{
    Q_OBJECT
public:
    Audio_playback();
    ~Audio_playback();

    void start();
    void stop();
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;
    // Qt 6's QAudioSink only pulls data in pull mode when these report some available.
    bool isSequential() const override;
    qint64 bytesAvailable() const override;
public slots:
    void set_decoded_buffer(QHQueue<qint16>* pBuffer);
    void set_audio_encoding(int encoding);
private:
    quint32 recv_ts;
    QHQueue <qint16> * pdecoded_buffer;
    int audio_encoding;
    G711A g711a;
    QHQueue<qint16> queue;
signals:
};

class Audio_processing : public QObject {
    Q_OBJECT
public:
    Audio_processing();
    ~Audio_processing();
public slots:
    void process_audio(char* header, char* buffer, int length);
    void set_queue(QHQueue<qint16> *buffer);
    void set_audio_channels(int c);
    void set_audio_encoding(int enc);
private:
    void aLawDecode(char* buffer,int length);
    void pcmDecode(char * buffer,int length);
    void init_decodetable();
    float buffer_in[RESAMPLING_BUFFER_SIZE];
    float buffer_out[RESAMPLING_BUFFER_SIZE];
    short decodetable[256];
    SRC_STATE *src_state;
    double src_ratio;
    SRC_DATA sr_data;
    QHQueue<qint16> queue;
    QHQueue<qint16> *pdecoded_buffer;
    int audio_channels;
    int audio_encoding;
};


class Audio : public QObject {
    Q_OBJECT
public:
    Audio();
    ~Audio();
    int get_audio_encoding();
    QHQueue <qint16> decoded_buffer;
    int audio_encoding;
    QMediaDevices *m_devices = nullptr;
signals:
    void audio_processing_process_audio(char* header,char* buffer,int length);
public slots:
    void updateAudioDevices(QComboBox *comboBox);
    void deviceChanged(int index);
    void select_audio(QAudioDevice info,int rate,int channels);
    void process_audio(char* header,char* buffer,int length);
    void clear_decoded_buffer(void);
    void get_audio_device(QAudioDevice * device);
    void set_audio_encoding(int enc);
    void set_volume(qreal);
private:
    void             initializeAudio(const QAudioDevice &deviceInfo);
    QScopedPointer<Audio_playback> m_audioPlayback;
    QAudioFormat     audio_format;
    QScopedPointer<QAudioSink>     m_audioOutput;
    QAudioSink     *audio_output;
    bool             connected;
    QThread*         audio_output_thread;
    QAudioDevice     audio_device;
    Audio_playback   *audio_out;
    int              sampleRate;
    int              audio_channels;
    Audio_processing *audio_processing;
    QThread          *audio_processing_thread;
    QComboBox        *devList;
};


#endif	/* AUDIO_H */
