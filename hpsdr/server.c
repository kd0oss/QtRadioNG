#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <getopt.h>
#include <unistd.h>
#include <math.h>
#include <semaphore.h>
#include <signal.h>
#else // Windows
#include "pthread.h"
#include <winsock.h>
#include "getopt.h"
#endif

#include "server.h"
#include "radio.h"
#include "discovered.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "hermes.h"


RECEIVER *receiver[MAX_RECEIVERS];
TRANSMITTER *transmitter; // FIXME: change to allow multiple attached transmitters

static RFUNIT rfunit[35]; // max channels defined in dspserver

TAILQ_HEAD(, _txiq_entry) txiq_buffer;

static sem_t txiq_semaphore[MAX_DEVICES];
static sem_t client_semaphore;

static pthread_t txiq_id;
static pthread_t tx_thread_id;
static pthread_t ka_thread_id;

static _Bool radioStarted[MAX_DEVICES];

static int attached_rcvrs[MAX_DEVICES];
static int attached_xmits[MAX_DEVICES];

static CLIENT client;
//static struct sockaddr_in cli_addr;
//static socklen_t cli_length;
bool send_manifest = false;

static char resp[80];

void* listener_thread(void* arg);
void* client_thread(void* arg);
void* tx_IQ_thread(void* arg);
void* txiq_send(void* arg);
void  init_transmitter(int8_t);
void* keepalive_thread(void* arg);

static char dsp_server_address[17] = "127.0.0.1";


char* attach_receiver(int8_t idx)
{
    DISCOVERED *radio = &discovered[rfunit[idx].radio_id];

    if (rfunit[idx].attached)
    {
        return RECEIVER_IN_USE;
    }

    if (attached_rcvrs[rfunit[idx].radio_id] >= radio->supported_receivers)
    {
        return RECEIVER_INVALID;
    }

    rfunit[idx].attached = true;
    rfunit[idx].isTx = false;
    radio->wideband->channel = rfunit[idx].radio_id;
    receiver[idx]->client = &client;
    attached_rcvrs[rfunit[idx].radio_id]++;
    sprintf(resp,"%s %d", OK, receiver[idx]->sample_rate);
    fprintf(stderr, "Receiver attached: radio id = %u  index = %u\n", rfunit[idx].radio_id, idx);

    return resp;
} // end attach_receiver


char* detach_receiver(int8_t idx)
{
    if (!rfunit[idx].attached)
    {
        return RECEIVER_NOT_ATTACHED;
    }

    // FIXME: this is probably not needed
    if (receiver[idx]->client != &client)
    {
        return RECEIVER_NOT_OWNER;
    }

    if (attached_rcvrs[rfunit[idx].radio_id] > 0) attached_rcvrs[rfunit[idx].radio_id]--;
    if (attached_rcvrs[rfunit[idx].radio_id] == 0)
        receiver[idx]->client = (CLIENT*)NULL;

    close(rfunit[idx].client.socket);
    rfunit[idx].client.socket = -1;
    rfunit[idx].attached = false;
    fprintf(stderr, "Receiver detached: radio id = %u  index = %u\n", rfunit[idx].radio_id, idx);
    return OK;
} // end detach_receiver


char* detach_transmitter(int8_t idx)
{
    if (!rfunit[idx].attached)
    {
        return TRANSMITTER_NOT_ATTACHED;
    }

    fprintf(stderr, "Attempting to detach transmitter...\n");
    if (attached_xmits[rfunit[idx].radio_id] > 0)
    {
        attached_xmits[rfunit[idx].radio_id]--;

        pthread_join(txiq_id, NULL);
        pthread_join(tx_thread_id, NULL);

        struct _txiq_entry *item;
        sem_wait(&txiq_semaphore[idx]);
        for (item = TAILQ_FIRST(&txiq_buffer); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            TAILQ_REMOVE(&txiq_buffer, item, entries);
            free(item->buffer);
            free(item);
        }
        sem_post(&txiq_semaphore[idx]);
        close(rfunit[idx].client.socket);
        rfunit[idx].client.socket = -1;
        rfunit[idx].attached = false;
        fprintf(stderr, "Transmitter detached: radio id = %u  index = %u\n", rfunit[idx].radio_id, idx);
    }
    return OK;
} // end detach_transmitter


char* attach_transmitter(int8_t idx)
{
    if (rfunit[idx].attached)
        return OK;

    rfunit[idx].attached = true;

    sprintf(resp, "%s", OK);
    rfunit[idx].isTx = true;
    attached_xmits[rfunit[idx].radio_id]++;
    fprintf(stderr, "Transmitter attached: radio id = %u  index = %u  Attached XMTRS = %d\n", rfunit[idx].radio_id, idx, attached_xmits[rfunit[idx].radio_id]);
    return resp;
} // end attach_transmitter


char* parse_command(CLIENT* client, char* command)
{
    _Bool  bDone = false;
    int8_t index = command[0];

    fprintf(stderr, "parse_command(idx): %u [%u] %u\n", (uint8_t)command[0], (uint8_t)command[1], (uint8_t)command[2]);

    if (attached_rcvrs[0] <= 0 && (uint8_t)command[1] < HQHARDWARE && (uint8_t)command[1] != STARTRADIO && (uint8_t)command[1] != STOPRADIO)
        return INVALID_COMMAND; // No valid receivers so abort commmand.

    switch ((uint8_t)command[1])
    {
    case STARTRADIO:
        bDone = true;
        if (!radioStarted[(int8_t)command[2]])
        {
            radio = &discovered[(int8_t)command[2]]; // FIXME: radio may need to be an array
            start_radio((int8_t)command[2]);
            start_receivers((int8_t)command[2]);
            radioStarted[(int8_t)command[2]] = true;
        }
        break;

    case STOPRADIO:
        bDone = true;
        if (radioStarted[(int8_t)command[2]])
        {
            attached_xmits[(int8_t)command[2]] = 0;
            attached_rcvrs[(int8_t)command[2]] = 0;
            main_delete((int8_t)command[2]);
            radioStarted[(int8_t)command[2]] = false;
        }
        break;

    case HATTACHRX:
    {
        bDone = true;
        uint8_t radio_id = (uint8_t)command[2];
        rfunit[index].radio_id = radio_id;
        init_receiver(index);
        return attach_receiver(index);
    }
        break;

    case HATTACHTX:
    {
        bDone = true;
        uint8_t radio_id = (uint8_t)command[2];
        rfunit[index].radio_id = radio_id;
        attach_transmitter(index);
        usleep(5000);
        init_transmitter(index);
        return OK;
    }
        break;

    case HDETACH:
    {
        bDone = true;
        uint8_t radio_id = (uint8_t)command[2];
        fprintf(stderr, "Detach Rid: %u  idx: %d\n", radio_id, index);
        if (rfunit[index].isTx)
            fprintf(stderr, "%s\n", detach_transmitter(index));
        else
            fprintf(stderr, "%s\n", detach_receiver(index));
    }
        break;

    case HQHARDWARE:
    {
     //   strcpy(command, "OK Hermes");
        fprintf(stderr, "Sending manifest.\n");
        send_manifest = true;
        bDone = true;
        return 0;
    }
        break;

    case SETPREAMP:
    {
        preamp_cb(index, (int)command[2]);
        bDone = true;
    }
        break;

    case SETDITHER:
    {
        dither_cb(index, (int)command[2]);
        bDone = true;
    }
        break;

    case SETRANDOM:
    {
        random_cb(index, (int)command[2]);
        bDone = true;
    }
        break;

    case SETPOWEROUT:
    {
        set_tx_power((int8_t)command[2]);
        bDone = true;
    }
        break;

    case SETMICBOOST:
    {
        mic_boost_cb((int)command[2]);
        bDone = true;
    }
        break;

    case SETRXANT:
    {
        set_alex_rx_antenna(index, (int)command[2]);
        bDone = true;
    }
        break;

    case HSETSAMPLERATE:
    {
        fprintf(stderr, "Setting sample rate.\n");
        long int r = 0;
        bDone = true;
        sscanf((const char*)(command+2), "%ld", &r);
        receiver_change_sample_rate(receiver[index], r);
    }
        break;

    case HSETFREQ:
    {
        // set frequency
        bDone = true;
        long long f = atol(command+2);
        setFrequency(index, f);
    }
        break;

    case HMOX:
    {
        bDone = true;
        if (rfunit[index].attached)
        {
            if ((uint8_t)command[2] == mox) return INVALID_COMMAND;
            if ((int8_t)command[2] == 0 || (int8_t)command[2] == 1)
            {
                rfunit[index].mox = (uint8_t)command[2];
                mox = rfunit[index].mox;
                fprintf(stderr, "MOX received: %d\n", mox);
                if (protocol == NEW_PROTOCOL)
                {
                    schedule_high_priority();
                    schedule_receive_specific();
                    if (!mox)
                        new_protocol_flush_iq_samples();
                    else // prime FIFO
                    {
                        struct _txiq_entry *item;
                        sem_wait(&txiq_semaphore[index]);
                        for (unsigned int i=0;i<3;i++)
                        {
                            item = malloc(sizeof(*item));
                            item->buffer = malloc(512);
                            memset(item->buffer, 0, 512);
                            TAILQ_INSERT_TAIL(&txiq_buffer, item, entries);
                        }
                        sem_post(&txiq_semaphore[index]);
                    }
                }
                return OK;
            }
            else
            {
                return INVALID_COMMAND;
            }
        }
        else
        {
            return TRANSMITTER_NOT_ATTACHED;
        }
    }
        break;

    case SETLINEIN:
    {
        bDone = true;
        if ((unsigned char)command[1] == 0 || (unsigned char)command[1] == 1)
            //////////         ozy_set_hermes_linein((unsigned char)command[1]);
            ;
        else
            return INVALID_COMMAND;
    }
        break;

    case SETLINEINGAIN:
    {
        bDone = true;
 //       int a = atoi(command+1);
 //       linein_gain_cb(a);
        ////////        ozy_set_hermes_lineingain((unsigned char)atoi(command+1));
    }
        break;

    case SETTXRELAY:
    {
        bDone = true;
        set_alex_tx_antenna((int)command[2]);
        ////////       ozy_set_alex_tx_relay((unsigned int)command[1]);
    }
        break;

    case SETOCOUTPUT:
    {
        bDone = true;
        /////////       ozy_set_open_collector_outputs((int)command[1]);
    }
        break;

    case HSTARGETSERIAL:
    {
        static char buf[50];
        bDone = true;
        ///////////     snprintf(buf, sizeof(buf), "OK %s\"- firmware %d\"", metis_ip_address(0), ozy_get_hermes_sw_ver());
        return buf;
    }
        break;

    case GETADCOVERFLOW:
    {
        static char buf[50];
        bDone = true;
        /////////snprintf(buf, sizeof(buf), "OK %d", ozy_get_adc_overflow());
        return buf;
    }
        break;

    case SETATTENUATOR:
    {
        bDone = true;
        int a = atoi(command+2);
    //    adc[0].attenuation = -a;
        set_alex_attenuation(index, a);
        fprintf(stderr, "Att: %d\n", a);
        return OK;
    }
        break;

    case HSETRECORD:
    {
        bDone = true;
        if ((unsigned char)command[0] == 0 || (unsigned char)command[0] == 1)
        {
            if ((unsigned char)command[2] == 1)
                ////////                ozy_set_record("hpsdr.iq");
                ;
            else
                if ((unsigned char)command[2] == 0)
                    ////////               ozy_stop_record();
                    ;
                else
                {
                    // invalid command string
                    return INVALID_COMMAND;
                }
        }
        else
        {
            // invalid command string
            return INVALID_COMMAND;
        }
    }
        break;

    case HSTARTIQ:
    {
        bDone = true;
    //    client->iq_port = atoi(command+1);
        /*
        if (pthread_create(&receiver[client->receiver].audio_thread_id, NULL, audio_thread, client) != 0)
        {
            fprintf(stderr, "failed to create audio thread for rx %d\n", client->receiver);
            return FAILED;
 //           exit(1);
        }
        else
            fprintf(stderr, "started iq on port: %d\n", client->iq_port);
            */
    }
        break;

    case HSTOPIQ:
    {
        bDone = true;
//        client->iq_port = -1;
    }
        break;

    case HSTARTBANDSCOPE:
    {
        bDone = true;
//        client->bs_port = atoi(command+1);
        /////       attach_bandscope(client);
    }
        break;

    case HSTOPBANDSCOPE:
    {
        bDone = true;
//        client->bs_port = -1;
        //////     detach_bandscope(client);
    }
        break;

    default:
        bDone = false;
        break;
    } // switch

    if (bDone)
        return OK;

    command[0] = 0;
    return INVALID_COMMAND;
} // parse_command


void create_client_thread(char *dsp_server_addr)
{
    pthread_t thread_id;
    int rc;

    strcpy(dsp_server_address , dsp_server_addr);

    // create the thread to make TCP connection to dspsever
    rc = pthread_create(&thread_id, NULL, client_thread, NULL);
    if (rc < 0)
    {
        perror("pthread_create client_thread failed");
        exit(1);
    }
}

/* Main client command thread to dspserver */
void* client_thread(void* arg)
{
//    CLIENT* client=(CLIENT*)arg;
    CLIENT* client;
    char command[64];
    int bytes_read;
    char* response;
    char byte[1];
    int8_t len = 0;
    bool packet_started = false;
    int csocket, rc, on, i = -1;
    struct sockaddr_in address;
    socklen_t command_length = sizeof(address);

    for (int i=0;i<35;i++) // max channels as defined dspserver
        rfunit[i].client.socket = -1;

    TAILQ_INIT(&txiq_buffer);

    sem_init(&client_semaphore, 0, 1);

    for (int i=0;i<MAX_DEVICES;i++)
    {
        radioStarted[i] = false;
        attached_rcvrs[i] = 0;
        attached_xmits[i] = 0;
        sem_init(&txiq_semaphore[i], 0, 1);
    }

    csocket = socket(AF_INET, SOCK_STREAM, 0);
    if (csocket < 0)
    {
        perror("Open socket failed.");
        exit(1);
    }

    setsockopt(csocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    fcntl(csocket, F_SETFL, O_NONBLOCK);  // set to non-blocking

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(dsp_server_address);
    address.sin_port = htons(COMMAND_PORT);

    // connect
    rc = connect(csocket, (struct sockaddr*)&address, command_length);
    if (rc < 0 && errno != EINPROGRESS)
    {
        perror("command channel connect to DSP server failed.");
        exit(1);
    }

    client = malloc(sizeof(CLIENT));
    client->socket = csocket;
    client->iq_length = command_length;
    client->iq_addr = address;
/*
    rc = pthread_create(&ka_thread_id, NULL, keepalive_thread, (void*)(intptr_t)client->socket);
    if (rc < 0)
    {
        perror("pthread_create keep-alive failed");
        exit(1);
    }
*/
    fprintf(stderr, "command channel connected: %s:%d\n", inet_ntoa(client->iq_addr.sin_addr), ntohs(client->iq_addr.sin_port));

    while (1)  // To infinity and beyond!
    {
//        command[0] = -1;
        sem_wait(&client_semaphore);
        bytes_read = recv(client->socket, byte, 1, 0);
        if (bytes_read < 0)
        {  // recv error, exit program.
            if (errno != EWOULDBLOCK)
            {
                sem_post(&client_semaphore);
                fprintf(stderr, "command channel error: %s  (%d)\n", strerror(errno), errno);
                break;
            }
        }
        sem_post(&client_semaphore);

        if (bytes_read == 0) // || command[0] < 0)
        {
            usleep(1000);
            continue;
        }
//fprintf(stderr,"%u ", (uint8_t)byte[0]);
        if (packet_started)
        {
            if (i == -1)
            {
                len = (int8_t)byte[0];
                i++;
            }
            else
            {
                command[i++] = (char)byte[0];
            }
            if (i < len)
                continue;
        }

        if (byte[0] == 0x7f && !packet_started)
        {
            fprintf(stderr, "packet started\n");
            i = -1;
            packet_started = true;
            continue;
        }
        if (!packet_started) continue;

        packet_started = false;

        response = parse_command(client, command);
        if (send_manifest)
        {
            int len = strlen(discovered_xml)+1;
            fprintf(stderr, "XML Len: %d\n", len);
            sem_wait(&client_semaphore);
            send(client->socket, (char*)&len, sizeof(int), 0);
            send(client->socket, discovered_xml, len, 0);
            sem_post(&client_semaphore);
            send_manifest = false;
        }
        else
        {
            sem_wait(&client_semaphore);
            send(client->socket, response, strlen(response), 0);
            sem_post(&client_semaphore);
            if (bytes_read > 0)
                fprintf(stderr,"response(idx): '%s'  '%u'\n", response, (uint8_t)command[1]);
        }
        command[0] = -1;
    } // while

// Should only get here on recv error.

#ifdef __linux__
    close(csocket);
#else
    closesocket(csocket);
#endif
    main_delete(0);

    fprintf(stderr, "client disconnected: %s:%d\n\n\n", inet_ntoa(client->iq_addr.sin_addr), ntohs(client->iq_addr.sin_port));

    free(client);
    exit(1);
    return 0;
} // end client_thread


void init_receiver(int8_t idx)
{
    int on = 1;
    struct sockaddr_in cli_addr;

    rfunit[idx].client.socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rfunit[idx].client.socket < 0)
    {
        fprintf(stderr, "create rx_iq_socket failed for iq_socket: %d\n", idx);
        exit(1);
    }

    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 10;
    setsockopt(rfunit[idx].client.socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);
    setsockopt(rfunit[idx].client.socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    rfunit[idx].client.iq_length = sizeof(cli_addr);
    memset(&rfunit[idx].client.iq_addr, 0, rfunit[idx].client.iq_length);
    rfunit[idx].client.iq_addr.sin_family = AF_INET;
    rfunit[idx].client.iq_addr.sin_addr.s_addr = inet_addr(dsp_server_address);
    rfunit[idx].client.iq_addr.sin_port = htons(RX_IQ_PORT_0 + idx);
    rfunit[idx].port = RX_IQ_PORT_0 + idx;
    fprintf(stderr, "Setup Rx%d IQ port: %d  Bandscope port: %d\n", idx, rfunit[idx].port, BANDSCOPE_PORT + rfunit[idx].radio_id);
} // end init_receivers


/* Send RX IQ data to dspserver. */
void send_IQ_buffer(int idx)
{
    struct sockaddr_in cli_addr;
    socklen_t cli_length;
    unsigned short offset = 0;
    BUFFERL buffer;
    int rc;
//if (idx != 1) return;
    if (rfunit[idx].client.socket > -1)
    {
        // send the IQ buffer
    //    fprintf(stderr, "Send to client: Rid: %d  rx: %d\n", rfunit[idx].radio_id, idx);
        cli_length = sizeof(cli_addr);
        memset((char*)&cli_addr, 0, cli_length);
        cli_addr.sin_family = AF_INET;
        cli_addr.sin_addr.s_addr = rfunit[idx].client.iq_addr.sin_addr.s_addr;
        cli_addr.sin_port = htons(rfunit[idx].port);

        buffer.radio_id = rfunit[idx].radio_id;
        buffer.receiver = idx;
        buffer.length = (receiver[idx]->buffer_size * 2) - offset; // FIXME: offset may not be needed
        if (buffer.length > 2048) buffer.length = 2048;

        memcpy((char*)buffer.data, (char*)receiver[idx]->iq_input_buffer, buffer.length*8);
        rc = sendto(rfunit[idx].client.socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, cli_length);
        if (rc <= 0)
        {
            fprintf(stderr, "sendto failed for rx iq data: Rx%d ret=%d\n", idx, rc);
            exit(1);
        }
    }
} // end send_IQ_buffer


/* Send Wideband IQ data to dspserver. */
void send_WB_IQ_buffer(int idx)
{
    struct sockaddr_in cli_addr;
    socklen_t cli_length;
    BUFFERWB buffer;
    int rc;
    WIDEBAND *w = discovered[idx].wideband;

    if (rfunit[idx].client.socket > -1)
    {
        // send the WB IQ buffer
    //    printf("Send to client: %d  on port: %d\n", rx, rfunit[idx].client.bs_port);
        cli_length = sizeof(cli_addr);
        memset((char*)&cli_addr, 0, cli_length);
        cli_addr.sin_family = AF_INET;
        cli_addr.sin_addr.s_addr = rfunit[idx].client.iq_addr.sin_addr.s_addr;
        cli_addr.sin_port = htons(BANDSCOPE_PORT + rfunit[idx].radio_id);

        buffer.radio_id = rfunit[idx].radio_id;
        buffer.receiver = idx;
        buffer.length = 16384;

        memcpy((char*)buffer.data, (char*)w->input_buffer, buffer.length*2);
        rc = sendto(rfunit[idx].client.socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, cli_length);
        if (rc <= 0)
        {
            fprintf(stderr, "sendto failed for wb iq data\n");
            exit(1);
        }
    }
} // end send_WB_IQ_buffer


void init_transmitter(int8_t idx)
{
    int rc;

    // create thread that sends TX IQ stream to radio
    rc = pthread_create(&txiq_id, NULL, txiq_send, (void*)(intptr_t)idx);
    if (rc < 0)
    {
        perror("pthread_create txiq_send thread failed");
        exit(1);
    }

    // create thread that receives TX IQ stream from dspserver
    rc = pthread_create(&tx_thread_id, NULL, tx_IQ_thread, (void*)(intptr_t)idx);
    if (rc < 0)
    {
        perror("pthread_create mic_IQ_thread failed");
        exit(1);
    }
    fprintf(stderr, "TX threads created.\n");
} // end init_transmitter


/* Send MIC audio to dspserver. */
void send_Mic_buffer(float sample)
{
    // FIXME: may need to be changed to allow multiple attached transmitters
    struct sockaddr_in cli_addr;
    socklen_t cli_length;
    int mic_socket;
    static int count = 0;
    static MIC_BUFFER buffer;
    int rc;

    buffer.data[count++] = sample;
    if (count < 512) return;

    updateTx(transmitter);
    buffer.fwd_pwr = (float)transmitter->fwd;
    buffer.rev_pwr = (float)transmitter->rev;
//    fprintf(stderr, "F: %2.2f  R: %2.2f\n", buffer.fwd_pwr, buffer.rev_pwr);

    count = 0;
    // send the Mic buffer
//       printf("Send to client: %d\n", rx);
    if ((mic_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    cli_length = sizeof(cli_addr);
    memset((char*)&cli_addr, 0, cli_length);
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = inet_addr(dsp_server_address);
    cli_addr.sin_port = htons(MIC_AUDIO_PORT); // + rfunit[0].radio_id);

    buffer.radio_id = rfunit[0].radio_id;
    buffer.tx = 0;
    buffer.length = 512 * sizeof(float);

    if (mic_socket > -1)
    {
        rc = sendto(mic_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, cli_length);
        if (rc <= 0)
        {
            fprintf(stderr, "sendto failed for mic data on port %u", MIC_AUDIO_PORT);
            exit(1);
        }
    }
    close(mic_socket);
} // end send_Mic_buffer


/* Send TX IQ to radio */
void* txiq_send(void* arg)
{
    struct _txiq_entry *item;
    int8_t idx = (intptr_t)arg;
    int old_state, old_type;
    double buffer[64];
    long isample;
    long qsample;
    double gain;
    bool iqswap = false;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);

    switch (protocol)
    {
       case ORIGINAL_PROTOCOL:
         gain = 32767.0;  // 16 bit
         break;
       case NEW_PROTOCOL:
         gain = 8388607.0; // 24 bit
         break;
    }

    while (attached_xmits[rfunit[idx].radio_id] > 0)
    {
        double is, qs;

        sem_wait(&txiq_semaphore[idx]);
        item = TAILQ_FIRST(&txiq_buffer);
        sem_post(&txiq_semaphore[idx]);
        if (item == NULL)
        {
            usleep(100);
            continue;
        }

        memcpy((char*)buffer, (char*)item->buffer, 512);
        for (int j=0; j<32; j++)
        {
            if (iqswap)
            {
                qs = buffer[j*2];
                is = buffer[(j*2)+1];
            }
            else
            {
                is = buffer[j*2];
                qs = buffer[(j*2)+1];
            }

            isample = is >= 0.0?(long)floor(is*gain+0.5):(long)ceil(is*gain-0.5);
            qsample = qs >= 0.0?(long)floor(qs*gain+0.5):(long)ceil(qs*gain-0.5);

            switch (protocol)
            {
            case ORIGINAL_PROTOCOL:
                old_protocol_iq_samples(isample, qsample);
                break;
            case NEW_PROTOCOL:
                new_protocol_iq_samples(isample, qsample);
                break;
            }
        }
        sem_wait(&txiq_semaphore[idx]);
        TAILQ_REMOVE(&txiq_buffer, item, entries);
        sem_post(&txiq_semaphore[idx]);
        free(item->buffer);
        free(item);
    }
    fprintf(stderr, "txiq_send thread closed.\n");
    return NULL;
} // end txiq_send


/* Receive TX IQ data from dspserver */
void* tx_IQ_thread(void* arg)
{
    struct sockaddr_in cli_addr;
    struct _txiq_entry *item;
    socklen_t cli_length;
    int8_t idx = (intptr_t)arg;
    int old_state, old_type;
    int bytes_read;
    BUFFER buffer;
    char buf[2];

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);

    if (rfunit[idx].client.socket == -1)
    {
        rfunit[idx].client.socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (rfunit[idx].client.socket < 0)
        {
            fprintf(stderr, "create tx_iq_socket failed for iq_socket: %d\n", idx);
            exit(1);
        }
        struct timeval read_timeout;
        read_timeout.tv_sec = 0;
        read_timeout.tv_usec = 10;
        setsockopt(rfunit[idx].client.socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

        cli_length = sizeof(cli_addr);
        memset((char*)&cli_addr, 0, cli_length);
        cli_addr.sin_family = AF_INET;
        cli_addr.sin_addr.s_addr = inet_addr(dsp_server_address);
        cli_addr.sin_port = htons(TX_IQ_PORT_0); // + attached_xmits[idx] - 1);

        fprintf(stderr, "connection to TX index %u tx IQ on port %d\n", idx, TX_IQ_PORT_0);
    }

    // this will setup UDP port from dspserver. FIXME: I don't like this method as it can hang on the dspserver side.
    sendto(rfunit[idx].client.socket, (char*)&buf, sizeof(buf), 0, (struct sockaddr*)&cli_addr, cli_length);
    sleep(2);
    sendto(rfunit[idx].client.socket, (char*)&buf, sizeof(buf), 0, (struct sockaddr*)&cli_addr, cli_length);
    fprintf(stderr, "sent TX sync bytes to dspserver\n");

    while (attached_xmits[rfunit[idx].radio_id] > 0)
    {
        if (rfunit[idx].client.socket > -1)
        {
            // get TX IQ from DSP server
            bytes_read = recvfrom(rfunit[idx].client.socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, &cli_length);
            if (bytes_read < 0)
            {
             //   perror("recvfrom socket failed for tx IQ buffer");
                continue;
            }
            if (bytes_read > 30)
            {
                item = malloc(sizeof(*item));
                item->buffer = malloc(512);
                memcpy(item->buffer, (char*)&buffer.data, 512);
                sem_wait(&txiq_semaphore[idx]);
                TAILQ_INSERT_TAIL(&txiq_buffer, item, entries);
                sem_post(&txiq_semaphore[idx]);
            }
        }
    }
    fprintf(stderr, "tx iq thread closed.\n");
    return NULL;
} // end tx_IQ_thread


void* keepalive_thread(void* arg)
{
    int socket = (intptr_t)arg;
    int bytes_read = 0;
    char command[13], response[64];

    sleep(10);
    sprintf(command, "keepalive 0");
    while (1)
    {
        sem_wait(&client_semaphore);
        if (send(socket, response, strlen(response), 0) < 0)
        {
            sem_post(&client_semaphore);
            fprintf(stderr, "keepalive failed, client vanished\n");
            exit(1);
        }

        bytes_read = recv(socket, command, 13, 0);
        if (bytes_read < 0)
        {  // recv error, exit program.
            if (errno != EWOULDBLOCK)
            {
                sem_post(&client_semaphore);
                fprintf(stderr, "command channel error: %s  (%d)\n", strerror(errno), errno);
                exit(1);
            }
        }
        sem_post(&client_semaphore);

        if (bytes_read > 0)
        {
            if (strcmp(command, "OK") != 0)
            {
                fprintf(stderr, "command channel error\n");
                exit(1);
            }
        }
        sleep(5);
    }
} // end keepalive_thread
