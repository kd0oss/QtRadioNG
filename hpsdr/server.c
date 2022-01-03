#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
TRANSMITTER *transmitter;

TAILQ_HEAD(, _txiq_entry) txiq_buffer;
static sem_t txiq_semaphore;
static pthread_t txiq_id;
static pthread_t tx_thread_id;

static int attached_rcvrs = 0;
static int attached_xmits = 0;

static CLIENT iqclient[MAX_RECEIVERS];
static struct sockaddr_in cli_addr;
static int cli_length;
bool send_manifest = false;

static char resp[80];

void* listener_thread(void* arg);
void* client_thread(void* arg);
void* tx_IQ_thread(void* arg);
void* txiq_send(void* arg);
void  init_transmitter(unsigned int, int);

static char dsp_server_address[17] = "127.0.0.1";


char* attach_receiver(int radio_id, int rx, CLIENT* client)
{
    DISCOVERED *d = &discovered[radio_id];

    if (client->receiver_state == RECEIVER_ATTACHED)
    {
        return CLIENT_ATTACHED;
    }

    if (rx - 1 >= d->supported_receivers)
    {
        return RECEIVER_INVALID;
    }

 //   if (receiver[rx]->client != (CLIENT *)NULL)
 //   {
 //       return RECEIVER_IN_USE;
//    }

    client->receiver_state = RECEIVER_ATTACHED;
    client->radio_id = radio_id;
    d->wideband->channel = radio_id;
    client->receiver = rx;
    init_receivers(radio_id, rx);
    receiver[rx]->client = client;

    sprintf(resp,"%s %d", OK, receiver[rx]->sample_rate);
    attached_rcvrs++;

    return resp;
} // end attach_receiver


char* detach_receiver(int radio_id, int rx, CLIENT* client)
{
    if (client->receiver_state == RECEIVER_DETACHED)
    {
        return CLIENT_DETACHED;
    }

    if (rx >= active_receivers)
    {
        return RECEIVER_INVALID;
    }

    if (receiver[rx]->client != client)
    {
        return RECEIVER_NOT_OWNER;
    }

    client->receiver_state = RECEIVER_DETACHED;
    receiver[rx]->client = (CLIENT*)NULL;
    if (attached_rcvrs > 0) attached_rcvrs--;

    //FIX-ME: Handle transmitter cleanup in a better way.
    if (attached_xmits > 0 && attached_rcvrs == 0)
    {
        //pthread_kill(tx_thread_id, SIGKILL);
        attached_xmits = 0;
        struct _txiq_entry *item;
        fprintf(stderr, "TX Detach...\n");
        sem_wait(&txiq_semaphore);
        for (item = TAILQ_FIRST(&txiq_buffer); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            TAILQ_REMOVE(&txiq_buffer, item, entries);
            free(item->buffer);
            free(item);
        }
        sem_post(&txiq_semaphore);
    }
    fprintf(stderr, "Receiver detached: radio id = %d  rx = %d\n", radio_id, rx);
    return OK;
} // end detach_receiver


char* attach_transmitter(CLIENT* client)
{
    if (client->receiver_state != RECEIVER_ATTACHED)
    {
        return RECEIVER_NOT_ATTACHED;
    }

//    if (client->receiver != 0)
//    {
//        return RECEIVER_NOT_ZERO;
//    }

    client->transmitter_state = TRANSMITTER_ATTACHED;

    sprintf(resp, "%s", OK);
    attached_xmits++;
    sem_init(&txiq_semaphore, 0, 1);
    TAILQ_INIT(&txiq_buffer);
    init_transmitter(client->radio_id, client->receiver);
    return resp;
} // end attach_transmitter


char* parse_command(CLIENT* client, char* command)
{
    _Bool  bDone = false;

    fprintf(stderr, "parse_command(Rx%d): [%02X] %u %u\n", client->receiver, (unsigned char)command[0], (unsigned int)command[1], (unsigned int)command[2]);

    if (attached_rcvrs <= 0 && (unsigned char)command[0] < HQHARDWARE) return INVALID_COMMAND; // No valid receivers so abort commmand.

    switch ((unsigned char)command[0])
    {
    case HATTACH:
    {
        bDone = true;
        command[0] = 0;
        if ((unsigned char)command[1] == HTX)
        {
            return attach_transmitter(client);
        }
        else
        {
            int rx = (int)command[1];
            radio_id = (short int)command[2];
            radio = &discovered[radio_id];
            start_radio(radio_id);  // change this FIX-ME
            fprintf(stderr, "Receiver attached: radio id = %d  rx = %d\n", radio_id, rx);
            return attach_receiver(radio_id, rx, client);
        }
    }
        break;

    case HDETACH:
    {
        bDone = true;
        command[0] = 0;
        int rx = (int)command[1];
        radio_id = (short int)command[2];
        fprintf(stderr, "Detach Rid: %d  Rx: %d\n", radio_id, rx);
        fprintf(stderr, "%s\n", detach_receiver(radio_id, rx, client));
        if (attached_rcvrs == 0)
            main_delete(radio_id);
    }
        break;

    case HQHARDWARE:
    {
     //   strcpy(command, "OK Hermes");
        fprintf(stderr, "Sending manifest.\n");
        command[0] = 0;
        send_manifest = true;
        bDone = true;
        return 0;
    }
        break;

    case SETPREAMP:
    {
        preamp_cb((int)command[1]);
        command[0] = 0;
        bDone = true;
    }
        break;

    case SETDITHER:
    {
        dither_cb((int)command[1]);
        command[0] = 0;
        bDone = true;
    }
        break;

    case SETRANDOM:
    {
        random_cb((int)command[1]);
        command[0] = 0;
        bDone = true;
    }
        break;

    case SETPOWEROUT:
    {
        set_tx_power((int)command[1]);
        command[0] = 0;
        bDone = true;
    }
        break;

    case SETMICBOOST:
    {
        mic_boost_cb((int)command[1]);
        command[0] = 0;
        bDone = true;
    }
        break;

    case SETRXANT:
    {
        set_alex_rx_antenna((int)command[1]);
        command[0] = 0;
        bDone = true;
    }
        break;

    case HSETSAMPLERATE:
    {
        fprintf(stderr, "Setting sample rate.\n");
        int rx = 0;
        long int r = 0;
        bDone = true;
        command[0] = 0;
        sscanf((const char*)(command+1), "%d %ld", &rx, &r);
        receiver_change_sample_rate(receiver[rx], r);
    }
        break;

    case HSETFREQ:
    {
        // set frequency
        bDone = true;
        command[0] = 0;
        long long f = atol(command+2);
        setFrequency(command[1], f);
    }
        break;

    case HMOX:
    {
        bDone = true;
        if (client->transmitter_state == TRANSMITTER_ATTACHED)
        {
            command[0] = 0;
            if ((int)command[1] == mox) return INVALID_COMMAND;
            if ((unsigned char)command[1] == 0 || (unsigned char)command[1] == 1)
            {
                client->mox = (int)command[1];
                mox = client->mox;
                fprintf(stderr, "MOX received: %d\n", client->mox);
                if (protocol == NEW_PROTOCOL)
                {
                    schedule_high_priority();
                    schedule_receive_specific();
                    if (!mox)
                        new_protocol_flush_iq_samples();
                    else // prime FIFO
                    {
                        struct _txiq_entry *item;
                        sem_wait(&txiq_semaphore);
                        for (unsigned int i=0;i<3;i++)
                        {
                            item = malloc(sizeof(*item));
                            item->buffer = malloc(512);
                            memset(item->buffer, 0, 512);
                            TAILQ_INSERT_TAIL(&txiq_buffer, item, entries);
                        }
                        sem_post(&txiq_semaphore);
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
        command[0] = 0;
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
        command[0] = 0;
 //       int a = atoi(command+1);
 //       linein_gain_cb(a);
        ////////        ozy_set_hermes_lineingain((unsigned char)atoi(command+1));
    }
        break;

    case SETTXRELAY:
    {
        command[0] = 0;
        bDone = true;
        set_alex_tx_antenna((int)command[1]);
        ////////       ozy_set_alex_tx_relay((unsigned int)command[1]);
    }
        break;

    case SETOCOUTPUT:
    {
        command[0] = 0;
        bDone = true;
        /////////       ozy_set_open_collector_outputs((int)command[1]);
    }
        break;

    case HSTARGETSERIAL:
    {
        static char buf[50];
        command[0] = 0;
        bDone = true;
        ///////////     snprintf(buf, sizeof(buf), "OK %s\"- firmware %d\"", metis_ip_address(0), ozy_get_hermes_sw_ver());
        command[0] = 0;
        return buf;
    }
        break;

    case GETADCOVERFLOW:
    {
        static char buf[50];
        command[0] = 0;
        bDone = true;
        /////////snprintf(buf, sizeof(buf), "OK %d", ozy_get_adc_overflow());
        return buf;
    }
        break;

    case SETATTENUATOR:
    {
        command[0] = 0;
        bDone = true;
        int a = atoi(command+1);
    //    adc[0].attenuation = -a;
        set_alex_attenuation(0, a);
        fprintf(stderr, "Att: %d\n", a);
        return OK;
    }
        break;

    case HSETRECORD:
    {
        bDone = true;
        command[0] = 0;
        if ((unsigned char)command[1] == 0 || (unsigned char)command[1] == 1)
        {
            if ((unsigned char)command[1] == 1)
                ////////                ozy_set_record("hpsdr.iq");
                ;
            else
                if ((unsigned char)command[1] == 0)
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
        command[0] = 0;
        client->iq_port = atoi(command+1);
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
        command[0] = 0;
        client->iq_port = -1;
    }
        break;

    case HSTARTBANDSCOPE:
    {
        bDone = true;
        command[0] = 0;
        client->bs_port = atoi(command+1);
        /////       attach_bandscope(client);
    }
        break;

    case HSTOPBANDSCOPE:
    {
        bDone = true;
        command[0] = 0;
        client->bs_port = -1;
        //////     detach_bandscope(client);
    }
        break;

    default:
        bDone = false;
        break;
    }

    if (bDone)
        return OK;

    command[0] = 0;
    return INVALID_COMMAND;
} // parse_command


void create_listener_thread(char *dsp_server_addr)
{
    pthread_t thread_id;
    int rc;

    strcpy(dsp_server_address , dsp_server_addr);

    // create the thread to listen for TCP connections
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
    char command[80];
    int bytes_read;
    char* response;
    int s, rc, on;
    struct sockaddr_in address;
    socklen_t command_length = sizeof(address);

    for (int i=0;i<MAX_RECEIVERS;i++)
        iqclient[i].socket = NULL;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        perror("Open socket failed.");
        exit(1);
    }

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(dsp_server_address);
    address.sin_port = htons(COMMAND_PORT);

    // connect
    rc = connect(s, (struct sockaddr*)&address, command_length);
    if (rc < 0)
    {
        perror("command channel connect to DSP server failed.");
        exit(1);
    }

    client = malloc(sizeof(CLIENT));
    client->socket = s;
    client->iq_length = command_length;
    client->iq_addr = address;
    client->iq_port = -1;
    client->bs_port = -1;
    client->audio_port = -1;

    client->receiver_state = RECEIVER_DETACHED;
    client->transmitter_state = TRANSMITTER_DETACHED;
    client->receiver = -1;
    client->mox = 0;

    fprintf(stderr, "command channel connected: %s:%d\n", inet_ntoa(client->iq_addr.sin_addr), ntohs(client->iq_addr.sin_port));

    while (1)
    {
        bytes_read = recv(client->socket, command, sizeof(command), 0);
        if (bytes_read < 0)
        {
            break;
        }
        if (command[0] == 0)
        {
            fprintf(stderr, "Command byte 0\n");
   //         continue;
        }
        command[bytes_read] = 0;
        response = parse_command(client, command);
        if (send_manifest)
        {
            int len = strlen(discovered_xml)+1;
            fprintf(stderr, "XML Len: %d\n", len);
            send(client->socket, (char*)&len, sizeof(int), 0);
            send(client->socket, discovered_xml, strlen(discovered_xml)+1, 0);
            send_manifest = false;
        }
        else
        {
            if (command[0] != 0)
                send(client->socket, command, strlen(command), 0);
            else
                send(client->socket, response, strlen(response), 0);
            if (bytes_read > 0)
                fprintf(stderr,"response(Rx%d): '%s'  '%s'\n", client->receiver, response, command);
        }
    }

    if (client->receiver_state == RECEIVER_ATTACHED)
    {
  //      receiver[client->receiver]->client = (CLIENT*)NULL;
    //    client->receiver_state = RECEIVER_DETACHED;
       detach_receiver(client->radio_id, client->receiver, client);
    }

    if (client->transmitter_state == TRANSMITTER_ATTACHED)
    {
        client->transmitter_state = TRANSMITTER_DETACHED;
    }

    client->mox = 0;
    client->bs_port = -1;
    //    detach_bandscope(client);

#ifdef __linux__
    close(client->socket);
#else
    closesocket(client->socket);
#endif
    main_delete(radio_id);

    fprintf(stderr, "client disconnected: %s:%d\n\n\n", inet_ntoa(client->iq_addr.sin_addr), ntohs(client->iq_addr.sin_port));

    free(client);
    exit(1);
    return 0;
} // end client_thread


void init_receivers(int radio_id, int rx)
{
    start_receivers(radio_id);

    for (int i=0;i<active_receivers;i++)
    {
        iqclient[i].socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (iqclient[i].socket < 0)
        {
            fprintf(stderr, "create rx_iq_socket failed for iq_socket: %d\n", i);
            exit(1);
        }

        struct timeval read_timeout;
        read_timeout.tv_sec = 0;
        read_timeout.tv_usec = 10;
        setsockopt(iqclient[i].socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

        iqclient[i].iq_length = sizeof(cli_addr);
        memset(&iqclient[i].iq_addr, 0, iqclient[i].iq_length);
        iqclient[i].iq_addr.sin_family = AF_INET;
        iqclient[i].iq_addr.sin_addr.s_addr = inet_addr(dsp_server_address);
        iqclient[i].iq_addr.sin_port = htons(RX_IQ_PORT_0 + i);
        iqclient[i].iq_port = RX_IQ_PORT_0 + i;
        iqclient[i].bs_port = BANDSCOPE_PORT + radio_id;
        fprintf(stderr, "Setup Rx%d IQ port: %d  Bandscope port: %d\n", i, iqclient[i].iq_port, iqclient[i].bs_port);
    }
} // end init_receivers


void init_transmitter(unsigned int radio_id, int rx)
{
    int rc;

    rc = pthread_create(&txiq_id, NULL, txiq_send, NULL);
    if (rc < 0)
    {
        perror("pthread_create txiq_send thread failed");
        exit(1);
    }
    rc = pthread_create(&tx_thread_id, NULL, tx_IQ_thread, (void*)rx);
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
    struct sockaddr_in cli_addr;
    int cli_length;
    int mic_socket;
    static int count = 0;
    static MIC_BUFFER buffer;
    int rc;

    buffer.data[count++] = sample;
    if (count < 512) return;

    updateTx(transmitter);
    buffer.fwd_pwr = (float)transmitter->fwd;
    buffer.rev_pwr = (float)transmitter->rev;
 //   fprintf(stderr, "F: %2.2f  R: %2.2f\n", buffer.fwd_pwr, buffer.rev_pwr);

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
    cli_addr.sin_port = htons(MIC_AUDIO_PORT + radio_id);

    buffer.radio_id = radio_id;
    buffer.tx = 0;
    buffer.length = 512 * sizeof(float);

    if (iqclient[0].socket != -1)
    {
        rc = sendto(mic_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, cli_length);
        if (rc <= 0)
        {
            fprintf(stderr, "sendto failed for mic data on port %d", MIC_AUDIO_PORT + radio_id);
            exit(1);
        }
    }
    close(mic_socket);
} // end send_Mic_buffer


/* Send RX IQ data to dspserver. */
void send_IQ_buffer(int rx)
{
    struct sockaddr_in cli_addr;
    int cli_length;
    unsigned short offset = 0;
    BUFFERL buffer;
    int rc;
fprintf(stderr, "IQ send on rc: %d\n", rx);
    if (iqclient[rx].socket == NULL) return;
    if (iqclient[rx].socket != -1)
    {
        // send the IQ buffer
     //   fprintf(stderr, "Send to client: Rid: %d  rx: %d\n", radio_id, rx);
        cli_length = sizeof(cli_addr);
        memset((char*)&cli_addr, 0, cli_length);
        cli_addr.sin_family = AF_INET;
        cli_addr.sin_addr.s_addr = iqclient[rx].iq_addr.sin_addr.s_addr;
        cli_addr.sin_port = htons(iqclient[rx].iq_port);

        buffer.radio_id = radio_id;
        buffer.receiver = rx;
        buffer.length = (receiver[rx]->buffer_size * 2) - offset;
        if (buffer.length > 2048) buffer.length = 2048;

        memcpy((char*)buffer.data, (char*)receiver[rx]->iq_input_buffer, buffer.length*8);
        rc = sendto(iqclient[rx].socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, cli_length);
        if (rc <= 0)
        {
            fprintf(stderr, "sendto failed for rx iq data: Rx%d ret=%d\n", rx, rc);
            exit(1);
        }
    }
} // end send_IQ_buffer


/* Send Wideband IQ data to dspserver. */
void send_WB_IQ_buffer(int rx)
{
    struct sockaddr_in cli_addr;
    int cli_length;
    unsigned short offset = 0;
    BUFFERWB buffer;
    int rc;
    WIDEBAND *w = discovered[rx].wideband;

    if (iqclient[rx].socket == NULL) return;
    if (iqclient[rx].socket != -1)
    {
        // send the WB IQ buffer
    //    printf("Send to client: %d  on port: %d\n", rx, iqclient[rx].bs_port);
        cli_length = sizeof(cli_addr);
        memset((char*)&cli_addr, 0, cli_length);
        cli_addr.sin_family = AF_INET;
        cli_addr.sin_addr.s_addr = iqclient[rx].iq_addr.sin_addr.s_addr;
        cli_addr.sin_port = htons(iqclient[rx].bs_port);

        buffer.radio_id = radio_id;
        buffer.receiver = rx;
        buffer.length = 16384;
      //  if (buffer.length > 2048) buffer.length = 2048;

        memcpy((char*)buffer.data, (char*)w->input_buffer, buffer.length*2);
        rc = sendto(iqclient[rx].socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, cli_length);
        if (rc <= 0)
        {
            fprintf(stderr, "sendto failed for wb iq data\n");
            exit(1);
        }
    }
} // end send_WB_IQ_buffer


/* Send TX IQ to radio */
void* txiq_send(void* arg)
{
    struct _txiq_entry *item;
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

    while (attached_xmits)
    {
        double is, qs;

        sem_wait(&txiq_semaphore);
        item = TAILQ_FIRST(&txiq_buffer);
        sem_post(&txiq_semaphore);
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
            } else
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
        sem_wait(&txiq_semaphore);
        TAILQ_REMOVE(&txiq_buffer, item, entries);
        sem_post(&txiq_semaphore);
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
    int cli_length;
    int rx = (int)arg;
    int old_state, old_type;
    int bytes_read;
    BUFFER buffer;
    char buf[2];

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);

    if (iqclient[rx].socket != -1)
    {
        cli_length = sizeof(cli_addr);
        memset((char*)&cli_addr, 0, cli_length);
        cli_addr.sin_family = AF_INET;
        cli_addr.sin_addr.s_addr = iqclient[rx].iq_addr.sin_addr.s_addr;
        cli_addr.sin_port = htons(TX_IQ_PORT_0 + radio_id);

        fprintf(stderr, "connection to rx %d tx IQ on port %d\n", rx, TX_IQ_PORT_0 + radio_id);
    }
    sendto(iqclient[rx].socket, (char*)&buf, sizeof(buf), 0, (struct sockaddr*)&cli_addr, cli_length);

    while (attached_xmits)
    {
        if (iqclient[rx].socket != -1)
        {
            // get audio from DSP server
            bytes_read = recvfrom(iqclient[rx].socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&cli_addr, &cli_length);
            if (bytes_read < 0)
            {
             //   perror("recvfrom socket failed for tx IQ buffer");
            //    exit(1);
            }
            if (bytes_read > 30)
            {
                item = malloc(sizeof(*item));
                item->buffer = malloc(512);
                memcpy(item->buffer, (char*)&buffer.data, 512);
                sem_wait(&txiq_semaphore);
                TAILQ_INSERT_TAIL(&txiq_buffer, item, entries);
                sem_post(&txiq_semaphore);
            }
        }
    }
    fprintf(stderr, "tx iq thread closed.\n");
    return NULL;
} // end tx_IQ_thread
