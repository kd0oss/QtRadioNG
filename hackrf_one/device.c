/* Copyright (C)
* 2026 - Rick Schnicker KD0OSS
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <libhackrf/hackrf.h>

#include "device.h"
#include "hackrf.h"

#define RING_SIZE (1u << 25)    // 32 MiB: about 0.8 s at 20 Msps

static hackrf_device  *dev = NULL;
static pthread_mutex_t dev_lock = PTHREAD_MUTEX_INITIALIZER;   // libhackrf control calls
static bool            streaming = false;
static bool            lib_initialised = false;

static char serial[40] = "";
static char firmware[64] = "";
static int  board_id = -1;

// cached settings, applied on open / start
static long      hw_rate = HACKRF_HW_RATE_DEFAULT;
static long long rx_frequency = 14200000;
static long long lo_frequency = 0;
static int       lna_gain = HACKRF_LNA_GAIN_DEFAULT;
static int       vga_gain = HACKRF_VGA_GAIN_DEFAULT;
static bool      rf_amp = false;
static bool      antenna_power = false;
static double    ppm = 0.0;
static long long tx_frequency = 14200000;
static int       tx_gain = HACKRF_TX_GAIN_DEFAULT;
static bool      tx_amp = false;
static bool      transmitting = false;
static bool      rx_was_streaming = false;
static device_tx_fill_fn tx_fill = NULL;
static void     *tx_fill_user = NULL;

// sample ring buffer: single producer (USB callback), single consumer (DSP thread)
static uint8_t        *ring = NULL;
static size_t          ring_head = 0;      // write position (total bytes written)
static size_t          ring_tail = 0;      // read position (total bytes read)
static unsigned long   overruns = 0;
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ring_cond = PTHREAD_COND_INITIALIZER;


static int report(const char *what, int rc)
{
    if (rc != HACKRF_SUCCESS)
        fprintf(stderr, "hackrf: %s failed: %s (%d)\n", what, hackrf_error_name((enum hackrf_error)rc), rc);
    return rc;
} // end report


static int rx_callback(hackrf_transfer *transfer)
{
    size_t n = (size_t)transfer->valid_length & ~(size_t)1;

    pthread_mutex_lock(&ring_lock);
    if (RING_SIZE - (ring_head - ring_tail) < n)
    {
        overruns++;          // consumer too slow: drop this transfer
    }
    else
    {
        size_t pos = ring_head % RING_SIZE;
        size_t first = (n < RING_SIZE - pos) ? n : RING_SIZE - pos;
        memcpy(ring + pos, transfer->buffer, first);
        memcpy(ring, transfer->buffer + first, n - first);
        ring_head += n;
        pthread_cond_signal(&ring_cond);
    }
    pthread_mutex_unlock(&ring_lock);
    return 0;
} // end rx_callback


size_t device_read(int8_t *buf, size_t max_bytes, int timeout_ms)
{
    struct timespec ts;
    size_t n;

    max_bytes &= ~(size_t)1;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&ring_lock);
    while (ring_head == ring_tail)
    {
        if (pthread_cond_timedwait(&ring_cond, &ring_lock, &ts) == ETIMEDOUT)
            break;
    }
    n = ring_head - ring_tail;
    if (n > max_bytes)
        n = max_bytes;
    if (n > 0)
    {
        size_t pos = ring_tail % RING_SIZE;
        size_t first = (n < RING_SIZE - pos) ? n : RING_SIZE - pos;
        memcpy(buf, ring + pos, first);
        memcpy((uint8_t*)buf + first, ring, n - first);
        ring_tail += n;
    }
    pthread_mutex_unlock(&ring_lock);
    return n;
} // end device_read


void device_flush(void)
{
    pthread_mutex_lock(&ring_lock);
    ring_tail = ring_head;
    pthread_mutex_unlock(&ring_lock);
} // end device_flush


unsigned long device_overruns(void)
{
    unsigned long n;

    pthread_mutex_lock(&ring_lock);
    n = overruns;
    pthread_mutex_unlock(&ring_lock);
    return n;
} // end device_overruns


static int tx_callback(hackrf_transfer *transfer)
{
    size_t n = (size_t)transfer->buffer_length / 2;

    if (tx_fill != NULL)
        tx_fill(tx_fill_user, (int8_t*)transfer->buffer, n);
    else
        memset(transfer->buffer, 0, (size_t)transfer->buffer_length);
    transfer->valid_length = transfer->buffer_length;
    return 0;
} // end tx_callback


/* Must be called with dev_lock held. */
static int apply_frequency(void)
{
    // A reference that is off by ppm scales both the LO and the sample clock,
    // so asking for f/(1+e) puts the signal (LO + IF offset) on f.
    double corrected = (double)(transmitting ? tx_frequency : rx_frequency) / (1.0 + ppm * 1e-6);

    lo_frequency = llround(corrected) - HACKRF_IF_OFFSET;
    if (lo_frequency < 0)
        lo_frequency = 0;
    if (dev == NULL)
        return 0;
    return report("hackrf_set_freq", hackrf_set_freq(dev, (uint64_t)lo_frequency));
} // end apply_frequency


/* Must be called with dev_lock held. */
static int apply_rate(void)
{
    int rc;
    uint32_t bw;

    if (dev == NULL)
        return 0;
    rc = report("hackrf_set_sample_rate", hackrf_set_sample_rate(dev, (double)hw_rate));
    if (rc != HACKRF_SUCCESS)
        return rc;
    // anti-alias filter at 75 % of the sample rate
    bw = hackrf_compute_baseband_filter_bw((uint32_t)(0.75 * (double)hw_rate));
    return report("hackrf_set_baseband_filter_bandwidth", hackrf_set_baseband_filter_bandwidth(dev, bw));
} // end apply_rate


/* Must be called with dev_lock held. */
static int apply_all(void)
{
    int rc = 0;

    rc |= apply_rate();
    rc |= apply_frequency();
    rc |= report("hackrf_set_lna_gain", hackrf_set_lna_gain(dev, (uint32_t)lna_gain));
    rc |= report("hackrf_set_vga_gain", hackrf_set_vga_gain(dev, (uint32_t)vga_gain));
    rc |= report("hackrf_set_amp_enable", hackrf_set_amp_enable(dev, rf_amp ? 1 : 0));
    rc |= report("hackrf_set_antenna_enable", hackrf_set_antenna_enable(dev, antenna_power ? 1 : 0));
    return rc;
} // end apply_all


int device_open(const char *want_serial)
{
    int rc;
    uint8_t id = 0;
    read_partid_serialno_t ps;

    pthread_mutex_lock(&dev_lock);
    if (dev != NULL)
    {
        pthread_mutex_unlock(&dev_lock);
        return 0;
    }

    if (!lib_initialised)
    {
        rc = report("hackrf_init", hackrf_init());
        if (rc != HACKRF_SUCCESS)
        {
            pthread_mutex_unlock(&dev_lock);
            return rc;
        }
        lib_initialised = true;
    }

    if (ring == NULL)
    {
        ring = malloc(RING_SIZE);
        if (ring == NULL)
        {
            pthread_mutex_unlock(&dev_lock);
            return -1;
        }
    }

    rc = report("hackrf_open_by_serial", hackrf_open_by_serial((want_serial && want_serial[0]) ? want_serial : NULL, &dev));
    if (rc != HACKRF_SUCCESS)
    {
        dev = NULL;
        pthread_mutex_unlock(&dev_lock);
        return rc;
    }

    if (hackrf_board_id_read(dev, &id) == HACKRF_SUCCESS)
        board_id = id;
    if (hackrf_version_string_read(dev, firmware, sizeof(firmware) - 1) != HACKRF_SUCCESS)
        strcpy(firmware, "unknown");
    memset(&ps, 0, sizeof(ps));
    if (hackrf_board_partid_serialno_read(dev, &ps) == HACKRF_SUCCESS)
        snprintf(serial, sizeof(serial), "%08x%08x%08x%08x", ps.serial_no[0], ps.serial_no[1], ps.serial_no[2], ps.serial_no[3]);
    else
        strcpy(serial, "unknown");

    rc = apply_all();
    pthread_mutex_unlock(&dev_lock);

    fprintf(stderr, "hackrf: opened %s  serial %s  firmware %s\n", device_board_name(), serial, firmware);
    return rc;
} // end device_open


void device_close(void)
{
    device_set_mox(false, NULL, NULL);
    device_stop();
    pthread_mutex_lock(&dev_lock);
    if (dev != NULL)
    {
        hackrf_close(dev);
        dev = NULL;
    }
    if (lib_initialised)
    {
        hackrf_exit();
        lib_initialised = false;
    }
    pthread_mutex_unlock(&dev_lock);
} // end device_close


bool device_is_open(void)
{
    return dev != NULL;
} // end device_is_open


const char *device_serial(void)
{
    return serial;
} // end device_serial


const char *device_firmware(void)
{
    return firmware;
} // end device_firmware


int device_board_id(void)
{
    return board_id;
} // end device_board_id


const char *device_board_name(void)
{
    if (board_id < 0)
        return "HackRF";
    return hackrf_board_id_name((enum hackrf_board_id)board_id);
} // end device_board_name


int device_start(void)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    if (dev == NULL)
    {
        pthread_mutex_unlock(&dev_lock);
        return -1;
    }
    if (!streaming)
    {
        device_flush();
        rc = report("hackrf_start_rx", hackrf_start_rx(dev, rx_callback, NULL));
        if (rc == HACKRF_SUCCESS)
        {
            streaming = true;
            // the firmware turns bias power off when idle; restore it
            hackrf_set_antenna_enable(dev, antenna_power ? 1 : 0);
        }
    }
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_start


void device_stop(void)
{
    pthread_mutex_lock(&dev_lock);
    if (dev != NULL && streaming)
    {
        report("hackrf_stop_rx", hackrf_stop_rx(dev));
        streaming = false;
    }
    pthread_mutex_unlock(&dev_lock);
} // end device_stop


int device_set_hw_rate(long rate)
{
    int rc = 0;

    if (rate < HACKRF_HW_RATE_MIN || rate > HACKRF_HW_RATE_MAX)
        return -1;
    pthread_mutex_lock(&dev_lock);
    if (transmitting)
    {
        pthread_mutex_unlock(&dev_lock);
        return -1;           // not while transmitting
    }
    hw_rate = rate;
    if (dev != NULL)
    {
        bool was_streaming = streaming;
        if (was_streaming)
        {
            hackrf_stop_rx(dev);
            streaming = false;
        }
        rc = apply_rate();
        device_flush();
        if (was_streaming)
        {
            if (report("hackrf_start_rx", hackrf_start_rx(dev, rx_callback, NULL)) == HACKRF_SUCCESS)
            {
                streaming = true;
                hackrf_set_antenna_enable(dev, antenna_power ? 1 : 0);
            }
        }
    }
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_hw_rate


long device_get_hw_rate(void)
{
    return hw_rate;
} // end device_get_hw_rate


int device_set_frequency(long long f)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    rx_frequency = f;
    if (!transmitting)
        rc = apply_frequency();
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_frequency


long long device_get_lo(void)
{
    return lo_frequency;
} // end device_get_lo


int device_set_ppm(double p)
{
    int rc;

    if (p < -200.0 || p > 200.0)
        return -1;
    pthread_mutex_lock(&dev_lock);
    ppm = p;
    rc = apply_frequency();
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_ppm


int device_set_lna_gain(int db)
{
    int rc = 0;

    if (db < 0)
        db = 0;
    if (db > 40)
        db = 40;
    db -= db % 8;
    pthread_mutex_lock(&dev_lock);
    lna_gain = db;
    if (dev != NULL)
        rc = report("hackrf_set_lna_gain", hackrf_set_lna_gain(dev, (uint32_t)db));
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_lna_gain


int device_set_vga_gain(int db)
{
    int rc = 0;

    if (db < 0)
        db = 0;
    if (db > 62)
        db = 62;
    db -= db % 2;
    pthread_mutex_lock(&dev_lock);
    vga_gain = db;
    if (dev != NULL)
        rc = report("hackrf_set_vga_gain", hackrf_set_vga_gain(dev, (uint32_t)db));
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_vga_gain


int device_set_rf_amp(bool on)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    rf_amp = on;
    if (dev != NULL && !transmitting)
        rc = report("hackrf_set_amp_enable", hackrf_set_amp_enable(dev, on ? 1 : 0));
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_rf_amp


int device_set_antenna_power(bool on)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    antenna_power = on;
    if (dev != NULL)
        rc = report("hackrf_set_antenna_enable", hackrf_set_antenna_enable(dev, on ? 1 : 0));
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_antenna_power


int device_set_mox(bool on, device_tx_fill_fn fill, void *user)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    if (dev == NULL || on == transmitting)
    {
        pthread_mutex_unlock(&dev_lock);
        return dev == NULL && on ? -1 : 0;
    }

    if (on)
    {
        rx_was_streaming = streaming;
        if (streaming)
        {
            report("hackrf_stop_rx", hackrf_stop_rx(dev));
            streaming = false;
        }
        transmitting = true;
        tx_fill = fill;
        tx_fill_user = user;
        rc |= apply_frequency();
        rc |= report("hackrf_set_txvga_gain", hackrf_set_txvga_gain(dev, (uint32_t)tx_gain));
        rc |= report("hackrf_set_amp_enable", hackrf_set_amp_enable(dev, tx_amp ? 1 : 0));
        if (rc == 0)
            rc = report("hackrf_start_tx", hackrf_start_tx(dev, tx_callback, NULL));
        if (rc == 0)
            hackrf_set_antenna_enable(dev, antenna_power ? 1 : 0);
        else
        {
            // could not transmit: go back to receive
            transmitting = false;
            tx_fill = NULL;
            apply_frequency();
            hackrf_set_amp_enable(dev, rf_amp ? 1 : 0);
            if (rx_was_streaming && hackrf_start_rx(dev, rx_callback, NULL) == HACKRF_SUCCESS)
                streaming = true;
        }
    }
    else
    {
        report("hackrf_stop_tx", hackrf_stop_tx(dev));
        transmitting = false;
        tx_fill = NULL;
        tx_fill_user = NULL;
        rc |= apply_frequency();
        rc |= report("hackrf_set_amp_enable", hackrf_set_amp_enable(dev, rf_amp ? 1 : 0));
        rc |= report("hackrf_set_lna_gain", hackrf_set_lna_gain(dev, (uint32_t)lna_gain));
        rc |= report("hackrf_set_vga_gain", hackrf_set_vga_gain(dev, (uint32_t)vga_gain));
        if (rx_was_streaming)
        {
            device_flush();
            if (report("hackrf_start_rx", hackrf_start_rx(dev, rx_callback, NULL)) == HACKRF_SUCCESS)
            {
                streaming = true;
                hackrf_set_antenna_enable(dev, antenna_power ? 1 : 0);
            }
        }
    }
    pthread_mutex_unlock(&dev_lock);
    fprintf(stderr, "hackrf: %s  LO %lld\n", on ? "transmit" : "receive", lo_frequency);
    return rc;
} // end device_set_mox


bool device_is_transmitting(void)
{
    return transmitting;
} // end device_is_transmitting


int device_set_tx_frequency(long long f)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    tx_frequency = f;
    if (transmitting)
        rc = apply_frequency();
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_tx_frequency


int device_set_tx_gain(int db)
{
    int rc = 0;

    if (db < 0)
        db = 0;
    if (db > 47)
        db = 47;
    pthread_mutex_lock(&dev_lock);
    tx_gain = db;
    if (dev != NULL && transmitting)
        rc = report("hackrf_set_txvga_gain", hackrf_set_txvga_gain(dev, (uint32_t)db));
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_tx_gain


int device_set_tx_amp(bool on)
{
    int rc = 0;

    pthread_mutex_lock(&dev_lock);
    tx_amp = on;
    if (dev != NULL && transmitting)
        rc = report("hackrf_set_amp_enable", hackrf_set_amp_enable(dev, on ? 1 : 0));
    pthread_mutex_unlock(&dev_lock);
    return rc;
} // end device_set_tx_amp
