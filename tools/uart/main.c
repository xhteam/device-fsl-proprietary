#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/types.h>

#include <cutils/sockets.h>
#include <cutils/properties.h>

#define PROP_IDR_PORT "ro.idr.port"
#define PROP_IDR_RATE "ro.idr.rate"

typedef struct uart_context{
	char    devname[256];
	int 	fd;
	int 	control[2];

	struct termios oldtio,tio;

	
	pthread_t	thread;
}uart_context;
/* commands sent to the uart thread */
enum state_thread_cmd {
	CMD_QUIT  = 0,
	CMD_START = 1,
	CMD_STOP  = 2,
	CMD_SEND  = 3
};

uart_context gCTX;
uint32_t scan_interval = 100;


void dump(const char *prefix,const unsigned char *data ,int size)
{
#define isprint(c)	(c>='!'&&c<='~')
//((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
	char* ptr;
	static char digits[2048] = {0};
	int i, j;	
	unsigned char *buf = (unsigned char*)data;
	fprintf(stderr,"%s[%d]\n",
			   prefix?prefix:" ", size);

	
	for (i=0; i<size; i+=16) 
	{
	  ptr = &digits[0];
	  ptr+=sprintf(ptr,"%06x: ",i);
	  for (j=0; j<16; j++) 
		if (i+j < size)
		 ptr+=sprintf(ptr,"%02x ",buf[i+j]);
		else
		 ptr+=sprintf(ptr,"%s","   ");

	  ptr+=sprintf(ptr,"%s","  ");
		
	  for (j=0; j<16; j++) 
		if (i+j < size)			
			ptr+=sprintf(ptr,"%c",isprint(buf[i+j]) ? buf[i+j] : '.');
	  *ptr='\0';
	  fprintf(stderr,"%s\n",digits);
	}
}

static int epoll_register( int  epoll_fd, int  fd ) {
	struct epoll_event  ev;
	int                 ret, flags;

	/* important: make the fd non-blocking */
	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	ev.events  = EPOLLIN;
	ev.data.fd = fd;
	do {
		ret = epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &ev );
	} while (ret < 0 && errno == EINTR);
	return ret;
}

static int epoll_deregister( int  epoll_fd, int  fd ) {
	int  ret;
	do {
		ret = epoll_ctl( epoll_fd, EPOLL_CTL_DEL, fd, NULL );
	} while (ret < 0 && errno == EINTR);
	return ret;
}


static int write_attr_file(const char* path,int val)
{
	int fd,nwr;
	char value[20]; 			
	fd = open(path, O_RDWR);
 	if(fd<0) return -1;
	
	nwr = sprintf(value, "%d\n", val);
	write(fd, value, nwr);
	close(fd);		

	return 0;

}


static void module_power(int on){
#define ATTR_POWER  "/sys/class/gpio/gpio45/value"
    fprintf(stderr,"module state -> power %s \n",(on>0)?"on":"off");
    if(on){
        write_attr_file(ATTR_POWER,1);
    }else{
        write_attr_file(ATTR_POWER,0);    
    }
}

/*
 *fork thread to handle read .
*/
void * uart_thread(void *args){
	uart_context* ctx = (uart_context*)args;	
	int epoll_fd   = epoll_create(2);
	int datafd     = ctx->fd;
	int control_fd = ctx->control[1];	
	int started    = 0;
	int ret_length=0;
	unsigned char  buff[1024];

	
	// register control file descriptors for polling
	epoll_register( epoll_fd, control_fd );
	if (datafd > -1) {
		epoll_register( epoll_fd, datafd );
	}

		// now loop
	for (;;) {
		struct epoll_event   events[2];
		int                  ne, nevents;

		nevents = epoll_wait( epoll_fd, events, datafd>-1 ? 2 : 1,
			scan_interval);
		if (nevents < 0) {
			if (errno != EINTR)
				printf("epoll_wait() unexpected error: %s", strerror(errno));
			continue;
		}else if(0==nevents){
			if(ret_length){
				dump("<",buff,ret_length);
				ret_length=0;
			}
			
		}
		for (ne = 0; ne < nevents; ne++) {
			if ((events[ne].events & (EPOLLERR|EPOLLHUP)) != 0) {
				fprintf(stderr,"EPOLLERR or EPOLLHUP after epoll_wait() !?");
				goto Exit;
			}
			if ((events[ne].events & EPOLLIN) != 0) {
				int  fd = events[ne].data.fd;

				if (fd == control_fd) {
					int   ret;
					int   i;
					char  buf[255];
					int   update_handled = 0;
					memset(buf, 0xff, 255);
					do {
						ret = read(fd, buf, 255);
					} while (ret < 0 && errno == EINTR);

					for (i = 0; i < ret && buf[i] != 0xff; i++) {
						char cmd = buf[i];
						if (cmd == CMD_QUIT) {
							goto Exit;
						} else if (cmd == CMD_START) {
							if (!started) {
								started = 1;
								//module_power(started);
							    }
						} else if (cmd == CMD_STOP) {
							if (started) {
								started = 0;
								//module_power(started);
							    
							}
						} else if (cmd == CMD_SEND) {

							//ignore this
							
						}
					}
				} else if (fd == datafd) {

					for (;;) {
						int  nn, ret;

						ret = read( fd, buff+ret_length, sizeof(buff)-ret_length );
						if (ret <= 0) {
							break;
							if (errno == EINTR)
								continue;
							if (errno != EWOULDBLOCK){
								printf("ERROR ERROR ERROR\n");
								break;
							}
	
						}else {
							ret_length+=ret;
							break;
						}
					}
					
				} else {
					fprintf(stderr,"epoll_wait() returned unkown fd %d ?", fd);
				}
			}
		}

		
	}
Exit:

	return NULL;
}

static void _state_thread_ctl(uart_context* ctx, enum state_thread_cmd val) {
	char cmd = (char)val;
	int ret;

	do { ret=write( ctx->control[0], &cmd, 1 ); }
	while (ret < 0 && errno == EINTR);

	if (ret != 1)
		fprintf(stderr,"%s: could not send command %d: ret=%d: %s", __func__, val, ret,
			strerror(errno));
	usleep(50000);
}

static void program_exit(int signum){
	uart_context* ctx = &gCTX;
	int fd = ctx->fd;
	//do some clean work here
	if(fd>=0){
		
		tcflush (fd, TCIOFLUSH);
		tcsetattr (fd, TCSANOW, &ctx->oldtio);
		close(fd);	
	}
}

static void usage(const char* program){
    fprintf(stderr, 
        "Usage: %s [-p </dev/ttySx>] [-b <baudrate>] \n", 
        program);
    exit(-1);
}

/* for debugging */
#define STATUS(format, args...) \
   printf("here: %d. ", __LINE__); printf(format, ## args); printf("\n"); fflush(stdout);


/* currently building the argc/argv stuff in a global context */
#define ARGV_MAX  255
#define ARGV_TOKEN_MAX  255
int    _argc;
char  *_argv[ARGV_MAX];
char  *_argv_token;

/* initialize empty argc/argv struct */
void
argv_init()
{
   _argc = 0;
   if ((_argv_token = calloc(ARGV_TOKEN_MAX, sizeof(char))) == NULL)
      fprintf(stderr, "argv_init: failed to calloc");
   bzero(_argv_token, ARGV_TOKEN_MAX * sizeof(char));
}
void
argv_dispose()
{
	while(_argc>0){
		free(_argv[_argc-1]);
		_argc--;
	}
	free(_argv_token);
}


/* add a character to the current token */
void
argv_token_addch(int c)
{
   int n;

   n = strlen(_argv_token);
   if (n == ARGV_TOKEN_MAX - 1)
       fprintf(stderr, "argv_token_addch: reached max token length (%d)", ARGV_TOKEN_MAX);

   _argv_token[n] = c;
}

/* finish the current token: copy it into _argv and setup next token */
void
argv_token_finish()
{
   if (_argc == ARGV_MAX)
      fprintf(stderr, "argv_token_finish: reached max argv length (%d)", ARGV_MAX);

/*STATUS("finishing token: '%s'\n", _argv_token);*/
   _argv[_argc++] = _argv_token;
   if ((_argv_token = calloc(ARGV_TOKEN_MAX, sizeof(char))) == NULL)
       fprintf(stderr,  "argv_token_finish: failed to calloc");
   bzero(_argv_token, ARGV_TOKEN_MAX * sizeof(char));
}

/* main parser */
void
str2argv(char *s)
{
   bool in_token;
   bool in_container;
   bool escaped;
   char container_start;
   char c;
   int  len;
   int  i;

   container_start = 0;
   in_token = false;
   in_container = false;
   escaped = false;

   len = strlen(s);

   argv_init();

   for (i = 0; i < len; i++) {
      c = s[i];

      switch (c) {
         /* handle whitespace */
         case ' ':
         case '\t':
         case '\n':
            if (!in_token)
               continue;

            if (in_container) {
               argv_token_addch(c);
               continue;
            }

            if (escaped) {
               escaped = false;
               argv_token_addch(c);
               continue;
            }

            /* if reached here, we're at end of token */
            in_token = false;
            argv_token_finish();
            break;

         /* handle quotes */
         case '\'':
         case '\"':

            if (escaped) {
               argv_token_addch(c);
               escaped = false;
               continue;
            }

            if (!in_token) {
               in_token = true;
               in_container = true;
               container_start = c;
               continue;
            }

            if (in_container) {
               if (c == container_start) {
                  in_container = false;
                  in_token = false;
                  argv_token_finish();
                  continue;
               } else {
                  argv_token_addch(c);
                  continue;
               }
            }

            /* XXX in this case, we:
             *    1. have a quote
             *    2. are in a token
             *    3. and not in a container
             * e.g.
             *    hell"o
             *
             * what's done here appears shell-dependent,
             * but overall, it's an error.... i *think*
             */
            printf("Parse Error! Bad quotes\n");
            break;

         case '\\':

            if (in_container && s[i+1] != container_start) {
               argv_token_addch(c);
               continue;
            }

            if (escaped) {
               argv_token_addch(c);
               continue;
            }

            escaped = true;
            break;

         default:
            if (!in_token) {
               in_token = true;
            }

            argv_token_addch(c);
      }
   }

   if(in_token){   	
		in_token = false;
		argv_token_finish();
   	}
   if (in_container)
      printf("Parse Error! Still in container\n");

   if (escaped)
      printf("Parse Error! Unused escape (\\)\n");
}

int m_slotstatus(uart_context* ctx){
	unsigned char p[]={0x65, 0x00, 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x65};
	int l = 11;
	dump(">",p,l);
	write(ctx->fd,p,l);	
	return 0;
}
int m_iccpwr(uart_context* ctx,int on){
	unsigned char iccon[]={0x62,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x60};
	unsigned char iccoff[]={0x63,0x00,0x00,0x00,0x00,0x00,0x13,0x00,0x00,0x00,0x70};
	int l;
	if(on){
		l= sizeof(iccon)/sizeof(iccon[0]);
		dump(">",iccon,l);
		write(ctx->fd,iccon,l);	
	}else{
		l= sizeof(iccoff)/sizeof(iccoff[0]);
		dump(">",iccoff,l);
		write(ctx->fd,iccoff,l);	
	}
	return 0;
}




int uart_cli_loop(uart_context* ctx){
	static char cmd[1024]; 
	static char buf[2048];
	int exit=0;
    int ret;
	int started = 0;
	int cmd_len,i;	
	int err;
	char* cmdptr;
    while (!exit) {
        // Display the usage
        printf("\n*******************************************\n");
        printf("Commands:\n");
        printf("Quit         ---quit\n");        
        printf("Power        ---toggle icc reader power\n");                
        printf("Write <...>  ---write data array to uart\n");
        printf("SlotStatus   ---slot status\n");  
        printf("*******************************************\n\n");

		//print prompt
		printf(">");
        // accept the command
        memset(cmd,0,sizeof(cmd));
		cmdptr = gets(cmd);
		cmd_len = strlen(cmd); 
		if(!cmdptr||!cmd_len) {
			fflush(0);
			continue;
		}
		for (i=0; i<cmd_len;i++)
			cmd[i] = tolower(cmd[i]); 

		str2argv(cmd);
		if(_argc) {
			if(!strncmp(_argv[0],"quit",1)){
	            printf("Quit...\n");
				_state_thread_ctl(ctx,CMD_QUIT);
	            exit=1;
	        }else if(!strncmp(_argv[0],"power",1)){
        	
	        	printf("power %s \n",started?"off":"on");
				if(started){
					started = 0;
					//gps_state_thread_ctl(ctx,CMD_STOP);
				}else {
					started = 1;				
					//gps_state_thread_ctl(ctx,CMD_START);
				}						
				m_iccpwr(ctx,started);
	        }else if(!strncmp(_argv[0],"write",1)){
	        	int i= 0;
				int v,r;
	        	unsigned char buf[1024];
				int argc  = _argc;
				if(argc>=2){
					argc--;
					while(i<argc){
						sscanf(_argv[1+i],"%x",&v);
						buf[i] = v;
						i++;
					}
					dump(">",buf,argc);
					r=write(ctx->fd,buf,argc);	            
		            fprintf(stderr,"write uart ret[%d]\n",r);
				}else {
					printf("usage:w <...>\n");
				}
        	}else if(!strncmp(_argv[0],"slo",3)){
	        	m_slotstatus(ctx);
	        }
				
       	}

		argv_dispose();

    }

	program_exit(0);
    return 0;
	
}

static int my_config(int fd,int baudrate,int datebites,int stopbits,int parity)
{
	int status;
	struct termios set_port;
	struct termios old_port;
	if(tcgetattr(fd,&old_port) != 0)//得到机器源端口的默认设置
	{
		printf("tcgetattr error!\n");
		return -1;
	}  

	printf("my_config baudrate[%d],databits:%d,stopbits:%d,parity:%c\n",baudrate,datebites,stopbits,parity);
	memset(&set_port,0,sizeof(set_port));  
    set_port.c_cflag |= CLOCAL | CREAD;//激活CLOCAL，CREAD用于本地连接和接收使能
    tcflush(fd,TCIOFLUSH);
        switch(baudrate)
    {
    
		case 1200:
			//tcflush(fd,TCIOFLUSH);//刷新输入输出缓冲
			cfsetispeed(&set_port,B1200);//分别设置输入和输出速率
			cfsetospeed(&set_port,B1200);
		break;		
		case 2400:
			//tcflush(fd,TCIOFLUSH);//刷新输入输出缓冲
			cfsetispeed(&set_port,B2400);//分别设置输入和输出速率
			cfsetospeed(&set_port,B2400);
		break;
		case 4800:
            //tcflush(fd,TCIOFLUSH);//刷新输入输出缓冲
            cfsetispeed(&set_port,B4800);//分别设置输入和输出速率
            cfsetospeed(&set_port,B4800);
            break;
         
        case 9600:
            //tcflush(fd,TCIOFLUSH);//刷新输入输出缓冲
            cfsetispeed(&set_port,B9600);//分别设置输入和输出速率
            cfsetospeed(&set_port,B9600);
            break;
        case 19200:
            //tcflush(fd,TCIOFLUSH);
            cfsetispeed(&set_port,B19200);
            cfsetospeed(&set_port,B19200);
            break;
		case 38400:
            //tcflush(fd,TCIOFLUSH);//刷新输入输出缓冲
            cfsetispeed(&set_port,B38400);//分别设置输入和输出速率
            cfsetospeed(&set_port,B38400);
            break;
         default:
            cfsetispeed(&set_port,B19200);
            cfsetospeed(&set_port,B19200);
            printf("default baudrate set to 19200\n");
            break;
    }
        
        if( tcsetattr(fd,TCSANOW,&set_port) != 0 )
          {
        perror("can't set the speed!\n");
        return -1;
    }
    tcflush(fd,TCIOFLUSH);
    /*设置比特率结束*/
        /*********设置数据位**********/
    set_port.c_cflag &= ~CSIZE;
    switch(datebites)
    {    
        case 7:
            set_port.c_cflag |= CS7;
            break;
        case 8:
            set_port.c_cflag |= CS8;
            break;
                default:
                        //set_port.c_cflag |=CS8;
            printf("please input the datebites. 7 or 8\n");
                return -1;    
    }
    tcflush(fd , TCIOFLUSH);
    /********设置数据位结束*******/
        /*********设置校验位*********/
        switch(parity)
    {
        case 'n':                 /*无校验*/
        case 'N':
            set_port.c_cflag &= ~PARENB;
            set_port.c_iflag &= ~INPCK;
            break;
        case 'o':                 /*奇检验*/
        case 'O':
            set_port.c_cflag |= (PARODD | PARENB);
            set_port.c_iflag |= INPCK;
            break;
        case 'e':                 /*偶校验*/
        case 'E':
            set_port.c_cflag |= PARENB;
            set_port.c_cflag &= ~PARODD;
            set_port.c_iflag |= INPCK;
            break;
        case 's':                /*Space校验*/
        case 'S':
            set_port.c_cflag &= ~PARENB;
            set_port.c_cflag &= ~CSTOPB;
            break;
        default:
            //set_port.c_cflag |= PARENB;
            //set_port.c_cflag &= ~PARODD;
            printf("please input the right parity\n");
            return -1;
      
    }
    if(parity != 'n')
    set_port.c_iflag |= INPCK;
    tcflush(fd, TCIOFLUSH); 
    /**********停止位***********/
        switch(stopbits)
    {
        case 1:
            set_port.c_cflag &= ~CSTOPB;
            break;
        case 2:
            set_port.c_cflag |= CSTOPB;
            break;
        default:
            printf("please input the right stopbites. 1 or 2\n");
            return -1;
    }
    tcflush(fd, TCIOFLUSH); 
    //设置停止位结束
       
        /*关掉ICRNL和IXON功能，使能接受二机制字符*/
    set_port.c_iflag &= ~(ICRNL | IXON);
    /*设置看控制时间*/
    set_port.c_lflag &=~ICANON;//设置串口为原始模式，在原始模式下下面两个字段才有效
    set_port.c_cc[VTIME]=0;
    set_port.c_cc[VMIN]=0;
        tcflush(fd,TCIOFLUSH);
    //刷新输入缓存或者输出缓存TCIFLUSH输入队列，TCIOFLUSH输入输出队列
    if(tcsetattr(fd,TCSANOW,&set_port)!=0)//立刻将设置写道串口中去
    {
        perror("com set error!\n");//设置错误
        return -1;
    }
        //tcflush(fd,TCIOFLUSH);
    return 0;

}
		


int main(int argc,char** argv){		
	char value[PROPERTY_VALUE_MAX];	
	char uart_dev_name[32];	
	uart_context* ctx = &gCTX;
    int ret=0;
    int i=0;
    char* port="/dev/ttyS1";
    int baudrate = 115200;
	int databits = 8;
	int stopbits = 1;
	int parity = 0;
	int fd;

	long BAUD;                      // derived baud rate from command line
	long DATABITS;
	long STOPBITS;
	long PARITYON;
	long PARITY;
	

    while (++i < argc){
        if (!strcmp(argv[i],"-p") || !strcmp(argv[i],"--port")){
            if (++i >= argc) goto fail;
            port = argv[i];
            continue;
        }
      
        if (!strcmp(argv[i],"-b") ){
            if (++i >= argc) goto fail;
            baudrate = atoi(argv[i]);
            continue;
        }
        
        fail:
            usage(argv[0]);
    }

	memset(ctx,0,sizeof(*ctx));
	ctx->fd = -1;
	//get port
	property_get(PROP_IDR_PORT, value, port);
	sprintf(uart_dev_name, "%s", value);

	sprintf(ctx->devname,"%s",uart_dev_name);
	#if 1
	fd = open(uart_dev_name, O_RDWR  | O_NOCTTY );

	if(fd<0)
		return fd;	

	#if 0
	// Get the current options for the port...
	tcgetattr(fd, &ctx->oldtio);
	memcpy(&ctx->tio,&ctx->oldtio,sizeof(struct termios));

	switch (baudrate)
	{
		 case 921600:
		 	BAUD=B921600;
			break;
		 case 460800:
		 	BAUD=B460800;
			break;
		 case 230400:
		 	BAUD=B230400;
			break;
		 case 115200:
		 	BAUD=B115200;
			break;
		 case 57600:
		 	BAUD = B57600;
			break;
         case 38400:
            BAUD = B38400;
            break;
         case 19200:
            BAUD  = B19200;
            break;
         case 9600:
		 default:
            BAUD  = B9600;
            break;
         case 4800:
            BAUD  = B4800;
            break;
         case 2400:
            BAUD  = B2400;
            break;
         case 1800:
            BAUD  = B1800;
            break;
         case 1200:
            BAUD  = B1200;
            break;
         case 600:
            BAUD  = B600;
            break;
         case 300:
            BAUD  = B300;
            break;
         case 200:
            BAUD  = B200;
            break;
         case 150:
            BAUD  = B150;
            break;
         case 134:
            BAUD  = B134;
            break;
         case 110:
            BAUD  = B110;
            break;
         case 75:
            BAUD  = B75;
            break;
         case 50:
            BAUD  = B50;
            break;
      }  //end of switch baud_rate
      switch (databits)
      {
         case 8:
         default:
            DATABITS = CS8;
            break;
         case 7:
            DATABITS = CS7;
            break;
         case 6:
            DATABITS = CS6;
            break;
         case 5:
            DATABITS = CS5;
            break;
      }  //end of switch data_bits
      switch (stopbits)
      {
         case 1:
         default:
            STOPBITS = 0;
            break;
         case 2:
            STOPBITS = CSTOPB;
            break;
      }  //end of switch stop bits
      switch (parity)
      {
         case 0:
         default:                       //none
            PARITYON = 0;
            PARITY = 0;
            break;
         case 1:                        //odd
            PARITYON = PARENB;
            PARITY = PARODD;
            break;
         case 2:                        //even
            PARITYON = PARENB;
            PARITY = 0;
            break;
      }  //end of switch parity	// Set the baud rates to 9600...
	
	ctx->tio.c_cflag = BAUD|  DATABITS | STOPBITS | PARITYON | PARITY | CLOCAL | CREAD;
	ctx->tio.c_iflag = IGNPAR;//ignore parity,no flow control
	ctx->tio.c_oflag = 0;
	ctx->tio.c_lflag = 0;
//	ctx->tio.c_cc[VTIME] = 1;   /* return after 0.5s */
//	ctx->tio.c_cc[VMIN] = 0;	  /*no blocking */
	tcflush (fd, TCIOFLUSH);
	tcsetattr (fd, TCSANOW, &ctx->tio);
	#endif
	ctx->fd = fd;

	//using alcormicro api 
	my_config(fd,baudrate,databits,stopbits,'N');

    signal(SIGHUP, program_exit);
    signal(SIGINT, program_exit);
    signal(SIGQUIT, program_exit);


	
	if ( socketpair( AF_LOCAL, SOCK_STREAM, 0, ctx->control ) < 0 ) {
		printf("%s: could not create thread control socket pair: %s", __func__,
			strerror(errno));
		goto FAIL;
	}

	
	if((ret = pthread_create(&ctx->thread,NULL,uart_thread, ctx)) != 0)
	{
		printf("pthread_create(): %s\n",strerror(ret));
		goto FAIL;
	}


	//start command loop
	return uart_cli_loop(ctx);
	#else
	return uart_cli_loop(ctx);
	
	#endif
FAIL:
	
	return ret;
}

