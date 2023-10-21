// Link layer protocol implementation

#include "link_layer.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

volatile unsigned alarmEnabled = TRUE;
char serialPort[50];
LinkLayerRole role;
int nRetransmissions;
int timeout;
int fd;
unsigned char frame_index = INF_FRAME_0;
int message_received=0;

enum packet_state state;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    state = START;
    puts("Alarm triggered");
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    strcpy(serialPort, connectionParameters.serialPort);
    role = connectionParameters.role;
    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;

    fd = open(serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPort);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    bzero(&newtio, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    (void)signal(SIGALRM, alarmHandler);

    state = START;

    if (role == LlTx)
    {
        unsigned attempts = nRetransmissions;

        while (attempts--)
        {
            puts("llopen: Sending SET frame");
            if (write_frame(fd, TX_ADD, SET) < 0)
            {
                perror("error: can't write SET packet");
                return 1;
            }

            alarm(timeout);
            alarmEnabled = TRUE;
            state = START;

            puts("llopen: Waiting for UA frame");
            if (read_frame(fd, RX_ADD, UA) == 0)
            {
                puts("llopen: Received UA frame");
                alarm(0);
                alarmEnabled = FALSE;
                state = START;
                return 0;
            }
            puts("llopen: Didn't receive UA frame");
        }
    }
    else if (role == LlRx)
    {
        puts("llopen: Waiting for SET frame");
        while (read_frame(fd, TX_ADD, SET) != 0)
        {
            puts("llopen: Didn't receive SET frame");
        }
        puts("llopen: Received SET frame");

        if (write_frame(fd, RX_ADD, UA) < 0)
        {
            perror("error: can't write UA frame");
            return 1;
        }
        puts("llopen: Sent UA frame");
        return 0;
    }
    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    int frame_size = bufSize + 6;
    unsigned char *frame = (unsigned char *)malloc(frame_size);
    bzero(frame, frame_size);

    unsigned index = 0;

    frame[index++] = FLAG;
    frame[index++] = TX_ADD;
    frame[index++] = frame_index;
    frame[index++] = TX_ADD ^ frame_index;

    for (int i = 0; i < bufSize; i++)
    {
        if (buf[i] == FLAG || buf[i] == ESC)
        {
            frame = (unsigned char *)realloc(frame, ++frame_size);
            frame[index++] = ESC;
            frame[index++] = buf[i] == FLAG ? 0x5E : 0x5D;
        }
        else
        {
            frame[index++] = buf[i];
        }
    }

    frame[index] = buf[0];
    for (int i = 1; i < bufSize; i++)
        frame[index] ^= buf[i];
    frame[++index] = FLAG;

    unsigned attempts = nRetransmissions;
    while (attempts--)
    {
        if (write(fd, frame, frame_size) == -1)
        {
            perror("error: error writting frame");
            return 1;
        }

        alarm(timeout);
        alarmEnabled = TRUE;
        state = START;

        while (alarmEnabled)
        {
            // TODO -> Acabar
        }
    }

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO
    int after_esc = FALSE;
    unsigned char tx_buf, control;
    int packet_size = 0;
    state = START;

    while (state != STOP)
    {
        if (read(fd, &tx_buf, 1) > 0)
        {
            switch (state)
            {
            case START:
                puts("state: START");
                if (tx_buf == FLAG)
                    state = F;
                break;
            case F:
                puts("state: FLAG");
                if (tx_buf == TX_ADD)
                    state = A;
                else if (tx_buf != FLAG)
                    state = START;
                break;
            case A:
                puts("state: ADDRESS");
                if (tx_buf == INF_FRAME_0 || tx_buf == INF_FRAME_1)
                {
                    state = C;
                    control = tx_buf;
                }
                else if(tx_buf == FLAG)
                    state = F;
                else 
                    state = START;
                break;
            case C:
                puts("state: CONTROL");
                if( tx_buf == ((control ^ TX_ADD) & 0xFF))
                    state = DATA;
                else if(tx_buf == FLAG)
                    state = F;
                else 
                    state = START;
                break;
            case DATA:
                puts("state: DATA");
                if(tx_buf == ESC)
                {
                    after_esc=TRUE;
                }
                else if(after_esc)
                {
                    after_esc=FALSE;
                    if(tx_buf == 0x5E)
                        packet[packet_size++] = FLAG;
                    else    
                        packet[packet_size++] = ESC;
                }
                else if(tx_buf == FLAG)
                {
                    unsigned char bcc2= packet[packet_size-1];

                    packet_size-=1;
                    packet[packet_size]='\0';

                    unsigned char read_bcc2=0;

                    for(int i=0; i< packet_size; i++)
                    {
                        read_bcc2 ^= packet[i];
                    }

                    if(read_bcc2 == bcc2)
                    {
                        state=STOP;
                        frame_index = (frame_index == INF_FRAME_0) ? INF_FRAME_1 : INF_FRAME_0;

                        if(write_frame(fd, RX_ADD, frame_index == INF_FRAME_0? RR0 : RR1) < 0)
                        {
                            free(packet);
                            perror("llread: Can't send RR response");
                            return -1;
                        }
                        return packet_size;
                    }
                    if(write_frame(fd,RX_ADD,frame_index == INF_FRAME_0? REJ0: REJ1))
                    {
                        free(packet);
                        perror("llread: Can't send RR response");
                        return -1;
                    }
                    perror("llread: reject message");
                    return -1;
                }
                else 
                    packet[packet_size++] = tx_buf;
                break;
            default:
                break;
            }
        }
    }
    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO
    state = START;

    if (role == LlTx)
    {
        unsigned attemps = nRetransmissions;

        while (attemps--)
        {
            puts("llclose: Sending DISC frame");
            if (write_frame(fd, TX_ADD, DISC) < 0)
            {
                perror("error: Can't write DISC frame");
                return 1;
            }

            alarm(timeout);
            alarmEnabled = TRUE;
            state = START;

            while (alarmEnabled && state != STOP)
            {
                if (!read_frame(fd, RX_ADD, DISC))
                {
                    perror("error:  Can't read DISC frame");
                }
            }

            if (state == STOP)
            {
                alarm(0);
                alarmEnabled = FALSE;
            }
        }
        puts("llclose: Received DISC frame");
        if (write_frame(fd, TX_ADD, UA) < 0)
        {
            perror("error: Can't write UA frame");
            return 1;
        }
        puts("llclose: Sent UA frame");
        return 0;
    }
    else if (role == LlRx)
    {
        perror("llclose: Waiting DISC frame");
        while (state != STOP)
        {
            if (!read_frame(fd, TX_ADD, DISC))
            {
                perror("error: Can't read DISC frame");
            }
        }
        puts("llclose: Recieved DISC frame");
        if (write_frame(fd, RX_ADD, DISC) < 0)
        {
            perror("error : Can't send DISC frame");
            return 1;
        }
        puts("llclose: Sent DISC frame");
        return 0;
    }
    return -1;
}

int read_frame(int fd, unsigned char addr, unsigned char ctrl)
{
    unsigned char rx_buf;

    while (alarmEnabled)
    {
        if (read(fd, &rx_buf, 1) < 0)
        {
            perror("error: error reading frame");
            return 1;
        }
        switch (state)
        {
        case START:
            puts("state: START");
            if (rx_buf == FLAG)
                state = F;
            break;
        case F:
            puts("state: F");
            if (rx_buf == addr)
                state = A;
            else if (rx_buf != F)
                state = START;
            break;
        case A:
            puts("state: A");
            if (rx_buf == ctrl)
                state = C;
            else if (rx_buf == FLAG)
                state = F;
            else
                state = START;
            break;
        case C:
            puts("state: C");
            if ((rx_buf == addr ^ ctrl) & 0xFF)
                state = BCC;
            else if (rx_buf == FLAG)
                state = F;
            else
                state = START;
            break;
        case BCC:
            puts("state: BCC");
            if (rx_buf == FLAG)
                state = STOP;
            else
                state = START;
            break;
        case STOP:
            puts("state: STOP");
            return 0;
        }
    }
    return 1;
}

int write_frame(int fd, unsigned char A, unsigned char C)
{
    unsigned char buf[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, buf, 5);
}