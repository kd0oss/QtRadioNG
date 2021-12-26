/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
*
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

/* 2021 - Altered for QtRadio NG by Rick Schnicker, KD0OSS */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "alex.h"
//#include "band.h"
//#include "bandstack.h"
//#include "channel.h"
#include "receiver.h"
//#include "meter.h"
//#include "filter.h"
//#include "mode.h"
#include "radio.h"
//#include "vfo.h"
//#include "vox.h"
//#include "meter.h"
#include "transmitter.h"
#include "new_protocol.h"
#include "old_protocol.h"


#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)

int tx_filter_low = 150;
int tx_filter_high = 2850;
static double *cw_shape_buffer48 = NULL;
static double *cw_shape_buffer192 = NULL;
static int cw_shape = 0;


TRANSMITTER *create_transmitter(int id, int buffer_size)
{
    int rc;

    TRANSMITTER *tx = (TRANSMITTER*)malloc(sizeof(TRANSMITTER));
    tx->id = id;
    tx->dac = 0;
    tx->buffer_size = buffer_size;

    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        tx->mic_sample_rate = 48000;
        tx->mic_dsp_rate = 48000;
        tx->iq_output_rate = 48000;
        break;
    case NEW_PROTOCOL:
        tx->mic_sample_rate = 48000;
        tx->mic_dsp_rate = 96000;
        tx->iq_output_rate = 192000;
        break;

    }
    int ratio = tx->iq_output_rate / tx->mic_sample_rate;
    tx->output_samples = tx->buffer_size * ratio;

    tx->displaying = 0;

    tx->alex_antenna = ALEX_TX_ANTENNA_1;

    fprintf(stderr,"create_transmitter: id=%d buffer_size=%d mic_sample_rate=%d mic_dsp_rate=%d iq_output_rate=%d output_samples=%d\n",tx->id, tx->buffer_size, tx->mic_sample_rate, tx->mic_dsp_rate, tx->iq_output_rate, tx->output_samples);

    tx->filter_low = tx_filter_low;
    tx->filter_high = tx_filter_high;
    tx->use_rx_filter = false;

    tx->out_of_band = 0;

    tx->low_latency = 0;

    tx->twotone = 0;
    tx->puresignal = 0;
    tx->feedback = 0;
    tx->auto_on = 0;
    tx->single_on = 0;

    tx->attenuation = 0;
    tx->ctcss = 11;
    tx->ctcss_enabled = false;

    tx->deviation = 2500;
    tx->am_carrier_level = 0.5;

    tx->drive = 50;
    tx->tune_percent = 10;
    tx->tune_use_drive = 0;

    tx->compressor = 0;
    tx->compressor_level = 0.0;

    tx->local_microphone = 0;
    tx->microphone_name = NULL;

    tx->xit_enabled = false;
    tx->xit = 0LL;

    // allocate buffers
    fprintf(stderr, "transmitter: allocate buffers: mic_input_buffer=%d iq_output_buffer=%d\n", tx->buffer_size, tx->output_samples);

    tx->mic_input_buffer = (double*)malloc((2*tx->buffer_size) * sizeof(double));
    tx->iq_output_buffer = (double*)malloc((2*tx->output_samples) * sizeof(double));
    tx->samples = 0;

    if (cw_shape_buffer48) free(cw_shape_buffer48);
    if (cw_shape_buffer192) free(cw_shape_buffer192);
    //
    // We need this one both for old and new protocol, since
    // is is also used to shape the audio samples
    cw_shape_buffer48 = (double*)malloc(tx->buffer_size * sizeof(double));
    if (protocol == NEW_PROTOCOL)
    {
        // We need this buffer for the new protocol only, where it is only
        // used to shape the TX envelope
        cw_shape_buffer192 = (double*)malloc(tx->output_samples * sizeof(double));
    }
    fprintf(stderr, "transmitter: allocate buffers: mic_input_buffer=%p iq_output_buffer=%p\n", tx->mic_input_buffer, tx->iq_output_buffer);

    return tx;
} // end create_transmitter


static double compute_power(double p)
{
    double interval = 10.0;
    switch (pa_power)
    {
    case PA_1W:
        interval = 100.0; // mW
        break;
    case PA_10W:
        interval = 1.0; // W
        break;
    case PA_30W:
        interval = 3.0; // W
        break;
    case PA_50W:
        interval = 5.0; // W
        break;
    case PA_100W:
        interval = 10.0; // W
        break;
    case PA_200W:
        interval = 20.0; // W
        break;
    case PA_500W:
        interval = 50.0; // W
        break;
    }
    int i=0;
    if (p > (double)pa_trim[10])
    {
        i = 9;
    }
    else
    {
        while (p > (double)pa_trim[i])
        {
            i++;
        }
        if (i > 0) i--;
    }

    double frac = (p - (double)pa_trim[i]) / ((double)pa_trim[i + 1] - (double)pa_trim[i]);
    return interval * ((1.0 - frac) * (double)i + frac * (double)(i + 1));
} // end compute_power


void updateTx(TRANSMITTER *tx)
{
    int rc;

    //fprintf(stderr,"update_display: tx id=%d\n",tx->id);
 //   if (tx->displaying)
    {
        // if "MON" button is active (tx->feedback is TRUE),
        // then obtain spectrum pixels from PS_RX_FEEDBACK,
        // that is, display the (attenuated) TX signal from the "antenna"
        //
        // POSSIBLE MISMATCH OF SAMPLE RATES IN ORIGINAL PROTOCOL:
        // TX sample rate is fixed 48 kHz, but RX sample rate can be
        // 2*, 4*, or even 8* larger. The analyzer has been set up to use
        // more pixels in this case, so we just need to copy the
        // inner part of the spectrum.
        // If both spectra have the same number of pixels, this code
        // just copies all of them
        //
#ifdef PURESIGNAL
        if (tx->puresignal && tx->feedback) {
            RECEIVER *rx_feedback=receiver[PS_RX_FEEDBACK];
            g_mutex_lock(&rx_feedback->mutex);
            GetPixels(rx_feedback->id,0,rx_feedback->pixel_samples,&rc);
            int full  = rx_feedback->pixels;  // number of pixels in the feedback spectrum
            int width = tx->pixels;           // number of pixels to copy from the feedback spectrum
            int start = (full-width) /2;      // Copy from start ... (end-1)
            float *tfp=tx->pixel_samples;
            float *rfp=rx_feedback->pixel_samples+start;
            int i;
            //
            // The TX panadapter shows a RELATIVE signal strength. A CW or single-tone signal at
            // full drive appears at 0dBm, the two peaks of a full-drive two-tone signal appear
            // at -6 dBm each. THIS DOES NOT DEPEND ON THE POSITION OF THE DRIVE LEVEL SLIDER.
            // The strength of the feedback signal, however, depends on the drive, on the PA and
            // on the attenuation effective in the feedback path.
            // We try to normalize the feeback signal such that is looks like a "normal" TX
            // panadapter if the feedback is optimal for PURESIGNAL (that is, if the attenuation
            // is optimal). The correction depends on the protocol (different peak levels in the TX
            // feedback channel, old=0.407, new=0.2899, difference is 3 dB).
            switch (protocol) {
            case ORIGINAL_PROTOCOL:
                for (i=0; i<width; i++) {
                    *tfp++ =*rfp++ + 12.0;
                }
                break;
            case NEW_PROTOCOL:
                for (i=0; i<width; i++) {
                    *tfp++ =*rfp++ + 15.0;
                }
                break;
            default:
                memcpy(tfp, rfp, width*sizeof(float));
                break;
            }
            g_mutex_unlock(&rx_feedback->mutex);
        } else {
#endif
#ifdef PURESIGNAL
        }
#endif
        double constant1 = 3.3;
        double constant2 = 0.095;
        int fwd_cal_offset = 6;

        int fwd_power;
        int rev_power;
        int ex_power;
        double v1;

        fwd_power = alex_forward_power;
        rev_power = alex_reverse_power;
        if (device == DEVICE_HERMES_LITE || device == DEVICE_HERMES_LITE2)
        {
            ex_power = 0;
        }
        else
        {
            ex_power = exciter_power;
        }
        switch (protocol)
        {
        case ORIGINAL_PROTOCOL:
            switch (device)
            {
            case DEVICE_METIS:
                constant1 = 3.3;
                constant2 = 0.09;
                break;
            case DEVICE_HERMES:
            case DEVICE_STEMLAB:
                constant1 = 3.3;
                constant2 = 0.095;
                break;
            case DEVICE_ANGELIA:
                constant1 = 3.3;
                constant2 = 0.095;
                break;
            case DEVICE_ORION:
                constant1 = 5.0;
                constant2 = 0.108;
                fwd_cal_offset = 4;
                break;
            case DEVICE_ORION2:
                constant1 = 5.0;
                constant2 = 0.08;
                fwd_cal_offset = 18;
                break;
            case DEVICE_HERMES_LITE:
            case DEVICE_HERMES_LITE2:
                // possible reversed depending polarity of current sense transformer
                if (rev_power > fwd_power)
                {
                    fwd_power = alex_reverse_power;
                    rev_power = alex_forward_power;
                }
                constant1 = 3.3;
                constant2 = 1.4;
                fwd_cal_offset = 6;
                break;
            }

            if (fwd_power == 0)
            {
                fwd_power = ex_power;
            }
            fwd_power = fwd_power-fwd_cal_offset;
            v1 = ((double)fwd_power/4095.0)*constant1;
            transmitter->fwd = (v1*v1)/constant2;

            if (device == DEVICE_HERMES_LITE || device == DEVICE_HERMES_LITE2)
            {
                transmitter->exciter = 0.0;
            }
            else
            {
                ex_power = ex_power-fwd_cal_offset;
                v1 = ((double)ex_power/4095.0)*constant1;
                transmitter->exciter = (v1*v1)/constant2;
            }

            transmitter->rev = 0.0;
            if (fwd_power != 0)
            {
                v1 = ((double)rev_power/4095.0)*constant1;
                transmitter->rev = (v1*v1)/constant2;
            }
            break;
        case NEW_PROTOCOL:
            switch (device)
            {
            case NEW_DEVICE_ATLAS:
                constant1 = 3.3;
                constant2 = 0.09;
                break;
            case NEW_DEVICE_HERMES:
                constant1 = 3.3;
                constant2 = 0.09;
                break;
            case NEW_DEVICE_HERMES2:
                constant1 = 3.3;
                constant2 = 0.095;
                break;
            case NEW_DEVICE_ANGELIA:
                constant1 = 3.3;
                constant2 = 0.095;
                break;
            case NEW_DEVICE_ORION:
                constant1 = 5.0;
                constant2 = 0.108;
                fwd_cal_offset = 4;
                break;
            case NEW_DEVICE_ORION2:
                constant1 = 5.0;
                constant2 = 0.08;
                fwd_cal_offset = 18;
                break;
            case NEW_DEVICE_HERMES_LITE:
            case NEW_DEVICE_HERMES_LITE2:
                constant1 = 3.3;
                constant2 = 0.09;
                break;
            }

            fwd_power = alex_forward_power;
            if (fwd_power == 0)
            {
                fwd_power = exciter_power;
            }
            fwd_power = fwd_power-fwd_cal_offset;
            v1 = ((double)fwd_power/4095.0)*constant1;
            transmitter->fwd = (v1*v1)/constant2;

            ex_power = exciter_power;
            ex_power = ex_power-fwd_cal_offset;
            v1 = ((double)ex_power/4095.0)*constant1;
            transmitter->exciter = (v1*v1)/constant2;

            transmitter->rev = 0.0;
            if (alex_forward_power != 0)
            {
                rev_power = alex_reverse_power;
                v1 = ((double)rev_power/4095.0)*constant1;
                transmitter->rev = (v1*v1)/constant2;
            }
            break;
        }

        double fwd = compute_power(transmitter->fwd);
        double rev = compute_power(transmitter->rev);

       // fprintf(stderr, "transmitter: meter_update: fwd:%f->%f rev:%f->%f ex_fwd=%d alex_fwd=%d alex_rev=%d\n",transmitter->fwd,fwd,transmitter->rev,rev,exciter_power,alex_forward_power,alex_reverse_power);

        if (!duplex)
        {
            //  meter_update(active_receiver,POWER,/*transmitter->*/fwd,/*transmitter->*/rev,transmitter->exciter,transmitter->alc);
        }
    }
} // end updateTx
