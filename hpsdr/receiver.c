#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "radio.h"
#include "receiver.h"
#include "discovered.h"
#include "server.h"
#include "old_protocol.h"


RECEIVER *create_receiver(int id, int buffer_size, int alexRxAntenna, int alexAttenuation)
{
    fprintf(stderr,"create_receiver: id=%d device=%d buffer_size=%d\n", id, radio->device, buffer_size);
    fflush(stderr);

    RECEIVER *rx = (RECEIVER*)malloc(sizeof(RECEIVER));
    rx->id = id;
    pthread_mutex_init(&rx->mutex, NULL);

    switch (id)
    {
    case 0:
        rx->adc = 0;
        break;
    default:
        switch (protocol)
        {
        case ORIGINAL_PROTOCOL:
            switch (radio->device)
            {
            case DEVICE_METIS:
            case DEVICE_HERMES:
            case DEVICE_HERMES_LITE:
            case DEVICE_HERMES_LITE2:
                rx->adc = 0;
                break;
            default:
                rx->adc = 1;
                break;
            }
            break;
        default:
            switch (radio->device)
            {
            case NEW_DEVICE_ATLAS:
            case NEW_DEVICE_HERMES:
            case NEW_DEVICE_HERMES2:
                rx->adc = 0;
                break;
            default:
                rx->adc = 1;
                break;
            }
            break;
        }
    }

    fprintf(stderr,"create_receiver: id=%d default adc=%d\n", rx->id, rx->adc);
    rx->frequency = 14010000LL;
    rx->lo = 0LL;
    rx->ctun = 0;
    rx->ctun_frequency = rx->frequency;
    rx->offset = 0;

    rx->sample_rate = 48000;
    rx->buffer_size = buffer_size;
//    rx->update_timer_id = -1;

    rx->samples = 0;
//    rx->displaying = 0;
    rx->rf_gain = 50.0;

    rx->dither = 0;
    rx->random = 0;
    rx->preamp = 0;

//    BAND *b=band_get_band(vfo[rx->id].band);
    rx->alex_antenna=alexRxAntenna;
    rx->alex_attenuation=alexAttenuation;

//    rx->agc=AGC_MEDIUM;
//    rx->agc_gain=80.0;
//    rx->agc_slope=35.0;
//    rx->agc_hang_threshold=0.0;

//    rx->playback_handle=NULL;
    rx->local_audio = 0;
//    g_mutex_init(&rx->local_audio_mutex);
    rx->local_audio_buffer = NULL;
    rx->local_audio_buffer_size = 2048;
//    rx->audio_name=NULL;
//    rx->mute_when_not_active = 0;
//    rx->audio_channel=STEREO;
//    rx->audio_device=-1;

    rx->low_latency = 0;

//    rx->squelch_enable = 0;
//    rx->squelch = 0;

//    rx->filter_high = 525;
//    rx->filter_low = 275;

    rx->deviation = 2500;

//    rx->mute_radio = 0;

    // allocate buffers
    rx->iq_input_buffer = (double*)malloc((2*rx->buffer_size) * sizeof(double));
    rx->audio_buffer_size = 480;
//    rx->audio_sequence=0L;

    fprintf(stderr,"create_receiver: rx=%p id=%d audio_buffer_size=%d local_audio=%d\n", rx, rx->id, rx->audio_buffer_size, rx->local_audio);
    //rx->audio_buffer=g_new(guchar,rx->audio_buffer_size);
    int scale = rx->sample_rate/48000;
    rx->output_samples = rx->buffer_size/scale;
//    rx->audio_output_buffer=(double*)malloc((2*rx->output_samples) * sizeof(double));

    fprintf(stderr,"create_receiver: id=%d output_samples=%d\n", rx->id, rx->output_samples);

    return rx;
} // end create_receiver


void dump_udp_buffer(unsigned char* buffer)
{
    unsigned int i;
    fprintf(stderr, "udp ...\n");
    for(i=0; i<1024; i+=16)
    {
        fprintf(stderr, "  [%04X] %02X%02X%02X%02X%02X%02X%02X%02X %02X%02X%02X%02X%02X%02X%02X%02X\n",
                i,
                buffer[i],buffer[i+1],buffer[i+2],buffer[i+3],buffer[i+4],buffer[i+5],buffer[i+6],buffer[i+7],
                buffer[i+8],buffer[i+9],buffer[i+10],buffer[i+11],buffer[i+12],buffer[i+13],buffer[i+14],buffer[i+15]
                );
    }
    fprintf(stderr, "\n");
} // end dump_upd_buffer


void add_iq_samples(RECEIVER *rx, double i_sample, double q_sample)
{
    rx->iq_input_buffer[rx->samples*2] = i_sample;
    rx->iq_input_buffer[(rx->samples*2)+1] = q_sample;
    rx->samples = rx->samples+1;
    // fprintf(stderr, "%d   %f  %f\n", rx->samples, i_sample, q_sample);
    if (rx->samples >= rx->buffer_size)
    {
    //    pthread_mutex_lock(&rx->mutex);
        send_IQ_buffer(rx->id);
   //     pthread_mutex_unlock(&rx->mutex);
        rx->samples=0;
    }
} // end add_iq_samples


//
// Note that we sum the second channel onto the first one.
//
void add_div_iq_samples(RECEIVER *rx, double i0, double q0, double i1, double q1)
{
    rx->iq_input_buffer[rx->samples*2]    = i0 + (div_cos*i1 - div_sin*q1);
    rx->iq_input_buffer[(rx->samples*2)+1]= q0 + (div_sin*i1 + div_cos*q1);
    //  printf("%3.3f  %3.3f\n", i0, q0);
    rx->samples=rx->samples+1;
    if (rx->samples>=rx->buffer_size)
    {
        send_IQ_buffer(rx->id);
        rx->samples=0;
    }
} // end add_div_iq_samples


void receiver_change_sample_rate(RECEIVER *rx, int sample_rate)
{
    //
    // For  the PS_RX_FEEDBACK receiver we have to change
    // the number of pixels in the display (needed for
    // conversion from higher sample rates to 48K such
    // that the central part can be displayed in the TX panadapter
    //

    pthread_mutex_lock(&rx->mutex);

    rx->sample_rate = sample_rate;
    int scale = rx->sample_rate/48000;
    rx->output_samples = rx->buffer_size/scale;

    fprintf(stderr, "receiver_change_sample_rate: id=%d rate=%d scale=%d buffer_size=%d output_samples=%d\n",rx->id,sample_rate,scale,rx->buffer_size,rx->output_samples);
#ifdef PURESIGNAL
    if (rx->id == PS_RX_FEEDBACK) {
        if (protocol == ORIGINAL_PROTOCOL) {
            rx->pixels = 2* scale * rx->width;
        } else {
            // We should never arrive here, since the sample rate of the
            // PS feedback receiver is fixed.
            rx->pixels = 8 * rx->width;
        }
        g_free(rx->pixel_samples);
        rx->pixel_samples=g_new(float,rx->pixels);
        init_analyzer(rx);
        fprintf(stderr,"PS FEEDBACK change sample rate:id=%d rate=%d buffer_size=%d output_samples=%d\n",
                rx->id, rx->sample_rate, rx->buffer_size, rx->output_samples);
        g_mutex_unlock(&rx->mutex);
        return;
    }
#endif
///////    if (rx->audio_output_buffer != NULL) {
///////        g_free(rx->audio_output_buffer);
/////////    }
/////////    rx->audio_output_buffer=g_new(gdouble,2*rx->output_samples);

    if (protocol == ORIGINAL_PROTOCOL)
    {
        for (int i=1; i<8; i++)
            ozy_send_buffer();
    }

    pthread_mutex_unlock(&rx->mutex);

    fprintf(stderr,"receiver_change_sample_rate: id=%d rate=%d buffer_size=%d output_samples=%d\n",rx->id, rx->sample_rate, rx->buffer_size, rx->output_samples);
} // end receiver_change_sample_rate
