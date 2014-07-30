#include <stdio.h>
#include <telephony/ril.h>
#include <cutils/properties.h>
#include <assert.h>
#include "misc.h"
#include "ril-handler.h"
#include "itu_network.h"



enum cdma_roaming
{
	kRoamingOff,
	kRoamingOn
};

enum eNetMode
{
	kNetModeOutOfService = 0,
	kNetModeAMPS,
	kNetModeCDMA = 2,
	kNetModeGSMGPRS,
	kNetModeHDR = 4,
	kNetModeWCDMA,
	kNetModeGPS,
	kNetModeGSMWCDMA,
	kNetModeCDMAHDR = 8
};

#define REPOLL_OPERATOR_SELECTED 30     /* 30 * 2 = 1M = ok? */
static const struct timeval TIMEVAL_OPERATOR_SELECT_POLL = { 2, 0 };

static  int cdma_netmode = kNetModeCDMAHDR;

RIL_RadioState sState = RADIO_STATE_UNAVAILABLE;
pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;

/*
typedef enum {
    RADIO_TECH_UNKNOWN = 0,
    RADIO_TECH_GPRS = 1,
    RADIO_TECH_EDGE = 2,
    RADIO_TECH_UMTS = 3,
    RADIO_TECH_IS95A = 4,
    RADIO_TECH_IS95B = 5,
    RADIO_TECH_1xRTT =  6,
    RADIO_TECH_EVDO_0 = 7,
    RADIO_TECH_EVDO_A = 8,
    RADIO_TECH_HSDPA = 9,
    RADIO_TECH_HSUPA = 10,
    RADIO_TECH_HSPA = 11,
    RADIO_TECH_EVDO_B = 12,
    RADIO_TECH_EHRPD = 13,
    RADIO_TECH_LTE = 14,
    RADIO_TECH_HSPAP = 15, // HSPA+
    RADIO_TECH_GSM = 16 // Only supports voice
} RIL_RadioTechnology;
*/
static char* radio_tech_name[]={
	"unknown",
	"GPRS",
	"EDGE",
	"UMTS",
	"IS95A",
	"IS95B",
	"1xRTT",
	"EvDo Rev.0",
	"EvDo Rev.A",
	"HSDPA",
	"HSUPA",
	"HSPA",
	"EvDo Rev.B",
	"EHRPD",
	"LTE",
	"HSPAP",
	"GSM",
};

static char* registered_status[] = 
{
	"not registered",
	"registered[home]",
	"not registered in searching",
	"registration denied",
	"unknown",
	"registered[roaming]",
	"not registered and emergency call enabled",
	"registered[home] and emergency call enabled",
	"not registered in searching and emergency call enabled",
	"registration denied and emergency call enabled",	
	"unknown and emergency call enabled",
};

//------------------------private function -----------------------------

static int get_reg_stat(int registered,int  roaming)
{
	if(registered == 0){
		return 0; //0 - Not registered, MT is not currently searching a new operator to register
	}else if(registered == 4){
		return 2; // 2 - Not registered, but MT is currently searching  a new operator to register
	}else if(roaming == 1){
	        return 5;// Registered, roaming
	 }else{
	 	return 1;// Registered, home network
	 }
}

/*

  0 - Unknown, 1 - GPRS, 2 - EDGE, 3 - UMTS,
 *                                  4 - IS95A, 5 - IS95B, 6 - 1xRTT,
 *                                  7 - EvDo Rev. 0, 8 - EvDo Rev. A,
 *                                  9 - HSDPA, 10 - HSUPA, 11 - HSPA,
 *                                  12 - EVDO Rev B


 <cdma_netmode>
0   no service
1   AMPS
2   CDMA
3   GSM/GPRS
4   HDR\u6a21\u5f0f
5   WCDMA\u6a21\u5f0f
6   GPS\u6a21\u5f0f
7   GSM/WCDMA
8   CDMA/HDR HYBRID

*/
static int networktype( int sysmode)
{
	int map[] = {0,8,6,1,8,3,3,3,8};
	if( sysmode >= 9 || sysmode < 0){
		return 0;
	}
	
	return map[sysmode];
}

static int ME3760_network_type_map(int act){
	/*
	0  GSM ÖÆÊ½ 
	1  GSM  ÔöÇ¿ÐÍ 
	2  UTRAN ÖÆÊ½ 
	3  GSM w/EGPRS 
	4  UTRAN w/HSDPA 
	5  UTRAN w/HSUPA 
	6  UTRAN w/HSDPA and HSUPA 
	7  E-UTRAN 
	*/
	int type_maps[]={
		1,2,3,1,9,10,11,11
	};
	if(act>7)
		act=7;
	if(act<0)
		act=2;
	return type_maps[act];
}
static int is_cdma1x_net( int netmode){
	int ret = 0;
	switch(netmode){
		case kNetModeOutOfService:
		case kNetModeCDMA:
			ret = 1;
			break;
		case kNetModeHDR:
		case kNetModeCDMAHDR:
			ret = 0;
			break;
	}
	return ret;
}
/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC
 *
 * Specify that the network should be selected automatically
 *
 * "data" is NULL
 * "response" is NULL
 *
 * This request must not respond until the new operator is selected
 * and registered
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  ILLEGAL_SIM_OR_ME
 *  GENERIC_FAILURE
 *
 * Note: Returns ILLEGAL_SIM_OR_ME when the failure is permanent and
 *       no retries needed, such as illegal SIM or ME.
 *       Returns GENERIC_FAILURE for all other causes that might be
 *       fixed by retries.
 *
 */
void requestSetNetworkSelectionAutomatic(void *data, size_t datalen,
                                         RIL_Token t)
{
	if((kRIL_HW_MC2716 == rilhw->model)||
		(kRIL_HW_MC8630 == rilhw->model))
	{
		//cdma device does not support 
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		return;
	}
	else //gsm
	{
		int err = 0;		
		err = at_send_command("AT+COPS=0", NULL);
		if (err < 0)
		goto error;
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		return;
	}
	error:
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	
}



/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL
 *
 * Manually select a specified network.
 *
 * "data" is const char * specifying MCCMNC of network to select (eg "310170")
 * "response" is NULL
 *
 * This request must not respond until the new operator is selected
 * and registered
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  ILLEGAL_SIM_OR_ME
 *  GENERIC_FAILURE
 *
 * Note: Returns ILLEGAL_SIM_OR_ME when the failure is permanent and
 *       no retries needed, such as illegal SIM or ME.
 *       Returns GENERIC_FAILURE for all other causes that might be
 *       fixed by retries.
 *
 */
void requestSetNetworkSelectionManual(void *data, size_t datalen,
                                      RIL_Token t)
{
	int err = 0;
	char *cmd = NULL;
	ATResponse *atresponse = NULL;
	if((kRIL_HW_MC2716 == rilhw->model)||
		(kRIL_HW_MC8630 == rilhw->model))
    {
    	//cdma device does not support
		RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL,0);
		return;
	}
	else //gsm
	{
		    /* 
	     * AT+COPS=[<mode>[,<format>[,<oper>[,<AcT>]]]]
	     *    <mode>   = 4 = Manual (<oper> field shall be present and AcT optionally) with fallback to automatic if manual fails.
	     *    <format> = 2 = Numeric <oper>, the number has structure:
	     *                   (country code digit 3)(country code digit 2)(country code digit 1)
	     *                   (network code digit 2)(network code digit 1) 
	     */

	    const char *mccMnc = (const char *) data;

	    /* Check inparameter. */
	    if (mccMnc == NULL) {
	        goto error;
	    }
	    /* Build and send command. */
	    asprintf(&cmd, "AT+COPS=4,2,\"%s\"", mccMnc);
	    err = at_send_command(cmd, &atresponse);
	    if (err < 0 || atresponse->success == 0)
	        goto error;

		free(cmd);
		at_response_free(atresponse);
	    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		return ;
	}
error:
	free(cmd);
	at_response_free(atresponse);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

#if 0
 typedef enum {
    PREF_NET_TYPE_GSM_WCDMA                = 0, /* GSM/WCDMA (WCDMA preferred) */
    PREF_NET_TYPE_GSM_ONLY                 = 1, /* GSM only */
    PREF_NET_TYPE_WCDMA                    = 2, /* WCDMA  */
    PREF_NET_TYPE_GSM_WCDMA_AUTO           = 3, /* GSM/WCDMA (auto mode, according to PRL) */
    PREF_NET_TYPE_CDMA_EVDO_AUTO           = 4, /* CDMA and EvDo (auto mode, according to PRL) */
    PREF_NET_TYPE_CDMA_ONLY                = 5, /* CDMA only */
    PREF_NET_TYPE_EVDO_ONLY                = 6, /* EvDo only */
    PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO = 7, /* GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL) */
    PREF_NET_TYPE_LTE_CDMA_EVDO            = 8, /* LTE, CDMA and EvDo */
    PREF_NET_TYPE_LTE_GSM_WCDMA            = 9, /* LTE, GSM/WCDMA */
    PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA  = 10, /* LTE, CDMA, EvDo, GSM/WCDMA */
    PREF_NET_TYPE_LTE_ONLY                 = 11  /* LTE only */
} RIL_PreferredNetworkType;
#endif
 
void requestSetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err = 0;
    int rat;
    int arg;
    char *cmd = NULL;
    RIL_Errno errno = RIL_E_GENERIC_FAILURE;

    rat = ((int *) data)[0];

	if(sState == RADIO_STATE_OFF){
		RIL_onRequestComplete(t,RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		return;
	}

	if((kRIL_HW_MC2716 == rilhw->model)||
		(kRIL_HW_MC8630 == rilhw->model))
	{
	 	#if CDMA_MASQUERADING
		if(1==rat)
			rat=5;
		else if(2==rat)
			rat=6;
		else 
			rat=4;
		#endif
	    switch (rat) 
		{
			// EVDO
			case 2:	case 6:	arg = kNetModeHDR; break;
			// CDMA
	    	case 1:	case 5: arg = kNetModeCDMA;break;
			// CDMA/EVDO auto mode
		    case 0:	case 3:	case 4:	case 7: default: arg = kNetModeCDMAHDR;	break;		
	    }

	    asprintf(&cmd, "AT^PREFMODE=%d", arg);
	    err = at_send_command(cmd, &atresponse);
	    if (err < 0 || atresponse->success == 0){
			errno = RIL_E_GENERIC_FAILURE;
	        goto error;
		}		
		free(cmd);
		at_response_free(atresponse);

	    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		return ;
		
	}else if(kRIL_HW_EM350==rilhw->model){
	    RIL_onRequestComplete(t, errno, NULL, 0);
	    return ;
	}else //gsm
	{


		if(rilhw->model == kRIL_HW_MF210)
		{
			//automatic nework selection GSM+WCDMA,WCDMA preferred
			err = at_send_command("AT+ZSNT=0,0,2", NULL);
			if (err < 0)
			goto error;
		
		}
		else if(rilhw->model == kRIL_HW_M305)
		{
			RIL_onRequestComplete(t,RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
			return;		
		}
		else
		{
			//change to using COPS,which is invalidated immediately
			#if 0
			switch (rat) 
			{
				case 0: case 3: case 4: case 7:
					arg = 4;   // atuo
					DBG("set preferred network is auto");
					break;
				case 1: case 5:
					arg = 13;	// GSM
					DBG("set preferred network is GSM");
					break;
				case 2: case 6:
					arg = 14;	// WCDMA
					DBG("set preferred network is WCDMA");
					break;	
				default:
				RIL_onRequestComplete(t,RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
				return;
			}
			asprintf(&cmd, "AT+ZMDS=%d", arg);
			#else
			switch (rat)
			{
				case 0: case 3: case 4: case 7:
					arg = 0;   // atuo
					asprintf(&cmd, "AT+COPS=%d", arg);
					DBG("set preferred network is auto");
					break;
				case 1: case 5:
					arg = 0;	// GSM
					asprintf(&cmd, "AT+COPS=,,,%d", arg);
					DBG("set preferred network is GSM");
					break;
				case 2: case 6: default:
					arg = 2;	// WCDMA
					asprintf(&cmd, "AT+COPS=,,,%d", arg);
					DBG("set preferred network is WCDMA");
					break;
			}
			#endif
			err = at_send_command(cmd, &atresponse);
			if (err < 0 || atresponse->success == 0)
			{
				errno = RIL_E_GENERIC_FAILURE;
				goto error;
			}		
			free(cmd);
			at_response_free(atresponse);
		}

	    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		return;
	}

error:
	if(cmd)	free(cmd);
	at_response_free(atresponse);
    RIL_onRequestComplete(t, errno, NULL, 0);
}


/**
 * RIL_REQUEST_SIGNAL_STRENGTH
 *
 * Requests current signal strength and bit error rate.
 *
 * Must succeed if radio is on.
 */
 
void requestSignalStrength(void *data, size_t datalen, RIL_Token t)
{
    RIL_SignalStrength response;
    ATResponse *p_response = NULL;
    int err;
    char *line;

	int rssi,ber;
    err = at_send_command_singleline("AT+CSQ", "+CSQ:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }
	memset(&response,99,sizeof(RIL_SignalStrength));

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &rssi);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &ber);
    if (err < 0) goto error;

	if(kRIL_HW_M305==rilhw->model){
		//100~199 is TDSCDMA extension rscp
		if(rssi>100)
		{
			//mapping 100~191,199 to 0~31,99
			rssi-=100;
			if(rssi<99)
				rssi = (rssi*32)/92;
		}				
		response.GW_SignalStrength.signalStrength = rssi;			
	}
	#if (CDMA_MASQUERADING==0)
	else if((kRIL_HW_MC2716 == rilhw->model)||
		(kRIL_HW_MC8630 == rilhw->model)){
		ATResponse *p_response2 = NULL;
		int evdo_rssi;
		err = at_send_command_singleline("AT^HDRCSQ", "^HDRCSQ:", &p_response2);		
		if (err < 0 || p_response2->success == 0) {
			goto error;
		}
		
		line = p_response2->p_intermediates->line;
		
		err = at_tok_start(&line);
		if (err < 0) goto error;
		
		err = at_tok_nextint(&line, &evdo_rssi);
		if (err < 0) goto error;

		ber=99;
		
		response.CDMA_SignalStrength.dbm = (31*(125-rssi))/50;
		response.CDMA_SignalStrength.ecio=0;
		if(evdo_rssi==0)
			response.EVDO_SignalStrength.dbm = 99 ;
		else if(evdo_rssi==20)		
			response.EVDO_SignalStrength.dbm = 105;		
		else if(evdo_rssi==40)
			response.EVDO_SignalStrength.dbm = 90;
		else if(evdo_rssi==60)
			response.EVDO_SignalStrength.dbm = 75;
		else if(evdo_rssi==80)
			response.EVDO_SignalStrength.dbm = 60;
		else
			response.EVDO_SignalStrength.dbm = 60;
		response.EVDO_SignalStrength.ecio=0;	
		response.EVDO_SignalStrength.signalNoiseRatio = 8;

		at_response_free(p_response2);
	}
	#endif
	else if(kRIL_HW_EM350 == rilhw->model){
          ATResponse *atresponse = NULL;
          char *tok = NULL;
          int cellid,freq,rsrp,rsrq,rssnr;
          int unused;
          err = at_send_command_singleline("AT^CSGQRY?","^CSGQRY:",
                                     &atresponse);
          if (err < 0 || atresponse->success == 0){
               response.GW_SignalStrength.signalStrength = rssi;
	  response.LTE_SignalStrength.signalStrength = rssi;
	  if(99==rssi)
	  	response.LTE_SignalStrength.rsrp=0x7FFFFFFF;
	  else
		response.LTE_SignalStrength.rsrp=113-2*rssi;
	  response.LTE_SignalStrength.rsrq=0x7FFFFFFF;
	  response.LTE_SignalStrength.rssnr=0x7FFFFFFF;
	  response.LTE_SignalStrength.cqi=0x7FFFFFFF;	
          }else {
    
           tok = atresponse->p_intermediates->line;

    at_tok_start(&tok);
    at_tok_nextint(&tok, &cellid);
    at_tok_nextint(&tok, &freq);
    at_tok_nextint(&tok, &rsrp);
    at_tok_nextint(&tok, &rsrq);
    at_tok_nextint(&tok, &unused);
    at_tok_nextint(&tok, &unused);
    at_tok_nextint(&tok, &rssnr);
    if(rsrp<0) rsrp=-rsrp;rsrp/=8;    
    if(rsrq<0) rsrq=-rsrq;rsrq/=8;
    if(rssnr<0) rssnr=-rssnr;rssnr/=8;
    response.LTE_SignalStrength.signalStrength=rssi;
    response.LTE_SignalStrength.rsrp=rsrp;
    response.LTE_SignalStrength.rsrq=rsrq;
    response.LTE_SignalStrength.rssnr=rssnr;
    response.LTE_SignalStrength.cqi=0x7FFFFFFF;
}
      at_response_free(atresponse);
	
	}else{
		//GSM 0~31,99
		//TD 100~199
		//LTE 100~199
		if(rssi>=100){
			if(199==rssi)
				rssi=99;
			else {
				rssi = rssi-100;
				rssi >>=1;
			}
		}
		response.GW_SignalStrength.signalStrength = rssi;
	}
	response.GW_SignalStrength.bitErrorRate = ber;

   
    RIL_onRequestComplete(t, RIL_E_SUCCESS,& response, sizeof(response));

    at_response_free(p_response);
    return;

error:
    ERROR("requestSignalStrength must never return an error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}


/**
 * RIL_REQUEST_QUERY_AVAILABLE_NETWORKS
 *
 * Scans for available networks.
 ok
*/
static const char* networkStatusToRilString(int state)
{
	switch(state){
		case 0: return("unknown");	 break;
		case 1: return("available"); break;
		case 2: return("current");	 break;
		case 3: return("forbidden"); break;
		default: return NULL;
	}
}

void requestQueryAvailableNetworks(void *data, size_t datalen, RIL_Token t)
{
	/* We expect an answer on the following form:
	   +COPS: (2,"AT&T","AT&T","310410",0),(1,"T-Mobile ","TMO","310260",0)
	 */

	int err, operators, i, skip, status;
	ATResponse *p_response = NULL;
	char * c_skip, *line, *p = NULL;
	char ** response = NULL;

	if((kRIL_HW_MC2716 == rilhw->model)||
		(kRIL_HW_MC8630 == rilhw->model))
	{
		RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
		return;
	}
	err = at_send_command_singleline("AT+COPS=?", "+COPS:", &p_response);

	if (err < 0 || p_response->success == 0) {
		goto error;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	/* Count number of '(' in the +COPS response to get number of operators*/
	operators = 0;
	for (p = line ; *p != '\0' ;p++) {
		if (*p == '(') operators++;
	}

	response = (char **)alloca(operators * 4 * sizeof(char *));

	for (i = 0 ; i < operators ; i++ )
	{
		err = at_tok_nextstr(&line, &c_skip);
		if (err < 0) goto error;
		if (strcmp(c_skip,"") == 0)
		{
			operators = i;
			continue;
		}
		status = atoi(&c_skip[1]);
		response[i*4+3] = (char*)networkStatusToRilString(status);

		err = at_tok_nextstr(&line, &(response[i*4+0]));
		if (err < 0) goto error;

		err = at_tok_nextstr(&line, &(response[i*4+1]));
		if (err < 0) goto error;

		err = at_tok_nextstr(&line, &(response[i*4+2]));
		if (err < 0) goto error;

		err = at_tok_nextstr(&line, &c_skip);

		if (err < 0) goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, (operators * 4 * sizeof(char *)));
	at_response_free(p_response);
	return;

error:
	ERROR("ERROR - requestQueryAvailableNetworks() failed");
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}







/**
 * RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE
 *
 * Query current network selectin mode
 *
 * "data" is NULL
 *
 * "response" is int *
 * ((const int *)response)[0] is
 *     0 for automatic selection
 *     1 for manual selection
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 *
 */
void requestQueryNetworkSelectionMode(
                void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    int response = 0;
    char *line;

	if((kRIL_HW_MC2716 == rilhw->model)||
		(kRIL_HW_MC8630 == rilhw->model))
	{
		RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
	    return;
	}
    
    err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);

    if (err < 0) {
        goto error;
    }

    err = at_tok_nextint(&line, &response);

    if (err < 0) {
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
    at_response_free(p_response);
    return;
error:
    at_response_free(p_response);
    ERROR("requestQueryNetworkSelectionMode must never return error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

int rssi2dbm(int rssi,int evdo)
{
	int dbm;
	/* android spec
		dbm:
			evdo 65>75>90>105 . cdma 75>85>95>100
		ecio:
			90>110>130>150
		snr:
			0~8
	*/
	//MC2716 spec
	//1...30 = 31x(125-rssi)/50
	//assume rssi = 31x(125-dBm)/50
	//then dBm = 125-(rssix50)/31

	/*    we should map in reverse:
	    0~31 to 75~100 in cdma mode,that means 0~31 ->0~25
	    0~31 to 65~105 in evdo mode, that means 0~31 ->0~40

	    so:
	    dbm = (25-rssi x 25/31)+75  in cdma mode
	    dbm = (40-rssi x 40/31)+65  in evdo mode
	    
	    
	*/
	if(evdo){//evdo
		/*
		if(rssi >= 80 ) return 60;
		if(rssi == 60) return 75;
		if(rssi == 40) return 90;
		if(rssi == 20) return 105;
		return 125;
		*/
		return (25-(rssi*25)/31)+65;
	}else{//cdma
		/*
		if(16<=rssi && rssi <= 31 ) return 75;
		if(8<=rssi && rssi < 16) return 85;
		if(4<=rssi && rssi< 8) return 95;
		if(1<=rssi && rssi < 4) return 100;
		return 125;
		*/
		return (40-(rssi*40)/31)+75;
	}
}




/**
 * RIL_REQUEST_REGISTRATION_STATE
 *
 * Request current registration state
 *
 * "data" is NULL
 * "response" is a "char **"
 * ((const char **)response)[0] is registration state 0-6,
 *              0 - Not registered, MT is not currently searching
 *                  a new operator to register
 *              1 - Registered, home network
 *              2 - Not registered, but MT is currently searching
 *                  a new operator to register
 *              3 - Registration denied
 *              4 - Unknown
 *              5 - Registered, roaming
 *             10 - Same as 0, but indicates that emergency calls
 *                  are enabled.
 *             12 - Same as 2, but indicates that emergency calls
 *                  are enabled.
 *             13 - Same as 3, but indicates that emergency calls
 *                  are enabled.
 *             14 - Same as 4, but indicates that emergency calls
 *                  are enabled.
 *
 * ((const char **)response)[1] is LAC if registered on a GSM/WCDMA system or
 *                              NULL if not.Valid LAC are 0x0000 - 0xffff
 * ((const char **)response)[2] is CID if registered on a * GSM/WCDMA or
 *                              NULL if not.
 *                                 Valid CID are 0x00000000 - 0xffffffff
 *                                    In GSM, CID is Cell ID (see TS 27.007)
 *                                            in 16 bits
 *                                    In UMTS, CID is UMTS Cell Identity
 *                                             (see TS 25.331) in 28 bits
 * ((const char **)response)[3] indicates the available radio technology 0-7,
 *                                  0 - Unknown, 1 - GPRS, 2 - EDGE, 3 - UMTS,
 *                                  4 - IS95A, 5 - IS95B, 6 - 1xRTT,
 *                                  7 - EvDo Rev. 0, 8 - EvDo Rev. A,
 *                                  9 - HSDPA, 10 - HSUPA, 11 - HSPA,
 *                                  12 - EVDO Rev B
 * ((const char **)response)[4] is Base Station ID if registered on a CDMA
 *                              system or NULL if not.  Base Station ID in
 *                              decimal format
 * ((const char **)response)[5] is Base Station latitude if registered on a
 *                              CDMA system or NULL if not. Base Station
 *                              latitude is a decimal number as specified in
 *                              3GPP2 C.S0005-A v6.0. It is represented in
 *                              units of 0.25 seconds and ranges from -1296000
 *                              to 1296000, both values inclusive (corresponding
 *                              to a range of -90° to +90°).
 * ((const char **)response)[6] is Base Station longitude if registered on a
 *                              CDMA system or NULL if not. Base Station
 *                              longitude is a decimal number as specified in
 *                              3GPP2 C.S0005-A v6.0. It is represented in
 *                              units of 0.25 seconds and ranges from -2592000
 *                              to 2592000, both values inclusive (corresponding
 *                              to a range of -180° to +180°).
 * ((const char **)response)[7] is concurrent services support indicator if
 *                              registered on a CDMA system 0-1.
 *                                   0 - Concurrent services not supported,
 *                                   1 - Concurrent services supported
 * ((const char **)response)[8] is System ID if registered on a CDMA system or
 *                              NULL if not. Valid System ID are 0 - 32767
 * ((const char **)response)[9] is Network ID if registered on a CDMA system or
 *                              NULL if not. Valid System ID are 0 - 65535
 * ((const char **)response)[10] is the TSB-58 Roaming Indicator if registered
 *                               on a CDMA or EVDO system or NULL if not. Valid values
 *                               are 0-255.
 * ((const char **)response)[11] indicates whether the current system is in the
 *                               PRL if registered on a CDMA or EVDO system or NULL if
 *                               not. 0=not in the PRL, 1=in the PRL
 * ((const char **)response)[12] is the default Roaming Indicator from the PRL,
 *                               if registered on a CDMA or EVDO system or NULL if not.
 *                               Valid values are 0-255.
 * ((const char **)response)[13] if registration state is 3 (Registration
 *                               denied) this is an enumerated reason why
 *                               registration was denied.  See 3GPP TS 24.008,
 *                               10.5.3.6 and Annex G.
 *                                 0 - General
 *                                 1 - Authentication Failure
 *                                 2 - IMSI unknown in HLR
 *                                 3 - Illegal MS
 *                                 4 - Illegal ME
 *                                 5 - PLMN not allowed
 *                                 6 - Location area not allowed
 *                                 7 - Roaming not allowed
 *                                 8 - No Suitable Cells in this Location Area
 *                                 9 - Network failure
 *                                10 - Persistent location update reject
 *
 * Please note that registration state 4 ("unknown") is treated
 * as "out of service" in the Android telephony system
 *
 * Registration state 3 can be returned if Location Update Reject
 * (with cause 17 - Network Failure) is received repeatedly from the network,
 * to facilitate "managed roaming"
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */

static void _requestCDMARegistrationState(void *data,
                                        size_t datalen, RIL_Token t)
{
    int err,i;
    char * responseStr[14];
    ATResponse *p_response = NULL;
    char *line, *p;
    int skip;
    int registered = 0;
    int roaming = 0;
    int net;
    err = at_send_command_singleline("AT^SYSINFO", "^SYSINFO", &p_response);

    if (err ||!p_response->success ||!p_response->p_intermediates) goto error;

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &skip);
    if (err < 0) goto error;
    
    err = at_tok_nextint(&line, &registered);
   if (err < 0) goto error;
   
    err = at_tok_nextint(&line, &roaming);
    if (err < 0) goto error;
	
    err = at_tok_nextint(&line, &cdma_netmode);
	if (err < 0) goto error;
    
    
    registered =  get_reg_stat(registered,roaming);
    net = networktype(cdma_netmode);
    INFO(" registered = %d,roaming =%d,cdma_netmode=%d",registered, roaming,cdma_netmode);

	#if CDMA_MASQUERADING
	if((7==net)||(8==net)||(12==net))
		net = 3;
	else if(6==net)
		net=1;
	#endif
    asprintf(&responseStr[0], "%d", registered);
    asprintf(&responseStr[1], "%x",0);
    asprintf(&responseStr[2], "%x",0);
    asprintf(&responseStr[3], "%d",net);//radio technology 8 - EvDo Rev. A
    
	DBG("radio tech == [%s],register state == %s",
		radio_tech_name[net],registered_status[registered]);
	
    ril_status(network_state)=registered;
	#if CDMA_MASQUERADING
    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 4*sizeof(char*));
    for(i=0;i<4;i++){
    	  free(responseStr[i]);
    }
	#else
    for(i=4;i<14;i++){
		responseStr[i]=NULL;
    }
	if(cdma_context.valid){
    	asprintf(&responseStr[4], "%d",cdma_context.bsid);
    	asprintf(&responseStr[8], "%d",cdma_context.sid);
    	asprintf(&responseStr[9], "%d",cdma_context.nid);
    	asprintf(&responseStr[14], "%d",cdma_context.pn);
	}
	asprintf(&responseStr[10], "%d",roaming?0:1);
	asprintf(&responseStr[12], "%d",roaming?0:1);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 14*sizeof(char*));
    for(i=0;i<14;i++){
		if(responseStr[i])
			free(responseStr[i]);
    }
	
	#endif
     at_response_free(p_response);
    return;
error:
    ERROR("requestRegistrationState must never return an error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}
/**
 * RIL_REQUEST_OPERATOR
 *
 * Request current operator ONS or EONS.
 */
static void requestCDMAOperator(void *data, size_t datalen, RIL_Token t)
{
    int err;
    int i;
    int skip;
    ATLine *p_cur;
    char *response[3];
    int ret = -1;
    ATResponse *atresponse = NULL;
     
    memset(response, 0, sizeof(response));

    ATResponse *p_response = NULL;

    err = at_send_command_raw("AT+CIMI", &atresponse);
    
    if (err == 0 && atresponse->success) 
    {
        //eat pre space ,why ZTE forget this?
        char* line = atresponse->p_intermediates->line;
        char* imsi = line;
        itu_operator op;
        
        while(*imsi==' ') imsi++;

        err = network_query_operator(imsi, &op);
        if (!err)
        {
            asprintf(&response[0], op.long_alpha);
            asprintf(&response[1], op.short_alpha);
            asprintf(&response[2], "%s", op.numeric);
			ret = 0;
        }else {
        	WARN("can't find out op info for imsi[%s]",imsi);
            asprintf(&response[0],"China Telecom");
            asprintf(&response[1], "CHT");
            asprintf(&response[2], "%s", "460003");
        	
    	}
    }
    at_response_free(atresponse);
    
    if(ret)
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
    for (i=0; i<3; i++)
        if (response[i]) free(response[i]);    
 
}

/**
 * RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE
 *
 * Query the preferred network type (CS/PS domain, RAT, and operation mode)
 * for searching and registering.

 * RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE
 *
 * Query the preferred network type (CS/PS domain, RAT, and operation mode)
 * for searching and registering
 *
 * "data" is NULL
 *
 * "response" is int *
 * ((int *)response)[0] is == 0 for GSM/WCDMA (WCDMA preferred)
 * ((int *)response)[0] is == 1 for GSM only
 * ((int *)response)[0] is == 2 for WCDMA only
 * ((int *)response)[0] is == 3 for GSM/WCDMA (auto mode, according to PRL)
 * ((int *)response)[0] is == 4 for CDMA and EvDo (auto mode, according to PRL)
 * ((int *)response)[0] is == 5 for CDMA only
 * ((int *)response)[0] is == 6 for EvDo only
 * ((int *)response)[0] is == 7 for GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL)
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 *
 * See also: RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE
 
 */
static void requestCDMAGetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t)
{
    int err = 0;
    int response = 0;
    int pref_mode;
    char *line;
    ATResponse *atresponse;

    err = at_send_command_singleline("AT^PREFMODE?", "^PREFMODE:", &atresponse);
    if (err < 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &pref_mode);
    if (err < 0)
        goto error;


    switch (pref_mode) {
	case kNetModeCDMA://cdma
		response = 5;
		break;
	case kNetModeHDR://evdo
		response = 6;
		break;
	case kNetModeCDMAHDR://auto
	default:
		response = 4;
		break;
    }
	#if CDMA_MASQUERADING
	if(5==response)
		response=1;
	else if(6==response)
		response = 2;
	else 
		response = 3;
	#endif

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(atresponse);
}



/**
 * RIL_REQUEST_REGISTRATION_STATE
 *
 * Request current registration state
 *
 * "data" is NULL
 * "response" is a "char **"
 * ((const char **)response)[0] is registration state 0-6,
 *              0 - Not registered, MT is not currently searching
 *                  a new operator to register
 *              1 - Registered, home network
 *              2 - Not registered, but MT is currently searching
 *                  a new operator to register
 *              3 - Registration denied
 *              4 - Unknown
 *              5 - Registered, roaming
 *             10 - Same as 0, but indicates that emergency calls
 *                  are enabled.
 *             12 - Same as 2, but indicates that emergency calls
 *                  are enabled.
 *             13 - Same as 3, but indicates that emergency calls
 *                  are enabled.
 *             14 - Same as 4, but indicates that emergency calls
 *                  are enabled.
 *
 * ((const char **)response)[1] is LAC if registered on a GSM/WCDMA system or
 *                              NULL if not.Valid LAC are 0x0000 - 0xffff
 * ((const char **)response)[2] is CID if registered on a * GSM/WCDMA or
 *                              NULL if not.
 *                                 Valid CID are 0x00000000 - 0xffffffff
 *                                    In GSM, CID is Cell ID (see TS 27.007)
 *                                            in 16 bits
 *                                    In UMTS, CID is UMTS Cell Identity
 *                                             (see TS 25.331) in 28 bits
 * ((const char **)response)[3] indicates the available radio technology 0-7,
 *                                  0 - Unknown, 1 - GPRS, 2 - EDGE, 3 - UMTS,
 *                                  4 - IS95A, 5 - IS95B, 6 - 1xRTT,
 *                                  7 - EvDo Rev. 0, 8 - EvDo Rev. A,
 *                                  9 - HSDPA, 10 - HSUPA, 11 - HSPA
 * ((const char **)response)[4] is Base Station ID if registered on a CDMA
 *                              system or NULL if not.  Base Station ID in
 *                              decimal format
 * ((const char **)response)[5] is Base Station latitude if registered on a
 *                              CDMA system or NULL if not. Base Station
 *                              latitude is a decimal number as specified in
 *                              3GPP2 C.S0005-A v6.0. It is represented in
 *                              units of 0.25 seconds and ranges from -1296000
 *                              to 1296000, both values inclusive (corresponding
 *                              to a range of -90° to +90°).
 * ((const char **)response)[6] is Base Station longitude if registered on a
 *                              CDMA system or NULL if not. Base Station
 *                              longitude is a decimal number as specified in
 *                              3GPP2 C.S0005-A v6.0. It is represented in
 *                              units of 0.25 seconds and ranges from -2592000
 *                              to 2592000, both values inclusive (corresponding
 *                              to a range of -180° to +180°).
 * ((const char **)response)[7] is concurrent services support indicator if
 *                              registered on a CDMA system 0-1.
 *                                   0 - Concurrent services not supported,
 *                                   1 - Concurrent services supported
 * ((const char **)response)[8] is System ID if registered on a CDMA system or
 *                              NULL if not. Valid System ID are 0 - 32767
 * ((const char **)response)[9] is Network ID if registered on a CDMA system or
 *                              NULL if not. Valid System ID are 0 - 65535
 * ((const char **)response)[10] is the TSB-58 Roaming Indicator if registered
 *                               on a CDMA or EVDO system or NULL if not. Valid values
 *                               are 0-255.
 * ((const char **)response)[11] indicates whether the current system is in the
 *                               PRL if registered on a CDMA or EVDO system or NULL if
 *                               not. 0=not in the PRL, 1=in the PRL
 * ((const char **)response)[12] is the default Roaming Indicator from the PRL,
 *                               if registered on a CDMA or EVDO system or NULL if not.
 *                               Valid values are 0-255.
 * ((const char **)response)[13] if registration state is 3 (Registration
 *                               denied) this is an enumerated reason why
 *                               registration was denied.  See 3GPP TS 24.008,
 *                               10.5.3.6 and Annex G.
 *                                 0 - General
 *                                 1 - Authentication Failure
 *                                 2 - IMSI unknown in HLR
 *                                 3 - Illegal MS
 *                                 4 - Illegal ME
 *                                 5 - PLMN not allowed
 *                                 6 - Location area not allowed
 *                                 7 - Roaming not allowed
 *                                 8 - No Suitable Cells in this Location Area
 *                                 9 - Network failure
 *                                10 - Persistent location update reject
 *
 * Please note that registration state 4 ("unknown") is treated
 * as "out of service" in the Android telephony system
 *
 * Registration state 3 can be returned if Location Update Reject
 * (with cause 17 - Network Failure) is received repeatedly from the network,
 * to facilitate "managed roaming"
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */

/**
 * RIL_REQUEST_GPRS_REGISTRATION_STATE
 *
 * Request current GPRS registration state
 *
 * "data" is NULL
 * "response" is a "char **"
 * ((const char **)response)[0] is registration state 0-5 from TS 27.007 10.1.20 AT+CGREG
 * ((const char **)response)[1] is LAC if registered or NULL if not
 * ((const char **)response)[2] is CID if registered or NULL if not
 * ((const char **)response)[3] indicates the available radio technology, where:
 *      0 == unknown
 *      1 == GPRS only
 *      2 == EDGE
 *      3 == UMTS
 *      9 == HSDPA
 *      10 == HSUPA
 *      11 == HSPA
 *
 * LAC and CID are in hexadecimal format.
 * valid LAC are 0x0000 - 0xffff
 * valid CID are 0x00000000 - 0x0fffffff
 *
 * Please note that registration state 4 ("unknown") is treated
 * as "out of service" in the Android telephony system
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE*/

static int _requestNetworkType(void)
{
	//access network type				
	ATResponse *p_response_op = NULL;
	int commas_op = 0;
	char *p_op, *line_op;
	int bypass=0;
	int response=0;
    int err;
    int value;

	if(kRIL_HW_M305 == rilhw->model||
		kRIL_HW_MC2716 ==rilhw->model||
		kRIL_HW_MC8630 == rilhw->model)
	{
		char* line;		
		int skip,network;
		 err = at_send_command_singleline("AT^SYSINFO", "^SYSINFO:", &p_response_op);
		
		 if (err ||!p_response_op->success ||!p_response_op->p_intermediates) goto error;
		
		 line = p_response_op->p_intermediates->line;
		
		 err = at_tok_start(&line);
		 if (err < 0) goto error;
		
		 err = at_tok_nextint(&line, &skip);
		 if (err < 0) goto error;
		 
		 err = at_tok_nextint(&line, &skip);
		if (err < 0) goto error;
		
		 err = at_tok_nextint(&line, &skip);
		 if (err < 0) goto error;
		 
		 err = at_tok_nextint(&line, &network);
		 if (err < 0) goto error;
		 at_response_free(p_response_op);
		 switch(network)
		 {
		 	default:
		 	case 3: network = 1;break;//GSM/GPRS
		 	case 5: network = 3;break;//WCDMA
		 	case 15:network = 3;break;//TDSCDMA,android not support ,just mapping to UMTS
		 	case 2: /* CDMA */
                network = 6;
                break;
		 	case 4: /* HDR */
		 	case 8: /* CDMA/HDR HYBRID */
		 		network = 8;
		 		break;
		 }
		 
		 return network;
		 
		 error:		 	
			at_response_free(p_response_op);
			return 0;//unknown network
	}else if(kRIL_HW_EM350 == rilhw->model){
		int network = 14;
		return network;
	}
	else
	{
		err = at_send_command_singleline("AT+ZPAS?", "+ZPAS:", &p_response_op); 			
		if (!err&&p_response_op->success)
		{					
			char* network;
			line_op = p_response_op->p_intermediates->line;
			if(!at_tok_start(&line_op)&&
				(!at_tok_nextstr(&line_op, &network)))
			{
				bypass = 1;
				if(!strcmp(network,"HSPA"))
				{
					response = 11;
				}
				else if(!strcmp(network,"EDGE"))
				{
					response = 2;							
				}
				else if(!strcmp(network,"GPRS"))
				{
					response = 1;							
				}
				else if(!strcmp(network,"HSDPA"))
				{
					response = 9;							
				}
				else if(!strcmp(network,"HSUPA"))
				{
					response = 10;							
				}
				
				else if(!strcmp(network,"GSM"))
				{
					response = 3;							
				}
				else
				{
					//pass failed
					bypass = 0;
				}
			}
			if(bypass)
			{
				DBG("network == %s",network);
			}
		}
		
		at_response_free(p_response_op);
	
		if(!bypass)
		{
			err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response_op);
			if (!err&&p_response_op->success)
			{
				/* We need to get the 4th return param */
				line_op = p_response_op->p_intermediates->line;
				
				for (p_op = line_op ; *p_op != '\0' ;p_op++) {
					if (*p_op == ',') commas_op++;
				}
				
				if (commas_op == 3) {
					err = at_tok_start(&line_op);
					if (err < 0) goto ret;
					err = at_tok_nextint(&line_op, &value);
					if (err < 0) goto ret;
					err = at_tok_nextint(&line_op, &value);
					if (err < 0) goto ret;
					err = at_tok_nextint(&line_op, &value);
					if (err < 0) goto ret;
					err = at_tok_nextint(&line_op, &value);
					if (err < 0) goto ret;
					/* Now translate to 'Broken Android Speak' - can't follow the GSM spec */
					switch(value) {
						/* GSM/GSM Compact - aka GRPS */
						case 0:
						case 1:
							response = 1;
							break;
							/* UTRAN - UMTS or better */
						case 2:
						default:
							response = 3;
							break;
					}
				}
			}
			ret:
			at_response_free(p_response_op);
		}
	}
	return response;
}


static int _requestGPRSRegistrationState(void *data, size_t datalen, RIL_Token t)
{
    int err;
    int response[4]={0,};
    char * responseStr[4]={NULL,};
    ATResponse *p_response = NULL;
    const char *cmd;
    const char *prefix;
    char *line, *p;
    int commas;
    int skip;
    int i = 0;
	
	cmd = "AT+CGREG?";
	prefix = "+CGREG:";
	
    err = at_send_command_singleline(cmd, prefix, &p_response);

    if (err||!p_response->success||!p_response->p_intermediates) goto error;
   
    line = p_response->p_intermediates->line;
   
    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CREG response is
     * +CREG: n, stat, [lac, cid]
     * and the unsolicited version is
     * +CREG: stat, [lac, cid]
     * The <n> parameter is basically "is unsolicited creg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, or 4 arguments here
     *
     * finally, a +CGREG: answer may have a fifth value that corresponds
     * to the network type, as in;
     *
     *   +CGREG: n, stat [,lac, cid [,networkType]]
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ;p++) {
        if (*p == ',') commas++;
    }

    
    switch (commas) {
        case 0: /* +CREG: <stat> */
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
			response[1]=response[2]=0;
			response[3]=_requestNetworkType();
        break;

        case 1: /* +CREG: <n>, <stat> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
			response[1]=response[2]=0;
			response[3]=_requestNetworkType();
        break;

        case 2: /* +CREG: <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
			response[3]=_requestNetworkType();
        break;
        case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
			/* Hack for broken +CGREG responses which don't return the network type */
			response[3]=_requestNetworkType();
        break;
        /* special case for CGREG, there is a fourth parameter
         * that is the network type (unknown/gprs/edge/umts)
         */
        case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[3]);
            if (err < 0) goto error;
        break;
        default:
            goto error;
    }


	if(kRIL_HW_ME3760==rilhw->model)
		response[3] = ME3760_network_type_map(response[3]);

	if(response[3]>12)
		response[3] = 3;//fallback to umts
    if(kRIL_HW_EM350 ==rilhw->model){
        response[3] = RADIO_TECH_LTE;
    }
    asprintf(&responseStr[0], "%d", response[0]);
    asprintf(&responseStr[1], "%x", response[1]);
    asprintf(&responseStr[2], "%x", response[2]);
	asprintf(&responseStr[3], "%d", response[3]);

	ril_status(data_network_state)=response[0];

	DBG("network [%s,%s]",
		radio_tech_name[response[3]],registered_status[response[0]]);
	
	if(t)
	    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 4*sizeof(char*));
	else
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED,NULL,0);
    at_response_free(p_response);

    for(i=0;i<4;i++){
    	if(responseStr[i]) free(responseStr[i]);
    }

    return 0;
error:
    ERROR("requestGPRSRegistrationState must never return an error when radio is on");
	if(t)
    	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
     for(i=0;i<4;i++){
    	if(responseStr[i]) free(responseStr[i]);
    }
     return -1;
}

static int _requestGPRSRegistrationStateForCDMA(void *data, size_t datalen, RIL_Token t)
{
    int err;
    int response[4]={0,};
    char * responseStr[4]={NULL,};
    ATResponse *p_response = NULL;
    const char *cmd;
    const char *prefix;
    char *line, *p;
    int commas;
    int skip;
    int i = 0;
	
	cmd = "AT+CGREG?";
	prefix = "+CGREG:";
	
    err = at_send_command_singleline(cmd, prefix, &p_response);

    if (err||!p_response->success||!p_response->p_intermediates) goto error;
   
    line = p_response->p_intermediates->line;
   
    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CREG response is
     * +CREG: n, stat, [lac, cid]
     * and the unsolicited version is
     * +CREG: stat, [lac, cid]
     * The <n> parameter is basically "is unsolicited creg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, or 4 arguments here
     *
     * finally, a +CGREG: answer may have a fifth value that corresponds
     * to the network type, as in;
     *
     *   +CGREG: n, stat [,lac, cid [,networkType]]
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ;p++) {
        if (*p == ',') commas++;
    }

    
    switch (commas) {
        case 0: /* +CREG: <stat> */
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
			response[1]=response[2]=0;
			response[3]=_requestNetworkType();
        break;

        case 1: /* +CREG: <n>, <stat> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
			response[1]=response[2]=0;
			response[3]=_requestNetworkType();
        break;

        case 2: /* +CREG: <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
			response[3]=_requestNetworkType();
        break;
        case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
			/* Hack for broken +CGREG responses which don't return the network type */
			response[3]=_requestNetworkType();
        break;
        /* special case for CGREG, there is a fourth parameter
         * that is the network type (unknown/gprs/edge/umts)
         */
        case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[3]);
            if (err < 0) goto error;
        break;
        default:
            goto error;
    }


    asprintf(&responseStr[0], "%d", response[0]);
    asprintf(&responseStr[1], "%x", response[1]);
    asprintf(&responseStr[2], "%x", response[2]);
	asprintf(&responseStr[3], "%d", response[3]);
	
	if(t)
	    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 4*sizeof(char*));
	else
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED,NULL,0);
    at_response_free(p_response);

	DBG("access gprs registration state");
    for(i=0;i<4;i++){
		DBG("response str[%d] = %s",i,
			responseStr[i]?responseStr[i]:"null");
    	if(responseStr[i]) free(responseStr[i]);
    }

    return 0;
error:
    ERROR("requestRegistrationState must never return an error when radio is on");
	if(t)
    	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
     for(i=0;i<4;i++){
    	if(responseStr[i]) free(responseStr[i]);
    }
     return -1;
}

/*
typedef enum {
    RADIO_TECH_UNKNOWN = 0,
    RADIO_TECH_GPRS = 1,
    RADIO_TECH_EDGE = 2,
    RADIO_TECH_UMTS = 3,
    RADIO_TECH_IS95A = 4,
    RADIO_TECH_IS95B = 5,
    RADIO_TECH_1xRTT =  6,
    RADIO_TECH_EVDO_0 = 7,
    RADIO_TECH_EVDO_A = 8,
    RADIO_TECH_HSDPA = 9,
    RADIO_TECH_HSUPA = 10,
    RADIO_TECH_HSPA = 11,
    RADIO_TECH_EVDO_B = 12,
    RADIO_TECH_EHRPD = 13,
    RADIO_TECH_LTE = 14,
    RADIO_TECH_HSPAP = 15, // HSPA+
    RADIO_TECH_GSM = 16 // Only supports voice
} RIL_RadioTechnology;

*/

static int _requestRegistrationState(void *data, size_t datalen, RIL_Token t)
{
    int err;
    int response[4]={0,};
    char * responseStr[14]={NULL,};
    ATResponse *p_response = NULL;
    const char *cmd;
    const char *prefix;
    char *line, *p;
    int commas;
    int skip;
    int count = 3;
    int i = 0;

    if(kRIL_HW_EM350 ==rilhw->model){
      cmd = "AT+CEREG?";
      prefix = "+CEREG:";
    }else {
    cmd = "AT+CREG?";
    prefix = "+CREG:";
   }
    
    err = at_send_command_singleline(cmd, prefix, &p_response);

    if (err||!p_response->success||!p_response->p_intermediates) goto error;
   
    line = p_response->p_intermediates->line;
   
    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CREG response is
     * +CREG: n, stat, [lac, cid]
     * and the unsolicited version is
     * +CREG: stat, [lac, cid]
     * The <n> parameter is basically "is unsolicited creg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, or 4 arguments here
     *
     * finally, a +CGREG: answer may have a fifth value that corresponds
     * to the network type, as in;
     *
     *   +CGREG: n, stat [,lac, cid [,networkType]]
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ;p++) {
        if (*p == ',') commas++;
    }

    
    switch (commas) {
        case 0: /* +CREG: <stat> */
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
			response[1]=response[2]=0;
			response[3]=_requestNetworkType();
        break;

        case 1: /* +CREG: <n>, <stat> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
			response[1]=response[2]=0;
			response[3]=_requestNetworkType();
        break;

        case 2: /* +CREG: <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
			response[3]=_requestNetworkType();
        break;
        case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
			response[3]=_requestNetworkType();
        break;
        case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &response[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[2]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &response[3]);
            if (err < 0) goto error;
        break;
        default:
            goto error;
    }

	if(kRIL_HW_ME3760==rilhw->model)
		response[3] = ME3760_network_type_map(response[3]);
	if(response[3]>12)
		response[3] = 3;//fallback to umts
    if(kRIL_HW_EM350 ==rilhw->model){
       response[3] = RADIO_TECH_LTE;
    }
    asprintf(&responseStr[0], "%d", response[0]);
    asprintf(&responseStr[1], "%x", response[1]);
    asprintf(&responseStr[2], "%x", response[2]);
	asprintf(&responseStr[3], "%d", response[3]);
	ril_status(network_state)=response[0];
	DBG("network [%s,%s]",
		radio_tech_name[response[3]],registered_status[response[0]]);
	
	if(t)
	    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 14*sizeof(char*));
	else
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED,NULL,0);
    at_response_free(p_response);


    for(i=0;i<4;i++){
    	if(responseStr[i]) free(responseStr[i]);
    }

    return 0;
error:
    ERROR("requestRegistrationState must never return an error when radio is on");
	if(t)
    	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
     for(i=0;i<4;i++){
    	if(responseStr[i]) free(responseStr[i]);
    }
     return -1;
}


void requestRegistrationState(void *data, size_t datalen, RIL_Token t)
{
	if(kRIL_HW_MC2716 ==rilhw->model||
	   kRIL_HW_MC8630 == rilhw->model)
	{
		_requestCDMARegistrationState(data,datalen,t);
	}
	else
	{
		//int wait = 20,ret = -1;
		//while(wait-- && ret == -1){
		//	ret = _requestRegistrationState(data,datalen,t);
		//	if(ret ) sleep(1);
		//}
		_requestRegistrationState(data,datalen,t);
	}
}

void requestGPRSRegistrationState(void *data, size_t datalen, RIL_Token t)
{
    if (kRIL_HW_MC2716 ==rilhw->model||
		kRIL_HW_MC8630 ==rilhw->model)
    {
        int i;
        int registered = 0;
        int romming = 0;
        int err;
        ATResponse* atresponse = NULL;
        int response[4] = {0};
        char* responseStr[14] = {NULL};
        
        err = at_send_command_singleline("AT^SYSINFO", "^SYSINFO:", &atresponse);
        if (err == 0 && atresponse->success && atresponse->p_intermediates->line) 
        {
            int skip;
            char* line = atresponse->p_intermediates->line;
            
            do
            {
                err = at_tok_start(&line);
                if (err < 0) break;
                
                err = at_tok_nextint(&line, &skip);
                if (err < 0) break;
                 
                err = at_tok_nextint(&line, &registered);
                if (err < 0) break;

                err = at_tok_nextint(&line, &romming);
                if (err < 0) break;
            } while (0);
            at_response_free(atresponse);

            response[0] = get_reg_stat(registered, romming);
            response[3] = _requestNetworkType();
			
			#if CDMA_MASQUERADING
			if((7==response[3])||(8==response[3])||(12==response[3]))
				response[3] = 3;
			else if(6==response[3])
				response[3]=1;
			#endif

            asprintf(&responseStr[0], "%d", response[0]);
            asprintf(&responseStr[1], "%x", response[1]);
            asprintf(&responseStr[2], "%x", response[2]);
            asprintf(&responseStr[3], "%d", response[3]);
			
			DBG("network type == [%s],register state == %s",
				radio_tech_name[response[3]],registered_status[response[0]]);

            ril_status(data_network_state)=response[0];

			
			#if CDMA_MASQUERADING
			RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 4*sizeof(char*));
			for(i=0;i<4;i++){
				  free(responseStr[i]);
			}
			#else
			for(i=4;i<14;i++){
				responseStr[i]=NULL;
			}
			if(cdma_context.valid){
				asprintf(&responseStr[4], "%d",cdma_context.bsid);
				asprintf(&responseStr[8], "%d",cdma_context.sid);
				asprintf(&responseStr[9], "%d",cdma_context.nid);				
				asprintf(&responseStr[14], "%d",cdma_context.pn);
			}
			asprintf(&responseStr[10], "%d",romming?0:1);
			asprintf(&responseStr[12], "%d",romming?0:1);
			RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 14*sizeof(char*));
			for(i=0;i<14;i++){
				if(responseStr[i])
					free(responseStr[i]);
			}
			
			#endif
        }
    }
    else
    {
    	//int wait = 20,ret = -1;
    	//while(wait-- && ret == -1){
    	//	ret = _requestGPRSRegistrationState(data,datalen,t);
    	//	if(ret ) sleep(1);
    	//}
    	_requestGPRSRegistrationState(data,datalen,t);
    }
}

static void requestGSMOperator(void *data, size_t datalen, RIL_Token t)
{
    int err;
    int i;
    int skip;
    ATLine *p_cur;
    char *response[3];

    memset(response, 0, sizeof(response));

    ATResponse *p_response = NULL;

    err = at_send_command_multiline(
        "AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?",
        "+COPS:", &p_response);

    /* we expect 3 lines here:
     * +COPS: 0,0,"T - Mobile"
     * +COPS: 0,1,"TMO"
     * +COPS: 0,2,"310170"
     */

    if (err != 0) goto error;

    for (i = 0, p_cur = p_response->p_intermediates
            ; p_cur != NULL
            ; p_cur = p_cur->p_next, i++
    ) {
        char *line = p_cur->line;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;

        // If we're unregistered, we may just get
        // a "+COPS: 0" response
        if (!at_tok_hasmore(&line)) {
            response[i] = NULL;
            continue;
        }

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;

        // a "+COPS: 0, n" response is also possible
        if (!at_tok_hasmore(&line)) {
            response[i] = NULL;
            continue;
        }

        err = at_tok_nextstr(&line, &(response[i]));
        if (err < 0) goto error;
    }

    if (i != 3) {
        /* expect 3 lines exactly */
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
    at_response_free(p_response);

    return;
error:
    ERROR("requestOperator must not return error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}

void requestOperator(void *data, size_t datalen, RIL_Token t)
{
	if(kRIL_HW_MC2716 ==rilhw->model||
		kRIL_HW_MC8630 == rilhw->model)
		requestCDMAOperator(data,datalen,t);
	else
		requestGSMOperator(data,datalen,t);
}
#if 0
 typedef enum {
    PREF_NET_TYPE_GSM_WCDMA                = 0, /* GSM/WCDMA (WCDMA preferred) */
    PREF_NET_TYPE_GSM_ONLY                 = 1, /* GSM only */
    PREF_NET_TYPE_WCDMA                    = 2, /* WCDMA  */
    PREF_NET_TYPE_GSM_WCDMA_AUTO           = 3, /* GSM/WCDMA (auto mode, according to PRL) */
    PREF_NET_TYPE_CDMA_EVDO_AUTO           = 4, /* CDMA and EvDo (auto mode, according to PRL) */
    PREF_NET_TYPE_CDMA_ONLY                = 5, /* CDMA only */
    PREF_NET_TYPE_EVDO_ONLY                = 6, /* EvDo only */
    PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO = 7, /* GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL) */
    PREF_NET_TYPE_LTE_CDMA_EVDO            = 8, /* LTE, CDMA and EvDo */
    PREF_NET_TYPE_LTE_GSM_WCDMA            = 9, /* LTE, GSM/WCDMA */
    PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA  = 10, /* LTE, CDMA, EvDo, GSM/WCDMA */
    PREF_NET_TYPE_LTE_ONLY                 = 11  /* LTE only */
} RIL_PreferredNetworkType;
#endif

static void requestGSMGetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t)
{
    int err = 0;
    int response = 0;
    int type;
    char *line;
    ATResponse *atresponse=NULL;

	/*
	if(rilhw->model == kRIL_HW_AD3812)
	{
	    err = at_send_command_singleline("AT+ZMDS?", "+ZMDS:", &atresponse);
	    if (err < 0 || atresponse->success == 0) {
	        goto error;
	    }

	    line = atresponse->p_intermediates->line;

	    err = at_tok_start(&line);
	    if (err < 0)
	        goto error;

	    err = at_tok_nextint(&line, &type);
	    if (err < 0)
	        goto error;


	    switch (type) {
		case 0: default: 
	    case 4:
	        response = 3;
	        break;
	    case 13://gsm only
	        response = 1;
	        break;
		case 14://wcdma only
	        response = 2;
	        break;
	    }
	}
	else */
	if(rilhw->model == kRIL_HW_M305)
	{
		//M305 does not support this ,
		response = 7; //GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL)
	}else if(rilhw->model == kRIL_HW_EM350){
	    response = PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA;
  	    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
	    return;
	}
	else if(rilhw->model == kRIL_HW_MF210)
	{
	    err = at_send_command_singleline("AT+ZSNT?", "+ZSNT:", &atresponse);
	    if (err < 0 || atresponse->success == 0) {
	        goto error;
	    }

	    line = atresponse->p_intermediates->line;

	    err = at_tok_start(&line);
	    if (err < 0)
	        goto error;

	    err = at_tok_nextint(&line, &type);
	    if (err < 0)
	        goto error;


	    switch (type) {
		case 0: default: //automode
	        response = 3;
	        break;
	    case 1://gsm only
	        response = 1;
	        break;
		case 2://wcdma only
	        response = 2;
	        break;
	    }
	}else {//fallback to AT+COPS?

		err = at_send_command_singleline("AT+COPS?", "+COPS:", &atresponse);
		if (!err&&atresponse->success)
		{
			char *p, *line;
			int commas=0;
			int value;
			/* We need to get the 4th return param */
			line = atresponse->p_intermediates->line;

			for (p = line ; *p != '\0' ;p++) {
				if (*p == ',') commas++;
			}

			if (commas == 3) {
				err = at_tok_start(&line);			if (err < 0) goto error;
				err = at_tok_nextint(&line, &value); if (err < 0) goto error;
				err = at_tok_nextint(&line, &value); if (err < 0) goto error;
				err = at_tok_nextint(&line, &value); if (err < 0) goto error;
				err = at_tok_nextint(&line, &value); if (err < 0) goto error;
				/* Now translate to 'Broken Android Speak' - can't follow the GSM spec */
				switch(value) {
					/* GSM/GSM Compact - aka GRPS */
					case 0:
					case 1:
						response = 1;
						break;
						/* UTRAN - UMTS or better */
					case 2:
					default:
						response = 3;
						break;
				}
			}else goto error;
		}else goto error;

	}
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(atresponse);
}


void requestGetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t)
{
	if(kRIL_HW_MC2716 ==rilhw->model||
		kRIL_HW_MC8630 ==rilhw->model)
		requestCDMAGetPreferredNetworkType(data,datalen,t);
	else
		requestGSMGetPreferredNetworkType(data,datalen,t);
}
void requestSetFacilityLock(void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *atresponse = NULL;
    char *cmd = NULL;
    char *facility_string = NULL;
    int facility_mode = -1;
    char *facility_mode_str = NULL;
    char *facility_password = NULL;
    char *facility_class = NULL;
    int num_retries = -1;

    assert(datalen >= (4 * sizeof(char **)));

    facility_string = ((char **) data)[0];
    facility_mode_str = ((char **) data)[1];
    facility_password = ((char **) data)[2];
    facility_class = ((char **) data)[3];

    assert(*facility_mode_str == '0' || *facility_mode_str == '1');
    facility_mode = atoi(facility_mode_str);


    asprintf(&cmd, "AT+CLCK=\"%s\",%d,\"%s\",%s", facility_string,
             facility_mode, facility_password, facility_class);
    err = at_send_command(cmd, &atresponse);
    free(cmd);
    if (err < 0 || atresponse->success == 0) {
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &num_retries, sizeof(int *));
    at_response_free(atresponse);
    return;

error:
    at_response_free(atresponse);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_QUERY_FACILITY_LOCK
 *
 * Query the status of a facility lock state.
 */
void requestQueryFacilityLock(void *data, size_t datalen, RIL_Token t)
{
    int err, rat, response;
    ATResponse *atresponse = NULL;
    char *cmd = NULL;
    char *line = NULL;
    char *facility_string = NULL;
    char *facility_password = NULL;
    char *facility_class = NULL;

    assert(datalen >= (3 * sizeof(char **)));

    facility_string = ((char **) data)[0];
    facility_password = ((char **) data)[1];
    facility_class = ((char **) data)[2];

    asprintf(&cmd, "AT+CLCK=\"%s\",2,\"%s\",%s", facility_string,
             facility_password, facility_class);
    err = at_send_command_singleline(cmd, "+CLCK:", &atresponse);
    free(cmd);
    if (err < 0 || atresponse->success == 0) {
        goto error;
    }

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);

    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &response);

    if (err < 0)
        goto error;
    
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));

finally:
    at_response_free(atresponse);
    return;

error:
	ERROR("requestQueryFacilityLock return error\n");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/* * "response" is const char **
 * ((const char **)response)[0] is MDN if CDMA subscription is available
 * ((const char **)response)[1] is a comma separated list of H_SID (Home SID) if
 *                              CDMA subscription is available, in decimal format
 * ((const char **)response)[2] is a comma separated list of H_NID (Home NID) if
 *                              CDMA subscription is available, in decimal format
 * ((const char **)response)[3] is MIN (10 digits, MIN2+MIN1) if CDMA subscription is available
 * ((const char **)response)[4] is PRL version if CDMA subscription is available*/
 
void requestCDMASubScription(void *data, size_t datalen, RIL_Token t)
{
	 char *response[5]={NULL,"22322","23233","1132565870","1.0"};
	 RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));   
}

void requestRadioPower(void *data, size_t datalen, RIL_Token t)
{
    int onOff;
    int err=0;
    ATResponse *p_response = NULL;

    assert (datalen >= sizeof(int *));
    onOff = ((int *)data)[0];

	DBG("requestRadioPower -> %s",onOff?"on":"off");

	if (onOff == 0 && sState != RADIO_STATE_OFF) 
	{
		err = at_send_command("AT+CFUN=0", &p_response);
		property_set("ril.modem.state","off");
		//if (err < 0 || p_response->success == 0) goto error;        

        setRadioState(RADIO_STATE_OFF);
		
    } else if (onOff > 0 && sState == RADIO_STATE_OFF) {
		char prop_value[PROPERTY_VALUE_MAX];		
		property_get("ril.modem.state",prop_value,"");
		if(!strcmp(prop_value,"off")&&(kRIL_HW_AD3812==rilhw->model)){
	        err = at_send_command("AT+CFUN=1,1", &p_response);
			property_set("ril.modem.state","on"); 
		}else	
	        err = at_send_command("AT+CFUN=1", &p_response);
        if (err < 0|| p_response->success == 0) {
            // Some stacks return an error when there is no SIM,
            // but they really turn the RF portion on
            // So, if we get an error, let's check to see if it
            // turned on anyway

            if (isRadioOn() != 1) {
                goto error;
            }
        }
		//init again 
		modem_init();
        setRadioState(radio_state_not_ready);
    }

    at_response_free(p_response);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    return;
error:
    at_response_free(p_response);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * RIL_REQUEST_SET_LOCATION_UPDATES
 *
 * Enables/disables network state change notifications due to changes in
 * LAC and/or CID (basically, +CREG=2 vs. +CREG=1).  
 *
 * Note:  The RIL implementation should default to "updates enabled"
 * when the screen is on and "updates disabled" when the screen is off.
 *
 * See also: RIL_REQUEST_SCREEN_STATE, RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED.
 */
void requestSetLocationUpdates(void *data, size_t datalen, RIL_Token t)
{
    int enable = 0;
    int err = 0;
    char *cmd;
    ATResponse *atresponse = NULL;

    enable = ((int *) data)[0];
    assert(enable == 0 || enable == 1);

    if(kRIL_HW_EM350==rilhw->model)
	asprintf(&cmd, "AT+CEREG=%d", (enable == 0 ? 1 : 2));
    else 
	asprintf(&cmd, "AT+CREG=%d", (enable == 0 ? 1 : 2));
    
    err = at_send_command(cmd, &atresponse);
    free(cmd);

    if (err < 0 || atresponse->success == 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}


void requestNeighboringCellIds(void * data, size_t datalen, RIL_Token t) 
{
	int err=1;
	int response[4];
	char * responseStr[4];
	ATResponse *p_response = NULL;
	char *line, *p;
	int commas;
	int skip;
	int i;
	int count = 3;
	int cur_cid;

	RIL_NeighboringCell **pp_cellIds;
	RIL_NeighboringCell *p_cellIds;

	pp_cellIds = (RIL_NeighboringCell **)alloca(sizeof(RIL_NeighboringCell *));
	p_cellIds = (RIL_NeighboringCell *)alloca(sizeof(RIL_NeighboringCell));
	pp_cellIds[0]=p_cellIds;

	for (i=0;i<4 && err != 0;i++) {
		if(kRIL_HW_EM350==rilhw->model)
			err = at_send_command_singleline("AT+CEREG?", "+CEREG:", &p_response);
		else
			err = at_send_command_singleline("AT+CREG?", "+CREG:", &p_response);
	}

    if (err||!p_response->success||!p_response->p_intermediates) goto error;

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;
	/* Ok you have to be careful here
	 * The solicited version of the CREG response is
	 * +CREG: n, stat, [lac, cid]
	 * and the unsolicited version is
	 * +CREG: stat, [lac, cid]
	 * The <n> parameter is basically "is unsolicited creg on?"
	 * which it should always be
	 *
	 * Now we should normally get the solicited version here,
	 * but the unsolicited version could have snuck in
	 * so we have to handle both
	 *
	 * Also since the LAC and CID are only reported when registered,
	 * we can have 1, 2, 3, or 4 arguments here
	 * 
	 * finally, a +CGREG: answer may have a fifth value that corresponds
	 * to the network type, as in;
	 *
	 *   +CGREG: n, stat [,lac, cid [,networkType]]
	 */

	/* count number of commas */
	commas = 0;
	for (p = line ; *p != '\0' ;p++) {
		if (*p == ',') commas++;
	}
	switch (commas) {
		case 0: /* +CREG: <stat> */
		case 1: /* +CREG: <n>, <stat> */
			goto error;
			break;

		case 2: /* +CREG: <stat>, <lac>, <cid> */
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[1]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &cur_cid);
			asprintf(&(p_cellIds[0].cid), "%x", cur_cid);
			if (err < 0) goto error;
			break;
		case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[1]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &cur_cid);
			asprintf(&(p_cellIds[0].cid), "%x", cur_cid);
			p_cellIds[0].rssi=2;
			if (err < 0) goto error;
			break;
		case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[1]);
			if (err < 0) goto error;
			err = at_tok_nextstr(&line, &p_cellIds[0].cid);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[3]);
			if (err < 0) goto error;
			count = 4;
			break;
		default:
			goto error;
	}
	
	at_response_free(p_response);

	RIL_onRequestComplete(t, RIL_E_SUCCESS, pp_cellIds, sizeof(*pp_cellIds));
	return;
error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * Synchronous call from the RIL to us to return current radio state.
 * RADIO_STATE_UNAVAILABLE should be the initial state.
 */
RIL_RadioState getRadioState(void)
{
    return sState;
}

void setRadioState(RIL_RadioState newState)
{
	static char* state_str[]=
	{
		"off",
		"unavailable",
		"SIM not ready",
		"SIM locked or absent",
		"SIM ready",
		"RUIM not ready",
		"RUIM ready",
		"RUIM locked or absent",
		"NV not ready",
		"NV ready"
	};
    RIL_RadioState oldState;

    pthread_mutex_lock(&s_state_mutex);

    oldState = sState;

    if (s_closed > 0) {
        // If we're closed, the only reasonable state is
        // RADIO_STATE_UNAVAILABLE
        // This is here because things on the main thread
        // may attempt to change the radio state after the closed
        // event happened in another thread
        newState = RADIO_STATE_UNAVAILABLE;
    }

    if (sState != newState || s_closed > 0) {
        sState = newState;

        pthread_cond_broadcast (&s_state_cond);
    }

    pthread_mutex_unlock(&s_state_mutex);


    /* do these outside of the mutex */
    if (sState != oldState) {
    	DBG("RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED [%s]->[%s]",
			state_str[oldState],state_str[sState]);
        RIL_onUnsolicitedResponse (RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
                                    NULL, 0);

        /* FIXME onSimReady() and onRadioPowerOn() cannot be called
         * from the AT reader thread
         * Currently, this doesn't happen, but if that changes then these
         * will need to be dispatched on the request thread
         */
        if (sState == radio_state_ready) {
            enqueueRILEvent(onSIMReady, NULL, NULL);   
        } else if (sState == radio_state_not_ready) {
            enqueueRILEvent(pollSIMState, NULL, NULL);
        }
    }
}


/** returns 1 if on, 0 if off, and -1 on error */
int isRadioOn()
{
    ATResponse *p_response = NULL;
    int err;
    char *line;
    char ret;

    err = at_send_command_singleline("AT+CFUN?", "+CFUN:", &p_response);

    if (err < 0 || p_response->success == 0) {
        // assume radio is off
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextbool(&line, &ret);
    if (err < 0) goto error;

    at_response_free(p_response);

    return (int)ret;

error:

    at_response_free(p_response);
    return -1;
}
