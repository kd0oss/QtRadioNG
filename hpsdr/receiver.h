#ifndef RECEIVER_H
#define RECEIVER_H

#include <pthread.h>
#include "server.h"

typedef struct _receiver {
    int radio_id;
    int id;
    CLIENT *client;
    pthread_mutex_t mutex;
    //  GMutex display_mutex;

    int ddc;
    int adc;

    long long frequency;
    long long lo;
    int ctun;
    long long ctun_frequency;
    long long offset;
    int mode;
    int filter;
    int rit_enabled;
    long long rit;

//    int displaying;
    //  audio_t audio_channel;
    int sample_rate;
    int buffer_size;
    int samples;
    int output_samples;
    double *iq_input_buffer;
    double *audio_output_buffer;
    int audio_buffer_size;
    //guchar *audio_buffer;
    //int audio_index;
    //uint32 audio_sequence;
//    int update_timer_id;
    double meter;

    int dither;
    int random;
    int preamp;

    double rf_gain;

    int alex_antenna;
    int alex_attenuation;

//    int filter_low;
//    int filter_high;

    int local_audio;
    int mute_when_not_active;
    //  int audio_device;
    //  char *audio_name;
    int local_audio_buffer_size;
    int local_audio_buffer_offset;
    void *local_audio_buffer;
    //  GMutex local_audio_mutex;

    int low_latency;

//    int squelch_enable;
//    double squelch;

    int deviation;

//    int mute_radio;

    double *buffer;
    void *resampler;
    double *resample_buffer;
    int resample_buffer_size;

} RECEIVER;


typedef struct _wideband {
  int channel; // WDSP channel
  int adc;
  int buffer_size;
  int fft_size;

  int samples;
  int pixels;

  double hz_per_pixel;

  long long sequence;
  int16_t *input_buffer;
  float *pixel_samples;
} WIDEBAND;


//extern RECEIVER *create_pure_signal_receiver(int id, int buffer_size,int sample_rate,int pixels);
extern RECEIVER *create_receiver(int id, int buffer_size, int alexRxAntenna, int alexAttenuation);
extern void add_iq_samples(RECEIVER *rx, double i_sample,double q_sample);
extern void add_div_iq_samples(RECEIVER *rx, double i0,double q0, double i1, double q1);
extern void receiver_change_sample_rate(RECEIVER *rx, int sample_rate);
extern void dump_udp_buffer(unsigned char* buffer);
/* extern void receiver_change_sample_rate(RECEIVER *rx,int sample_rate);
extern void receiver_change_adc(RECEIVER *rx,int adc);
extern void receiver_frequency_changed(RECEIVER *rx);
//extern void receiver_mode_changed(RECEIVER *rx);
extern void receiver_filter_changed(RECEIVER *rx);
//extern void receiver_vfo_changed(RECEIVER *rx);

//extern void set_mode(RECEIVER* rx,int m);
extern void set_filter(RECEIVER *rx,int low,int high);
extern void set_agc(RECEIVER *rx, int agc);
extern void set_offset(RECEIVER *rx, long long offset);
extern void set_deviation(RECEIVER *rx);

extern void reconfigure_receiver(RECEIVER *rx,int height);
*/
#endif // RECEIVER_H
