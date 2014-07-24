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
#include "ptt.h"

static char* NextSplit(char **p_cur,char split)
{
    if (*p_cur == NULL)
        return NULL;

    while (**p_cur != '\0' && **p_cur != split)
        (*p_cur)++;

    if (**p_cur == split)
        return (*p_cur);
    return NULL;
}
//parse +CGIU: 2;0; ”12345”;1,2; 1,10,0,””; 2,0,0,”X-man”
static int parse_cgiu(char* line){
  char *start,*p,*p1;
  int group_number,dyn_group_number;
  char* tun;
  int emergency_type,emergency_number;
  int gid,gpriority,gstate;
  char* gname;
  DBG("CGIU line:%s",line);
  start = line;
  at_tok_start(&start);//skip prefix XXX: 
  emergency_type=emergency_number=0;
  p = start;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_nextint(&start,&group_number);
  DBG("groupnumber=%d",group_number);
  
  start = p = ++p1;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_nextint(&start,&dyn_group_number);
  DBG("dyn groupnumber=%d",dyn_group_number);

  start = p = ++p1;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_nextstr(&start,&tun);
  DBG("tun=%s",tun);

  start = p = ++p1;
  p1 = NextSplit(&p,';');
  if(p1) *p1='\0';
  at_tok_start(&start);
  at_tok_nextint(&start,&emergency_type);
  at_tok_nextint(&start,&emergency_number);
  DBG("emergency_type=%d,emergency_number=%d",emergency_type,emergency_number);

  do {
    start = p = ++p1;
    if(at_tok_hasmore(&start)){
    p1 = NextSplit(&p,';');if(p1) *p1='\0';
    gid=gpriority=gstate=0;
    gname=NULL;
    at_tok_nextint(&start,&gid);
    at_tok_nextint(&start,&gpriority);
    at_tok_nextint(&start,&gstate);
    at_tok_nextstr(&start,&gname);
    DBG("group->gid[%d],gpriority[%d],gstate[%d],gname[%s]",gid,gpriority,gstate,gname);
    }else {
      break;
    }
    if(!p1) break;
    
  }while(1);

  return 0;
  
  
}
/*
 *case
*/
int get_ptt_group_info(void){
  ATResponse *atresponse = NULL;
  int err;
  char* line;

  err = at_send_command_singleline("AT+CGIU","+CGIU:", &atresponse);
  if(err != 0 || atresponse->success == 0){
    ERROR("cgiu query failed\n");
    goto ptt_get_failed;
  }
  line = strdup(atresponse->p_intermediates->line);
 //+CGIU: 2;0; ”12345”;1,2; 1,10,0,””; 2,0,0,”X-man”
  parse_cgiu(line);  
  free(line);
  
ptt_get_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}
int join_ptt_group(int gid,int priority){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CTGS=%d,%d",gid,priority);
  err = at_send_command_singleline(cmd,"+CTGS:", &atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("ctgs query failed cme error=%d\n",cme_error);    
    goto join_ptt_group_failed;
  }
  DBG("Join GID[%d],Priority[%d] okay",gid,priority);
join_ptt_group_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}

int quit_ptt_group(int gid){
  return 0;
}
int request_ptt_group_master_call(int instance,int aiservice,int priority,int pid){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CAPTTD=%d,%d,%d,\"%d\"",instance,aiservice,priority,pid);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTD failed cme error=%d\n",cme_error);    
    goto capttd_failed;
  }
  
  DBG("call okay");

capttd_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}
int release_ptt_group_master_call(int instance,int pid){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CAPTTR=%d,%d",instance,pid);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
   if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTR failed cme error=%d\n",cme_error);    
    goto capttr_failed;
  }
  
  DBG("release call okay");

capttr_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}

static struct PttCall currcall;

int request_ptt_group_p2p_call(int instance,int aiservice,int priority,int pid){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  
  asprintf(&cmd,"AT+CAPTTD=%d,%d,%d,\"%d\"",instance,aiservice,priority,pid);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTD failed cme error=%d\n",cme_error);    
    goto capttd_failed;
  }
  
  DBG("p2p call okay");

  pttcall_call_info_indicate(ePttCallActive,ePttCallInstanceDefault,ePttCallStatusProgressing,eAirInterfaceServiceVoiceP2PCall,pid,0);

capttd_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}

int request_ptt_group_p2p_call2(int instance,char* address){
  ATResponse *atresponse = NULL;
  int err;
  char* line;
  char* cmd;
  int pid=0;
  
  asprintf(&cmd,"AT+CAPTTD=%d,%d,%d,\"%s\"",instance,eAirInterfaceServiceVoiceP2PCall,ePttCallRefPriorityNormal,address);
  err = at_send_command(cmd,&atresponse);
  free(cmd);
  if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CAPTTD failed cme error=%d\n",cme_error);    
    goto capttd_failed;
  }
  pid = atoi(address);
  pttcall_call_info_indicate(ePttCallActive,ePttCallInstanceDefault,ePttCallStatusProgressing,eAirInterfaceServiceVoiceP2PCall,pid,0);
  DBG("p2p call okay");

capttd_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
  
}

int hook_ptt_group_p2p_call(void){
  ATResponse *atresponse = NULL;
  int err;
     
  err = at_send_command("AT+CATA",&atresponse);
   if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CATA failed cme error=%d\n",cme_error);    
    goto cata_failed;
  }
  
  DBG("answer call okay");

cata_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;
}
int hangup_ptt_group_p2p_call(void){
  ATResponse *atresponse = NULL;
  int err;
     
  err = at_send_command("AT+CATH",&atresponse);
   if(err != 0 || atresponse->success == 0){
    int cme_error;
    at_get_cme_error(atresponse,(ATCmeError*)&cme_error);
    ERROR("CATA failed cme error=%d\n",cme_error);    
    goto cath_failed;
  }
  
  DBG("hangup call okay");

cath_failed:
  if(atresponse)
    at_response_free(atresponse);
  return err;

}



static int ctccStateToRILState(int state, RIL_CallState *p_state)
{
    switch(state) {
        case 0:
	case 2: *p_state = RIL_CALL_ACTIVE;   return 0;
        case 1: *p_state = RIL_CALL_HOLDING;  return 0;
       // case 2: *p_state = RIL_CALL_DIALING;  return 0;
        case 3: *p_state = RIL_CALL_ALERTING; return 0;
        case 4: case 99: *p_state = RIL_CALL_INCOMING; return 0;
        case 5: *p_state = RIL_CALL_WAITING;  return 0;
        default: return -1;
    }
}
static void convert_pttcall_to_rilcall(struct PttCall* ptt,RIL_Call* ril){
  ril->index = ptt->inst+1;//inst seems always start from 0;
  ctccStateToRILState(ptt->state,&ril->state);
  ril->number = ptt->number;
  #if (RIL_VERSION>2)
  ril->uusInfo = NULL;
  #endif
}
//There is no command to query current call 
void requestGetCurrentCallsPTT(void *data, size_t datalen, RIL_Token t){
    int err;
    ATResponse *p_response;
    ATLine *p_cur;
    int countCalls;
    int countValidCalls;
    RIL_Call *p_calls=NULL;
    RIL_Call **pp_calls=NULL;
    int i;

    //count the calls
    countCalls = currcall.active?1:0;
    countValidCalls = 0; 
    if(countCalls){
    //yes, there's an array of pointers and then an array of structures 
    pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
    p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
    memset (p_calls, 0, countCalls * sizeof(RIL_Call));

    // init the pointer array 
    for(i = 0; i < countCalls ; i++) {
        pp_calls[i] = &(p_calls[i]);
    }

     convert_pttcall_to_rilcall(&currcall,p_calls+countValidCalls);
     countValidCalls++;
   }
    
    RIL_onRequestComplete(t, RIL_E_SUCCESS, pp_calls,
            countValidCalls * sizeof (RIL_Call *));

    return;
error:
	//
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);    
}

void pttcall_call_info_indicate(int active,int inst,int callstatus,int aiservice,int pid,int isMT){
  currcall.active = active;
  if(active){
     currcall.inst = inst;
     currcall.state = callstatus;
     if(pid){
     	if(currcall.number) free(currcall.number);
     	asprintf(&currcall.number,"%d",pid);
     }
     currcall.isMT = isMT;
  }
}

void requestPttQueryAvailableGroups(void *data, size_t datalen, RIL_Token t){	
}
void requestPttGroupSetup(void *data, size_t datalen, RIL_Token t){	
}
void requestPttGroupRelease(void *data, size_t datalen, RIL_Token t){	
}
void requestPttCallDial(void *data, size_t datalen, RIL_Token t){	
}
void requestPttCallHangup(void *data, size_t datalen, RIL_Token t){	
}
void requestPttCurrentGroupScanlistUpdate(void *data, size_t datalen, RIL_Token t){	
}
void requestPttQueryBlockedIndicator(void *data, size_t datalen, RIL_Token t){	
}
void requestPttDeviceInfo(void *data, size_t datalen, RIL_Token t){	
}
void requestPttBizState(void *data, size_t datalen, RIL_Token t){
	
}

