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

extern int ifc_init(void);
extern void ifc_close(void);
extern int ifc_set_addr(const char *name, in_addr_t addr);
extern int ifc_set_prefixLength(const char *name, int prefixLength);
extern int ifc_up(const char *name);

static int ndis_stat_query(void){
    int err,state;
    char* line;	
    ATResponse *p_response;
    state = 0;
	//query NDIS status
	err = at_send_command_singleline("AT^NDISSTATQRY?","^NDISSTATQRY:", &p_response);
	if(err != 0 || p_response->success == 0){
	    ERROR("NDIS QUERY failed\n");
	    goto ndis_query_failed;
	}
line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto ndis_query_failed;

    err = at_tok_nextint(&line, &state);
    if (err < 0) goto ndis_query_failed;
    DBG("%s state=%d",__func__,state);

ndis_query_failed:
    at_response_free(p_response);
    return state;
}

static char* NextDot(char **p_cur)
{
    if (*p_cur == NULL)
        return NULL;

    while (**p_cur != '\0' && **p_cur != '.')
        (*p_cur)++;

    if (**p_cur == '.')
        return (*p_cur);
    return NULL;
}

static int NetmaskToPrefixLength(char* subnet)
{

    int prefixLength = 0;
    char netmask[20]={0};
    uint32_t m;
    char* s,*p,*p1;
    if(!subnet) return 0;
    strcpy(netmask,subnet);
    s = p1 = netmask;
    p = NextDot(&p1);p1++;
    *p='\0';
    m = atoi(s)<<24;
    s = p1 =p++;
    p = NextDot(&p1);p1++;
    *p='\0';
    m += atoi(s)<<16;
    s = p1 =p++;
    p = NextDot(&p1);p1++;
    *p='\0';
    m += atoi(s)<<8;
    s=p++;
    m += atoi(s);
    DBG("mask = %#x",m);

    while (m & 0x80000000) {
        prefixLength++;
        m = m << 1;
    }
    return prefixLength;
}


void requestDataCallListEM350(void *data, size_t datalen, RIL_Token t){
    int err,state;	
    ATResponse *p_response=NULL;
    RIL_Data_Call_Response_v6 response;
    int bearid;
    char* line;
    char* apn,*subnet,*dns1,*dns2;
    char* addr_subnet;
    char addresses[128]={0};
    char prop_key[PROPERTY_KEY_MAX];
    char prop_value[PROPERTY_VALUE_MAX];
    err = at_send_command_singleline("AT+CGCONTRDP=1","+CGCONTRDP:", &p_response);
if (err < 0 || p_response->success == 0) {
        goto pdp_query_failed;
    }
   line = p_response->p_intermediates->line;
 err = at_tok_start(&line);
    if (err < 0) goto pdp_query_failed;

    err = at_tok_nextint(&line, &response.cid);
    if (err < 0) goto pdp_query_failed;

    err = at_tok_nextint(&line, &bearid);
    if (err < 0) goto pdp_query_failed;
  
    err = at_tok_nextstr(&line, &apn);
    if (err < 0) goto pdp_query_failed;
   
    #if 0
    err = at_tok_nextstr(&line, &response.addresses);
    if (err < 0) goto pdp_query_failed;
    property_set("net.hed0.address",response.addresses);

    err = at_tok_nextstr(&line, &subnet);
    if (err < 0) goto pdp_query_failed;
    property_set("net.hed0.subnet",subnet);
    #else
    err = at_tok_nextstr(&line, &addr_subnet);
    {
      char suffix[12];
      char* p,*p1;
      p=p1=addr_subnet;
      subnet=NextDot(&p1);p1++;
      subnet=NextDot(&p1);p1++;
      subnet=NextDot(&p1);p1++;
      subnet=NextDot(&p1);p1++;
      *subnet='\0';
       DBG("addr=%s,subnet=%s",p,++subnet);
      strcpy(addresses,p);
      DBG("addresses=%s",addresses);
      property_set("net.hed0.address",addresses);    
      //subnet++;
      //property_set("net.hed0.subnet",subnet);
      //sprintf(suffix,"/%d",NetmaskToPrefixLength(subnet));
      //strcat(addresses,suffix);
      response.addresses = addresses;
        

    }
    #endif
    err = at_tok_nextstr(&line, &response.gateways);
    if (err < 0) goto pdp_query_failed;    
    property_set("net.hed0.gw",response.gateways);

    dns1=NULL;
    err = at_tok_nextstr(&line, &dns1);
    if (err < 0) goto pdp_query_failed;

    dns2=NULL;
    at_tok_nextstr(&line, &dns2);
    response.dnses= malloc(strlen(dns1)+strlen(dns2)+10);
    if(dns1){
      strcpy(response.dnses,dns1);
      property_set("net.hed0.dns1",dns1);
   }
    if(dns2){
      strcat(response.dnses,dns2);
      property_set("net.hed0.dns2",dns2);
  }
    

			response.status = PDP_FAIL_NONE;
			response.suggestedRetryTime = 0;
			
			response.active = 2;
			response.ifname = "hed0";
			response.type = "IP";

			DBG("NDIS linkup localip[%s],dns[%s],gw[%s]\n",
				response.addresses,
				response.dnses,
				response.gateways);
	if(t)
	  RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(RIL_Data_Call_Response_v6));
	else
	  RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, &response, sizeof(RIL_Data_Call_Response_v6));
	
     at_response_free(p_response);
     free(response.dnses);
    return;
pdp_query_failed:
  if(p_response)
	at_response_free(p_response);
  if(t)
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL,0);
  else
    RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, NULL, 0);
}

static const char *ipaddr_to_string(in_addr_t addr)
{
    struct in_addr in_addr;

    in_addr.s_addr = addr;
    return inet_ntoa(in_addr);
}

static int net_config(char* ifname,in_addr_t address,uint32_t prefixLength){
   ifc_init();
   if (ifc_up(ifname)) {
        ERROR("failed to turn on interface %s: %s\n", ifname, strerror(errno));
        ifc_close();
        return -1;
    }
    if (ifc_set_addr(ifname, address)) {
        ERROR("failed to set ipaddr %s: %s\n", ipaddr_to_string(address), strerror(errno));
        ifc_close();
        return -1;
    }
    if (prefixLength&&ifc_set_prefixLength(ifname, prefixLength)) {
        ERROR("failed to set prefixLength %d: %s\n", prefixLength, strerror(errno));
        ifc_close();
        return -1;
    }
   ifc_close();
   return 0;
}

void requestSetupDataCallEM350(void *data, size_t datalen, RIL_Token t){
    const char *radio,*prof,*apn,*user,*pass,*auth,*type;
    ATResponse *p_response;
    char* command;
    int state,timeout=10;
    int err;
	//note apn/user/pass may be null
	radio = ((const char **)data)[0];
	prof =((const char **)data)[1];
    apn =  ((const char **)data)[2];
	user = ((const char **)data)[3];
	pass = ((const char **)data)[4];
	auth = ((const char **)data)[5];
	type = ((const char **)data)[6];
	
    state  = ndis_stat_query();
    if(state){
      at_send_command("AT^NDISDUP=1,0",NULL);
    }

   	asprintf(&command, 
			"AT^NDISDUP=1,1");//,%s,%s,%s,%d",apn,user,pass,(int)type);	
	err = at_send_command(command, &p_response);
	free(command);

	//check response now
	if(err != 0 || p_response->success == 0){
	    ERROR("PDP activate failed cid[1]\n");
	    goto error;
	}
	at_response_free(p_response);
	//waiting for 
	do{
       if(!timeout){
          at_send_command("AT^NDISDUP=1,0",NULL);
          ERROR("NDISDUP timeout");
          goto error;
       }
       sleep(1);
       state=ndis_stat_query();
       DBG("ndis state=%d",state);
       if(1==state) break;
       timeout--;
     }while(1);
        
        DBG("NDIS dial success");
        
	//run here means NDIS okay,fetch address now
	requestDataCallListEM350(0,0,t);

	#if 1
	//get ipaddress,dns,gw
	{
		char prop_key[PROPERTY_KEY_MAX];
		char address[128],subnet[128];
		char* interface="hed0";
		in_addr_t ipaddr;
		uint32_t prefixLength=0;		
		sprintf(prop_key,"net.%s.address",interface);
		property_get(prop_key,address,"");
		if(strlen(address)){
		   inet_aton(address,(struct in_addr*)&ipaddr);
		}
		
		sprintf(prop_key,"net.%s.subnet",interface);
		property_get(prop_key,subnet,"");
		DBG("config %s ipaddr %#x",interface,ipaddr);
                net_config(interface,ipaddr,prefixLength);
	}
	#endif
	return;
error:
      RIL_onRequestComplete(t, PDP_FAIL_ERROR_UNSPECIFIED, NULL, 0);	
}
void requestDeactivateDataCallEM350(void *data, size_t datalen, RIL_Token t){
    const char * cid = ((const char**)data)[0];	
    at_send_command("AT^NDISDUP=1,0", NULL);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
