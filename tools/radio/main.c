#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>


static int speed_arr[] = {
	B115200, B57600, B38400, B19200, B9600, B4800, B2400, B1200, B600, B300, 
};

static int name_arr[] = {
	115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200, 600, 300, 
};

int uart_set_speed(int fd, int speed)
{
    int i, status;
    struct termios Opt;

    tcgetattr(fd, &Opt);
    for (i=0; i<sizeof(speed_arr)/sizeof(int); i++)
    {
        if (speed == name_arr[i])
        {
            tcflush(fd, TCIOFLUSH);
            cfsetispeed(&Opt, speed_arr[i]);
            cfsetospeed(&Opt, speed_arr[i]);
            status = tcsetattr(fd, TCSANOW, &Opt);
            if (status != 0) {
				perror("tcsetattr fd1");
            	return -1;
            }
        }
        tcflush(fd,TCIOFLUSH);
    }

	return 0;
}

int uart_set_parity(int fd, int databits, int stopbits, int parity)
{
    struct termios options;
    if (tcgetattr(fd, &options) != 0)
    {
        perror("SetupSerial 1");
        return -1;
    }

    options.c_cflag &= ~CSIZE;
    switch (databits)
    {
        case 7:
            options.c_cflag |= CS7;
            break;
        case 8:
            options.c_cflag |= CS8;
            break;
        default:
            fprintf(stderr,"Unsupported data size\n");
            return -1;
    }

    switch (parity)
    {
        case 'n':
        case 'N':
            options.c_cflag &= ~PARENB;  /* Clear parity enable */
            options.c_iflag &= ~INPCK;   /* Enable parity checking */
        	break;
	    case 'o':
    	case 'O':
    	    options.c_cflag |= (PARODD | PARENB);
    	    options.c_iflag |= INPCK;    /* Disnable parity checking */
    	    break;
    	case 'e':
    	case 'E':
    	    options.c_cflag |= PARENB;   /* Enable parity */
    	    options.c_cflag &= ~PARODD; 
    	    options.c_iflag |= INPCK;    /* Disnable parity checking */
    	    break;
    	case 'S':
    	case 's':  /*as no parity*/
    	    options.c_cflag &= ~PARENB;
    	    options.c_cflag &= ~CSTOPB;
    	    break;
    	default:
    	    fprintf(stderr,"Unsupported parity\n");
    	    return -1;
    }

	switch (stopbits)
  	{
  		case 1:
      		options.c_cflag &= ~CSTOPB;
    		break;
		case 2:
    		options.c_cflag |= CSTOPB;
    		break;
		default:
    		fprintf(stderr,"Unsupported stop bits\n");
    		return -1;
	}

	/* Set input parity option */
	if (parity != 'n') options.c_iflag |= INPCK;
	options.c_cc[VTIME] = 150; // 15 seconds
	options.c_cc[VMIN] = 0;

  	tcflush(fd,TCIFLUSH); /* Update the options and do it NOW */
	if (tcsetattr(fd,TCSANOW,&options) != 0)
  	{
      	perror("SetupSerial 3");
    	return -1;
	}

	return 0;
}

int uart_open(char *dev)
{
	int fd, res;

	fd = open(dev, O_RDWR);
    if (-1 == fd)
    { 
		printf("Can't Open Serial Port!\n");
		return -1;
    }
    res = uart_set_speed(fd, 19200);
    if (res < 0)
	{
        printf("Set Speed Error\n");
		close(fd);
 		return -1;
    }

    res = uart_set_parity(fd, 8, 1, 'N');
    if (res < 0)
    {
        printf("Set Parity Error\n");
		close(fd);
 		return -1;
    }

	return fd;
}

void uart_close(int fd)
{
	close(fd);
}


///////////////////////////////////////////////////

#define DEVNAME  "/dev/ttyUSB0"

int at_fd;
int at_thread_flag = 0;

#define CR_CHAR  0x0A
#define LF_CHAR  0x0A

char lastchar = 0;

void print_command(char *cmd)
{
	int i, len;

	len = strlen(cmd);

	for (i=0; i<len; i++) {
		if (cmd[i] == CR_CHAR || cmd[i] == LF_CHAR)	{
			if (lastchar != CR_CHAR && lastchar != LF_CHAR) { 
				printf("\n");
			}
		} else if (cmd[i] < 0x20 || cmd[i] > 0x7e) {
			if (lastchar == CR_CHAR && lastchar == LF_CHAR) { 
				printf("[RECV][%2d] ", len);
			}
			printf("<0x%02X>", cmd[i]);
		} else {
			printf("%c", cmd[i]);
		}
		lastchar = cmd[i];
	}
}

void single_command(char *cmd)
{
	if (!strncmp(cmd, "^RSSI", 5)) return;

	printf("[RECV] %s\n", cmd);
}


char at_buffer[1024];
int at_len=0;

int at_analyse(char *buff)
{
	char *p1, *p2;
	int len = strlen(buff);

	if (len <= 0)
	{
		return 0;
	}

	p1 = buff;

analyse_cycle:

	// remove the CR & LF char in the front
	for (p1=buff; *p1!=0 && (*p1==CR_CHAR || *p1==LF_CHAR); p1++);
	if (*p1==0) return 0;

	// fine the CR & LF char in the command
	for (p2=p1; *p2!=0 && *p2!=CR_CHAR && *p2!=LF_CHAR; p2++);

	if (*p2==CR_CHAR || *p2==LF_CHAR)
	{
		*p2 = 0;
		single_command(p1);
		p1 = p2++;
        goto analyse_cycle;
	}
	else
	{
		single_command(p1);
	}
	return 0;
}

static void *at_receive_thread(void *param)
{
	int len;
    char buff[10];

	//read(at_fd, buff, 256);
    while(at_thread_flag)
    {
        len = read(at_fd, buff, 10);
		if (len > 0)
        {
            buff[len]='\0';
            printf("%s", buff);
			//at_analyse(buff);
            //print_command(buff);
        }
		usleep(1000*1000);
    }
	return NULL;
}

int at_send_command(char *cmd)
{
	int len;
	len = write(at_fd, cmd, strlen(cmd));
    printf("[SEND][%2d] %s\n", len, cmd);
	return 0;
}


int main(int argc, char **argv)
{
	int res;
              char *dev;
              dev = argv[1];

	pthread_t tid;
    pthread_attr_t attr;

	// Open modem device
    //at_fd = uart_open(DEVNAME);
    at_fd = uart_open(dev);
    if (at_fd <= 0)
    {
        printf("Can't Open Serial Port!\n");
        return -1;
    }
	printf("Open %s successfully\n", DEVNAME);

	// Start receive thread
              /*
	at_thread_flag = 1;
	pthread_attr_init (&attr);
    res = pthread_create(&tid, &attr, at_receive_thread, NULL);
	printf("Start receive thread\n");
	*/
	// send AT
	//at_send_command("AT\r");
	//at_send_command("AT+CMEE?\r");
	//at_send_command("AT+CMEE=?\r");
	//at_send_command("AT+CMEE=2\r");
	//at_send_command("ATE0\r");
	//at_send_command("ATE1\r");
	//at_send_command("ATS3\r");


	// input and send AT
	/*while (1) 
	{
		int len;
		char cmd[128];

		memset(cmd, 0, 128);
		scanf("%s", cmd);
		len = strlen(cmd);
		cmd[len]='\r';
		cmd[len+1]='\0';
		at_send_command(cmd);

		usleep(1000*1000);
	}
              */
    at_send_command("at+cfun=0\r");
    usleep(2000*1000);
    at_send_command("at+cfun=1\r");
    usleep(2000*1000);
    at_send_command("at^ndisdup=1,1\r");
	// Close device
	at_thread_flag = 0;
	usleep(20*1000);
    uart_close(at_fd);
	printf("Close device and done the test\n");

	return 0;
}
