#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/inotify.h>
#include <hardware_legacy/power.h>
#include <cutils/properties.h>

#ifdef HAS_LIBUSB
#include <libusb/libusb.h>
#endif
#include "ril-core.h"
#include "ril-handler.h"
#include "atchannel.h"

static int ad3812_hook(struct ril_hardware* rilhw,int event);
#define ANDROID_WAKE_LOCK_NAME "ril_hardware"

enum {
	kErrNoError=0,
	kErrNoHw=-1,
	kErrAccessServicePort=-2,
	kErrAccessModemPort=-3,
};

static ST_RIL_HARDWARE hw_mapping[] = 
{
	//model,		prefer net, vid,		pid,		service port,		modem port, 	voice port, 	voice format
	{
		.model_name		= "MF210",
		.model 			= kRIL_HW_MF210,
		.prefer_net 	= kPREFER_NETWORK_TYPE_WCDMA,
		.vid 			= 0x19d2,
		.pid			= 0x2003,
		.service_port	= "/dev/ttyUSB1",
		.modem_port		= "/dev/ttyUSB3",
		.voice_port		= "/dev/ttyUSB2",
		.voice_format	= "ulaw",
		.voice_rw_period= "20",
	},
	{
		.model_name 	= "AD3812",
		.alias_name		= "MG3732",
		.model			= kRIL_HW_AD3812,
		.prefer_net 	= kPREFER_NETWORK_TYPE_WCDMA,
		.vid			= 0x19d2,
		.pid			= 0xffeb,
		.service_port	= "/dev/ttyUSB0",
		.modem_port 	= "/dev/ttyUSB3",
		.voice_port 	= "/dev/ttyUSB1",		
		.voice_format	= "raw",
		.voice_rw_period= "20",
		.no_pinonoff	= 1,
		.no_suspend		= 1,
		.hook			= ad3812_hook,
	},
	{
		.model_name 	= "MC2716",
		.model			= kRIL_HW_MC2716,
		.prefer_net 	= kPREFER_NETWORK_TYPE_CDMA_EVDV,
		.vid			= 0x19d2,
		.pid			= 0xffed,
		.service_port	= "/dev/ttyUSB1",
		.modem_port 	= "/dev/ttyUSB0",
		.voice_port 	= "/dev/ttyUSB2",	
		.voice_format	= "raw",
		.voice_rw_period= "20",
	},
	{
		.model_name 	= "M305",
		.model			= kRIL_HW_M305,
		.prefer_net 	= kPREFER_NETWORK_TYPE_TD_SCDMA,
		.vid			= 0x19d2,
		.pid			= 0x1303,
		.service_port	= "/dev/ttyUSB2",
		.modem_port 	= "/dev/ttyUSB0",
		.voice_port 	= "/dev/ttyUSB4",	
		.voice_format	= "raw",
		.voice_rw_period= "20",
	},
	{
		.model_name 	= "MC8630",
		.model			= kRIL_HW_MC8630,
		.prefer_net 	= kPREFER_NETWORK_TYPE_CDMA_EVDV,
		.vid			= 0x19d2,
		.pid			= 0xfffe,
		.service_port	= "/dev/ttyUSB1",
		.modem_port 	= "/dev/ttyUSB0",
		.voice_port 	= "/dev/ttyUSB2",	
		.voice_format	= "raw",
		.voice_rw_period= "20",
		.no_suspend		= 1,
	},
	{
		.model_name 	= "MU509",
		.model			= kRIL_HW_MU509,
		.prefer_net 	= kPREFER_NETWORK_TYPE_WCDMA,
		.vid			= 0x12d1,
		.pid			= 0x1001,
		.service_port	= "/dev/ttyUSB2",
		.modem_port 	= "/dev/ttyUSB0",
		.voice_port 	= "/dev/ttyUSB3",	
		.voice_format	= "raw",
		.voice_rw_period= "20",
	},		
	{
		.model_name 	= "WM630",
		.model			= kRIL_HW_WM630,
		.prefer_net 	= kPREFER_NETWORK_TYPE_WCDMA,
		.vid			= 0x257a,
		.pid			= 0x2601,
		.service_port	= "/dev/ttyUSB2",
		.modem_port 	= "/dev/ttyUSB0",
		.voice_port	= "/dev/ttyUSB1",
		.voice_format	= "raw",
		.voice_rw_period= "20",
	},
	{
		.model_name 	= "SEW290",
		.model		= kRIL_HW_SEW290,
		.prefer_net 	= kPREFER_NETWORK_TYPE_WCDMA,
		.vid		= 0x21f5,
		.pid		= 0x2012,
		.service_port	= "/dev/ttyUSB2",
		.modem_port 	= "/dev/ttyUSB0",
	},
	{
		.model_name 	= "SEV850",
		.model			= kRIL_HW_SEV850,
		.prefer_net 	= kPREFER_NETWORK_TYPE_CDMA_EVDV,
		.vid			= 0x21f5,
		.pid			= 0x2009,
		.service_port	= "/dev/ttyUSB1",
		.modem_port 	= "/dev/ttyUSB0",
		.voice_port		= "/dev/ttyUSB2",
	},
	{
		.model_name 	= "CWM930",
		.model			= kRIL_HW_CWM930,
		.prefer_net 	= kPREFER_NETWORK_TYPE_WCDMA,
		.vid			= 0x257a,
		.pid			= 0x2606,
		.service_port	= "/dev/ttyUSB2",
		.modem_port 	= "/dev/ttyUSB0",
	},
	{
		.model_name 	= "AnyData",
		.model			= kRIL_HW_ANYDATA,
		.prefer_net 	= kPREFER_NETWORK_TYPE_CDMA_EVDV,
		.vid			= 0x16d5,
		.pid			= 0x6502,
		.service_port	= "/dev/ttyUSB2",
		.modem_port 	= "/dev/ttyUSB0",
	},	
	{
		.model_name		= "unknown",
		.model 			= 0,
	},
	
};


static int readfile(const char *path, char *content, size_t size)
{
	int ret;
	FILE *f;
	f = fopen(path, "r");
	if (f == NULL)
		return -1;

	ret = fread(content, 1, size, f);
	fclose(f);
	return ret;
}

static int writeStringToFile( const char* file,const char* str)
{
    int fd = open( file, O_WRONLY );
    if( fd >= 0 ){
		int len = write( fd, str, strlen(str));
		close(fd);
        if( len > 0 ){            
            return 0;
        }
    }
    return -1;
}

static const char *USB_DIR_BASE = "/sys/bus/usb/devices/";
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
static PST_RIL_HARDWARE find_matched_device(PST_RIL_HARDWARE map)
{
	struct dirent *dent;
	DIR *usbdir;
	PST_RIL_HARDWARE device = NULL;
	char *path, *path2;
	char idvendor[64];
	char idproduct[64];
	unsigned long vid,pid;
	int ret, i,j;

	path = malloc(PATH_MAX);
	path2 = malloc(PATH_MAX);
	if (!path || !path2)
		return NULL;

	usbdir = opendir(USB_DIR_BASE);
	if (usbdir == NULL) {
		free(path);
		free(path2);
		return NULL;
	}

	memset(path, 0, PATH_MAX);
	memset(path2, 0, PATH_MAX);

	while ((dent = readdir(usbdir)) != NULL) {
		if (strcmp(dent->d_name, ".") == 0
		    || strcmp(dent->d_name, "..") == 0)
			continue;
		memset(idvendor, 0, sizeof(idvendor));
		memset(idproduct, 0, sizeof(idproduct));
		path = strcpy(path, USB_DIR_BASE);
		path = strcat(path, dent->d_name);
		strcpy(path2, path);
		path = strcat(path, "/idVendor");
		path2 = strcat(path2, "/idProduct");

		ret = readfile(path, idvendor, 4);
		if (ret <= 0)
			continue;
		ret = readfile(path2, idproduct, 4);
		if (ret <= 0)
			continue;

		vid=strtoul(idvendor, NULL, 16);
		pid=strtoul(idproduct, NULL, 16);
		for(j=0;;j++){
			if(!map[j].vid) break;
			if((vid == map[j].vid)&&(pid== map[j].pid))	{
				device = &map[j];
				break;				
			}
		}
		
		if (device != NULL)
			goto out;
	}

	if (device == NULL)
		WARN("Runtime 3G can't find supported modem");
out:
	closedir(usbdir);
	free(path);
	free(path2);

	return device;
}


 
/*
 * return port numbers
*/
static int rilhw_check(PST_RIL_HARDWARE hw)
{
	struct stat statport;
	int err;
	if(!hw)
		return kErrNoHw;
	err = stat(hw->service_port,&statport);
	if(err)
		return kErrAccessServicePort;
	err = stat(hw->modem_port,&statport);
	if(err)
		return kErrAccessModemPort;
	return kErrNoError;
}

#ifdef HAS_LIBUSB
static int print_usbdev_config(libusb_device* dev){
	
	struct libusb_config_descriptor *config;
	int r;
	int i,j,numintf;

	r = libusb_get_active_config_descriptor(dev, &config);
	if (r < 0) {
		ERROR(
			"could not retrieve active config descriptor");
		return LIBUSB_ERROR_OTHER;
	}
	DBG("busnumber:%d",libusb_get_bus_number(dev));
	DBG("config->bNumInterfaces=%d",config->bNumInterfaces);
	numintf = config->bNumInterfaces;
	for(i=0;i<numintf;i++){
		for(j=0;j<config->interface[i].num_altsetting;j++){
			DBG("config interface number=%d",config->interface[i].altsetting[j].bInterfaceNumber);
		}
		
	}

	libusb_free_config_descriptor(config);

	return 0;
}
#endif

static void str2lowercase(char* str){
	int i=0;
	char c;
	while (str[i])
	{
		str[i]=tolower(str[i]);
	    i++;
	}
}
void rilhw_found(PST_RIL_HARDWARE *found)
{
    DBG("checking RIL hardware ...");
	PST_RIL_HARDWARE map = &hw_mapping[0];

	#ifdef HAS_LIBUSB
    //sanity checking    
	libusb_device **devs;
	int r;
	ssize_t cnt;

	*found = NULL;

	r = libusb_init(NULL);
	if (r < 0)
	{	
		WARN("libusb init failed[%d]",r);
		return ;
	}
		cnt = libusb_get_device_list(NULL, &devs);
		if (cnt < 0)
		{
			WARN("getusb device list failed");
		}
		else
		{
		
			libusb_device *dev;
			int i = 0,j;
			
			while ((dev = devs[i++]) != NULL) {
				struct libusb_device_descriptor desc;
				int r = libusb_get_device_descriptor(dev, &desc);
				if (r < 0) {
					ERROR("failed to get device descriptor");
					break;
				}

				for(j=0;;j++)
				{
					if(!map[j].vid) break;
					if((desc.idVendor == map[j].vid)&&
						(desc.idProduct== map[j].pid))
					{
						*found = &map[j];
						

						//print_usbdev_config(dev);


						
						goto finished;
					}
				}
				DBG("usb device %04x:%04x found but not RIL HW\n",
					desc.idVendor, desc.idProduct);
			}
			
		}


finished:		
	libusb_free_device_list(devs, 1);
	
	libusb_exit(NULL);
	#else	
	//using new sysfs to find matched modem device
	*found = find_matched_device(map);	
	#endif

	//find out the port names of found device
	if(NULL!=*found)
	{
		int len=0;		
		char value[PROPERTY_VALUE_MAX];
		char buffer[256];
		PST_RIL_HARDWARE hw = *found;
		if(rilhw_check(hw)){
			ERROR("rilhw check failed\n");
			*found=NULL;
			return;
		}
			
		/* BatteryService.java uses ril.hw.model to turn off 3G modem when battery capacity is low */
		if(hw->model_name)
			property_set("ril.hw.model", hw->model_name);
		if(hw->voice_port)
			property_set("ril.vmodem.target",hw->voice_port);
		if(hw->voice_format)
			property_set("ril.vmodem.pcm",hw->voice_format); 
		if(hw->voice_rw_period)
			property_set("ril.vmodem.period",hw->voice_rw_period);
		
		switch(hw->model)
		{
			case kRIL_HW_MC2716:
				property_set("ril.vmodem.dlink.volume","160");
				//sms_mode = AT_SMS_MODE_TEXT;
				break;
			case kRIL_HW_AD3812:
			//roperty_set("ril.vmodem.volume",180);
			//reak;
			case kRIL_HW_MF210:
			default:
				property_set("ril.vmodem.dlink.volume","200");
				break;					
		}

		//print usage information
		len=sprintf(buffer,"found rilhw[%s],",hw->model_name);
		if(hw->alias_name)
			len+=sprintf(buffer+strlen(buffer),"[alias:%s],",hw->alias_name);
		len+=sprintf(buffer+strlen(buffer),"[%s],",
				(kPREFER_NETWORK_TYPE_WCDMA==hw->prefer_net)?"WCDMA":
				(kPREFER_NETWORK_TYPE_CDMA_EVDV==hw->prefer_net)?"CDMA-EVDV":
				(kPREFER_NETWORK_TYPE_TD_SCDMA==hw->prefer_net)?"TD-SCDMA":"unknown");
		if(hw->service_port)
				len+=sprintf(buffer+strlen(buffer),"service[%s],",hw->service_port);
		if(hw->modem_port)
				len+=sprintf(buffer+strlen(buffer),"modem[%s],",hw->modem_port);
		if(hw->voice_port)
				len+=sprintf(buffer+strlen(buffer),"voice[%s]",hw->voice_port);
		
		DBG("%s\n",buffer);
				
		//check if property based autosuspend setting
		sprintf(buffer,"persist.ril.autosuspend.%s",hw->model_name);
		str2lowercase(buffer);		
		if(property_get(buffer, value, NULL)>0){
			hw->no_suspend=atoi(value);
		}else if(hw->alias_name) {			
			sprintf(buffer,"persist.ril.autosuspend.%s",hw->alias_name);
			str2lowercase(buffer);		
			if(property_get(buffer, value, NULL)>0)
				hw->no_suspend=atoi(value);
		}
		
		//apply autosuspend setting
		rilhw_autosuspend(hw,1);

		if(hw->no_pinonoff){
			hw->no_pinonoff=0;
			writeStringToFile("/sys/devices/platform/usb_modem/usb_modem","onoff off\n");
			writeStringToFile("/sys/devices/platform/usb_modem/usb_modem","onoff skip\n");
			rilhw_power(hw,kRequestStateReset);
		}

		if(hw->hook)
			hw->hook(hw,0);
		
	}
	
		
}

static int readIntFromFile( const char* file, int* pResult )
{
	int fd = -1;
	fd = open( file, O_RDONLY );
	if( fd >= 0 ){
		char buf[20];
		int rlt = read( fd, buf, sizeof(buf) );
		close(fd);
		if( rlt > 0 ){
			buf[rlt] = '\0';
			*pResult = atoi(buf);
			return 0;
		}
	}
	return -1;
}


int rilhw_power(PST_RIL_HARDWARE hardware,int reqstate)
{
	char property[PROPERTY_VALUE_MAX];
	char buf[64];
	int len;
	int ret=-1;
	int fd;	
	int powermonitor_enable=1;
	property_get("persist.ril.powermonitor", property, "1");
	powermonitor_enable = atoi(property);
	if(!powermonitor_enable) return 0;
	fd = open( "/sys/devices/platform/usb_modem/usb_modem", O_WRONLY );
	if( fd >= 0 ){
		len=sprintf(buf,"state %s 100\n",(kRequestStateOn==reqstate)?"on":"off");
		
		len = write( fd, buf, strlen(buf));
		if( len > 0 ){
			ret=0;
		}
		close(fd);
	}
	if(kRequestStateReset==reqstate){
		//power on again
		usleep(10000);
		fd = open( "/sys/devices/platform/usb_modem/usb_modem", O_WRONLY );
		if( fd >= 0 ){
			len=sprintf(buf,"state on 100\n");			
			len = write( fd, buf, strlen(buf));
			if( len > 0 ){
				ret=0;
			}
			close(fd);
		}
	}
	return ret;
	
}

int rilhw_wakeup(PST_RIL_HARDWARE hardware,int wake){	
	char property[PROPERTY_VALUE_MAX];
	int autosuspend_enable=0;
	char buf[64];
	int len;
	int ret=-1;	
	property_get("persist.ril.wakeup", property, "0");	
	autosuspend_enable = atoi(property);
	if(!autosuspend_enable){
		return 0;
	}
	len=sprintf(buf,"wake %s\n",wake?"on":"off");
	ret=writeStringToFile("/sys/devices/platform/usb_modem/usb_modem",buf);	


	return ret;
}
int rilhw_autosuspend(PST_RIL_HARDWARE hardware,int enable){					
	char property[PROPERTY_VALUE_MAX];
	int autosuspend_enable=0;
	int autosuspend_timeout=5;

	//check device autosuspend setting
	if(hardware&&hardware->no_suspend) return 0;

	//check the global autosuspend setting (default enabled)
	property_get("persist.ril.autosuspend", property, "1");	
	autosuspend_enable = atoi(property);
	if(autosuspend_enable)	{
		char* command;
		property_get("persist.ril.autosuspendtimeout", property, "2");	
		autosuspend_timeout = atoi(property);
		if(autosuspend_timeout<0||autosuspend_timeout>120)
			autosuspend_timeout=5;
		asprintf(&command, "modem_rt:%s %d %lx",(enable>0)?"auto":"on",
			(enable>0)?autosuspend_timeout:-1,
			hardware->vid);
		property_set("ctl.start",command);
		free(command);
	}
	return enable;
}

int rilhw_power_state(void)
{
	int fd = open( "/sys/devices/platform/usb_modem/usb_modem", O_RDONLY );
	int ret=-1;
	if( fd >= 0 )
	{
		char buf[30];
		char *p,*q;
		int rlt = read( fd, buf, sizeof(buf) );
		if( rlt > 0 ){
			buf[rlt] = '\0';
            p = strstr( buf, "modem state \"" );
            if (p != NULL)
			{
				p += strlen("modem state \"");
				q  = strpbrk( p, " \t\n\"" );
				if (q != NULL)
				{
					*q = 0;
					if(strcmp(p,"on")==0)
						ret=kRequestStateOn;
					else if(strcmp(p,"off")==0)
						ret=kRequestStateOff;
					else
						ret=kRequestStateInvalid;
				}
			}
		}
		close(fd);
	}
	return ret;
}
//Ellie add end

static int rilhw_link_change(void)
{
	int fd = open( "/sys/devices/platform/usb_modem/link_state", O_RDWR );
	int ret=0;
	if( fd >= 0 )
	{
		char buf[20];
		int rlt = read( fd, buf, sizeof(buf) );
		if( rlt > 0 ){
			buf[rlt] = '\0';
			ret = atoi(buf);
		}
		if(ret)
		{
			write(fd,"0\n",strlen("0\n"));
		}
		close(fd);
	}
	return ret;
}

int rilhw_notify_screen_state(int state)
{
	int fd = open( "/sys/devices/platform/usb_modem/screen_state", O_WRONLY );
	if( fd >= 0 ){
		char buf[48];
		int len;
		len=sprintf(buf,"%d\n",state);
		len = write( fd, buf, strlen(buf));
		close(fd);
		if( len > 0 ){
			return 0;
		}
	}

	return -1;
}


static int rilhw_wakelock_count=0;

int rilhw_acquire_wakelock()
{
	if(!rilhw_wakelock_count)
    {
    	DBG("%s\n",__func__);
    	acquire_wake_lock(PARTIAL_WAKE_LOCK, ANDROID_WAKE_LOCK_NAME);
		rilhw_wakelock_count = 1;
	}
	return 0;
}
int rilhw_release_wakelock()
{
	if(rilhw_wakelock_count)
	{
    	DBG("%s\n",__func__);
		release_wake_lock(ANDROID_WAKE_LOCK_NAME);
		rilhw_wakelock_count=0;
	}
	return 0;
}

struct wd_entry
{
	int wd;
	int (*on_change)(unsigned int event);
	
};

#define MAX_WD_NUM 16
struct hardware_monitor_param
{
	int fd;//notify fd
	int wd_count;
	struct wd_entry entries[MAX_WD_NUM]; //max 16 wd
};

static int link_state_changed(unsigned int event)
{
	int link_state = rilhw_link_change();
	DBG("link state changed->%d\n",link_state);
	pdp_check();
	if(sState!=radio_state_ready)
	{
		WARN("radio state not ready,ignore link state change\n");
		return 0;
	}
	switch(link_state)
	{
		case 2:
			{
				DBG("\n\nradio link changed\n\n");
		        RIL_onUnsolicitedResponse (
		            RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
		            NULL, 0);
				CheckSMSStorage();
			}
			break;
		case 1:
			{
				DBG("link state changed\n");
				if(!rilhw_wakelock_count)
				{
					RIL_onUnsolicitedResponse (
						RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
						NULL, 0);
					CheckSMSStorage();
				}
			}
			break;
		
	}

	return 0;

}
static void *hardware_monitor(void* param)
{
#define EVENT_NUM 16
#define MAX_BUF_SIZE 1024		

	struct hardware_monitor_param* monitor_param = param;
	#if 0
	int i;
	char buffer[1024];
	char* offset = NULL;
	struct inotify_event * event;
	struct wd_entry* entry;
	int len, tmp_len;	

	INFO("%s started\n",__func__);
	while((len = read(monitor_param->fd, buffer, MAX_BUF_SIZE))>0) 
	{
		offset = buffer;
		event = (struct inotify_event *)buffer;
		while (((char *)event - buffer) < len) 
		{
			entry = NULL;
			for (i=0; i<MAX_WD_NUM; i++) {
				if (event->wd != monitor_param->entries[i].wd) continue;
				else
				{
					entry = &monitor_param->entries[i];
					break;
				}
			}
			if(entry&&entry->on_change)			
				entry->on_change(event->mask);
			tmp_len = sizeof(struct inotify_event) + event->len;
			event = (struct inotify_event *)(offset + tmp_len); 
			offset += tmp_len;
		}
	
	}
	
	#else
	fd_set mfds,fds;
	int nfds;
	int i,ret;
	struct timeval to={2,0};
	while(1)    
	{	    
		FD_ZERO(&mfds);
		nfds=0;
		for (i = 0; i < monitor_param->wd_count; i++)	
		{
			if (monitor_param->entries[i].wd < 0) continue;
			FD_SET(monitor_param->entries[i].wd, &mfds);	
			if (monitor_param->entries[i].wd >= nfds)		
				nfds = monitor_param->entries[i].wd+1;	
		}
		
		// make local copy of read fd_set    	
		memcpy(&fds, &mfds, sizeof(fd_set)); 
		/* call the select */    	
		if ((ret = select(nfds, &fds, NULL, NULL, &to)) < 0)    
		{            
			ERROR(" hardware_monitor select fail.\n");   
			goto bail;        
		}	
		for (i = 0; i < monitor_param->wd_count; i++)	
		{
			if (FD_ISSET(monitor_param->entries[i].wd,&fds))
			{
				if(monitor_param->entries[i].on_change)
					monitor_param->entries[i].on_change(0);
			}
		}
	}
	#endif

bail:
	return NULL;
}

#ifdef ANDROID_PROPERTY_WATCH
struct prop_watch nt_autoswitch_watch;

static void nt_switch(void* arg){
	char* cmd;
	asprintf(&cmd, "AT+COPS=,,,%d", (int)arg);
	at_send_command(cmd,NULL);
	free(cmd);
}

static int autoswitch_notifier_call(struct prop_watch * pw, char* name , char* value){	
	if(!value) return 0;
	if(!strcmp(value,"2g")){
		DBG("switch to 2g prefer mode");
		enqueueRILEvent(nt_switch,(void*)0,NULL);
	}else if(!strcmp(value,"3g")){
		DBG("switch to 3g prefer mode");
		enqueueRILEvent(nt_switch,(void*)2,NULL);
	}
	return 1;
}

#endif
static int ad3812_hook(struct ril_hardware* rilhw,int event){	
	#ifdef ANDROID_PROPERTY_WATCH
	init_prop_watch(&nt_autoswitch_watch,autoswitch_notifier_call,"ril.radio.autoswitch",0);
	register_prop_watch(&nt_autoswitch_watch);
	#endif
	return 0;
}

#ifdef ANDROID_PROPERTY_WATCH
#include <cutils/properties.h>

#include <sys/atomics.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>


extern prop_area *__system_property_area__;

typedef struct pwatch pwatch;

struct pwatch
{
    const prop_info *pi;
    unsigned serial;
};

static pwatch watchlist[1024];
struct prop_watch* chain=NULL;


int init_prop_watch(struct prop_watch* pw,notifier_call __notifier,char* __prop_key,void* __priv){
	if(!pw||!__prop_key||!__notifier)
		return -1;
	pw->notifier = __notifier;
	pw->priv = __priv;
	memset(pw->prop_key,0,PROPERTY_KEY_MAX);
	strcpy(pw->prop_key,__prop_key);
	pw->next = NULL;

	return 0;
	
}
int register_prop_watch(struct prop_watch* pw){
	if(!pw)
		return -1;
	if(!chain){
		chain = pw;
	}else {
		struct prop_watch* iter=chain;
		while(NULL!=iter) {
			if(pw==iter) return 0;

			if(NULL==iter->next)
				break;
			iter = iter->next;
		}
		iter->next = pw;
	}
	return 0;
}

static void announce(const prop_info *pi)
{
    char name[PROP_NAME_MAX];
    char value[PROP_VALUE_MAX];
    char *x;

    __system_property_read(pi, name, value);

    for(x = value; *x; x++) {
        if((*x < 32) || (*x > 127)) *x = '.';
    }

	#if 0
    DBG("%10d %s = '%s'\n", (int) time(0), name, value);
	#endif
	{
		struct prop_watch* iter=chain;
		while(NULL!=iter){
			if(!strcmp(iter->prop_key,name)){
			  if(iter->notifier){
			  	if(iter->notifier(iter,name,value))
					break;
			  }
			}
			iter = iter->next;
		}
			
	}
}

static void *props_watch(void* param)
{
	prop_area *pa = __system_property_area__;
	unsigned serial = pa->serial;
	unsigned count = pa->count;
	unsigned n;

	if(count >= 1024) return NULL;

	for(n = 0; n < count; n++) {
		watchlist[n].pi = __system_property_find_nth(n);
		watchlist[n].serial = watchlist[n].pi->serial;
	}

	DBG("props_watch is running\n");
	for(;;) {
		do {
			__futex_wait(&pa->serial, serial, 0);
		} while(pa->serial == serial);

		while(count < pa->count){
			watchlist[count].pi = __system_property_find_nth(count);
			watchlist[count].serial = watchlist[n].pi->serial;
			announce(watchlist[count].pi);
			count++;
			if(count == 1024) return NULL;
		}

		for(n = 0; n < count; n++){
			unsigned tmp = watchlist[n].pi->serial;
			if(watchlist[n].serial != tmp) {
				announce(watchlist[n].pi);
				watchlist[n].serial = tmp;
			}
		}
	}

	return NULL;

}
#endif

int rilhw_init(void)
{
	static pthread_t s_tid_hw=-1;
	static struct hardware_monitor_param param;
	
	#ifdef ANDROID_PROPERTY_WATCH
	static pthread_t s_tid_props_watch=-1;
	#endif
	
	if(-1==s_tid_hw)
	{
		int ret,fd,wd;
		int i;
		pthread_attr_t attr;		
		pthread_attr_init (&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		memset(&param,0,sizeof(struct hardware_monitor_param));
		//use select/poll API to monitor sysfs fs since inotify does not support sysfs
		#if 0
		fd = inotify_init();
		if(fd<0)
		{	
			ERROR("fail to init notify\n");
			return -1;
		}
		param.fd = fd;		
		//
		//Add wd files here ,don't over MAX_WD_NUM
		//
		wd = inotify_add_watch(fd,"/sys/devices/platform/usb_modem/link_state",IN_MODIFY);
		#else
		for(i=0;i<MAX_WD_NUM;i++)
			param.entries[i].wd = -1;
		wd = open("/sys/devices/platform/usb_modem/link_state",O_RDONLY);
		#endif
		if(wd>=0)
			param.entries[param.wd_count].wd = wd;
		param.entries[param.wd_count].on_change = link_state_changed;
		param.wd_count++;

		
		
		ret = pthread_create(&s_tid_hw, &attr, hardware_monitor, &param);
		if (ret < 0) {
			perror ("pthread_create");
			return -1;
		}
		
	}
	
	#ifdef ANDROID_PROPERTY_WATCH
	if(-1==s_tid_props_watch){		
		pthread_attr_t attr;	
		int ret;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&s_tid_props_watch, &attr, props_watch, NULL);
		if (ret < 0) {
			perror ("pthread_create");
			return -1;
		}
	}
	#endif

    return 0;

}


