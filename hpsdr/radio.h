#ifndef RADIO_H
#define RADIO_H

#include "adc.h"
#include "dac.h"
#include "discovered.h"
#include "receiver.h"
#include "transmitter.h"
#include <sys/utsname.h>

extern struct utsname unameData;


#define NEW_MIC_IN 0x00
#define NEW_LINE_IN 0x01
#define NEW_MIC_BOOST 0x02
#define NEW_ORION_MIC_PTT_ENABLED 0x00
#define NEW_ORION_MIC_PTT_DISABLED 0x04
#define NEW_ORION_MIC_PTT_RING_BIAS_TIP 0x00
#define NEW_ORION_MIC_PTT_TIP_BIAS_RING 0x08
#define NEW_ORION_MIC_BIAS_DISABLED 0x00
#define NEW_ORION_MIC_BIAS_ENABLED 0x10

#define OLD_MIC_IN 0x00
#define OLD_LINE_IN 0x02
#define OLD_MIC_BOOST 0x01
#define OLD_ORION_MIC_PTT_ENABLED 0x40
#define OLD_ORION_MIC_PTT_DISABLED 0x00
#define OLD_ORION_MIC_PTT_RING_BIAS_TIP 0x00
#define OLD_ORION_MIC_PTT_TIP_BIAS_RING 0x08
#define OLD_ORION_MIC_BIAS_DISABLED 0x00
#define OLD_ORION_MIC_BIAS_ENABLED 0x20

#define NONE 0

#define ALEX 1
#define APOLLO 2
#define CHARLY25 3
#define N2ADR 4

#define REGION_OTHER 0
#define REGION_UK 1
#define REGION_WRC15 2  // 60m band allocation for countries implementing WRC15

#define CHANNEL_RX0 0
#define CHANNEL_RX1 1
#define CHANNEL_RX2 2
#define CHANNEL_RX3 3
#define CHANNEL_RX4 4
#define CHANNEL_RX5 5
#define CHANNEL_RX6 6
#define CHANNEL_RX7 7
#define CHANNEL_TX 8
#define CHANNEL_BS 9
#define CHANNEL_SUBRX 10
#define CHANNEL_AUDIO 11

#define modeLSB 0
#define modeUSB 1
#define modeDSB 2
#define modeCWL 3
#define modeCWU 4
#define modeFMN 5
#define modeAM 6
#define modeDIGU 7
#define modeSPEC 8
#define modeDIGL 9
#define modeSAM 10
#define modeDRM 11
#define MODES 12
extern char *mode_string[MODES];

enum {
  band136=0,
  band472,
  band160,
  band80,
  band60,
  band40,
  band30,
  band20,
  band17,
  band15,
  band12,
  band10,
  band6,
  bandWWV,
  bandGen,
  BANDS
};

#define XVTRS 8

enum {
  PA_1W=0,
  PA_10W,
  PA_30W,
  PA_50W,
  PA_100W,
  PA_200W,
  PA_500W
};

extern DISCOVERED *radio;

extern int region;

// specify how many receivers: for PURESIGNAL need two extra
#define RECEIVERS 7
#ifdef PURESIGNAL
#define MAX_RECEIVERS (RECEIVERS+2)
#define PS_TX_FEEDBACK (RECEIVERS)
#define PS_RX_FEEDBACK (RECEIVERS+1)
#else
#define MAX_RECEIVERS RECEIVERS
#endif
#define MAX_DDC (RECEIVERS+2)
//extern int MAX_DDC;
extern RECEIVER *receiver[];
extern RECEIVER *active_receiver;
extern int active_receivers;

#define MAX_TRANSMITTERS 2
extern TRANSMITTER *transmitter;

#define PA_DISABLED 0
#define PA_ENABLED 1

#define KEYER_STRAIGHT 0
#define KEYER_MODE_A 1
#define KEYER_MODE_B 2

#define MAX_BUFFER_SIZE 2048

extern DISCOVERED *radio;
extern unsigned int radio_id;

extern int have_rx_gain;   // TRUE on HermesLite/RadioBerry
extern int rx_gain_calibration;  // position of the RX gain slider that
                 // corresponds to zero amplification/attenuation
extern int disablePA;
extern int band;
extern int filter_board;
extern int pa_enabled;
extern int pa_power;
extern int pa_trim[11];
extern int apollo_tuner;
extern int n_adc;
extern int eer_pwm_min;
extern int eer_pwm_max;

extern int atlas_penelope;
extern int atlas_clock_source_10mhz;
extern int atlas_clock_source_128mhz;
extern int atlas_config;
extern int atlas_mic_source;
extern int atlas_janus;

extern int lt2208Dither;
extern int lt2208Random;
extern int attenuation;
extern unsigned long alex_rx_antenna;
extern unsigned long alex_tx_antenna;
extern unsigned long alex_attenuation;
extern double div_cos, div_sin;
extern double div_gain, div_phase;

extern int new_pa_board;
extern int ozy_software_version;
extern int mercury_software_version;
extern int penelope_software_version;
extern int ptt;
extern int mox;
extern int tune;
extern int memory_tune;
extern int full_tune;
extern int adc_overload;
extern int pll_locked;
extern unsigned int exciter_power;
extern unsigned int temperature;
extern unsigned int average_temperature;
extern unsigned int n_temperature;
extern unsigned int current;
extern unsigned int average_current;
extern unsigned int n_current;
extern unsigned int alex_forward_power;
extern unsigned int alex_reverse_power;
extern unsigned int IO1;
extern unsigned int IO2;
extern unsigned int IO3;
extern unsigned int AIN3;
extern unsigned int AIN4;
extern unsigned int AIN6;
extern int supply_volts;
extern int receivers;
extern int protocol;
extern int buffer_size;
extern int device;
extern int CAT_cw_is_active;
extern int cw_key_hit;
extern bool duplex;
extern int classE;
extern double mic_gain;
extern int binaural;

extern int mic_linein;
extern int linein_gain;
extern int mic_boost;
extern int mic_bias_enabled;
extern int mic_ptt_enabled;
extern int mic_ptt_tip_bias_ring;
extern int cw_keys_reversed;
extern int cw_keyer_speed;
extern int cw_keyer_mode;
extern int cw_keyer_weight;
extern int cw_keyer_spacing;
extern int cw_keyer_internal;
extern int cw_keyer_sidetone_volume;
extern int cw_keyer_ptt_delay;
extern int cw_keyer_hang_time;
extern int cw_keyer_sidetone_frequency;
extern int cw_breakin;
extern int cw_is_on_vfo_freq;
extern unsigned char OCtune;
extern int OCfull_tune_time;
extern int OCmemory_tune_time;
extern long long tune_timeout;

extern int cw_keyer_internal;

extern ADC adc[2];
extern DAC dac[2];
extern int adc_attenuation[2];
extern int sequence_errors;
extern double drive_max;
extern int diversity_enabled;

extern char *discovered_xml;

extern int main_start(char *);
extern int main_delete(int);
extern bool start_radio(int);
extern int isTransmitting();
extern int band_get_current();
extern void calcDriveLevel(double pa_calibration);
extern void start_receivers(int);
extern void start_transmitter(int, int);
extern void setFrequency(int8_t idx, long long f);
extern void set_attenuation(int8_t, int value);
extern void set_alex_rx_antenna(int8_t, int v);
extern void set_alex_tx_antenna(int v);
extern void set_alex_attenuation(int8_t rx, int v);
extern void dither_cb(int8_t, bool);
extern void random_cb(int8_t, bool);
extern void preamp_cb(int8_t, bool);
extern void mic_boost_cb(bool);
extern void set_tx_power(int8_t pow);
extern void linein_gain_cb(int gain);
extern void add_wideband_sample(WIDEBAND *w, int16_t sample);
#endif // RADIO_H
