#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <sys/timeb.h>
/* For fcntl */
#include <fcntl.h>

#include <samplerate.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "server.h"
#include "hardware.h"
#include "audiostream.h"
#include "main.h"
#include "wdsp.h"
#include "buffer.h"
#include "G711A.h"
#include "util.h"

#define true  1
#define false 0

float multimeterCalibrationOffset;

extern int encoding;
extern int send_audio;
extern int sample_rate;
extern int zoom;
extern int low, high;
extern sem_t bufferevent_semaphore,
             iq_semaphore,
             wb_iq_semaphore,
             audio_bufevent_semaphore;

int8_t active_receivers = 0;
int8_t active_transmitters = 0;

extern timer_t spectrum_timerid[MAX_RFSTREAMS];
extern float *widebandBuffer[5];

bool bUseNB2 = false;
bool bUseNB = false;
bool audio_enabled[MAX_RFSTREAMS];
int rxMeterMode;
int txMeterMode;
int lastMode;
int panadapterMode;
float displayCalibrationOffset;


float getFilterSizeCalibrationOffset()
{
    int size = 1024; // dspBufferSize
    float i = log10((float)size);
    return 3.0f*(11.0f-i);
} // end getFilterSizeCalibrationOffset


void init_analyzer(int disp, int length)
{
    multimeterCalibrationOffset = -41.0f;
    displayCalibrationOffset = -48.0f;

    initAnalyzer(disp, 128, 15, length);
} // end init_analyzer


void calculate_display_average(int disp)
{
    double display_avb;
    int display_average;

    double t = 0.001 * 170.0f;

    display_avb = exp(-1.0 / ((double)15 * t));
    display_average = fmax(2, (int)fmin(60, (double)15 * t));
    SetDisplayAvBackmult(disp, 0, display_avb);
    SetDisplayNumAverage(disp, 0, display_average);
    fprintf(stderr, "Disp: %d  avb: %f   avg: %d\n", disp, display_avb, display_average);
} // end calculate_display_average


void initAnalyzer(int disp, int pixels, int frame_rate, int blocksize)
{
    //maximum number of frames of pixels to average
    //   int frame_rate = 15;
    double tau = 0.12;
    int n_pixout = 1;
    int overlap = 0;
    int fft_size = 8192; //32768;
    int sample_rate = 48000;
    int clip = 0;
    int stitches = 1;
    double z_slider = 0.0;
    int span_clip_l = 0;
    double pan_slider = 0.5;
    int span_clip_h = 0;
    const double KEEP_TIME = 0.1;
    int max_w;
    //    double freq_offset = 12000.0;
    int spur_eliminationtion_ffts = 1;
    static int h_flip[] = {0};
    //   int blocksize = 512;
    int window_type = 5;
    double kaiser_pi = 14.0;
    int calibration_data_set = 0;
    double span_min_freq = 0.0;
    double span_max_freq = 0.0;

    const int MAX_AV_FRAMES = 60;

    //compute multiplier for weighted averaging
    double avb = exp(-1.0 / (frame_rate * tau));
    //compute number of frames to average for window averaging
    int display_average = MAX(2, (int)MIN(MAX_AV_FRAMES, frame_rate * tau));

    //no spur elimination => only one spur_elim_fft and it's spectrum is flipped
    //    h_flip[0] = 0;

    int low = 0;
    int high = 0;
    double bw_per_subspan = 0.0;

        //fraction of the spectrum to clip off each side of each sub-span
        const double CLIP_FRACTION = 0.017;

        //set overlap as needed to achieve the desired frame_rate
        overlap = (int)MAX(0.0, ceil(fft_size - (double)sample_rate / (double)frame_rate));

        //clip is the number of bins to clip off each side of each sub-span
        clip = (int)floor(CLIP_FRACTION * fft_size);

        //the amount of frequency in each fft bin (for complex samples) is given by:
        double bin_width = (double)sample_rate / (double)fft_size;

        //the number of useable bins per subspan is
        int bins_per_subspan = fft_size - 2 * clip;

        //the amount of useable bandwidth we get from each subspan is:
        bw_per_subspan = bins_per_subspan * bin_width;

        //the total number of bins available to display is:
        int bins = stitches * bins_per_subspan;

        //apply log function to zoom slider value
        double zoom_slider = log10(9.0 * z_slider + 1.0);

        //limits how much you can zoom in; higher value means you zoom more
        const double zoom_limit = 100;

        int width = (int)(bins * (1.0 - (1.0 - 1.0 / zoom_limit) * zoom_slider));

        //FSCLIPL is 0 if pan_slider is 0; it's bins-width if pan_slider is 1
        //FSCLIPH is bins-width if pan_slider is 0; it's 0 if pan_slider is 1
        span_clip_l = (int)floor(pan_slider * (bins - width));
        span_clip_h = bins - width - span_clip_l;
        /*
                        if (Display.RX1DSPMode == DSPMode.DRM)
                        {
                            //Apply any desired frequency offset
                            int bin_offset = (int)(freq_offset / bin_width);
                            if ((span_clip_h -= bin_offset) < 0) span_clip_h = 0;
                            span_clip_l = bins - width - span_clip_h;
                        }
        */
        //As for the low and high frequencies that are being displayed:
        low = -(int)((double)stitches / 2.0 * bw_per_subspan - (double)span_clip_l * bin_width + bin_width / 2.0);
        high = +(int)((double)stitches / 2.0 * bw_per_subspan - (double)span_clip_h * bin_width - bin_width / 2.0);
        //Note that the bin_width/2.0 factors are included because the complex FFT has one more negative output bin
        //  than positive output bin.
        max_w = fft_size + (int)MIN(KEEP_TIME * sample_rate, KEEP_TIME * fft_size * frame_rate);

    fprintf(stderr, "Disp: %d  avb: %f  davg: %d  low: %d  high: %d  clip: %d  sclip_l: %d  sclip_h: %d  overlap: %d  maxw: %d\n", disp, avb, display_average, low, high, clip, span_clip_l, span_clip_h, overlap, max_w);
    SetAnalyzer(disp,
                n_pixout,
                spur_eliminationtion_ffts,
                1,
                h_flip,
                fft_size,
                blocksize,
                window_type,
                kaiser_pi,
                overlap,
                clip,
                span_clip_l,
                span_clip_h,
                pixels,
                stitches,
                calibration_data_set,
                span_min_freq,
                span_max_freq,
                max_w);
    fprintf(stderr, "Analyzer created.\n");
} // end initAnalyzer


int widebandInitAnalyzer(int disp, int pixels)
{
    int result=0;

    XCreateAnalyzer(disp, &result, 262144, 1, 1, "");
    if (result != 0)
    {
        printf("XCreateAnalyzer channel=%d failed: %d\n", disp, result);
        return result;
    }
    //maximum number of frames of pixels to average
    //   int frame_rate = 15;
    int frame_rate = 10;
    //    double tau = 0.12;
    int data_type = 1;
    int n_pixout = 1;
    int overlap = 0;
    int fft_size = 16384;
    int blocksize = 16384;
    //    int sample_rate = 48000;
    int clip = 0;
    int stitches = 1;
    int span_clip_l = 0;
    int span_clip_h = 0;
    const double KEEP_TIME = 0.1;
    int max_w;
    int spur_eliminationtion_ffts = 1;
    static int h_flip[] = {0};
    int window_type = 4;
    double kaiser_pi = 14.0;
    int calibration_data_set = 0;
    double span_min_freq = 0.0;
    double span_max_freq = 0.0;

    max_w = fft_size + (int)MIN(KEEP_TIME * (double)frame_rate, KEEP_TIME * (double)fft_size * (double)frame_rate);

    //    fprintf(stderr, "Disp: %d  avb: %f  davg: %d  low: %d  high: %d  clip: %d  sclip_l: %d  sclip_h: %d  overlap: %d  maxw: %d\n", disp, avb, display_average, low, high, clip, span_clip_l, span_clip_h, overlap, max_w);
    SetAnalyzer(disp,
                n_pixout,
                spur_eliminationtion_ffts,
                data_type,
                h_flip,
                fft_size,
                blocksize,
                window_type,
                kaiser_pi,
                overlap,
                clip,
                span_clip_l,
                span_clip_h,
                pixels*2,
                stitches,
                calibration_data_set,
                span_min_freq,
                span_max_freq,
                max_w);

    SetDisplayDetectorMode(disp, 0, DETECTOR_MODE_AVERAGE/*display_detector_mode*/);
    SetDisplayAverageMode(disp, 0,  AVERAGE_MODE_LOG_RECURSIVE/*display_average_mode*/);
    fprintf(stderr, "Wideband analyzer created.\n");
    return 0;
} // end windbandInitAnalyzer


void wb_destroy_analyzer(int8_t stream)
{
  //  sem_wait(&wb_iq_semaphore);
    if (!rfstream[stream].enabled)
        DestroyAnalyzer(rfstream[stream].dsp_channel);
 //   sem_post(&wb_iq_semaphore);
} // end wb_destroy_analyzer


void shutdown_client_rfstreams(struct _client_entry *current_item)
{
    for (int i=0;i<MAX_RFSTREAMS;i++)
    {
        if (current_item->rfstream_enabled[i])
        {
            sem_wait(&iq_semaphore);
            if (rfstream[i].isTX)
                active_transmitters--;
            else
                active_receivers--;
            sem_post(&iq_semaphore);

            sem_wait(&bufferevent_semaphore);
            current_item->rfstream_enabled[i] = false;
            if (rfstream[i].enabled)
            {
                DestroyAnalyzer(rfstream[i].dsp_channel);
                CloseChannel(rfstream[i].dsp_channel);
                rfstream[i].enabled = false;
            }
            sem_post(&bufferevent_semaphore);

            if (active_receivers == 0 && active_transmitters == 0)
                setStopIQIssued(1);
            fprintf(stderr, "STOPALLCLIENT: rfstream %d closed.\n", i);
        }
    }
} // end shutdown_client_rfstreams


void shutdown_wideband_rfstreams(struct _client_entry *item)
{
    for (int i=0;i<MAX_RFSTREAMS;i++)
    {
        if (item->rfstream_enabled[i] && rfstream[i].spectrum.type == BS)
        {
            rfstream[i].enabled = false;
            DestroyAnalyzer(WIDEBAND_CHANNEL-rfstream[i].radio.radio_id);
            if (widebandBuffer[rfstream[i].radio.radio_id] != NULL)
            {
                free(widebandBuffer[rfstream[i].radio.radio_id]);
                widebandBuffer[rfstream[i].radio.radio_id] = NULL;
            }
        }
    }
} // end shutdown_wideband_rfstreams


void runXanbEXT(int8_t stream, double *data)
{
    xanbEXT(rfstream[stream].dsp_channel, data, data);
} // end runXanbEXT


void runXnobEXT(int8_t stream, double *data)
{
    xnobEXT(rfstream[stream].dsp_channel, data, data);
} // end runNobEXT


int runFexchange0(int8_t stream, double *data, double *dataout)
{
    int err=0;
    fexchange0(rfstream[stream].dsp_channel, data, dataout, &err);
    return err;
} // endFexchange0


void runSpectrum0(int8_t stream, double *data)
{
    Spectrum0(1, rfstream[stream].dsp_channel, 0, 0, data);
} // end runSpectrum0


int runGetPixels(int8_t stream, float *data)
{
    int flag=0;
    GetPixels(rfstream[stream].dsp_channel, 0, data, &flag);
    return flag;
} // end runGetPixels


void runRXAGetaSipF1(int8_t stream, float *data, int samples)
{
    RXAGetaSipF1(rfstream[stream].dsp_channel, data, samples);
} // end runRXAGetaSipF1


void process_tx_iq_data(int stream, double *mic_buf, double *tx_IQ)
{
    int error = 0;

    sem_wait(&iq_semaphore);
    if (active_transmitters == 0)
    {
        sem_post(&iq_semaphore);
        return;
    }
    fexchange0(rfstream[stream].dsp_channel, mic_buf, tx_IQ, &error);
    if (error != 0)
        fprintf(stderr, "TX Error Ch: %d:  Error: %d\n", stream, error);
    Spectrum0(1, rfstream[stream].dsp_channel, 0, 0, tx_IQ);
    sem_post(&iq_semaphore);
} // end process_tx_iq_data


char *dsp_command(struct _client_entry *current_item, unsigned char *message)
{
    char   answer[80];
    int8_t stream = message[0];
    int rc = 0;

    if (stream == -1 || stream >= MAX_RFSTREAMS)
        goto badcommand;

    if (message[1] == STARCOMMAND)
    {
        fprintf(stderr,"HARDWARE DIRECTED: ");
        fprintf(stderr, "Message for Rf stream: %d  [%u] [%u]\n", stream, (uint8_t)message[1], (uint8_t)message[2]);
        if (current_item->client_type[stream] == CONTROL)
        {
            // if privilged client, forward the message to the hardware
            if (message[2] == ATTACHRX || message[2] == ATTACHTX)
            {
                make_connection((short int)stream);
            }
            else
            {
                if (message[2] == DETACH)
                {
                    unsigned char command[64];
                    command[0] = DETACH;
                    //command[1] = (char)rfstream[stream].index;
                    command[1] = (char)rfstream[stream].radio.radio_id;
                    hwSendStarCommand(stream, command, 2);
                    fprintf(stderr, "** Rf stream %d detached. **\n", stream);
                }
                else
                    hwSendStarCommand(stream, message+2, 62);
            }
            /////                answer_question(message, role, bev);
        }
        else
        {
            // if non-privileged client don't forward the message
        }
        return "OK";
    }

    //****** Add client command permission checks here *********/

    if (message[1] == QUESTION)
    {
        //            answer_question((const char*)(message+1), role, bev);
        fprintf(stderr, "Question.....%02X\n", message[2]);

        switch (message[2])
        {
        case STARHARDWARE:
        {
            fprintf(stderr, "Hardware.....\n");

            char hdr[4];
            for (int i=0;i<connected_radios;i++)
            {
                hdr[2] = 77; //READ_MANIFEST
                hdr[0] = ((strlen(manifest_xml[i])+1) & 0xff00) >> 8;
                hdr[1] = (strlen(manifest_xml[i])+1) & 0xff;
                hdr[3] = i;
                bufferevent_write(current_item->bev, hdr, 4);
                bufferevent_write(current_item->bev, manifest_xml[i], strlen(manifest_xml[i])+1);
                fprintf(stderr, "Manifest for radio Id %d sent.\n", i);
            }
        }
            break;

        case QINFO:
        {  // FIX_ME: this mess needs to be changed to binary structure.
            answer[0] = 0;
            char hdr[4];
            hdr[2] = QINFO;
            strcat(answer, version);
            //                if (strcmp(clienttype, role) == 0)
            //              {
            //                  strcat(answer, "^s;0");
            //              }
            //                else
            //                {
            strcat(answer, "^s;1");
            //                }
            strcat(answer, ";f;");
            char f[50];
            sprintf(f,"%lld;m;", lastFreq);
            strcat(answer,f);
            char m[50];
            sprintf(m, "%d;z;", lastMode);
            strcat(answer,m);
            char z[50];
            sprintf(z, "%d;l;", zoom);
            strcat(answer,z);
            char l[50];
            sprintf(l, "%d;r;", low);       // Rx filter low
            strcat(answer,l);              // Rx filter high
            char h[50];
            sprintf(h, "%d;", high);
            strcat(answer, h);
            char c[50];
            sprintf(c, "%d;", stream);
            strcat(answer, c);

            hdr[0] = ((strlen(answer)+1) & 0xff00) >> 8;
            hdr[1] = (strlen(answer)+1) & 0xff;
            if (bufferevent_write(current_item->bev, hdr, 4) != 0)
            fprintf(stderr, "Wite failed!\n");
            bufferevent_write(current_item->bev, answer, strlen(answer)+1);
            fprintf(stderr, "QINFO sent.\n");
        }
            break;

        case GETPSINFO:
        {
            if (!rfstream[stream].isTX) break;
            int info[16];
            char hdr[4];

            GetPSInfo(rfstream[stream].dsp_channel, &info[0]);

            hdr[0] = (sizeof(info) & 0xff00) >> 8;
            hdr[1] = (sizeof(info) & 0xff);
            hdr[2] = stream;
            hdr[3] = GETPSINFO;
            bufferevent_write(current_item->bev, hdr, 4);
            bufferevent_write(current_item->bev, &info, sizeof(info));
        }
            break;

        case GETPSMAXTX:
        {
            if (!rfstream[stream].isTX) break;
            double max;
            char hdr[4];

            GetPSMaxTX(rfstream[stream].dsp_channel, &max);

            hdr[0] = (sizeof(double) & 0xff00) >> 8;
            hdr[1] = (sizeof(double) & 0xff);
            hdr[2] = stream;
            hdr[3] = GETPSMAXTX;
            bufferevent_write(current_item->bev, hdr, 4);
            bufferevent_write(current_item->bev, &max, sizeof(double));
        }
            break;

        case GETPSHWPEAK:
        {
            if (!rfstream[stream].isTX) break;
            double peak;
            char hdr[4];

            GetPSHWPeak(rfstream[stream].dsp_channel, &peak);

            hdr[0] = (sizeof(double) & 0xff00) >> 8;
            hdr[1] = (sizeof(double) & 0xff);
            hdr[2] = stream;
            hdr[3] = GETPSMAXTX;
            bufferevent_write(current_item->bev, hdr, 4);
            bufferevent_write(current_item->bev, &peak, sizeof(double));
        }
            break;

        default:
            break;
        }
    }
    else
    {
        fprintf(stderr, "Message for rf stream: %d -> [%u] [%u] [%u]\n", (uint8_t)message[0], (uint8_t)message[1], (uint8_t)message[2], (uint8_t)message[3]);
        switch ((uint8_t)message[1])
        {
        case SETMAIN:
        {
            if (current_item->client_type[stream] != CONTROL)
            {
                sdr_log(SDR_LOG_INFO, "Set to CONTROL allowed\n");
                sem_wait(&bufferevent_semaphore);
                //TAILQ_REMOVE(&Client_list, current_item, entries);
                //TAILQ_INSERT_HEAD(&Client_list, current_item, entries);
                current_item->client_type[stream] = CONTROL;
                sem_post(&bufferevent_semaphore);
            }
        }
            break;

        case STARTXCVR:
        {
            if (!rfstream[stream].isTX)
            {
                int8_t rx = rfstream[stream].dsp_channel;

                int buffer_size = 512;
                if (rfstream[stream].protocol == 1)
                    buffer_size = 1024;

                OpenChannel(rx, buffer_size, 2048, 48000, 48000, 48000, 0, 0, 0.010, 0.025, 0.000, 0.010, 0);
                printf("Rf stream: %d  --  DSP channel %d opened.\n", stream, rx);fflush(stdout);

                create_anbEXT(rx, 0, buffer_size, 48000, 0.0001, 0.0001, 0.0001, 0.05, 20);
                create_nobEXT(rx, 0, 0, buffer_size, 48000, 0.0001, 0.0001, 0.0001, 0.05, 20);
                create_divEXT(rx, 0, 2, buffer_size);

                RXASetNC(rx, 2048);
                RXASetMP(rx, 0);

                SetRXAPanelGain1(rx, 0.05f);
                SetRXAPanelSelect(rx, 3);
                SetRXAPanelPan(rx, 0.5f);
                SetRXAPanelCopy(rx, 0);
                SetRXAPanelBinaural(rx, 0);
                SetRXAPanelRun(rx, true);

                SetRXAEQRun(rx, false);

                SetRXABandpassRun(rx, true);

                SetRXAShiftFreq(rx, LO_offset);

                SetRXAAGCMode(rx, 3); //AGC_MEDIUM
                SetRXAAGCSlope(rx, 35.0f);
                SetRXAAGCTop(rx, 90.0f);
                SetRXAAGCAttack(rx, 2);
                SetRXAAGCHang(rx, 0);
                SetRXAAGCDecay(rx, 250);
                SetRXAAGCHangThreshold(rx, 100);

                SetEXTANBRun(rx, false);
                SetEXTNOBRun(rx, false);

                SetRXAEMNRPosition(rx, 0);
                SetRXAEMNRgainMethod(rx, 2);
                SetRXAEMNRnpeMethod(rx, 0);
                SetRXAEMNRRun(rx, false);
                SetRXAEMNRaeRun(rx, 1);

                SetRXAANRVals(rx, 64, 16, 16e-4, 10e-7); // defaults
                SetRXAANRRun(rx, false);
                SetRXAANFRun(rx, false);
                SetRXASNBARun(rx, false);

                RXANBPSetRun(rx, true);

                int rc = 0;
                XCreateAnalyzer(rx, &rc, 262144, 1, 1, "");
                if (rc < 0) printf("XCreateAnalyzer failed on channel %d.\n", rx);
                init_analyzer(rx, buffer_size);

                SetDisplayDetectorMode(rx, 0, DETECTOR_MODE_PEAK/*display_detector_mode*/);
                SetDisplayAverageMode(rx, 0, AVERAGE_MODE_NONE/*display_average_mode*/);
                calculate_display_average(rx);

                SetChannelState(rx, 1, 1);
                sem_wait(&iq_semaphore);
                rfstream[stream].enabled = true;
                current_item->rfstream_enabled[stream] = true;
                active_receivers++;
                sem_post(&iq_semaphore);
                spectrum_timer_init(stream);
                hw_startIQ(stream);
                audio_enabled[stream] = false;
            }
            else
            {
                int8_t tx = rfstream[stream].dsp_channel;
                if (tx < 0) break;

                int buffer_size = 512;
                if (rfstream[stream].protocol == 1)
                    buffer_size = 1024;

                OpenChannel(tx, buffer_size, 2048, 48000, 96000, 192000, 1, 0, 0.010, 0.025, 0.000, 0.010, 0);
                printf("TX channel %d opened.\n", tx);fflush(stdout);

                rc = 0;
                XCreateAnalyzer(tx, &rc, 262144, 1, 1, "");
                if (rc < 0) printf("XCreateAnalyzer failed on TX channel %d.\n", tx);

                initAnalyzer(tx, 128, 15, buffer_size);  // sample (128) will get changed to spectrum display width when client connects.

                TXASetNC(tx, 2048);
                TXASetMP(tx, 0);
                SetTXABandpassWindow(tx, 1);
                SetTXABandpassRun(tx, 1);

                SetTXAFMEmphPosition(tx, 1);

                SetTXACFIRRun(tx, 0);
                SetTXAEQRun(tx, 0);

                SetTXAAMSQRun(tx, 0);
                SetTXAosctrlRun(tx, 0);

                SetTXAALCAttack(tx, 1);
                SetTXAALCDecay(tx, 10);
                SetTXAALCSt(tx, 1); // turn it on (always on)

                SetTXALevelerAttack(tx, 1);
                SetTXALevelerDecay(tx, 500);
                SetTXALevelerTop(tx, 5.0);
                SetTXALevelerSt(tx, false);

                SetTXAPreGenMode(tx, 0);
                SetTXAPreGenToneMag(tx, 0.0);
                SetTXAPreGenToneFreq(tx, 0.0);
                SetTXAPreGenRun(tx, 0);

                SetTXAPostGenMode(tx, 0);
                SetTXAPostGenToneMag(tx, 0.2);
                SetTXAPostGenTTMag(tx, 0.2, 0.2);
                SetTXAPostGenToneFreq(tx, 0.0);
                SetTXAPostGenRun(tx, 0);

                SetTXAPanelGain1(tx, pow(10.0, 0.0 / 20.0));
                SetTXAPanelRun(tx, 1);

                SetTXAFMDeviation(tx, 2500.0);
                SetTXAAMCarrierLevel(tx, 0.5);

                SetTXACompressorGain(tx, 0.0);
                SetTXACompressorRun(tx, false);

                //  create_eerEXT(0, 0, 1024, 48000, 0.5, 200.0, true, 200/1.e6, 200/1e6, 1);
                //  SetEERRun(2, 1);

                SetTXABandpassFreqs(tx, 150, 2850);
                SetTXAMode(tx, TXA_USB);
                SetChannelState(tx, 0, 0);

                SetDisplayDetectorMode(tx, 0, DETECTOR_MODE_PEAK/*display_detector_mode*/);
                SetDisplayAverageMode(tx, 0, AVERAGE_MODE_NONE/*display_average_mode*/);

                rfstream[stream].spectrum.sample_rate = 48000;
                sem_wait(&iq_semaphore);
                rfstream[stream].enabled = true;
                current_item->rfstream_enabled[stream] = true;
                active_transmitters++;
                sem_post(&iq_semaphore);
                spectrum_timer_init(stream);
                hw_startIQ(stream);
            }
        }
            break;

        case STOPXCVR:
        {
            timer_delete(spectrum_timerid[stream]);
            usleep(5000);
            sem_wait(&iq_semaphore);
            if (rfstream[stream].isTX)
                active_transmitters--;
            else
                active_receivers--;
            sem_post(&iq_semaphore);

            sem_wait(&bufferevent_semaphore);
            current_item->rfstream_enabled[stream] = false;
            if (rfstream[stream].enabled)
            {
                DestroyAnalyzer(rfstream[stream].dsp_channel);
                CloseChannel(rfstream[stream].dsp_channel);
                rfstream[stream].enabled = false;
            }
            sem_post(&bufferevent_semaphore);

            if (active_receivers == 0 && active_transmitters == 0)
                setStopIQIssued(1);
            fprintf(stderr, "STOPXCVR: Rf stream %d closed.\n", stream);
        }
            break;

        case STARTIQ:
        {
    //        hw_startIQ(ch);
            fprintf(stderr, "************** IQ thread started for Rf stream: %d\n", stream);
        }
            break;

        case SETRXAANFPOSITION:
        {
            int pos = 0;
            pos = message[2];
            SetRXAANFPosition(rfstream[stream].dsp_channel, pos);
        }
            break;

        case SETRXAANRPOSITION:
        {
            int pos = 0;
            pos = message[2];
            SetRXAANRPosition(rfstream[stream].dsp_channel, pos);
        }
            break;

        case SETRXAEMNRPOSITION:
        {
            int pos = 0;
            pos = message[2];
            SetRXAEMNRPosition(rfstream[stream].dsp_channel, pos);
        }
            break;

        case SETTXAFMEMPHPOSITION:
        {
            int pos = 0;
            pos = message[2];
            SetTXAFMEmphPosition(rfstream[stream].dsp_channel, pos);
        }
            break;

        case SETTXAOSCTRLRUN:
        {
            int run = 0;
            run = message[2];
            SetTXACompressorRun(rfstream[stream].dsp_channel, run);
            SetTXAosctrlRun(rfstream[stream].dsp_channel, run);
        }
            break;

        case SETRXAEMNREARUN:
        {
            int run = 0;
            run = message[2];
            SetRXAEMNRaeRun(rfstream[stream].dsp_channel, run);
            fprintf(stderr, "SetRXAEMNRaeRun: %d\n", run);
        }
            break;

        case SETRXAEMNRGAINMETHOD:
        {   // Methods: 0 = Gaussian speech distribution, linear amplitude scale; 1 = Gaussian speech distribution, log amplitude scale; 2 = Gamma speech distribution
            int method = 0;
            method = message[2];
            SetRXAEMNRgainMethod(rfstream[stream].dsp_channel, method);
            fprintf(stderr, "SetRXAEMNRgainMethod: %d\n", method);
        }
            break;

        case SETRXAEMNRNPEMETHOD:
        {   // Methods: 0 = Optimal Smoothing Minimum Statistics (OSMS); 1 = Minimum Mean‐Square Error (MMSE)
            int method = 0;
            method = message[2];
            SetRXAEMNRnpeMethod(rfstream[stream].dsp_channel, method);
            fprintf(stderr, "SetRXAEMNRnpeMethod: %d\n", method);
        }
            break;

        case SETRXAAGCMODE:
        {
            int agc = 0;
            agc = message[2];
            SetRXAAGCMode(rfstream[stream].dsp_channel, agc);
        }
            break;

        case SETRXAGCFIXED:
        {
            double agc = 0.0f;
            agc = atof((const char*)(message+2));
            SetRXAAGCFixed(rfstream[stream].dsp_channel, agc);
        }
            break;

        case SETRXAGCATTACK:
        {
            int attack = atoi((const char*)(message+2));
            SetRXAAGCAttack(rfstream[stream].dsp_channel, attack);
        }
            break;

        case SETRXAGCDECAY:
        {
            int decay = atoi((const char*)(message+2));
            SetRXAAGCDecay(rfstream[stream].dsp_channel, decay);
        }
            break;

        case SETRXAGCSLOPE:
        {
            int slope = atoi((const char*)(message+2));
            SetRXAAGCSlope(rfstream[stream].dsp_channel, slope);
        }
            break;

        case SETRXAGCHANG:
        {
            int hang = atoi((const char*)(message+2));
            SetRXAAGCHang(rfstream[stream].dsp_channel, hang);
        }
            break;

        case SETTXLEVELERST:
            SetTXALevelerSt(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            break;

        case SETRXAGCHANGLEVEL:
        {
            double level = atof((const char*)(message+2));
            SetRXAAGCHangLevel(rfstream[stream].dsp_channel, level);
        }
            break;

        case SETTXLEVELERATTACK:
        {
            int attack = atoi((const char*)(message+2));
            SetTXALevelerAttack(rfstream[stream].dsp_channel, attack);
        }
            break;

        case SETTXLEVELERDECAY:
        {
            int decay = atoi((const char*)(message+2));
            SetTXALevelerDecay(rfstream[stream].dsp_channel, decay);
        }
            break;

        case SETRXAGCHANGTHRESH:
        {
            int thresh = atoi((const char*)(message+2));
            SetRXAAGCHangThreshold(rfstream[stream].dsp_channel, thresh);
        }
            break;

        case SETRXAGCTHRESH:
        {
            double thresh = 0.0f;
            double size = 0.0f;
            double rate = 0.0f;
            sscanf((const char*)(message+2), "%lf %lf %lf", &thresh, &size, &rate);
            SetRXAAGCThresh(rfstream[stream].dsp_channel, thresh, size, rate);
        }
            break;

        case SETRXAGCTOP:
        {
            double max_agc = atof((const char*)(message+2));
            SetRXAAGCTop(rfstream[stream].dsp_channel, max_agc);
        }
            break;

        case ENABLERXEQ:
            SetRXAEQRun(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETRXEQPRO:
        {
            double freq[11] = {0.0, 32.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
            double gain[11];

            sscanf((const char*)(message+3), "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   &gain[0], &gain[1], &gain[2], &gain[3], &gain[4], &gain[5], &gain[6], &gain[7], &gain[8], &gain[9], &gain[10]);
            SetRXAEQProfile(rfstream[stream].dsp_channel, message[1], freq, gain);
        }
            break;

        case SETRXAEQWINTYPE:
            SetRXAEQWintype(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETTXEQPRO:
        {
            if (rfstream[stream].dsp_channel < 0) break;

            double freq[11] = {0.0, 32.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
            double gain[11];

            sscanf((const char*)(message+3), "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   &gain[0], &gain[1], &gain[2], &gain[3], &gain[4], &gain[5], &gain[6], &gain[7], &gain[8], &gain[9], &gain[10]);
            SetTXAEQProfile(rfstream[stream].dsp_channel, message[1], freq, gain);
        }
            break;

        case SETTXAEQWINTYPE:
            SetTXAEQWintype(rfstream[stream].dsp_channel, message[2]);
            break;

        case ENABLETXEQ:
            SetTXAEQRun(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETTXALCATTACK:
        {
            int attack = 0;
            sscanf((const char*)(message+2), "%d", &attack);
            SetTXAALCAttack(rfstream[stream].dsp_channel, attack);
        }
            break;

        case SETTXALCDECAY:
        {
            int decay = 0;
            sscanf((const char*)(message+2), "%d", &decay);
            SetTXAALCDecay(rfstream[stream].dsp_channel, decay);
        }
            break;

        case SETTXALCMAXGAIN:
        {
            double maxgain = 0.0f;
            sscanf((const char*)(message+2), "%lf", &maxgain);
            SetTXAALCMaxGain(rfstream[stream].dsp_channel, maxgain);
        }
            break;

        case SETTXAPREGENRUN:
        {
            int run = message[2];
            SetTXAPreGenRun(rfstream[stream].dsp_channel, run);
            fprintf(stderr, "SetTXAPreGenRun: %d\n", run);
        }
            break;

        case SETTXAPREGENMODE:
        {  // Modes:  Tone = 0; Noise = 2; Sweep = 3; Sawtooth = 4; Triangle = 5; Pulse = 6; Silence = 99
            int mode = message[2];
            SetTXAPreGenMode(rfstream[stream].dsp_channel, mode);
            fprintf(stderr, "SetTXAPreGenMode: %d\n", mode);
        }
            break;

        case SETTXAPREGENTONEMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPreGenToneMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPreGenToneMag: %lf\n", mag);
        }
            break;

        case SETTXAPREGENTONEFREQ:
        {
            double freq = 0.0f;
            sscanf((const char*)(message+2), "%lf", &freq);
            SetTXAPreGenToneFreq(rfstream[stream].dsp_channel, freq);
            fprintf(stderr, "SetTXAPreGenTonefreq: %lf\n", freq);
        }
            break;

        case SETTXAPREGENNOISEMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPreGenNoiseMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPreGenNoiseMag: %lf\n", mag);
        }
            break;

        case SETTXAPREGENSWEEPMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPreGenSweepMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPreGenSweepMag: %lf\n", mag);
        }
            break;

        case SETTXAPREGENSWEEPFREQ:
        {
            double freq1 = 0.0f;
            double freq2 = 0.0f;
            sscanf((const char*)(message+2), "%lf %lf", &freq1, &freq2);
            SetTXAPreGenSweepFreq(rfstream[stream].dsp_channel, freq1, freq2);
            fprintf(stderr, "SetTXAPreGenTonefreq: Freq 1: %lf   Freq 2: %lf\n", freq1, freq2);
        }
            break;

        case SETTXAPREGENSWEEPRATE:
        {
            double rate = 0.0f;
            sscanf((const char*)(message+2), "%lf", &rate);
            SetTXAPreGenSweepRate(rfstream[stream].dsp_channel, rate);
            fprintf(stderr, "SetTXAPreGenSweepRate: %lf\n", rate);
        }
            break;

        case SETTXAPREGENSAWTOOTHMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPreGenSawtoothMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPreGenSawtoothMag: %lf\n", mag);
        }
            break;

        case SETTXAPREGENSAWTOOTHFREQ:
        {
            double freq = 0.0f;
            sscanf((const char*)(message+2), "%lf", &freq);
            SetTXAPreGenSawtoothFreq(rfstream[stream].dsp_channel, freq);
            fprintf(stderr, "SetTXAPreGenSawtoothfreq: %lf\n", freq);
        }
            break;

        case SETTXAPREGENTRIANGLEMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPreGenTriangleMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPreGenTriangleMag: %lf\n", mag);
        }
            break;

        case SETTXAPREGENTRIANGLEFREQ:
        {
            double freq = 0.0f;
            sscanf((const char*)(message+2), "%lf", &freq);
            SetTXAPreGenTriangleFreq(rfstream[stream].dsp_channel, freq);
            fprintf(stderr, "SetTXAPreGenTrianglefreq: %lf\n", freq);
        }
            break;

        case SETTXAPREGENPULSEMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPreGenPulseMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPreGenPulseMag: %lf\n", mag);
        }
            break;

        case SETTXAPREGENPULSEFREQ:
        {
            double freq = 0.0f;
            sscanf((const char*)(message+2), "%lf", &freq);
            SetTXAPreGenPulseFreq(rfstream[stream].dsp_channel, freq);
            fprintf(stderr, "SetTXAPreGenPulsefreq: %lf\n", freq);
        }
            break;

        case SETTXAPREGENPULSEDUTYCYCLE:
        {
            double dc = 0.0f;
            sscanf((const char*)(message+2), "%lf", &dc);
            if (dc < 0.0f || dc > 1.0f) break;
            SetTXAPreGenPulseDutyCycle(rfstream[stream].dsp_channel, dc);
            fprintf(stderr, "SetTXAPreGenPulseDutyCycle: %lf\n", dc);
        }
            break;

        case SETTXAPREGENPULSETONEFREQ:
        {
            double freq = 0.0f;
            sscanf((const char*)(message+2), "%lf", &freq);
            SetTXAPreGenPulseToneFreq(rfstream[stream].dsp_channel, freq);
            fprintf(stderr, "SetTXAPreGenPulseTonefreq: %lf\n", freq);
        }
            break;

        case SETTXAPREGENPULSETRANSITION:
        {
            double transtime = 0.0f;
            sscanf((const char*)(message+2), "%lf", &transtime);
            SetTXAPreGenPulseTransition(rfstream[stream].dsp_channel, transtime);
            fprintf(stderr, "SetTXAPreGenPulseTransition: %lf\n", transtime);
        }
            break;

        case SETTXAPOSTGENRUN:
        {
            int run = message[2];
            SetTXAPostGenRun(rfstream[stream].dsp_channel, run);
            fprintf(stderr, "SetTXAPostGenRun: %d\n", run);
        }
            break;

        case SETTXAPOSTGENMODE:
        {  // Modes:  Tone = 0; Two‐Tone = 1; Sweep = 3; Silence = 99
            int mode = message[2];
            SetTXAPostGenMode(rfstream[stream].dsp_channel, mode);
            fprintf(stderr, "SetTXAPostGenMode: %d\n", mode);
        }
            break;

        case SETTXAPOSTGENTONEMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPostGenToneMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPostGenToneMag: %lf\n", mag);
        }
            break;

        case SETTXAPOSTGENTONEFREQ:
        {
            double freq = 0.0f;
            sscanf((const char*)(message+2), "%lf", &freq);
            SetTXAPostGenToneFreq(rfstream[stream].dsp_channel, freq);
            fprintf(stderr, "SetTXAPostGenTonefreq: %lf\n", freq);
        }
            break;

        case SETTXAPOSTGENTTMAG:
        {
            double mag1 = 0.0f;
            double mag2 = 0.0f;
            sscanf((const char*)(message+2), "%lf %lf", &mag1, &mag2);
            if (mag1 < 0.0f || mag1 > 1.0f || mag2 < 0.0f || mag2 > 1.0f) break;
            SetTXAPostGenTTMag(rfstream[stream].dsp_channel, mag1, mag2);
            fprintf(stderr, "SetTXAPostGenTTMag: Mag 1: %lf   Mag 2: %lf\n", mag1, mag2);
        }
            break;

        case SETTXAPOSTGENTTFREQ:
        {
            double freq1 = 0.0f;
            double freq2 = 0.0f;
            sscanf((const char*)(message+2), "%lf %lf", &freq1, &freq2);
            SetTXAPostGenTTFreq(rfstream[stream].dsp_channel, freq1, freq2);
            fprintf(stderr, "SetTXAPostGenTTfreq: Freq 1: %lf   Freq 2: %lf\n", freq1, freq2);
        }
            break;

        case SETTXAPOSTGENSWEEPMAG:
        {
            double mag = 0.0f;
            sscanf((const char*)(message+2), "%lf", &mag);
            if (mag < 0.0f || mag > 1.0f) break;
            SetTXAPostGenSweepMag(rfstream[stream].dsp_channel, mag);
            fprintf(stderr, "SetTXAPostGenSweepMag: %lf\n", mag);
        }
            break;

        case SETTXAPOSTGENSWEEPFREQ:
        {
            double freq1 = 0.0f;
            double freq2 = 0.0f;
            sscanf((const char*)(message+2), "%lf %lf", &freq1, &freq2);
            SetTXAPostGenSweepFreq(rfstream[stream].dsp_channel, freq1, freq2);
            fprintf(stderr, "SetTXAPostGenTonefreq: Freq 1: %lf   Freq 2: %lf\n", freq1, freq2);
        }
            break;

        case SETTXAPOSTGENSWEEPRATE:
        {
            double rate = 0.0f;
            sscanf((const char*)(message+2), "%lf", &rate);
            SetTXAPostGenSweepRate(rfstream[stream].dsp_channel, rate);
            fprintf(stderr, "SetTXAPostGenSweepRate: %lf\n", rate);
        }
            break;

        case SETFPS:
        {
            int samp, fps;

            sscanf((const char*)(message+2), "%d,%d", &samp, &fps);
            sem_wait(&bufferevent_semaphore);
            if (current_item->client_type != CONTROL)
            {
                rfstream[stream].spectrum.nsamples = samp;
                rfstream[stream].spectrum.fps = fps;
            }
            else
            {
                rfstream[stream].spectrum.nsamples = samp;
                rfstream[stream].spectrum.fps = fps;
            }
            int buffer_size = 512;
            if (rfstream[stream].protocol == 1)
                buffer_size = 1024;

            if (!rfstream[stream].isTX)
                initAnalyzer(rfstream[stream].dsp_channel, samp, fps, buffer_size);
            else
                initAnalyzer(rfstream[stream].dsp_channel, samp, fps, buffer_size);
            sem_post(&bufferevent_semaphore);
            sdr_log(SDR_LOG_INFO, "Spectrum fps set to = '%d'  Samples = '%d'\n", fps, samp);
        }
            break;

        case SETFREQ:
            hwSetFrequency(stream, atoll((const char*)(message+2)));
            fprintf(stderr, "Set frequency: %lld\n", atoll((const char*)(message+2)));
            break;

        case SETMODE:
        {
            int mode;
            mode = message[2];
            lastMode = mode;
            fprintf(stderr, "************** Mode change: %d *******************\n", mode);
            switch (mode)
            {
            case USB:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_USB);
                    RXASetPassband(rfstream[stream].dsp_channel, 150, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_USB);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, 150, 2850);
                }
                sdr_log(SDR_LOG_INFO, "Mode set to USB\n");
                break;
            case LSB:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_LSB);
                    RXASetPassband(rfstream[stream].dsp_channel, -2850, -150);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_LSB);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -2850, -150);
                }
                sdr_log(SDR_LOG_INFO, "Mode set to LSB\n");
                break;
            case AM:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_AM);
                    RXASetPassband(rfstream[stream].dsp_channel, -2850, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_AM);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -2850, 2850);
                }
                break;
            case SAM:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_SAM);
                    RXASetPassband(rfstream[stream].dsp_channel, -2850, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_SAM);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -2850, 2850);
                }
                break;
            case FM:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_FM);
                    RXASetPassband(rfstream[stream].dsp_channel, -4800, 4800);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_FM);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -4800, 4800);
                }
                break;
            case DSB:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_DSB);
                    RXASetPassband(rfstream[stream].dsp_channel, 150, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_DSB);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, 150, 2850);
                }
                break;
            case CWU:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_CWU);
                    RXASetPassband(rfstream[stream].dsp_channel, 150, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_CWU);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, 150, 2850);
                }
                break;
            case CWL:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_CWL);
                    RXASetPassband(rfstream[stream].dsp_channel, -2850, -150);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_CWL);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -2850, -150);
                }
                break;
            case DIGU:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_DIGU);
                    RXASetPassband(rfstream[stream].dsp_channel, 150, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_DIGU);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, 150, 2850);
                }
                break;
            case DIGL:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_DIGL);
                    RXASetPassband(rfstream[stream].dsp_channel, -2850, -150);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_DIGL);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -2850, -150);
                }
                break;
            case SPEC:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_SPEC);
                    RXASetPassband(rfstream[stream].dsp_channel, 150, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_SPEC);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, 150, 2850);
                }
                break;
            case DRM:
                if (!rfstream[stream].isTX)
                {
                    SetRXAMode(rfstream[stream].dsp_channel, RXA_DRM);
                    RXASetPassband(rfstream[stream].dsp_channel, 150, 2850);
                }
                else
                {
                    SetTXAMode(rfstream[stream].dsp_channel, TXA_DRM);
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, 150, 2850);
                }
                break;
            default:
                if (!rfstream[stream].isTX)
                    RXASetPassband(rfstream[stream].dsp_channel, -4800, 4800);
                else
                    SetTXABandpassFreqs(rfstream[stream].dsp_channel, -4800, 4800);
            }
        }
            break;

        case SETFILTER:
        {
            int low, high;
            sscanf((const char*)(message+2), "%d,%d", &low, &high);
            if (rfstream[stream].isTX)
            {
                printf("TX: %d  Low: %d   High: %d\n", rfstream[stream].dsp_channel, low, high);
                SetTXABandpassFreqs(rfstream[stream].dsp_channel, (double)low, (double)high);
            }
            else
            {
                printf("RX: %d  Low: %d   High: %d\n", rfstream[stream].dsp_channel, low, high);
                RXASetPassband(rfstream[stream].dsp_channel, (double)low, (double)high);
            }
        }
            break;

        case SETENCODING:
        {
            int enc;
            enc = message[1]; // FIXME: May need to set per client
            /* This used to force to 0 on error, now it leaves unchanged */
            if (enc >= 0 && enc <= 2)
            {
                sem_wait(&audiostream_sem);
                audiostream_conf.encoding = enc;
                audiostream_conf.age++;
                sem_post(&audiostream_sem);
            }
            sdr_log(SDR_LOG_INFO, "encoding changed to %d\n", enc);
        }
            break;

        case ENABLEAUDIO:
        {
            for (int i=0;i<MAX_RFSTREAMS;i++)
                audio_enabled[i] = false;
            bool enabled = (bool)message[2];
            audio_enabled[stream] = enabled;
        }
            break;

        case STARTAUDIO:
        {
            int ntok, bufsize, rate, channels, micEncoding;
            if (current_item->client_type != CONTROL)
            {
                fprintf(stderr, "STARTAUDIO: Not CONTROL type client...aborting command.\n");
                break;
            }

            audio_stream_init(0);
            /* FIXME: this is super racy */

            bufsize = AUDIO_BUFFER_SIZE;
            rate = 8000;
            channels = 1;
            micEncoding = 0;
            ntok = sscanf((const char*)(message+1), "%d,%d,%d,%d,%d", &bufsize, &rate, &channels, &micEncoding, &radioMic);

            if (ntok >= 1)
            {
                /* FIXME: validate */
                /* Do not vary buffer size according to buffer size setting from client
                   as it causes problems when the buffer size set by primary is smaller
                   then slaves */
                if (bufsize < AUDIO_BUFFER_SIZE)
                    bufsize = AUDIO_BUFFER_SIZE;
                else
                    if (bufsize > 32000)
                        bufsize = 32000;
            }
            if (ntok >= 2)
            {
                if (rate != 8000 && rate != 48000)
                {
                    sdr_log(SDR_LOG_INFO, "Invalid audio sample rate: %d\n", rate);
                    rate = 8000;
                }
            }
            if (ntok >= 3)
            {
                if (channels != 1 && channels != 2)
                {
                    sdr_log(SDR_LOG_INFO, "Invalid audio channels: %d\n", channels);
                    channels = 1;
                }
            }
            if (ntok >= 4)
            {
                if (micEncoding != MIC_ENCODING_ALAW)
                {
                    sdr_log(SDR_LOG_INFO, "Invalid mic encoding: %d\n", micEncoding);
                    micEncoding = MIC_ENCODING_ALAW;
                }
            }
            if (ntok >= 5)
            {
                if (radioMic)
                    fprintf(stderr, "Using radio microphone.\n");
            }
            sem_wait(&audiostream_sem);
            audiostream_conf.bufsize = bufsize;
            audiostream_conf.samplerate = rate;
            hw_set_src_ratio();
            audiostream_conf.channels = channels;
            audiostream_conf.micEncoding = micEncoding;
            audiostream_conf.age++;
            sem_post(&audiostream_sem);

            sdr_log(SDR_LOG_INFO, "starting audio stream at rate %d channels %d bufsize %d encoding %d micEncoding %d\n",
                    rate, channels, bufsize, encoding, micEncoding);

            //audio_stream_reset();
            sem_wait(&audio_bufevent_semaphore);
            send_audio = 1;
            sem_post(&audio_bufevent_semaphore);
        }
            break;

        case STOPAUDIO:
            break;

        case SETPAN:
            SetRXAPanelPan(rfstream[stream].dsp_channel, atof((const char*)(message+2)));
            break;

        case SETPANADAPTERMODE:
            panadapterMode = message[2];
            break;

        case SETANFVALS:
        {
            int taps, delay;
            double gain, leakage;

            fprintf(stderr, "%s\n", (const char*)(message+2));
            if (sscanf((const char*)(message+2), "%d,%d,%lf,%lf", &taps, &delay, &gain, &leakage) != 4)
                goto badcommand;

            SetRXAANFVals(rfstream[stream].dsp_channel, taps, delay, gain, leakage);
            fprintf(stderr, "Set RX ANF values: Taps: %d  Delay: %d  Gain: %lf  Leakage: %lf\n", taps, delay, gain, leakage);
        }
            break;

        case SETANF:
            SetRXAANFRun(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETNRVALS:
        {
            int taps, delay;
            double gain, leakage;

            if (sscanf((const char*)(message+2), "%d,%d,%f,%f", &taps, &delay, (float*)&gain, (float*)&leakage) != 4)
                goto badcommand;

            SetRXAANRVals(rfstream[stream].dsp_channel, taps, delay, gain, leakage);
        }
            break;

        case SETTXACFIRRUN:
            SetTXACFIRRun(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETNR:
            SetRXAANRRun(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETNB:
            SetEXTANBRun(rfstream[stream].dsp_channel, message[2]);
            bUseNB = message[1];
            break;

        case SETNB2:
            SetEXTNOBRun(rfstream[stream].dsp_channel, message[2]);
            bUseNB2 = message[1];
            break;

        case SETNBVAL:
        {
            double thresh;
            sscanf((const char*)(message+2), "%lf", &thresh);
            SetEXTNOBThreshold(rfstream[stream].dsp_channel, thresh);
        }
            break;

        case SETEXTNOBMODE:
            SetEXTNOBMode(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETSQUELCHVAL:
            SetRXAAMSQThreshold(rfstream[stream].dsp_channel, atof((const char*)(message+2)));
            fprintf(stderr, "Squelch thresh: %lf dBm\n", atof((const char*)(message+2)));
            break;

        case SETSQUELCHSTATE:
            SetRXAAMSQRun(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETWINDOW:
            if (!rfstream[stream].isTX)
                SetRXABandpassWindow(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            else
                SetTXABandpassWindow(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            break;

        case SETCLIENT:
            sdr_log(SDR_LOG_INFO, "Client is %s\n", (const char*)(message+2));
            break;

        case SETRXOUTGAIN:
            SetRXAPanelGain1(rfstream[stream].dsp_channel, (double)atof((const char*)(message+2))/100.0);
            break;

        case SETTXAPANELSELECT:
            SetTXAPanelSelect(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            break;

        case SETMICGAIN:
        {
            double gain;

            if (rfstream[stream].dsp_channel == -1) break;

            if (sscanf((const char*)(message+2), "%lf", (double*)&gain) > 1)
                goto badcommand;

            SetTXAPanelGain1(rfstream[stream].dsp_channel, pow(10.0, gain / 20.0));
            fprintf(stderr, "Mic gain: %lf\n", pow(10.0, gain / 20.0));
        }
            break;

        case SETSAMPLERATE:
        {

            fprintf(stderr, "Set sample rate for rf stream: %d  rx dsp: %d\n", stream, rfstream[stream].dsp_channel);
            if (sscanf((const char*)(message+2), "%ld", (long*)&sample_rate) > 1)
                goto badcommand;

            rfstream[stream].spectrum.sample_rate = sample_rate;
            SetChannelState(rfstream[stream].dsp_channel, 0, 1);
            hwSetSampleRate(stream, sample_rate);
            SetDSPSamplerate(rfstream[stream].dsp_channel, sample_rate);
  //          int buffer_size = 512;
  //          if (rfstream[stream].protocol == 1)
 //               buffer_size = 1024;
 //           init_analyzer(ch, buffer_size);
 //           SetInputSamplerate(ch, sample_rate);
            SetEXTANBSamplerate(rfstream[stream].dsp_channel, sample_rate);
            SetEXTNOBSamplerate(rfstream[stream].dsp_channel, sample_rate);
            setSpeed(stream, sample_rate);
            SetChannelState(rfstream[stream].dsp_channel, 1, 0);
        }
            break;

        case SETTXAMCARLEV:
        {
            double level;
            char user[20];
            char pass[20];

            if (rfstream[stream].dsp_channel == -1) break;

            if (sscanf((const char*)(message+2), "%lf %s %s", (double*)&level, user, pass) > 3)
                goto badcommand;

            SetTXAAMCarrierLevel(rfstream[stream].dsp_channel, level * 10.0);
            fprintf(stderr, "AM carrier level: %lf\n", level * 10.0);
        }
            break;

        case SETRXBPASSWIN:
            SetRXABandpassWindow(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            RXANBPSetWindow(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            break;

        case SETTXBPASSWIN:
            if (rfstream[stream].dsp_channel > -1)
                SetTXABandpassWindow(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            break;

        case SETRXAMETER:
            rxMeterMode = message[2];
            break;

        case SETTXAMETER:
            txMeterMode = message[2];
            break;

        case SETPSINTSANDSPI:
        {
            int ints = 0;
            int spi = 0;
            sscanf((const char*)(message+2), "%d %d", &ints, &spi);
            SetPSIntsAndSpi(rfstream[stream].dsp_channel, ints, spi);
        }
            break;

        case SETPSSTABILIZE:
            SetPSStabilize(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETPSMAPMODE:
            SetPSMapMode(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETPSHWPEAK:
            SetPSHWPeak(rfstream[stream].dsp_channel, atof((const char*)(message+2)));
            break;

        case SETPSCONTROL:
        {
            int mode = message[2];
            if (mode == 0) //reset
                SetPSControl(rfstream[stream].dsp_channel, 1, 0, 0, 0);
            if (mode == 1) //mancal
                SetPSControl(rfstream[stream].dsp_channel, 0, 1, 0, 0);
            if (mode == 2) //automode
                SetPSControl(rfstream[stream].dsp_channel, 0, 0, 1, 0);
            if (mode == 3) //turnon
                SetPSControl(rfstream[stream].dsp_channel, 0, 0, 0, 1);
        }
            break;

        case SETPSMOXDELAY:
            SetPSMoxDelay(rfstream[stream].dsp_channel, atof((const char*)(message+2)));
            break;

        case SETPSTXDELAY:
            SetPSTXDelay(rfstream[stream].dsp_channel, atof((const char*)(message+2)));
            break;

        case SETPSLOOPDELAY:
            SetPSLoopDelay(rfstream[stream].dsp_channel, atof((const char*)(message+2)));
            break;

        case SETPSMOX:
            SetPSMox(rfstream[stream].dsp_channel, message[2]);
            break;

        case SETPSFEEDBACKRATE:
            SetPSFeedbackRate(rfstream[stream].dsp_channel, atoi((const char*)(message+2)));
            break;

        case ENABLENOTCHFILTER:
            RXANBPSetNotchesRun(rfstream[stream].dsp_channel, message[2]);
            sdr_log(SDR_LOG_INFO, "Notch filter set to: %d\n", message[2]);
            break;

        case SETNOTCHFILTER:
        {
            double fcenter, fwidth;
            int ret = 0;
        //    printf("%s\n", message+2);
            sscanf((const char*)(message+3), "%lf %lf", &fcenter, &fwidth);
            ret = RXANBPAddNotch(rfstream[stream].dsp_channel, message[2]-1, fcenter, fwidth, true);
            sdr_log(SDR_LOG_INFO, "Notch filter added: Id: %d  F: %lf   W: %lf  Ret: %d\n", message[2]-1, fcenter, fwidth, ret);
        }
            break;

        case SETNOTCHFILTERTUNE:
        {
            double fcenter;
        //    printf("%s\n", message+2);
            sscanf((const char*)(message+2), "%lf", &fcenter);
            RXANBPSetTuneFrequency(rfstream[stream].dsp_channel, fcenter * 100000.0f);
            sdr_log(SDR_LOG_INFO, "Notch filter set tune frequency:  Freq: %lf\n", fcenter);
        }
            break;

        case SETNOTCHFILTERSHIFT:
        {
            double fshift;
        //    printf("%s\n", message+2);
            sscanf((const char*)(message+2), "%lf", &fshift);
            RXANBPSetShiftFrequency(rfstream[stream].dsp_channel, fshift);
            sdr_log(SDR_LOG_INFO, "Notch filter set shift frequency:  Shift Freq: %lf\n", fshift);
        }
            break;

        case EDITNOTCHFILTER:
        {
            double fcenter, fwidth;
        //    printf("%s\n", message+2);
            sscanf((const char*)(message+3), "%lf %lf", &fcenter, &fwidth);
            RXANBPEditNotch(rfstream[stream].dsp_channel, message[2]-1, fcenter, fwidth, true);
            sdr_log(SDR_LOG_INFO, "Notch filter updated: Id: %d  F: %lf   W: %lf\n", message[2]-1, fcenter, fwidth);
        }
            break;

        case DELNOTCHFILTER:
        {
            RXANBPDeleteNotch(rfstream[stream].dsp_channel, message[2]);
            sdr_log(SDR_LOG_INFO, "Notch filter id: %d deleted.\n", message[1]);
        }
            break;

        case MOX:
        {
            int8_t radio_id = rfstream[stream].radio.radio_id;
            char user[20];
            char pass[20];
            bool mox = false;

            if (!rfstream[stream].isTX) break;

            if (sscanf((const char*)(message+2), "%d %s %s", (int*)&mox, user, pass) > 3)
                goto badcommand;

            rfstream[stream].radio.mox = mox;

            if (mox)  // FIXME: Need to make some changes for full duplex operation.
            {
                for (int i=0;i<MAX_RFSTREAMS;i++)
                {
                    if (rfstream[i].radio.radio_id == radio_id && rfstream[i].enabled && !rfstream[i].isTX)
                        SetChannelState(rfstream[i].dsp_channel, 0, 1);
                }
                SetChannelState(rfstream[stream].dsp_channel, 1, 0);
                hwSetMox(stream, mox);
            }
            else
            {
                hwSetMox(stream, mox);
                SetChannelState(rfstream[stream].dsp_channel, 0, 1);
                for (int i=0;i<MAX_RFSTREAMS;i++)
                {
                    if (rfstream[i].radio.radio_id == radio_id && rfstream[i].enabled && !rfstream[i].isTX)
                        SetChannelState(rfstream[i].dsp_channel, 1, 0);
                }
            }
            sdr_log(SDR_LOG_INFO, "Mox set to: %d for tx channel %d\n", mox, rfstream[stream].dsp_channel);
        }
            break;

        default:
            fprintf(stderr, "DSP_COM: Unknown command. 0x%02X\n", message[1]);
            break;
        }
    }
    return "OK";
badcommand:
    sdr_log(SDR_LOG_INFO, "DSP_COM: invalid command: %d for Rf stream: %d\n", message[1], stream);
    return "ERROR";
} // end dsp_command

