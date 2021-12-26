/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "discovered.h"
#include "receiver.h"
#include "transmitter.h"
//#include "gpio.h"
#include "radio.h"
#include "server.h"
//#include "version.h"
#ifdef I2C
#include "i2c.h"
#endif
#include "discovery.h"
#include "new_protocol.h"
#include "old_protocol.h"

#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)

struct utsname unameData;
struct _vfo vfo[MAX_VFOS];
//struct _mode_settings mode_settings[MODES];

int dot;
int dash;
int memory_tune=0;
int full_tune=0;

char *mode_string[]={
    "LSB"
    ,"USB"
    ,"DSB"
    ,"CWL"
    ,"CWU"
    ,"FMN"
    ,"AM"
    ,"DIGU"
    ,"SPEC"
    ,"DIGL"
    ,"SAM"
    ,"DRM"
};

DISCOVERED *radio = NULL;
int region=REGION_OTHER;
int band = band20;
int disablePA = true;

RECEIVER *receiver[MAX_RECEIVERS];
RECEIVER *active_receiver;
TRANSMITTER *transmitter;
int active_receivers = 0;

unsigned int radio_id = 0;
int buffer_size=1024; // 64, 128, 256, 512, 1024, 2048
int atlas_penelope=0;
int atlas_clock_source_10mhz=0;
int atlas_clock_source_128mhz=0;
int atlas_config=0;
int atlas_mic_source=0;
int atlas_janus=0;

int classE=0;
int eer_pwm_min=100;
int eer_pwm_max=800;
double div_cos=1.0;        // I factor for diversity
double div_sin=1.0;	   // Q factor for diversity
double div_gain=0.0;	   // gain for diversity (in dB)
double div_phase=0.0;	   // phase for diversity (in degrees, 0 ... 360)

int tx_out_of_band=0;
int filter_board=ALEX;
int pa_enabled=PA_ENABLED;
int pa_power=0;
int pa_trim[11];
int mic_linein=0;
int linein_gain=16; // 0..31
int mic_boost=0;
int mic_bias_enabled=0;
int mic_ptt_enabled=0;
int mic_ptt_tip_bias_ring=0;

int receivers=RECEIVERS;

ADC adc[2];
DAC dac[2];
int adc_attenuation[2];
int lt2208Dither = 0;
int lt2208Random = 0;
int attenuation = 0; // 0dB
int protocol;
int device;
int new_pa_board=0; // Indicates Rev.24 PA board for HERMES/ANGELIA/ORION
int ozy_software_version;
int mercury_software_version;
int penelope_software_version;
int adc_overload;
int pll_locked;
unsigned int exciter_power;
unsigned int temperature;
unsigned int average_temperature;
unsigned int n_temperature;
unsigned int current;
unsigned int average_current;
unsigned int n_current;
unsigned int alex_forward_power;
unsigned int alex_reverse_power;
unsigned int AIN3;
unsigned int AIN4;
unsigned int AIN6;
unsigned int IO1;
unsigned int IO2;
unsigned int IO3;
int supply_volts;
int ptt=0;
int mox=0;
int tune=0;
int have_rx_gain=0;
int rx_gain_calibration=14;
int can_transmit=0;
bool duplex=false;
int vox=0;
int n_adc=1;
bool iqswap;
int diversity_enabled=0;
int CAT_cw_is_active=0;
int cw_key_hit=0;
int cw_keys_reversed=0; // 0=disabled 1=enabled
int cw_keyer_speed=16; // 1-60 WPM
int cw_keyer_mode=KEYER_MODE_A;
int cw_keyer_weight=50; // 0-100
int cw_keyer_spacing=0; // 0=on 1=off
int cw_keyer_internal=1; // 0=external 1=internal
int cw_keyer_sidetone_volume=50; // 0-127
int cw_keyer_ptt_delay=20; // 0-255ms
int cw_keyer_hang_time=500; // ms
int cw_keyer_sidetone_frequency=800; // Hz
int cw_breakin=1; // 0=disabled 1=enabled

int cw_is_on_vfo_freq=1;   // 1= signal on VFO freq, 0= signal offset by side tone
unsigned char OCtune=0;
int OCfull_tune_time=2800; // ms
int OCmemory_tune_time=550; // ms
long long tune_timeout;

double pa_calibration = 0.0f;
double drive_max=100;

char *discovered_xml=NULL;
// ****************** end of declaration list **********************


void status_text(char *text)
{
    fprintf(stderr,"splash_status: %s\n", text);
    usleep(10000);
} // end status_text


int main_delete(int radio_id)
{
    DISCOVERED *radio = &discovered[radio_id];
    if (radio != NULL)
    {
#ifdef GPIO
        gpio_close();
#endif
        switch (protocol)
        {
        case ORIGINAL_PROTOCOL:
            old_protocol_stop();
       //     free(radio);
            break;
        case NEW_PROTOCOL:
            new_protocol_stop();
            free(radio->wideband->input_buffer);
            free(radio->wideband);
       //     free(radio);
            break;
        }
    }
    receivers = active_receivers = 0;
    return 0; ////    _exit(0);
} // end main_delete


static void activatehpsdr()
{
    //  fprintf(stderr,"Build: %s %s\n",build_date,version);

    uname(&unameData);
    fprintf(stderr,"sysname: %s\n",unameData.sysname);
    fprintf(stderr,"nodename: %s\n",unameData.nodename);
    fprintf(stderr,"release: %s\n",unameData.release);
    fprintf(stderr,"version: %s\n",unameData.version);
    fprintf(stderr,"machine: %s\n",unameData.machine);

    receivers = active_receivers;
    discovery();
} // end activatehpsdr


/* Radio_id indicates array index of selected discovered device. */
bool start_radio(int radio_id)
{
    int i = 0;
    DISCOVERED *radio = &discovered[radio_id];
    if (radio->status != STATE_AVAILABLE)
    {
        fprintf(stderr, "Radio %d is unavailable.\n", radio_id);
        return false;
    }

    printf("start_radio: selected radio=%p device=%d\n", radio, radio->device);

    protocol = radio->protocol;
    device = radio->device;

    // set the default power output and max drive value
    drive_max = 100.0;
    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        switch (device)
        {
        case DEVICE_METIS:
            pa_power=PA_1W;
            break;
        case DEVICE_HERMES:
            pa_power=PA_100W;
            break;
        case DEVICE_GRIFFIN:
            pa_power=PA_100W;
            break;
        case DEVICE_ANGELIA:
            pa_power=PA_100W;
            break;
        case DEVICE_ORION:
            pa_power=PA_100W;
            break;
        case DEVICE_HERMES_LITE:
            pa_power=PA_1W;
            break;
        case DEVICE_HERMES_LITE2:
            pa_power=PA_10W;
            break;
        case DEVICE_ORION2:
            pa_power=PA_200W;
            break;
        case DEVICE_STEMLAB:
            pa_power=PA_100W;
            break;
        }
        break;
    case NEW_PROTOCOL:
        switch (device)
        {
        case NEW_DEVICE_ATLAS:
            pa_power=PA_1W;
            break;
        case NEW_DEVICE_HERMES:
        case NEW_DEVICE_HERMES2:
            pa_power=PA_100W;
            break;
        case NEW_DEVICE_ANGELIA:
            pa_power=PA_100W;
            break;
        case NEW_DEVICE_ORION:
            pa_power=PA_100W;
            break;
        case NEW_DEVICE_HERMES_LITE:
            pa_power=PA_1W;
            break;
        case NEW_DEVICE_HERMES_LITE2:
            pa_power=PA_10W;
            break;
        case NEW_DEVICE_ORION2:
            pa_power=PA_200W;
            break;
        case DEVICE_STEMLAB:
            pa_power=PA_100W;
            break;
        }
        break;
    }

    switch (pa_power)
    {
    case PA_1W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i*100;
        }
        break;
    case PA_10W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i;
        }
        break;
    case PA_30W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i*3;
        }
        break;
    case PA_50W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i*5;
        }
        break;
    case PA_100W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i*10;
        }
        break;
    case PA_200W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i*20;
        }
        break;
    case PA_500W:
        for (i=0;i<11;i++)
        {
            pa_trim[i]=i*50;
        }
        break;
    }

    //
    // have_rx_gain determines whether we have "ATT" or "RX Gain" Sliders
    // It is set for HermesLite (and RadioBerry, for that matter)
    //
    have_rx_gain = 0;
    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        switch (device)
        {
        case DEVICE_HERMES_LITE:
        case DEVICE_HERMES_LITE2:
            have_rx_gain=1;
            rx_gain_calibration=14;
            break;
        default:
            have_rx_gain=0;
            rx_gain_calibration=0;
            break;
        }
        break;
    case NEW_PROTOCOL:
        switch (device)
        {
        case NEW_DEVICE_HERMES_LITE:
        case NEW_DEVICE_HERMES_LITE2:
            have_rx_gain=1;
            rx_gain_calibration=14;
            break;
        default:
            have_rx_gain=0;
            rx_gain_calibration=0;
            break;
        }
        break;
    default:
        have_rx_gain=0;
        break;
    }

    //
    // can_transmit decides whether we have a transmitter.
    //
    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
        can_transmit=1;
        break;
    }

    char text[1024];
    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
#ifdef USBOZY
        if (device == DEVICE_OZY)
        {
            sprintf(text,"%s (%s) on USB /dev/ozy\n", radio->name, protocol==ORIGINAL_PROTOCOL?"Protocol 1":"Protocol 2");
        }
        else
        {
#endif
            sprintf(text,"Starting %s (%s v%d.%d)",
                    radio->name,
                    protocol == ORIGINAL_PROTOCOL?"Protocol 1":"Protocol 2",
                    radio->software_version / 10,
                    radio->software_version % 10);
#ifdef USBOZY
        }
#endif
        break;
    }


    char p[32];
    char version[32];
    char mac[32];
    char ip[32];
    char iface[64];

    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        strcpy(p,"Protocol 1");
        sprintf(version,"v%d.%d)",
                radio->software_version/10,
                radio->software_version%10);
        sprintf(mac,"%02X:%02X:%02X:%02X:%02X:%02X",
                radio->info.network.mac_address[0],
                radio->info.network.mac_address[1],
                radio->info.network.mac_address[2],
                radio->info.network.mac_address[3],
                radio->info.network.mac_address[4],
                radio->info.network.mac_address[5]);
        sprintf(ip,"%s", inet_ntoa(radio->info.network.address.sin_addr));
        sprintf(iface,"%s", radio->info.network.interface_name);
        break;
    case NEW_PROTOCOL:
        strcpy(p,"Protocol 2");
        sprintf(version,"v%d.%d)",
                radio->software_version/10,
                radio->software_version%10);
        sprintf(mac,"%02X:%02X:%02X:%02X:%02X:%02X",
                radio->info.network.mac_address[0],
                radio->info.network.mac_address[1],
                radio->info.network.mac_address[2],
                radio->info.network.mac_address[3],
                radio->info.network.mac_address[4],
                radio->info.network.mac_address[5]);
        sprintf(ip,"%s", inet_ntoa(radio->info.network.address.sin_addr));
        sprintf(iface,"%s", radio->info.network.interface_name);
        break;
    }

    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
#ifdef USBOZY
        if (device == DEVICE_OZY)
        {
            sprintf(text,"%s (%s) on USB /dev/ozy\n", radio->name, p);
        }
        else
        {
#endif
            sprintf(text,"Starting %s (%s %s)",
                    radio->name,
                    p,
                    version);
#ifdef USBOZY
        }
#endif
        break;
    }

    status_text(text);

    sprintf(text,"HPSDR: %s (%s %s) %s (%s) on %s",
            radio->name,
            p,
            version,
            ip,
            mac,
            iface);

    status_text(text);

    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        switch (device)
        {
        case DEVICE_ORION2:
            //meter_calibration=3.0;
            //display_calibration=3.36;
            break;
        default:
            //meter_calibration=-2.44;
            //display_calibration=-2.1;
            break;
        }
        break;
    case NEW_PROTOCOL:
        switch (device)
        {
        case NEW_DEVICE_ORION2:
            //meter_calibration=3.0;
            //display_calibration=3.36;
            break;
        default:
            //meter_calibration=-2.44;
            //display_calibration=-2.1;
            break;
        }
        break;
    }

    //
    // Determine number of ADCs in the device
    //
    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        switch (device)
        {
        case DEVICE_METIS: // No support for multiple MERCURY cards on a single ATLAS bus.
        case DEVICE_HERMES:
        case DEVICE_HERMES_LITE:
        case DEVICE_HERMES_LITE2:
            n_adc=1;
            break;
        default:
            n_adc=2;
            break;
        }
        break;
    case NEW_PROTOCOL:
        switch (device)
        {
        case NEW_DEVICE_ATLAS:
            n_adc=1; // No support for multiple MERCURY cards on a single ATLAS bus.
            break;
        case NEW_DEVICE_HERMES:
        case NEW_DEVICE_HERMES2:
        case NEW_DEVICE_HERMES_LITE:
        case NEW_DEVICE_HERMES_LITE2:
            n_adc=1;
            radio->wideband = (WIDEBAND*)malloc(sizeof(WIDEBAND));
            radio->wideband->buffer_size = 16384;
            radio->wideband->adc = 0;
            radio->wideband->input_buffer = malloc((radio->wideband->buffer_size)*sizeof(int16_t));
            radio->wideband->sequence = 0;
            break;
        default:
            n_adc=2;
            break;
        }
        break;
    default:
        break;
    }

    iqswap=0;

    adc_attenuation[0]=0;
    adc_attenuation[1]=0;

    if (have_rx_gain)
    {
        adc_attenuation[0]=14;
        adc_attenuation[1]=14;
    }

    adc[0].antenna=ANTENNA_1;
    adc[0].filters=AUTOMATIC;
    adc[0].hpf=HPF_13;
    adc[0].lpf=LPF_30_20;
    adc[0].dither=false;
    adc[0].random=false;
    adc[0].preamp=false;
    adc[0].attenuation=0;

    adc[1].antenna=ANTENNA_1;
    adc[1].filters=AUTOMATIC;
    adc[1].hpf=HPF_9_5;
    adc[1].lpf=LPF_60_40;
    adc[1].dither=false;
    adc[1].random=false;
    adc[1].preamp=false;
    adc[1].attenuation=0;

    //printf("meter_calibration=%f display_calibration=%f\n", meter_calibration, display_calibration);

    temperature=0;
    average_temperature=0;
    n_temperature=0;
    current=0;
    average_current=0;
    n_current=0;

    //display_sequence_errors=true;

    //
    // It is possible that an option has been read in
    // which is not compatible with the hardware.
    // Change setting to reasonable value then.
    // This could possibly be moved to radioRestoreState().
    //
    // Sanity Check #1: restrict buffer size in new protocol
    //
    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        if (buffer_size > 2048) buffer_size=2048;
        break;
    case NEW_PROTOCOL:
        if (buffer_size > 512) buffer_size=512;
        break;
    }
    //
    // Sanity Check #2: enable diversity only if there are two RX and two ADCs
    //
    if (RECEIVERS < 2 || n_adc < 2)
    {
        diversity_enabled=0;
    }

    //////////  radio_change_region(region);

    if (can_transmit)
    {
    //    calcDriveLevel(pa_calibration);
        if (transmitter->puresignal)
        {
            ////////     tx_set_ps(transmitter,transmitter->puresignal);
        }
    }
    return true;
} // end start_radio


int isTransmitting()
{
    return mox | vox | tune;
} // end isTransmitting


void radio_change_receivers(int r)
{
    fprintf(stderr, "radio_change_receivers: from %d to %d\n", receivers, r);

    if (receivers == r) return;

    //
    // When changing the number of receivers, restart the
    // old protocol
    //
    if (protocol == ORIGINAL_PROTOCOL)
    {
        old_protocol_stop();
    }

    switch (r)
    {
    case 1:
        receivers = 1;
        break;
    case 2:
        receivers = 2;
        break;
    }

    active_receiver = receiver[0];

    if (protocol == NEW_PROTOCOL)
    {
        schedule_high_priority();
    }

    if (protocol == ORIGINAL_PROTOCOL)
    {
        old_protocol_run();
    }
} // end radio_change_receivers


void start_receiver(int radio_id, int rx)
{
    DISCOVERED *radio = &discovered[radio_id];

    receiver[rx] = create_receiver(rx, buffer_size, 0, 0); // receiver[i]->alex_antenna, receiver[i]->alex_attenuation);
    printf("Created receiver %d\n", receiver[rx]->id);
        ////////   setSquelch(receiver[i]);

    //
    // Sanity check: in old protocol, all receivers must have the same sample rate
    //
///////   if ((protocol == ORIGINAL_PROTOCOL) && (RECEIVERS == 2) && (receiver[0]->sample_rate != receiver[1]->sample_rate))
///////    {
///////        receiver[1]->sample_rate = receiver[0]->sample_rate;
///////    }

    active_receiver = receiver[rx];
    receivers++;
//    if (rx > 0)
  //      receivers++;

    printf("Starting: receivers=%d RECEIVERS=%d\n", receivers, RECEIVERS);
    if (receivers != RECEIVERS)
    {
        int r=receivers;
        /////////   receivers=RECEIVERS;
        printf("Starting receiver: calling radio_change_receivers: receivers=%d r=%d\n", RECEIVERS, r);
        //////////    radio_change_receivers(r);
    }

    if (radio->supported_transmitters > 0)
        start_transmitter(radio_id, 0);

    switch (protocol)
    {
    case ORIGINAL_PROTOCOL:
        old_protocol_init(receiver[rx]->sample_rate);
        break;
    case NEW_PROTOCOL:
        new_protocol_init();
        break;
    }

    if (protocol == NEW_PROTOCOL)
    {
        printf("Schedule_high_priority\n");
        schedule_high_priority();
    }
} // end start_receiver


void start_transmitter(int radio_id, int tx)
{
    // TEMP
    if (can_transmit)
    {
        fprintf(stderr, "Create transmitter: TX:%d  Buffer size: %d\n", tx, buffer_size);

        transmitter = create_transmitter(tx, buffer_size);

        calcDriveLevel(pa_calibration);

#ifdef PURESIGNAL
        double pk;
        tx_set_ps_sample_rate(transmitter,protocol==NEW_PROTOCOL?192000:active_receiver->sample_rate);
        receiver[PS_TX_FEEDBACK]=create_pure_signal_receiver(PS_TX_FEEDBACK, buffer_size,protocol==ORIGINAL_PROTOCOL?active_receiver->sample_rate:192000,display_width);
        receiver[PS_RX_FEEDBACK]=create_pure_signal_receiver(PS_RX_FEEDBACK, buffer_size,protocol==ORIGINAL_PROTOCOL?active_receiver->sample_rate:192000,display_width);
        switch (protocol)
        {
        case NEW_PROTOCOL:
            pk = 0.2899;
            break;
        case ORIGINAL_PROTOCOL:
            switch (device)
            {
            case DEVICE_HERMES_LITE2:
                pk = 0.2300;
                break;
            default:
                pk = 0.4067;
                break;
            }
        }
        SetPSHWPeak(transmitter->id, pk);
#endif
    }

    bool init_gpio = false;
#ifdef LOCALCW
    init_gpio = true;
#endif
#ifdef PTT
    init_gpio = true;
#endif
#ifdef GPIO
    init_gpio = true;
#endif

    if (init_gpio)
    {
#ifdef GPIO
        if (gpio_init() < 0)
        {
            printf("GPIO failed to initialize\n");
        }
#endif
    }

#ifdef LOCALCW
    // init local keyer if enabled
    if (cw_keyer_internal == 0)
    {
        printf("Initialize keyer.....\n");
        keyer_update();
    }
#endif
} // end start_transmitter


static int calcLevel(double d, double pa_calibration)
{
    int level=0;

    double target_dbm = 10.0 * log10(d * 1000.0);
    double gbb = pa_calibration;
    target_dbm -= gbb;
    double target_volts = sqrt(pow(10, target_dbm * 0.1) * 0.05);
    double volts = min((target_volts / 0.8), 1.0);
    double actual_volts = volts*(1.0/0.98);

    if (actual_volts < 0.0)
    {
        actual_volts = 0.0;
    }
    else
        if (actual_volts > 1.0)
        {
            actual_volts = 1.0;
        }

    level = (int)(actual_volts * 255.0);

    return level;
} // end calcLevel


void set_tx_power(int pow)
{
    transmitter->drive = pow;
    if (protocol == NEW_PROTOCOL)
    {
        schedule_high_priority();
    }
} // end set_tx_power


void calcDriveLevel(double pa_calibration)
{
    transmitter->drive_level = calcLevel(transmitter->drive, pa_calibration);
    if (isTransmitting() && protocol == NEW_PROTOCOL)
    {
        schedule_high_priority();
    }
    //fprintf(stderr, "calcDriveLevel: drive=%d drive_level=%d\n",transmitter->drive,transmitter->drive_level);
} // end calcDriveLevel


int create_manifest()
{
    DISCOVERED *d;
    char *xml;
    char line[80];

    xml = (char*)malloc(1);
    xml[0] = 0;
    sprintf(line, "<radios=%d>\n", devices);
    xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
    strcat(xml, line);
    for (int i=0;i<devices;i++)
    {
        d = &discovered[i];
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(d->info.network.address.sin_addr), str, INET_ADDRSTRLEN);
        if (strcmp(str, "127.0.0.1") == 0) continue;
        sprintf(line, "<radio=%d>\n", i);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<radio_type>hermes</radio_type>\n");
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<protocol>%d</protocol>\n", d->protocol);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<device>%d</device>\n", d->device);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<use_tcp>%d</use_tcp>\n", d->use_tcp);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<name>%s</name>\n", d->name);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<software_version>%d</software_version>\n", d->software_version);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<status>%d</status>\n", d->status);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<bandscope>1</bandscope>\n");
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<supported_receivers>%d</supported_receivers>\n", d->supported_receivers);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<supported_transmitters>%d</supported_transmitters>\n", d->supported_transmitters);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<adcs>%d</adcs>\n", d->adcs);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<dacs>%d</dacs>\n", d->dacs);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<frequency_min>%f</frequency_min>\n", d->frequency_min);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<frequency_max>%f</frequency_max>\n", d->frequency_max);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<mac_address>%02x:%02x:%02x:%02x:%02x:%02x</mac_address>\n",
                d->info.network.mac_address[0], d->info.network.mac_address[1],
                d->info.network.mac_address[2], d->info.network.mac_address[3],
                d->info.network.mac_address[4], d->info.network.mac_address[5]);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<ip_address>%s</ip_address>\n", str);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<local_audio>%d</local_audio>\n", true);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<local_mic>%d</local_mic>\n", true);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "<ant_switch>%d</ant_switch>\n", 3);
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
        sprintf(line, "</radio>\n");
        xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
        strcat(xml, line);
    }
    sprintf(line, "</radios>\n");
    xml = (char*)realloc((char*)xml, strlen(xml)+strlen(line)+1);
    strcat(xml, line);
    discovered_xml = (char*)malloc(strlen(xml)+1);
    memcpy((char*)discovered_xml, (char*)xml, strlen(xml)+1);
    free(xml);
    return 0;
} // end create_manifest


int band_get_current()
{
    return band;
} // end band_get_current


void set_alex_rx_antenna(int v)
{
    if (active_receiver->id == 0)
    {
        active_receiver->alex_antenna = v;
        if (protocol == NEW_PROTOCOL)
        {
            schedule_high_priority();
        }
    }
} // end set_alex_rx_antenna


void set_alex_tx_antenna(int v)
{
    transmitter->alex_antenna = v;
    if (protocol == NEW_PROTOCOL)
    {
        schedule_high_priority();
    }
} // end set_alex_tx_antenna


//
// There is an error here.
// The alex att should not be associated with a receiver,
// but with an ADC. *all* receivers bound to that ADC
// will experience the same attenuation.
//
// This means, alex_attenuation should not be stored in thre
// receiver, but separately (as is the case with adc_attenuation).
//
void set_alex_attenuation(int rx, int v)
{
    receiver[rx]->alex_attenuation = v;
    adc_attenuation[0] = 0;
    if (v == 1) {adc_attenuation[0] = 10; adc_attenuation[1] = 10;}
    if (v == 2) {adc_attenuation[0] = 20; adc_attenuation[1] = 20;}
    if (v == 3) {adc_attenuation[0] = 30; adc_attenuation[1] = 30;}
    printf("Att: %d for RX: %d\n", receiver[rx]->alex_attenuation, rx);
    if (protocol == NEW_PROTOCOL)
    {
        schedule_high_priority();
    }
} // end set_alex_attenuation


void dither_cb(bool enable)
{
  active_receiver->dither = enable;
  if (protocol == NEW_PROTOCOL)
  {
    schedule_high_priority();
  }
} // end dither_cb


void random_cb(bool enable)
{
  active_receiver->random = enable;
  if (protocol == NEW_PROTOCOL)
  {
    schedule_high_priority();
  }
} // end random_cb


void preamp_cb(bool enable)
{
  active_receiver->preamp = enable;
  if (protocol == NEW_PROTOCOL)
  {
    schedule_high_priority();
  }
} // end preamp_cb


void mic_boost_cb(bool enable)
{
  mic_boost = enable;
  if (protocol == NEW_PROTOCOL)
  {
    schedule_high_priority();
  }
} // end mic_boost_cb


void linein_gain_cb(int gain)
{
  linein_gain = gain;
  if (protocol == NEW_PROTOCOL)
  {
    schedule_high_priority();
  }
} // end linein_gain_cb


void setFrequency(int v, long long f)
{
    ////////  int v=active_receiver->id;
    ///////// vfo[v].band=get_band_from_frequency(f);

    switch (protocol)
    {
    case NEW_PROTOCOL:
    case ORIGINAL_PROTOCOL:
        if (vfo[v].ctun)
        {
            long long minf = vfo[v].frequency - (long long)(active_receiver->sample_rate/2);
            long long maxf = vfo[v].frequency + (long long)(active_receiver->sample_rate/2);
            if (f < minf) f = minf;
            if (f > maxf) f = maxf;
            vfo[v].ctun_frequency = f;
            vfo[v].offset = f-vfo[v].frequency;
            ////////  set_offset(active_receiver,vfo[v].offset);
            return;
        }
        else
        {
            vfo[v].frequency = f;
            fprintf(stderr, "Set Freq: %lld\n", f);
        }
        break;
    }

    switch (protocol)
    {
    case NEW_PROTOCOL:
        schedule_high_priority();
        break;
    case ORIGINAL_PROTOCOL:
        break;
    }
} // end setFrequency


int main_start(char *dsp_server_address)
{
    int status = 0;
    char name[1024];

    sprintf(name, "org.kd0oss.hpsdr_new.pid%d", getpid());
    activatehpsdr();
    if (devices > 0)
    {
        create_manifest();

        if (devices == 1)
        {
            radio = &discovered[0];
        }
    }

    create_listener_thread(dsp_server_address);

    // loop until terminated with extreme prejudice
    while (1)
    {
        sleep(1);
    }

    // "should" not get this far
    fprintf(stderr,"exiting ...\n");
    if (discovered_xml != NULL)
        free(discovered_xml);
    main_delete(0);
    return status;
} // end main_start


void add_wideband_sample(WIDEBAND *w, int16_t sample)
{
    w->input_buffer[w->samples] = sample;
    w->samples = w->samples+1;
    if (w->samples >= 16384)
    {
        send_WB_IQ_buffer(w->channel);
        w->samples = 0;
    }
} // end add_wideband_sample

