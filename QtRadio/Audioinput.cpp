
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

#include "Audioinput.h"

AudioInput::AudioInput()
{
    m_audioInput = NULL;
    m_audioInfo = NULL;
    m_sampleRate = 48000;
    m_audio_encoding = 0;
    m_channels = 1;

//    m_byte_order=QAudioFormat::LittleEndian;

    m_format.setSampleFormat(QAudioFormat::Int16);
    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(1);

//    m_format.setSampleSize(16);
//    m_format.setCodec("audio/pcm");
//    m_format.setByteOrder(m_byte_order);

}


AudioInput::~AudioInput()
{
}


QAudioDevice AudioInput::defaultInputDevice()
{
    QAudioDevice device = QMediaDevices::defaultAudioInput();

    if (device.isNull())
    {
        // No system default set: fall back to the first available input.
        const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
        if (!devices.isEmpty())
            device = devices.first();
    }
    return device;
} // end defaultInputDevice


void AudioInput::get_audioinput_devices(QComboBox* comboBox)
{
//    QList<QAudioDevice> devices=QAudioDevice:: availableDevices(QAudio::AudioInput);
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();

    qDebug() << "Audio::get_audioinput_devices ================";
    if (comboBox != NULL)
    {
        comboBox->clear();
        // "Default" carries a null device; select_audio() maps it to the system default.
        comboBox->addItem(tr("Default"), QVariant::fromValue(QAudioDevice()));
        for (const QAudioDevice &device : devices)
        {
            qDebug() << "Audio::get_audioinput_devices: " << device.description();
            comboBox->addItem(device.description(), QVariant::fromValue(device));
        }
    }

    m_device = defaultInputDevice();
    qDebug() << "Audioinput::get_audio_devices: default is " << m_device.description();

    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(m_channels);
    m_format.setSampleFormat(QAudioFormat::Int16);
    if (!m_device.isFormatSupported(m_format))
    {
        qDebug()<<"**************** Audio format not supported by Mic input device.";
    }

    m_audioInput = new QAudioSource(m_device, m_format, this);
    qDebug() << "Mic volume:" << QString("%1").arg(m_audioInput->volume(), 0, 'f', 3);
    m_audioInput->setVolume(0.5);
    m_format = m_audioInput->format();
    connect(m_audioInput, &QAudioSource::stateChanged, this, &AudioInput::stateChanged);

    qDebug() << "QAudioInput: error=" << m_audioInput->error() << " state=" << m_audioInput->state();

    m_audioInfo = new AudioInfo(m_format, this);
    connect(m_audioInfo, &AudioInfo::update, this, &AudioInput::slotMicUpdated);
    m_audioInfo->start();
    m_audioInput->start(m_audioInfo);

    if (m_audioInput->error() != 0)
    {
        qDebug() << "QAudioInput: after start error =" << m_audioInput->error() << " state =" << m_audioInput->state();
     //   m_format = m_device.preferredFormat();
        qDebug() << "Format:";
        qDebug() << "    sample rate: " << m_format.sampleRate();
        if (m_format.sampleFormat() == QAudioFormat::UInt8)
            qDebug() << "    sample type: Uint8";
        if (m_format.sampleFormat() == QAudioFormat::Int16)
            qDebug() << "    sample type: Int16";
        if (m_format.sampleFormat() == QAudioFormat::Int32)
            qDebug() << "    sample type: Int32";
        if (m_format.sampleFormat() == QAudioFormat::Float)
            qDebug() << "    sample format: Float";

//        qDebug() << "    codec: " << m_format.codec();
//        qDebug() << "    byte order: " << m_format.byteOrder();
//        qDebug() << "    sample size: " << m_format.sampleSize();
//        qDebug() << "    sample type: " << m_format.sampleType();
        qDebug() << "    channels: " << m_format.channelCount();
//        m_input = NULL;
        delete m_audioInput;
        m_audioInput = NULL;

        if (m_audioInfo != NULL)
        {
            m_audioInfo->stop();
            delete m_audioInfo;
            m_audioInfo = NULL;
        }
    }
}


void AudioInput::select_audio(QAudioDevice info, int rate, int channels)
{
    m_device = info.isNull() ? defaultInputDevice() : info;
    m_sampleRate = rate;
    m_channels = channels;
//    m_byte_order = byteOrder;
    qDebug() << "New mic source selected.";

    if (m_audioInput != NULL)
    {
        m_audioInput->stop();
        m_audioInput->disconnect(this);
        delete m_audioInput;
    }

    if (m_audioInfo != NULL)
    {
        m_audioInfo->stop();
        m_audioInfo->disconnect(this);
        delete m_audioInfo;
    }

    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(m_channels);
    m_format.setSampleFormat(QAudioFormat::Int16);
//    m_format.setByteOrder(m_byte_order);

    if (!m_device.isFormatSupported(m_format))
    {
        qDebug()<<"Audio format not supported by Mic input device.";
    }

    m_audioInput = new QAudioSource(m_device, m_format, this);
    qDebug() << "Mic volume:" << QString("%1").arg(m_audioInput->volume(), 0, 'f', 3);
    m_audioInput->setVolume(0.5);
    m_format = m_audioInput->format();

    connect(m_audioInput,&QAudioSource::stateChanged,this, &AudioInput::stateChanged);


    m_audioInfo  = new AudioInfo(m_format, this);
    connect(m_audioInfo,&AudioInfo::update,this,&AudioInput::slotMicUpdated);
    m_audioInfo->start();
    m_audioInput->start(m_audioInfo);

    if (m_audioInput->error()!=0)
    {
        qDebug() << "QAudioInput: after start errorn =" << m_audioInput->error() << " state=" << m_audioInput->state();

        qDebug() << "Format:";
        qDebug() << "    sample rate: " << m_format.sampleRate();
//        qDebug() << "    codec: " << m_format.codec();
//        qDebug() << "    byte order: " << m_format.byteOrder();
//        qDebug() << "    sample size: " << m_format.sampleSize();
//        qDebug() << "    sample type: " << m_format.sampleType();
        qDebug() << "    channels: " << m_format.channelCount();
//        m_input = NULL;
        delete m_audioInput;
        m_audioInput = NULL;

        if (m_audioInfo != NULL)
        {
            m_audioInfo->stop();
            delete m_audioInfo;
            m_audioInfo = NULL;
        }
    }
}


void AudioInput::stateChanged(QAudio::State State)
{
    switch (State)
    {
    case QAudio::StoppedState:
        if (m_audioInput->error() != QAudio::NoError)
        {
            qDebug() << "QAudioInput: after start error=" << m_audioInput->error() << " state=" << State;
            break;
        }
    case QAudio::IdleState:
    case QAudio::SuspendedState:
    case QAudio::ActiveState:
    default:
        //           qDebug() << "QAudioInput: state changed" << " state=" << State;
        return;
    }
}


void AudioInput::slotMicUpdated(QQueue<qint16>* queue)
{
    emit mic_update_level(m_audioInfo->level());
    emit mic_send_audio(queue);
}


int AudioInput::getMicEncoding()
{
    return m_audio_encoding;
}


void AudioInput::setMicEncoding(int encoding)
{
    m_audio_encoding = encoding;
    qDebug() << "Mic encoding changed to " << m_audio_encoding;
}


AudioInfo::AudioInfo(const QAudioFormat &format, QObject *parent) : QIODevice(parent), m_format(format), m_maxAmplitude(0), m_level(0.0)
{
    m_aout_max_buf_idx = 0;
    memset(m_aout_max_buf, 0, sizeof(m_aout_max_buf));
    m_aout_gain = 100;
    m_volume = 0.5f;
    m_maxAmplitude = 65535;

    switch (m_format.bytesPerSample())
    {
    case 1:
        switch (m_format.sampleFormat())
        {
        case QAudioFormat::UInt8:
            m_maxAmplitude = 256;
            break;
        default: ;
        }
        break;
    case 2:
        switch (m_format.sampleFormat())
        {
        case QAudioFormat::Int16:
            m_maxAmplitude = 65535;
            break;
        default: ;
        }
        break;
    default:
    {
        m_maxAmplitude = 1;
    }
    }
}


AudioInfo::~AudioInfo()
{
}


void AudioInfo::start()
{
    open(QIODevice::WriteOnly);
}


void AudioInfo::stop()
{
    close();
}


qint64 AudioInfo::readData(char *data, qint64 maxlen)
{
    Q_UNUSED(data)
    Q_UNUSED(maxlen)

    return 0;
}

#ifdef ONE
qint64 AudioInfo::writeData(const char *data, qint64 len)
{
    const int channelBytes = m_format.bytesPerSample();
    const int sampleBytes = m_format.channelCount() * channelBytes;
    qint64 length = m_format.bytesForDuration(1000000);
    Q_ASSERT((length % sampleBytes) == 0);
    Q_UNUSED(sampleBytes); // suppress warning in release builds

    int sampleIndex = 0;
    qint16 v = 0;

    while (length)
    {
        // Produces value (-1..1)
        const qreal x = qSin(2 * M_PI * 600 * qreal(sampleIndex++ % m_format.sampleRate()) / m_format.sampleRate());
        for (int i = 0; i < m_format.channelCount(); ++i)
        {
            switch (m_format.sampleFormat())
            {
            case QAudioFormat::UInt8:
                v = ((1.0 + x) / 2 * 255);
                break;
            case QAudioFormat::Int16:
                v = (x * 32767);
                break;
            case QAudioFormat::Int32:
                v = (x * std::numeric_limits<qint32>::max());
                break;
            case QAudioFormat::Float:
                v = x * 32762;
                break;
            default:
                break;
            }

            if (i == 0)
            {
                //      printf("NS: %3d  v: %5d\n", numSamples, v);
                m_queue.enqueue(v);            // use only 1st channel
            }
        //    ptr += channelBytes;
            length -= channelBytes;
        }
    }
    emit update(&m_queue);
    return len;
}
#endif

qint64 AudioInfo::writeData(const char *data, qint64 len)
{
    qint16 v;
    quint64 maxValue = 0;

    if (m_maxAmplitude)
    {
    //    Q_ASSERT(m_format.sampleFormat() % 8 == 0);
        const int channelBytes = m_format.bytesPerSample();
        const int sampleBytes = m_format.channelCount() * channelBytes;
        Q_ASSERT((len % sampleBytes) == 0);
        const int numSamples = len / sampleBytes;

        const unsigned char *ptr = reinterpret_cast<const unsigned char *>(data);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int j = 0; j < m_format.channelCount(); ++j)
            {
                quint16 value = 0;

                v = 0;
                if (m_format.sampleFormat() == QAudioFormat::UInt8)
                {
                    v = *reinterpret_cast<const quint8*>(ptr);
                    value = qAbs(v * 128);
                } else if (m_format.sampleFormat() == QAudioFormat::Int16)
                {
                    v = qFromLittleEndian<quint16>(ptr);
                    value = qAbs(v * 32767);
                } else if (m_format.sampleFormat() == QAudioFormat::Float)
                {
                    float f = qBound(-1.0f, qFromLittleEndian<float>(ptr), 1.0f);
                    v = (qint16)(f * 32767.0f);
                    value = qAbs(v);
                }
                if (j == 0)
                {
                    m_queue.enqueue(v);            // use only 1st channel
                    maxValue += value; //qMax(value, maxValue);
                }
                ptr += channelBytes;
            }
        }

        maxValue = maxValue / (numSamples / m_format.channelCount());
        maxValue = qMin(maxValue, m_maxAmplitude);
        m_level = qreal(maxValue) / m_maxAmplitude;
    }
    process_audio(&m_queue);
    emit update(&m_queue);
 //   printf("Level %f\n", m_level);
 //   fflush(stdout);
    return len;
}


// process_audio() based on code from DSD https://github.com/szechyjs/dsd
void AudioInfo::process_audio(QQueue<qint16>* queue)
{
    float aout_abs, max, gainfactor, gaindelta, maxbuf;
    size_t s = (size_t)queue->size();

    if (s == 0)
        return;
    m_audio_out_temp_buf = (float*)malloc(s * sizeof(float));
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    for(qsizetype i = 0; i < queue->size(); ++i){
        m_audio_out_temp_buf[i] = static_cast<float>(queue->at(i));
    }

    // detect max level
    max = 0;
  //  m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    for (size_t i = 0; i < s; i++){
        aout_abs = fabsf(*m_audio_out_temp_buf_p);

        if (aout_abs > max){
            max = aout_abs;
        }

        m_audio_out_temp_buf_p++;
    }

    // store this block's peak in the 25-entry history ring
    m_aout_max_buf[m_aout_max_buf_idx] = max;
    m_aout_max_buf_idx++;

    if (m_aout_max_buf_idx > 24){
        m_aout_max_buf_idx = 0;
    }

    // lookup max history
    for (size_t i = 0; i < 25; i++){
        maxbuf = m_aout_max_buf[i];

        if (maxbuf > max){
            max = maxbuf;
        }
    }

    // determine optimal gain level
    if (max > static_cast<float>(0)){
        gainfactor = (static_cast<float>(30000) / max);
    }
    else{
        gainfactor = static_cast<float>(50);
    }

    if (gainfactor < m_aout_gain){
        m_aout_gain = gainfactor;
        gaindelta = static_cast<float>(0);
    }
    else{
        if (gainfactor > static_cast<float>(50)){
            gainfactor = static_cast<float>(50);
        }

        gaindelta = gainfactor - m_aout_gain;

        if (gaindelta > (static_cast<float>(0.05) * m_aout_gain)){
            gaindelta = (static_cast<float>(0.05) * m_aout_gain);
        }
    }

    gaindelta /= static_cast<float>(s); //160

    // adjust output gain
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    for (size_t i = 0; i < s; i++){
        *m_audio_out_temp_buf_p = (m_aout_gain + (static_cast<float>(i) * gaindelta)) * (*m_audio_out_temp_buf_p);
        m_audio_out_temp_buf_p++;
    }

    m_aout_gain += (static_cast<float>(s) * gaindelta);
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    queue->clear();
    for (size_t i = 0; i < s; i++){
        *m_audio_out_temp_buf_p *= m_volume;
        if (*m_audio_out_temp_buf_p > static_cast<float>(32760)){
            *m_audio_out_temp_buf_p = static_cast<float>(32760);
        }
        else if (*m_audio_out_temp_buf_p < static_cast<float>(-32760)){
            *m_audio_out_temp_buf_p = static_cast<float>(-32760);
        }
        queue->enqueue( static_cast<int16_t>(*m_audio_out_temp_buf_p));
        m_audio_out_temp_buf_p++;
    }
    free(m_audio_out_temp_buf);
}

