#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H



#include <QObject>
#include <QtCore>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaCaptureSession>
#include <QMediaRecorder>

#include <QComboBox>

#include <QMutex>
#include <QQueue>

Q_DECLARE_METATYPE(QQueue<qint16>);
Q_DECLARE_METATYPE(QQueue<qint16>*);


class AudioInfo : public QIODevice
{
    Q_OBJECT
public:
    AudioInfo(const QAudioFormat &format, QObject *parent);
    ~AudioInfo();

    void start();
    void stop();

    qreal level() const { return m_level; }
    qint64 readData(char *data, qint64 maxlen);
    qint64 writeData(const char *data, qint64 len);
    void process_audio(QQueue<qint16>* queue);

private:
    const QAudioFormat m_format;
    quint16 m_maxAmplitude;
    qreal m_level; // 0.0 <= m_level <= 1.0
    QQueue<qint16> m_queue;
    QByteArray m_buffer;
    float *m_audio_out_temp_buf;   //!< output of decoder
    float *m_audio_out_temp_buf_p;
    float m_aout_max_buf[25];      //!< per-block peak history (persists across calls)
    int m_aout_max_buf_idx;
    float m_aout_gain;
    float m_volume;

signals:
    void update(QQueue<qint16>* queue);
};

class AudioInput : public QObject
{
    Q_OBJECT
public:
    AudioInput();
    ~AudioInput();

    void get_audioinput_devices(QComboBox* comboBox);
    int getMicEncoding(void);
    static QAudioDevice defaultInputDevice();

signals:
    void mic_update_level(qreal level);
    void mic_send_audio(QQueue<qint16>* queue);

public slots:
    void stateChanged(QAudio::State);
    void select_audio(QAudioDevice info,int rate,int channels);
    void slotMicUpdated(QQueue<qint16>*);
    void setMicEncoding(int encoding);

private:
    QMediaDevices m_devices;
    QAudioDevice m_device;
    QAudioFormat m_format;
    QAudioSource *m_audioInput;
    QIODevice *m_input;
    QByteArray m_buffer;

    AudioInfo *m_audioInfo;
    QMediaFormat selectedMediaFormat() const;
    QMediaRecorder *m_audioRecorder = nullptr;
    QMediaCaptureSession m_captureSession;

    int m_sampleRate;
    int m_channels;
    int m_audio_encoding;
};



#endif // AUDIOINPUT_H
