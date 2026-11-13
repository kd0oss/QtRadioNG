/*
 * File:   Audio.cpp
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


Additions and changes by Rick Schnicker KD0OSS 2021
Updated to Qt6 libraries. Rick Schnicker KD0OSS 2024
*/

#ifdef _OPENMP
#include <omp.h>
#endif
#include "Audio.h"

Audio_playback::Audio_playback():QIODevice()
{
    recv_ts = 0;
    audio_encoding = 0;
    pdecoded_buffer = &queue;
}


Audio_playback::~Audio_playback()
{
}


void Audio_playback::start()
{
    open(QIODevice::ReadOnly);
}

void Audio_playback::stop()
{
    close();
}


void Audio_playback::set_decoded_buffer(QHQueue<qint16> *pBuffer)
{
    pdecoded_buffer = pBuffer;
}


void Audio_playback::set_audio_encoding(int encoding)
{
    audio_encoding = encoding;
}


bool Audio_playback::isSequential() const
{
    return true;   // a live audio stream: no size, no seeking
}


qint64 Audio_playback::bytesAvailable() const
{
    // Qt 6's QAudioSink reads in pull mode only as much as this reports, and
    // stops pulling for good if it reports 0 (QTBUG-108672). readData() pads
    // with silence when the decoded queue runs dry, so always report at least
    // one chunk to keep the stream running, as QAudioOutput did in Qt 5.
    const qint64 queued = (qint64)pdecoded_buffer->count() * (qint64)sizeof(qint16);
    return qMax<qint64>(queued, 2000) + QIODevice::bytesAvailable();
}


qint64 Audio_playback::readData(char *data, qint64 maxlen)
{
    qint64 bytes_read;
    qint16 v;
    qint64 bytes_to_read = maxlen > 2000 ? 2000: maxlen;
    int audio_byte_order =  0;
    // note both TCP and RTP audio enqueue PCM data in decoded_buffer
    bytes_read = 0;

    if (pdecoded_buffer->isEmpty())
    {
        // probably not connected or late arrival of packets.  Send silence.
        memset(data, 0, bytes_to_read);
        bytes_read = bytes_to_read;
    } else {
        while ((!pdecoded_buffer->isEmpty()) && (bytes_read < bytes_to_read))
        {
            v = pdecoded_buffer->dequeue();
            switch(audio_byte_order)
            {
            case 0:
                data[bytes_read++]=(char)(v&0xFF);
                data[bytes_read++]=(char)((v>>8)&0xFF);
                break;
            case 1:
                data[bytes_read++]=(char)((v>>8)&0xFF);
                data[bytes_read++]=(char)(v&0xFF);
                break;
            }
        }
        while (bytes_read < bytes_to_read) data[bytes_read++] = 0;
    }
    return bytes_read;
}

qint64 Audio_playback::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data)
    Q_UNUSED(len)
    return 0;
}


Audio::Audio() : m_devices(new QMediaDevices(this))
{
    audio_output = NULL;
    connected = false;
    sampleRate = 48000;
    audio_encoding = 0;
    audio_channels = 1;
    initializeAudio(m_devices->defaultAudioOutput());
    audio_processing = new Audio_processing;
    audio_processing->set_audio_channels(audio_channels);
    audio_processing->set_audio_encoding(audio_encoding);
    audio_processing->set_queue(&decoded_buffer);
    audio_processing_thread = new QThread;
    //qDebug() << "QThread: audio_processing_thread = " << audio_processing_thread;
    audio_processing->moveToThread(audio_processing_thread);
    connect(this,&Audio::audio_processing_process_audio,audio_processing,&Audio_processing::process_audio);
    audio_processing_thread->start(QThread::LowPriority);
 //   connect(m_devices, SIGNAL(audioOutputsChanged()), this, SLOT(updateAudioDevices()));
    m_audioOutput->start(m_audioPlayback.data());
    fprintf(stderr, "Audio processing initialized.\n");
}


Audio::~Audio()
{
    disconnect(this,&Audio::audio_processing_process_audio,audio_processing,&Audio_processing::process_audio);
    audio_processing->deleteLater();
}


void Audio::initializeAudio(const QAudioDevice &deviceInfo)
{
    QAudioFormat format = deviceInfo.preferredFormat();

    format.setSampleRate(sampleRate);
    format.setSampleFormat(QAudioFormat::Int16);
    format.setChannelCount(audio_channels);
    m_audioPlayback.reset(new Audio_playback());
    m_audioPlayback->set_decoded_buffer(&decoded_buffer);
    m_audioOutput.reset(new QAudioSink(deviceInfo, format));
    m_audioPlayback->start();
}


void Audio::select_audio(QAudioDevice info,int rate,int channels)
{
    QAudioFormat format = info.preferredFormat();

    //qDebug() << "selected audio " << info.deviceName() <<  " sampleRate:" << rate << " Channels: " << channels << " Endian:" << (byteOrder==QAudioFormat::BigEndian?"BigEndian":"LittleEndian");

    sampleRate=rate;
    audio_channels=channels;
    format.setSampleRate(rate);
    format.setChannelCount(channels);
 //   m_audioOutput.reset(new QAudioSink(info, format));
}


void Audio::set_volume(qreal vol)
{
    m_audioOutput->setVolume(vol);
}

void Audio::clear_decoded_buffer(void)
{
    decoded_buffer.clear();
}


int Audio::get_audio_encoding()
{
    return audio_encoding;
}


void Audio::set_audio_encoding(int enc)
{
    audio_encoding = enc;
    m_audioPlayback->set_audio_encoding(enc);
    audio_processing->set_audio_encoding(enc);
    fprintf(stderr, "Set audio encoding to %d\n", enc);
}


void Audio::get_audio_device(QAudioDevice * device){
    *device = audio_device;
}


void Audio::deviceChanged(int index)
{
    m_audioPlayback->stop();
    m_audioOutput->stop();
    m_audioOutput->disconnect(this);
    initializeAudio(devList->itemData(index).value<QAudioDevice>());
}

void Audio::updateAudioDevices(QComboBox *comboBox)
{
    devList = comboBox;
    comboBox->clear();
    const QList<QAudioDevice> devices = m_devices->audioOutputs();
    for (const QAudioDevice &deviceInfo : devices)
        comboBox->addItem(deviceInfo.description(), QVariant::fromValue(deviceInfo));
}


void Audio::process_audio(char* header, char* buffer, int length)
{
    emit audio_processing_process_audio(header, buffer, length);
}


Audio_processing::Audio_processing()
{
    int sr_error;

    src_state =  src_new(
                //SRC_SINC_BEST_QUALITY,  // NOT USABLE AT ALL on Atom 300 !!!!!!!
                //SRC_SINC_MEDIUM_QUALITY,
                SRC_SINC_FASTEST,
                //SRC_ZERO_ORDER_HOLD,
                //SRC_LINEAR,
                1, &sr_error
              ) ;

    if (src_state == 0)
    {
        //qDebug() <<  "Audio: SR INIT ERROR: " << src_strerror(sr_error);
    }
    init_decodetable();
    src_ratio = 1.0;
    pdecoded_buffer = &queue;
}


Audio_processing::~Audio_processing()
{
     src_delete(src_state);
}


void Audio_processing::set_queue(QHQueue<qint16> *buffer)
{
    pdecoded_buffer = buffer;
}


void Audio_processing::set_audio_channels(int c){
    audio_channels = c;
    int sr_error;

    src_delete(src_state);
    src_state =  src_new (
                //SRC_SINC_BEST_QUALITY,  // NOT USABLE AT ALL on Atom 300 !!!!!!!
                //SRC_SINC_MEDIUM_QUALITY,
                SRC_SINC_FASTEST,
                //SRC_ZERO_ORDER_HOLD,
                //SRC_LINEAR,
                c, &sr_error
              ) ;

    if (src_state == 0)
    {
        //qDebug() <<  "Audio: SR INIT ERROR: " << src_strerror(sr_error);
    }
}


void Audio_processing::set_audio_encoding(int enc)
{
    audio_encoding = enc;
}


void Audio_processing::process_audio(char* header, char* buffer, int length)
{
    if (pdecoded_buffer->count() < 4000)
    {
        if (audio_encoding == 0) aLawDecode(buffer,length);
        else if (audio_encoding == 1) pcmDecode(buffer,length);
        else
        {
            //qDebug() << "Error: Audio::process_audio:  audio_encoding = " << audio_encoding;
        }
    }
    if (buffer != NULL) free(buffer);
}


void Audio_processing::aLawDecode(char* buffer,int length)
{
    int i;
    qint16 v;

    for (i=0; i < length; i++)
    {
        v=decodetable[buffer[i]&0xFF];
        pdecoded_buffer->enqueue(v);
    }
}


void Audio_processing::pcmDecode(char* buffer,int length)
{
    int i;
    short v;

    for (i=0; i < length; i+=2)
    {
        v = (buffer[i] & 0xff) | ((buffer[i+1] & 0xff) << 8);
        pdecoded_buffer->enqueue(v);
    }
}


void Audio_processing::init_decodetable()
{
    //qDebug() << "init_decodetable";
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < 256; i++)
    {
        int input = i ^ 85;
        int mantissa = (input & 15) << 4;
        int segment = (input & 112) >> 4;
        int value = mantissa + 8;
        if (segment >= 1)
        {
            value += 256;
        }
        if (segment > 1)
        {
            value <<= (segment - 1);
        }
        if ((input & 128) == 0)
        {
            value = -value;
        }
        decodetable[i] = (short) value;
    }
}
