#include <stdio.h>
#include <telephony/ril.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/route.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include <pthread.h>
#include <semaphore.h>
#include <hardware_legacy/power.h>
#include <cutils/properties.h>
#include "eventset.h"
#include "ril-handler.h"


#define USE_32_SUBNET_ROUTE_WORKAROUND 1
#define PPP_UP         1
#define PPP_DOWN       0

#define PPP_SVC_PROP "init.svc.ppp_daemon"

//pppd event
#define EVENT_DISCONNECTED     		1
#define EVENT_START  				2
#define EVENT_STOP    				3
#define EVENT_PING					4
#define EVENT_RESTART				5
#define EVENT_TIMEOUT				ETIMEDOUT


extern RIL_RadioState sState;

#define DATACALL_STATE_UNUSED 0
#define DATACALL_STATE_ACTIVE 1

#define DATACALL_LINKSTATE_INACTIVE 0
#define DATACALL_LINKSTATE_DOWN 	1
#define DATACALL_LINKSTATE_UP		2
typedef struct datacall_context{

	int state;	
	int active;/* 0=inactive, 1=active/physical link down, 2=active/physical link up */
	
	RIL_Token token;//token for report link result

	//sync
	EventSet pdp_eventset;
	pthread_mutex_t	command_lock;
	pthread_cond_t command_cv;

	//information from modem
	char* cid;
	char* interface;
	char* address;//numerci IPV4 or IPV6 address
	char* dns; 	//space separated list
	char* gw;	//numeric IPV4 or IPV6 address
	
	char* radio;
	char* prof;
	char* apn;
	char* user;
	char* pass;
	char* auth;
	char* type;

	//statistics
	int dialcount;

	int request;
	
}DATACALL_CONT,*PDATACALL_CONT;
static DATACALL_CONT dcs[MAX_DATA_CALL_COUNT];

static PDATACALL_CONT getdc(void){
	int i;	
	PDATACALL_CONT dc;
	char buffer[64];
	for(i=0;i<MAX_DATA_CALL_COUNT;i++){
		if(DATACALL_STATE_UNUSED==dcs[i].state)
			break;
	}
	if(i==MAX_DATA_CALL_COUNT)
		return NULL;
	dc = &dcs[i];
	sprintf(buffer,"%d",i+1);
	dc->cid = strdup(buffer);
	sprintf(buffer,"ppp%d",i);
	dc->interface = strdup(buffer);

	
	eventset_create(&dc->pdp_eventset);
	pthread_cond_init(&dc->command_cv,NULL);
	pthread_mutex_init(&dc->command_lock,NULL);	

	dcs[i].state = DATACALL_STATE_ACTIVE;
	dcs[i].active = DATACALL_LINKSTATE_INACTIVE;
	dcs[i].dialcount = 0;

	return &dcs[i];
}

static void freedc(PDATACALL_CONT dc){
	if(dc->state==DATACALL_STATE_ACTIVE){
		if(dc->cid){free(dc->cid);dc->cid=NULL;}
		if(dc->interface){free(dc->interface);dc->interface=NULL;}
		if(dc->address){free(dc->address);dc->address=NULL;}
		if(dc->dns){free(dc->dns);dc->dns=NULL;}
		if(dc->gw){free(dc->gw);dc->gw=NULL;}
		if(dc->radio){free(dc->radio);dc->radio=NULL;}
		if(dc->prof){free(dc->prof);dc->prof=NULL;}
		if(dc->apn){free(dc->apn);dc->apn=NULL;}
		if(dc->user){free(dc->user);dc->user=NULL;}
		if(dc->pass){free(dc->pass);dc->pass=NULL;}
		if(dc->auth){free(dc->auth);dc->auth=NULL;}
		if(dc->type){free(dc->type);dc->type=NULL;}
		dc->state = DATACALL_STATE_UNUSED;

		
		eventset_destroy(dc->pdp_eventset);
		pthread_cond_destroy(&dc->command_cv);
		pthread_mutex_destroy(&dc->command_lock);			
		dc->active = DATACALL_LINKSTATE_INACTIVE;

		
		
	}
}

static PDATACALL_CONT finddc(const char* cid){
	int i;
	char buffer[64];
	for(i=0;i<MAX_DATA_CALL_COUNT;i++){
		if(dcs[i].cid&&!strcmp(cid,dcs[i].cid))
			break;
	}
	if(i==MAX_DATA_CALL_COUNT)
		return NULL;

	return &dcs[i];
}
static void dumpdc(PDATACALL_CONT dc){
	if(dc){
		DBG("cid[%s]",dc->cid?dc->cid:"null");
		DBG("type[%s]",dc->type?dc->type:"null");			
		DBG("apn[%s]",dc->apn?dc->apn:"null");			
		DBG("dns[%s]",dc->dns?dc->dns:"null");			
		DBG("address[%s]",dc->address?dc->address:"null");	
		DBG("active[%d]",dc->active);			
	}
}

/*
	The dial number for main operators different
	China Mobile:
	 *99***1#
	China Unicom:
	*99#
	China Telecom:
	#777
*/
static char* gsm_dial_string = "ATD*99***1#";
//
//"ATD*99***1#"
//"ATDT*99#"

static char* cdma_dial_string = "ATDT#777";


//----------------------------pppd----------------------------------------------
static int query_ifc_info( const char *interface, unsigned *flags)
{  
  DIR  *dir_path = NULL;
  struct ifreq ifr;
  struct dirent *de;
  int query_sock = -1;
  int ret = -1;
   
  query_sock = socket(AF_INET, SOCK_DGRAM, 0);
   if(query_sock < 0){
       ERROR("ZTERIL: failed to create query interface socket");
   	goto result;
  }
  // open the sys/class/net
  dir_path = opendir(SYS_NET_PATH);
  if(dir_path == 0){
  	ERROR("failed to opendir %s",SYS_NET_PATH);
    	goto result;
  }

  while((de = readdir(dir_path))){
    if(strcmp(de->d_name, interface) == 0){
      memset(&ifr, 0x00, sizeof(struct ifreq));
      strncpy((char *)&(ifr.ifr_name), interface, IFNAMSIZ);
      ifr.ifr_name[IFNAMSIZ -1] = 0x00;

      if (flags != NULL) {
	if(ioctl(query_sock, SIOCGIFFLAGS, &ifr) < 0) {
	  *flags = 0;
	} else {
	  *flags = ifr.ifr_flags;
	}
      }
      ret = 0;
      goto result;
    }
  }  
  // interface information is not found
 result:
	 if(query_sock != -1){
    		close(query_sock);
    		query_sock = -1;
	 }
	 if(dir_path){
	 	closedir(dir_path);
	 	dir_path = NULL;
	 }
 
  return ret;
}
static int pdp_wakelock_count=0;

int pdp_acquire_wakelock()
{
	if(!pdp_wakelock_count)
    {
    	acquire_wake_lock(PARTIAL_WAKE_LOCK, "ril_pdp");
		pdp_wakelock_count = 1;
	}
	return 0;
}
int pdp_release_wakelock()
{
	if(pdp_wakelock_count)
	{
		release_wake_lock("ril_pdp");
		pdp_wakelock_count=0;		
	}
	return 0;
}



static int checkPPPConnection( char* interface ,int sTimeout, unsigned int flag)
{
	int     ret = -1;
	int count = sTimeout;
	unsigned  pppInterFlags;	
	char prop_value[PROPERTY_VALUE_MAX];
	do{
		if(query_ifc_info(interface, &pppInterFlags) == 0)
		{
			if(flag == (pppInterFlags & 0x01))
			{
				ret = 0;
				break;
			}
		}
		sleep(1);
		property_get(PPP_SVC_PROP,prop_value,"");
		if(!strcmp(prop_value, "stopped"))
			break;
	}while(--count > 0);
	return ret;
}

//
//Currently we don't support multi instances ,may support later.
//
static int start_pppd(PDATACALL_CONT dc,int  ms)
{
	char* user,*pwd;
	char dialnumber[256];
	int err;
	char prop_key[PROPERTY_KEY_MAX];
	char prop_value[PROPERTY_VALUE_MAX];
	char optionfile[256];
	char* command;
	char line[256];
	DBG("start ppp daemon...");

	pdp_acquire_wakelock();

	user = ((dc->user && dc->user[0])? dc->user: "guest");
	pwd = ((dc->pass && dc->pass[0])? dc->pass: "guest");
	
	property_get(PPP_SVC_PROP,prop_value,"");
	while (strcmp(prop_value, "running") == 0)
	{
		WARN("ppp daemon already running,restart it");
		property_set("ctl.stop","ppp_daemon");
		usleep(1*1000*1000);
		property_get(PPP_SVC_PROP,prop_value,"");
	}

	//FIXME,only support ppp0 
	sprintf(optionfile,"/data/ppp/options");
	
	DBG("open /etc/ppp/options");
	FILE* dOption = fopen("/system/etc/ppp/options","r");
	if(dOption)
	{
		DBG("open /system/etc/ppp/options success!!!");
		FILE* hOption = fopen(optionfile,"w+");
		if(hOption)
		{
		
			DBG("file %s success as pdp option",optionfile);
			//ppp port and baudrate
			asprintf(&command, "%s %d\n",s_modem_port,115200);
			fputs(command,hOption);
			free(command);

			while(fgets( line, 256, dOption) != NULL) 
			{
				fputs(line,hOption);
			}	
			
			//append user and password options
			if(strlen(user)){
				asprintf(&command, "user %s\n",user);
				fputs(command,hOption);
				free(command);
			}
			//pass
			if(strlen(pwd)){
				asprintf(&command, "password %s\n",pwd);
				fputs(command,hOption);
				free(command);
			}

			asprintf(&command, "linkname %s\n",dc->interface);
			fputs(command,hOption);
			free(command);		

	        fclose(dOption);
	        fclose(hOption);
			DBG("fclose the file ");
		}
		else
		{
			DBG("file %s failed as pdp option",optionfile);			
			fclose(dOption);
			goto bail;
		}
	}
	else
	{
		//write option /data/etc/options
		FILE* hOption = fopen(optionfile,"w+");
		if(hOption)
		{
			DBG("file %s success as pdp option",optionfile);
			//user
			asprintf(&command, "%s %d\n",s_modem_port,115200);
			fputs(command,hOption);
			free(command);
			

			fputs("novj \n",hOption);
			fputs("novjccomp \n",hOption);
			fputs("noauth\n",hOption);
			fputs("noccp\n",hOption);		
			fputs("nodetach\n",hOption);
			fputs("ipcp-max-failure 30\n",hOption);

			//connect script
			fputs("connect \"/data/ppp/pppondialer\"\n",hOption);		

			fputs("ipcp-accept-local\n",hOption);
			fputs("ipcp-accept-remote\n",hOption);
			fputs("defaultroute\n",hOption);
			fputs("usepeerdns\n",hOption);
			
			//fputs("dump\n",hOption);
			//fputs("debug\n",hOption);

			
			//user
			if(strlen(user)){
				asprintf(&command, "user %s\n",user);
				fputs(command,hOption);
				free(command);
			}
			//pass
			if(strlen(pwd)){
				asprintf(&command, "password %s\n",pwd);
				fputs(command,hOption);
				free(command);
			}

			//connect delay		
			asprintf(&command, "connect-delay %d\n",2000);
			fputs(command,hOption);
			free(command);		
			
			//link name ,hard code here
			asprintf(&command, "linkname %s\n",dc->interface);
			fputs(command,hOption);
			free(command);		
			fclose(hOption);
				
			
		}
		else
		{
			ERROR("fail to create ppp daemon option file");
			goto bail;
		}
	}
	FILE* hOnDialer = fopen("data/ppp/pppondialer","w+");
	if(hOnDialer)
	{			
		if(kPREFER_NETWORK_TYPE_CDMA_EVDV==rilhw->prefer_net){
			strcpy(dialnumber,cdma_dial_string);
		}else
			sprintf(dialnumber,"ATD*99***%s#",dc->cid);

		//shell 
		fputs("#!/system/bin/sh\n",hOnDialer);

		//chat
		//"OK 'AT+CSQ' \\\n"		
	    asprintf(&command, "/system/bin/chat -v -s -S \\\n"
			"ABORT '\\nNO CARRIER\\r' \\\n"
			"ABORT '\\nNODIALTONE\\r' \\\n"
			"ABORT '\\nERROR\\r' \\\n"
			"ABORT '\\nNO ANSWER\\r' \\\n"
			"ABORT '\\nBUSY\\r' \\\n"
			"TIMEOUT 30 \\\n"
			"\"\" AT \\\n");
		fputs(command,hOnDialer);
		free(command);

		#if 0
		if(kPREFER_NETWORK_TYPE_CDMA_EVDV!=rilhw->prefer_net)
		{
			asprintf(&command, 
				"OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\",,0,0 \\\n"
				"OK ATS0=0 \\\n",
				default_pdp.apn);			
			fputs(command,hOnDialer);
			free(command);
		}
		#endif

	
		asprintf(&command,			
			"OK %s \\\n"	
			"TIMEOUT 30 \\\n"
			"CONNECT '' \n",	
			dialnumber);
		fputs(command,hOnDialer);
		free(command);

		fclose(hOnDialer);

		//change the access previledge
		err = chmod("data/ppp/pppondialer",S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IXOTH);
		if(err)
		{
			ERROR("failed to change ppp script previledge\n");
		}
	}
	else
	{
		ERROR("fail to create ppp on dialer script\n");
		goto bail;
	}

	
	dc->dialcount++;
	#if 0
	err = at_send_command(dialnumber,NULL);
	if (err != 0) {
		/* If failed, retry just with data context activation */		
		asprintf(&command, "AT+CGDATA=\"PPP\",%s",dc->cid);
		err = at_send_command(command,NULL);
		free(command);
		if (err != 0) {
			ERROR("failed to activate PDP %s",dc->cid);
			return -1;
		}
	}
	#endif

	//
	//reach here means PDP activated,we can start ppp 
	//
	//start ppp daemon 	
	asprintf(&command, "ppp_daemon:%s",optionfile);
	err = property_set("ctl.start",command);
	free(command);

	if(err)
	{
		ERROR("failed to start ppp daemon");
		goto bail;
	}

	
	//check ppp daemon service or check ppp0 interface?
	if(checkPPPConnection(dc->interface,ms, PPP_UP) != 0){
		WARN("ppp connection not ready");
		goto bail;
	}

	//get ipaddress,dns,gw
	{
		char dns[128];
		sprintf(prop_key,"net.%s.local-ip",dc->interface);
		property_get(prop_key,prop_value,"");		
		if(strlen(prop_value))dc->address  =strdup(prop_value);
		
		sprintf(prop_key,"net.%s.gw",dc->interface);
		property_get(prop_key,prop_value,"");		
		if(strlen(prop_value)) dc->gw=strdup(prop_value);
		
		sprintf(prop_key,"net.%s.dns1",dc->interface);
		property_get(prop_key,prop_value,"");
		if(strlen(prop_value)){
			sprintf(dns,"%s",prop_value);
		}
		sprintf(prop_key,"net.%s.dns2",dc->interface);
		property_get(prop_key,prop_value,"");
		if(strlen(prop_value)){
			sprintf(dns+strlen(dns)," %s",prop_value);
		}
		dc->dns = strdup(dns);	

		if(!dc->gw&&dc->address)
			dc->gw = strdup(dc->address);
		
	}		
	pdp_release_wakelock();
  	DBG("ppp connection ready.");
  	return 0;
bail:
	pdp_release_wakelock();
	return -1;
}

static int stop_pppd(void)
{
	char prop_value[PROPERTY_VALUE_MAX];
	int retry=5;
	property_get(PPP_SVC_PROP,prop_value,"");
	if(strcmp(prop_value, "running"))
		return 0;
	pdp_acquire_wakelock();
	property_set("ctl.stop","ppp_daemon");
	do{
		retry--;
		sleep(1);
		property_get(PPP_SVC_PROP,prop_value,"");
		if(strcmp(prop_value, "running"))
			break;
		WARN("ppp daemon still running,retrying to stop it[%d]",retry);
		property_set("ctl.stop","ppp_daemon");		
	}while (retry>0);

	pdp_release_wakelock();
	return 0;
}



static void send_pdp_command(PDATACALL_CONT dc ,unsigned event)
{	
//	pthread_mutex_lock(&dc->command_lock); 	
	eventset_set(dc->pdp_eventset,event);
//	pthread_cond_wait(&dc->command_cv,&dc->command_lock);	
//	pthread_mutex_unlock(&dc->command_lock); 	
}

static void broadcast_pdp_command(unsigned event)
{	
	int i;	
	PDATACALL_CONT pdc;
	for(i=0;i<MAX_DATA_CALL_COUNT;i++){
		pdc=&dcs[i];
		if(pdc->cid&&(pdc->state==DATACALL_STATE_ACTIVE)){
			send_pdp_command(pdc,event);
		}
	}
}


static int get_host_by_name(const char* hostname)
{
	struct hostent *hostnm = gethostbyname(hostname);
	if(hostname == NULL)  return -1;
	if( hostnm == (struct hostent *)0){
			return -1;
	}
	return 0;
}

void reportDataCallStatus(PDATACALL_CONT dc){	
	if(dc->active == DATACALL_LINKSTATE_UP){
		if(dc->token){			
			RIL_Data_Call_Response_v6 response;
			response.status = PDP_FAIL_NONE;
			response.suggestedRetryTime = 0;
			response.cid = atoi(dc->cid);
			response.active = dc->active;
			response.ifname = alloca(strlen(dc->interface) + 1);
			strcpy(response.ifname,dc->interface);
			response.type = alloca(strlen(dc->type) + 1);
			strcpy(response.type,dc->type);
			response.dnses= alloca(strlen(dc->dns) + 1);
			strcpy(response.dnses,dc->dns);
			response.gateways= alloca(strlen(dc->gw) + 1);
			strcpy(response.gateways,dc->gw);
			
			if(dc->address){
				response.addresses=  alloca(strlen(dc->address) + 1);
				strcpy(response.addresses,dc->address);
			}else
				response.addresses = "0.0.0.0";
			DBG("ppp%s linkup localip[%s],dns[%s],gw[%s]\n",dc->cid,
				dc->address,
				dc->dns,
				dc->gw);
			//here we just assign invalid address here ,later PDP will report proper address after PDP link 
			//connected or disconnected 
			RIL_onRequestComplete(dc->token, RIL_E_SUCCESS, &response, sizeof(RIL_Data_Call_Response_v6));
			dc->token=NULL;
		}else	{			 
			RIL_Data_Call_Response_v6 response;			
			memset(&response,0,sizeof(RIL_Data_Call_Response_v6));
			response.status = PDP_FAIL_NONE;			
			response.suggestedRetryTime = 0;
			response.cid = atoi(dc->cid);
			response.active = dc->active;
			if(dc->interface){
				response.ifname = alloca(strlen(dc->interface) + 1);
				strcpy(response.ifname,dc->interface);
			}
			if(dc->type){
				response.type = alloca(strlen(dc->type) + 1);
				strcpy(response.type,dc->type);			
			}
			if(dc->dns){
				response.dnses= alloca(strlen(dc->dns) + 1);
				strcpy(response.dnses,dc->dns);
			}
			if(dc->gw){
				response.gateways= alloca(strlen(dc->gw) + 1);
				strcpy(response.gateways,dc->gw);
			}
			if(dc->address){
				response.addresses=  alloca(strlen(dc->address) + 1);
				strcpy(response.addresses,dc->address);
			}else
				response.addresses = "0.0.0.0";
			RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
										&response, sizeof(RIL_Data_Call_Response_v6));
		}
	}else if(DATACALL_LINKSTATE_DOWN==dc->active){	
		if(dc->token){
			RIL_onRequestComplete(dc->token, RIL_E_GENERIC_FAILURE, NULL,0);
			dc->token=NULL;
		}
		eventset_set(dc->pdp_eventset,EVENT_STOP);
	}else if(DATACALL_LINKSTATE_INACTIVE==dc->active){	
		RIL_Data_Call_Response_v6 response;
		memset(&response,0,sizeof(RIL_Data_Call_Response_v6));
		response.status = PDP_FAIL_ERROR_UNSPECIFIED;
		response.suggestedRetryTime = 0;
		response.cid = atoi(dc->cid);
		response.active = dc->active;	
		if(dc->interface){
			response.ifname = alloca(strlen(dc->interface) + 1);
			strcpy(response.ifname,dc->interface);
		}
		if(dc->type){
			response.type = alloca(strlen(dc->type) + 1);
			strcpy(response.type,dc->type);
		}
		if(dc->address){
			response.addresses =  alloca(strlen(dc->address) + 1);
			strcpy(response.addresses,dc->address);
		}else
			response.addresses = "0.0.0.0";
		RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
									&response, sizeof(RIL_Data_Call_Response_v6));
	}
		
}

#define WATCHDOG_STOPPED 0
#define WATCHDOG_STARTED 1
#define PDP_DIALING_RETRY 3
static void* pdp_manager(void *arg)
{
	PDATACALL_CONT dc = (PDATACALL_CONT)arg;
	int event;
	int pdp_connect_required = 0;
	int  stop = 0;
	int  wdg_status = WATCHDOG_STOPPED;
	int ping=0;
	int timeout_ms=50;
	int pdp_restart=0;
	//detach myself from parent
	pthread_detach(pthread_self());		
	
	rilhw_autosuspend(rilhw,0);
	INFO("PPPD manager started .");
	while(!stop){
		event = eventset_wait_timeout(dc->pdp_eventset,timeout_ms);
		switch(event)
		{
			case EVENT_TIMEOUT:
				{					
					INFO("EVENT_TIMEOUT"); 			
					if(wdg_status == WATCHDOG_STARTED)
					{					
						char prop_value[PROPERTY_VALUE_MAX];
						
						property_get(PPP_SVC_PROP,prop_value,"");
						if (strcmp(prop_value, "running") != 0&&!pdp_restart)
						{
							WARN("ppp daemon exit");
							eventset_set(dc->pdp_eventset,EVENT_STOP);
							//report pdp list changed
						}
						else if(ping)
						{
							int disconnected;
							disconnected = get_host_by_name("www.baidu.com");
							if(disconnected)
							{
								WARN("ppp link disconnected,restart");
								eventset_set(dc->pdp_eventset,EVENT_DISCONNECTED);
							}
								
						}
					}
				}
				break;
			case EVENT_DISCONNECTED:
				INFO("EVENT_DISCONNECTED");	
				//just report to Android,Android will call SetupDefaultPDP again.
				dc->active = DATACALL_LINKSTATE_INACTIVE;
				wdg_status = WATCHDOG_STOPPED;
				enqueueRILEvent(onDataCallListChanged,NULL,NULL);
				break;				

			case EVENT_PING:{
					INFO("EVENT_PING");
					//ping = 1;
					break;
				}
			case EVENT_START:{		
					if(pdp_restart>0)
						pdp_restart--;
					INFO("EVENT_START");
					int result = start_pppd(dc,
						(kPREFER_NETWORK_TYPE_CDMA_EVDV==rilhw->prefer_net)?50:START_PPPD_TIMEOUT);
					if(!result){
						wdg_status = WATCHDOG_STARTED;
						dc->active = DATACALL_LINKSTATE_UP;
						timeout_ms=1000;
						reportDataCallStatus(dc);
					}else {
					
						if(!pdp_restart){
							dc->active = DATACALL_LINKSTATE_DOWN;
							reportDataCallStatus(dc);
						}
						else{
							timeout_ms=1000;
							eventset_set(dc->pdp_eventset,EVENT_START);
						}
					}
					
					break;
				}
			case EVENT_STOP:
				
				INFO("EVENT_STOP");
				wdg_status = WATCHDOG_STOPPED;				
				stop_pppd();
				dc->active = DATACALL_LINKSTATE_INACTIVE;
				stop = 1;
				pdp_connect_required  = 0;
				timeout_ms=50;
				break;

			case EVENT_RESTART:{
					DBG("restart pdp");
					pdp_restart=PDP_DIALING_RETRY;
					stop_pppd();				
					timeout_ms=50;
					eventset_set(dc->pdp_eventset,EVENT_START);
				}
				break;
			default:
				ERROR("pppd manager unknown event %d",event);
				break;
		}

		//notify command finished		
		//pthread_mutex_lock(&dc->command_lock);	
		//pthread_cond_signal(&dc->command_cv);	
		//pthread_mutex_unlock(&dc->command_lock);	
		
	}

	
	reportDataCallStatus(dc);
	freedc(dc);
	
	rilhw_autosuspend(rilhw,1);

	DBG("PPPD manager EXIT .");
	
  	return 0;
}

void OnPDPListCheck(void *param){
    ATResponse *atresponse = NULL;
    RIL_Data_Call_Response_v6 *responses = NULL;
    ATLine *cursor;
    int err;
    int number_of_contexts = 0;
    int i = 0;
    int curr_bearer, fetched;
    char *out;
	PDATACALL_CONT pdc;
	char buf[64];

	if(!rilhw)
		return;
	if(rilhw->prefer_net==kPREFER_NETWORK_TYPE_CDMA_EVDV){
		broadcast_pdp_command(EVENT_RESTART);		
		return;
	}
    /* Read the activation states */
    err = at_send_command_multiline("AT+CGACT?", "+CGACT:", &atresponse);
    if (err < 0 || atresponse->success == 0)
        goto error;

    /* Calculate size of buffer to allocate*/
    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next)
        number_of_contexts++;

    if (number_of_contexts == 0)
        /* return empty list (NULL with size 0) */
        goto error;

    responses = alloca(number_of_contexts * sizeof(RIL_Data_Call_Response_v6));
    memset(responses, 0, sizeof(responses));

    for (i = 0; i < number_of_contexts; i++) {
        responses[i].cid = -1;
        responses[i].active = -1;
    }

    /*parse the result*/
    i = 0;
    fetched = 0;

    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next) {
        char *line = cursor->line;
        int state;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        err = at_tok_nextint(&line, &responses[i].cid);
        if (err < 0)
            goto error;

        err = at_tok_nextint(&line, &state);
        if (err < 0)
            goto error;

        if (state == 0)
            responses[i].active = 0;  /* 0=inactive */
        else {
             /* (defaulting to physical link up) */
               responses[i].active = 2; /* 2=active/physical link up */
        }
		sprintf(buf, "%d", responses[i].cid);
		pdc = finddc(buf);
		if(pdc&&responses[i].active!=pdc->active){
			responses[i].active = pdc->active;
			//restart pdp
			send_pdp_command(pdc,EVENT_RESTART);
		}

        i++;
    }
    at_response_free(atresponse);
    atresponse = NULL;
	return;
error:	
    at_response_free(atresponse);	
}

void pdp_check(void){	
	enqueueRILEvent(OnPDPListCheck,NULL,NULL);
}

int pdp_init(void)
{
	return 0;
}

void pdp_uninit()
{
	stop_pppd();
}



void requestScreenState(void *data, size_t datalen, RIL_Token t)
{
	int err;
	int on =  ((int *)data)[0];
	INFO("requestScreenState = %s",on ?"Screen On":"Screen Off");
	ril_status(screen_state) = on;
	rilhw_notify_screen_state(on);

	#if 0
    if (on == 1) {
        /* Screen is on - be sure to enable all unsolicited notifications again */

        /* Enable proactive network registration notifications */
        err = at_send_command("AT+CREG=2",NULL);

        /* Enable proactive network registration notifications */
        err = at_send_command("AT+CGREG=2",NULL);


        /* Enable GPRS reporting */
        err = at_send_command("AT+CGEREP=1,0",NULL);



    } else if (on == 0) {

        /* Screen is off - disable all unsolicited notifications */
        err = at_send_command("AT+CREG=0",NULL);
        err = at_send_command("AT+CGREG=0",NULL);
        err = at_send_command("AT+CGEREP=0,0",NULL);


    } else {
        /* Not a defined value - error */
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);	
		return;
    }
	#endif

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void requestOrSendPDPContextList(RIL_Token *t)
{
	if(!rilhw){
		if (t != NULL)
			RIL_onRequestComplete(*t, RIL_E_GENERIC_FAILURE, NULL, 0);
		return;
	}
	if(rilhw->prefer_net==kPREFER_NETWORK_TYPE_CDMA_EVDV){
		RIL_Data_Call_Response_v6 *responses = malloc(sizeof(RIL_Data_Call_Response_v6)*MAX_DATA_CALL_COUNT);
		RIL_Data_Call_Response_v6* response = responses;
		PDATACALL_CONT pdc;
		int i,valid;
		if(!responses&&(t != NULL)){
				RIL_onRequestComplete(*t, RIL_E_SUCCESS, 
								NULL,0);
				return;
		}
		for(i=0,valid=0;i<MAX_DATA_CALL_COUNT;i++){
			memset(response,0,sizeof(RIL_Data_Call_Response_v6));
			pdc=&dcs[i];
			if(DATACALL_STATE_ACTIVE==pdc->state){
				response->status = PDP_FAIL_NONE;
				response->cid = atoi(pdc->cid);
				response->active = pdc->active;
				response->type = alloca(strlen(pdc->type) + 1);				
				strcpy(response->type,pdc->type);
				if(pdc->address){
					response->addresses = alloca(strlen(pdc->address) + 1);	
					strcpy(response->addresses,pdc->address);
				}
				response++;
				valid++;
			}			
		}

	    if (t != NULL)
	        RIL_onRequestComplete(*t, RIL_E_SUCCESS, 
	        				responses, valid*sizeof(RIL_Data_Call_Response_v6));
	    else
	        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
	        				responses, valid*sizeof(RIL_Data_Call_Response_v6));
		free(responses);
	}else {

    ATResponse *atresponse = NULL;
    RIL_Data_Call_Response_v6 *responses = NULL;
    ATLine *cursor;
    int err;
    int number_of_contexts = 0;
    int i = 0;
    int curr_bearer, fetched;
    char *out;
	PDATACALL_CONT pdc;

    /* Read the activation states */
    err = at_send_command_multiline("AT+CGACT?", "+CGACT:", &atresponse);
    if (err < 0 || atresponse->success == 0)
        goto error;

    /* Calculate size of buffer to allocate*/
    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next)
        number_of_contexts++;

    if (number_of_contexts == 0)
        /* return empty list (NULL with size 0) */
        goto finally;

    responses = alloca(number_of_contexts * sizeof(RIL_Data_Call_Response_v6));
    memset(responses, 0, sizeof(RIL_Data_Call_Response_v6)*number_of_contexts);

    for (i = 0; i < number_of_contexts; i++) {
        responses[i].cid = -1;
        responses[i].active = -1;
    }

    /*parse the result*/
    i = 0;
    fetched = 0;

    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next) {
        char *line = cursor->line;
        int state;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        err = at_tok_nextint(&line, &responses[i].cid);
        if (err < 0)
            goto error;

        err = at_tok_nextint(&line, &state);
        if (err < 0)
            goto error;

        if (state == 0){
            responses[i].active = 0;  /* 0=inactive */
			responses[i].status = PDP_FAIL_NONE;
    	}
        else {
             /* (defaulting to physical link up) */
			responses[i].active = 2; /* 2=active/physical link up */
			responses[i].status = PDP_FAIL_ERROR_UNSPECIFIED;
        }

        i++;
    }
    at_response_free(atresponse);
    atresponse = NULL;

    /* Read the currend pdp settings */
    err = at_send_command_multiline("AT+CGDCONT?", "+CGDCONT:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next) {
        char *line = cursor->line;
        int cid;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        err = at_tok_nextint(&line, &cid);
        if (err < 0)
            goto error;

        for (i = 0; i < number_of_contexts; i++)
            if (responses[i].cid == cid)
                break;

        if (i >= number_of_contexts)
            /* Details for a context we didn't hear about in the last request.*/
            continue;

        err = at_tok_nextstr(&line, &out);
        if (err < 0)
            goto error;

        responses[i].type = alloca(strlen(out) + 1);
        strcpy(responses[i].type, out);

        err = at_tok_nextstr(&line, &out);
        if (err < 0)
            goto error;


        err = at_tok_nextstr(&line, &out);
        if (err < 0)
            goto error;

        //responses[i].address = alloca(strlen(out) + 1);
        //strcpy(responses[i].address, out);
		{
			char buf[64];
			sprintf(buf, "%d", responses[i].cid);
			pdc = finddc(buf);
			if(pdc&&pdc->address){
				responses[i].addresses= alloca(strlen(pdc->address) + 1);
				strcpy(responses[i].addresses, pdc->address);				
			}else if(strlen(out)){
				responses[i].addresses = alloca(strlen(out) + 1);
				strcpy(responses[i].addresses, out);
			}
			if(pdc&&pdc->interface){
				responses[i].ifname= alloca(strlen(pdc->interface) + 1);
				strcpy(responses[i].ifname, pdc->interface);
			}
			
		}
    }

finally:
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_SUCCESS, responses,
                           number_of_contexts * sizeof(RIL_Data_Call_Response_v6));
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, responses,
                           number_of_contexts * sizeof(RIL_Data_Call_Response_v6));

    /*
     * To keep internal list up to date all deactivated contexts are removed
     * from modem and interface is set to DOWN...
     */
    //cleanupPDPContextList(responses, number_of_contexts);

    goto exit;

error:
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, NULL, 0);

exit:
    at_response_free(atresponse);
	}

}

/**
 * RIL_UNSOL_PDP_CONTEXT_LIST_CHANGED
 *
 * Indicate a PDP context state has changed, or a new context
 * has been activated or deactivated.
*
 * See also: RIL_REQUEST_PDP_CONTEXT_LIST
 */
void onDataCallListChanged(void *param)
{
    requestOrSendPDPContextList(NULL);
}

/**
 * RIL_REQUEST_DATA_CALL_LIST
 *
 * Queries the status of PDP contexts, returning for each
 * its CID, whether or not it is active, and its PDP type,
 * APN, and PDP adddress.
 * replaces RIL_REQUEST_PDP_CONTEXT_LIST
 *
 * "data" is NULL
 * "response" is an array of RIL_Data_Call_Response_v6
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE (radio resetting)
 *  GENERIC_FAILURE
 */
void requestDataCallList(void *data, size_t datalen, RIL_Token t)
{
    requestOrSendPDPContextList(&t);
}

/**
 * RIL_REQUEST_SETUP_DATA_CALL
 *
 * Configure and activate PDP context for default IP connection.
 *
 ok
 */
void requestSetupDataCall(void *data, size_t datalen, RIL_Token t)
{
    const char *radio,*prof,*apn,*user,*pass,*auth,*type;
    int err=0;	
	char* command;
	ATResponse *p_response;	
	pthread_t thread;
	PDATACALL_CONT dc;
	//note apn/user/pass may be null
	radio = ((const char **)data)[0];
	prof =((const char **)data)[1];
    apn =  ((const char **)data)[2];
	user = ((const char **)data)[3];
	pass = ((const char **)data)[4];
	auth = ((const char **)data)[5];
	type = ((const char **)data)[6];
	if(!apn){
		RIL_onRequestComplete(t, PDP_FAIL_MISSING_UKNOWN_APN, NULL, 0);
		return;
	}

	dc = getdc();
	if(!dc){
		RIL_onRequestComplete(t, PDP_FAIL_INSUFFICIENT_RESOURCES, NULL, 0);
		return;		
	}
	dc->apn = strdup(apn);
	if(user)
	{
		dc->user = strdup(user);
	}
	else
	{
		//fall back to cdma null user mode
		if(kRIL_HW_MC2716 ==rilhw->model)
			dc->user = strdup("card");		
	}
	if(pass)
	{
		dc->pass = strdup(pass);		
	}
	else
	{
		//fall back to cdma null password mode		
		if(kRIL_HW_MC2716 ==rilhw->model)
			dc->pass = strdup("card");		
	}

	//some other optional information
	if(radio)
		dc->radio = strdup(radio);
	if(prof)
		dc->prof = strdup(prof);
	if(auth)
		dc->auth = strdup(auth);
		
	if(type)
		dc->type = strdup(type);
	else
		dc->type = strdup("IP");
	
    
  	DBG("requesting data connection to APN[%s],user:[%s] pwd:[%s],radio[%s],profile[%s],auth[%s],type[%s] on interface[%s]\n", 
		apn,
		user?user:"null",
		pass?pass:"null",
		radio?radio:"unknown",
		prof?prof:"unknown",
		auth?auth:"unknown",
		type?type:"unknown",
		dc->interface);

	//setup PDP context	
	if(kPREFER_NETWORK_TYPE_CDMA_EVDV!=rilhw->prefer_net)
	{	

		
		//deactivate PDP context (format <state>[,<cid>[,<cid>[,<cid>[,...]]]]
		asprintf(&command, 
			"AT+CGACT=0,%s",dc->cid);	
		at_send_command(command, NULL);
		free(command);

		/* packet-domain event reporting */
		err = at_send_command("AT+CGEREP=1,0",NULL);

		//active PDP context format:
		//AT+CGDCONT=[<cid> [,<PDP_type>[,<APN>[,<PDP_addr>[,<d_comp> 
		//[,<h_comp>]]]]]] 
		//no compress
		asprintf(&command, 
			"AT+CGDCONT=%s,\"%s\",\"%s\",,0,0",
			dc->cid,dc->type,dc->apn);			
		err = at_send_command(command,&p_response);		
		free(command);

		/* Set required QoS params to default */		
		asprintf(&command, 
			"AT+CGQREQ=%s",dc->cid);	
		at_send_command(command, NULL);
		free(command);

		/* Set minimum QoS params to default */
		/* Set required QoS params to default */		
		asprintf(&command,"AT+CGQMIN=%s",dc->cid);	
		at_send_command(command, NULL);
		free(command);

		/* Attach to GPRS network */
		
		asprintf(&command, 
			"AT+CGATT=1");	
		at_send_command(command, NULL);
		free(command);	

		if(err != 0 || p_response->success == 0){
			ERROR("PDP activate failed cid[%s]\n",dc->cid);
			goto DATACALL_FAILED;
		}
	}
	
	//async operaion
	dc->token = t;
	dc->active = DATACALL_LINKSTATE_DOWN;
	err = pthread_create(&thread, NULL,pdp_manager, dc);
	if(err<0){
		ERROR("failed to create pdp manager for cid %s\n",dc->cid);
		goto DATACALL_FAILED;
	}
	
    // Start data on PDP context 1
    send_pdp_command(dc,EVENT_START);

	return;
	
DATACALL_FAILED:
	freedc(dc);	

}

/**
 * RIL_REQUEST_DEACTIVATE_DEFAULT_PDP
 *
 * Deactivate PDP context created by RIL_REQUEST_SETUP_DEFAULT_PDP.
 *
 * See also: RIL_REQUEST_SETUP_DEFAULT_PDP.
 */
void requestDeactivateDataCall(void *data, size_t datalen, RIL_Token t)
{
	const char * cid = ((const char**)data)[0];	
	PDATACALL_CONT dc = finddc(cid);
	//hang up current connection
	DBG("deactivate PDP cid=%s dc[%p]",cid,dc);
	if(kPREFER_NETWORK_TYPE_CDMA_EVDV==rilhw->prefer_net)
	{
		at_send_command("ATH",NULL);		
	}
	else
	{
		char * cmd;
		asprintf(&cmd,"AT+CGACT=0,%s",cid);
		at_send_command(cmd,NULL);
		free(cmd);		
	}
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	if(dc)	{
		send_pdp_command(dc,EVENT_STOP);	
	}
	

}

/**
 * RIL_REQUEST_LAST_PDP_FAIL_CAUSE
 * 
 * Requests the failure cause code for the most recently failed PDP 
 * context activate.
 *
 * See also: RIL_REQUEST_LAST_CALL_FAIL_CAUSE.
 *  
 ok
 */
void requestLastDataCallFailCause(void *data, size_t datalen, RIL_Token t)
{
	int lastFailCause=PDP_FAIL_ERROR_UNSPECIFIED;
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &lastFailCause,sizeof(int));
}



