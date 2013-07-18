#include "ril-handler.h"
#include <cutils/properties.h>


#if (RIL_VERSION==5)
#define RIL_CardStatus RIL_CardStatus_v5
#elif (RIL_VERSION>5)
#define RIL_CardStatus RIL_CardStatus_v6
#endif


#define MESSAGE_STORAGE_READY_TIMER 3

static const struct timespec TIMEVAL_SIMPOLL = { 1, 0 };

/** do post- SIM ready initialization */
void onSIMReady()
{
	modem_init();


    /*
     * Configure preferred message storage
     *  mem1 = SM, mem2 = SM
     */
     //will be set in checkMessageStorageReady
//    setPreferredMessageStorage();
	

    at_send_command_singleline("AT+CSMS=1", "+CSMS:", NULL); 
    /*
     * Always send SMS messages directly to the TE
     *
     * mode = 1 // discard when link is reserved (link should never be
     *             reserved)
     * mt = 2   // most messages routed to TE
     * bm = 2   // new cell BM's routed to TE
     * ds = 1   // Status reports routed to TE
     * bfr = 1  // flush buffer
     
     */
    switch(rilhw->model)
    {
    	case kRIL_HW_MC2716:
		case kRIL_HW_MC8630:
			//store in SIM and report to TE or report to TE directly
			//status report to TE?
			//at_send_command("AT+CNMI=1,1,1,0", NULL); 
			at_send_command("AT+CNMI=1,1,0,2,0", NULL); 
			break;
		case kRIL_HW_AD3812:
			if(ril_config(sms_mem)==RIL_SMS_MEM_SM)
				at_send_command("AT+CNMI=2,1,0,0,0", NULL); 
			else
				at_send_command("AT+CNMI=2,2,0,0,0", NULL); 
			break;			
		default:
			at_send_command("AT+CNMI=2,1,2,2,0", NULL); 
			break;					
    }
	checkMessageStorageReady();

}

static void resetSim(void *param)
{
    ATResponse *atresponse = NULL;
    int err, state;
    char *line;

    err = at_send_command_singleline("AT*ESIMSR?", "*ESIMSR:", &atresponse);
    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &state);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &state);
    if (err < 0)
        goto error;

    if (state == 7) {
        at_send_command("AT*ESIMR", NULL);
    } else {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED,
                                  NULL, 0);
        pollSIMState(NULL);
    }

finally:
    at_response_free(atresponse);
    return;

error:
    goto finally;
}

void onSimStateChanged(const char *s)
{
    int err, state;
    char *tok;
    char *line = tok = strdup(s);

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);


    /* 
     * Now, find out if we went to poweroff-state. If so, enqueue some loop
     * to try to reset the SIM for a minute or so to try to recover.
     */
    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &state);
    if (err < 0)
        goto error;

finally:
    free(tok);
    return;

error:
    ERROR("ERROR in onSimStateChanged!");
    goto finally;
}


/** Returns one of SIM_*. Returns SIM_NOT_READY on error. */
SIM_Status getSIMStatus()
{
    ATResponse *atresponse = NULL;
    SIM_Status ret = SIM_ABSENT;
    char *cpinLine = NULL;
    char *cpinResult = NULL;
    ATCmeError cme_error_code;

    if (getRadioState() == RADIO_STATE_OFF ||
        getRadioState() == RADIO_STATE_UNAVAILABLE) {
        ret = SIM_NOT_READY;
        goto exit;
    }

    if (at_send_command_singleline("AT+CPIN?", "+CPIN:", &atresponse) != 0) {
        ret = SIM_NOT_READY;
        goto exit;
    }

    if (atresponse->success == 0) {
        if (at_get_cme_error(atresponse, &cme_error_code)) {
            switch (cme_error_code) {
            case CME_SIM_NOT_INSERTED:
			case CME_SIM_FAILURE:
                ret = SIM_ABSENT;
                break;
            case CME_SIM_PIN_REQUIRED:
                ret = SIM_PIN;
                break;
            case CME_SIM_PUK_REQUIRED:
                ret = SIM_PUK;
                break;
            case CME_SIM_PIN2_REQUIRED:
                ret = SIM_PIN2;
                break;
            case CME_SIM_PUK2_REQUIRED:
                ret = SIM_PUK2;
                break;
            case CME_NETWORK_PERSONALIZATION_PIN_REQUIRED:
                ret = SIM_NETWORK_PERSO;
                break;
            case CME_NETWORK_PERSONALIZATION_PUK_REQUIRED:
                ret = SIM_NETWORK_PERSO_PUK;
                break;
            case CME_NETWORK_SUBSET_PERSONALIZATION_PIN_REQUIRED:
                ret = SIM_NETWORK_SUBSET_PERSO;
                break;
            case CME_NETWORK_SUBSET_PERSONALIZATION_PUK_REQUIRED:
                ret = SIM_NETWORK_SUBSET_PERSO_PUK;
                break;
            case CME_SERVICE_PROVIDER_PERSONALIZATION_PIN_REQUIRED:
                ret = SIM_SERVICE_PROVIDER_PERSO;
                break;
            case CME_SERVICE_PROVIDER_PERSONALIZATION_PUK_REQUIRED:
                ret = SIM_SERVICE_PROVIDER_PERSO_PUK;
                break;
            case CME_PH_SIMLOCK_PIN_REQUIRED: /* PUK not in use by modem */
                ret = SIM_SIM_PERSO;
                break;
            case CME_CORPORATE_PERSONALIZATION_PIN_REQUIRED:
                ret = SIM_CORPORATE_PERSO;
                break;
            case CME_CORPORATE_PERSONALIZATION_PUK_REQUIRED:
                ret = SIM_CORPORATE_PERSO_PUK;
                break;
            default:
                ret = SIM_NOT_READY;
                break;
            }
        }
        goto exit;
    }

    /* CPIN? has succeeded, now look at the result. */
    cpinLine = atresponse->p_intermediates->line;

    if (at_tok_start(&cpinLine) < 0) {
        ret = SIM_NOT_READY;
        goto exit;
    }

    if (at_tok_nextstr(&cpinLine, &cpinResult) < 0) {
        ret = SIM_NOT_READY;
        goto exit;
    }

    if (0 == strcmp(cpinResult, "READY")) {
        ret = SIM_READY;
    } else if (0 == strcmp(cpinResult, "SIM PIN")) {
        ret = SIM_PIN;
    } else if (0 == strcmp(cpinResult, "SIM PUK")) {
        ret = SIM_PUK;
    } else if (0 == strcmp(cpinResult, "SIM PIN2")) {
        ret = SIM_PIN2;
    } else if (0 == strcmp(cpinResult, "SIM PUK2")) {
        ret = SIM_PUK2;
    } else if (0 == strcmp(cpinResult, "PH-NET PIN")) {
        ret = SIM_NETWORK_PERSO;
    } else if (0 == strcmp(cpinResult, "PH-NETSUB PIN")) {
        ret = SIM_NETWORK_SUBSET_PERSO;
    } else if (0 == strcmp(cpinResult, "PH-SP PIN")) {
        ret = SIM_SERVICE_PROVIDER_PERSO;
    } else if (0 == strcmp(cpinResult, "PH-CORP PIN")) {
        ret = SIM_CORPORATE_PERSO;
    } else if (0 == strcmp(cpinResult, "PH-SIMLOCK PIN")) {
        ret = SIM_SIM_PERSO;
    } else if (0 == strcmp(cpinResult, "PH-ESL PIN")) {
        ret = SIM_STERICSSON_LOCK;
    } else if (0 == strcmp(cpinResult, "BLOCKED")) {
        int numRetries = 3;
        if (numRetries == -1 || numRetries == 0)
            ret = SIM_PERM_BLOCKED;
        else
            ret = SIM_PUK2_PERM_BLOCKED;
    } else if (0 == strcmp(cpinResult, "PH-SIM PIN")) {
        /*
         * Should not happen since lock must first be set from the phone.
         * Setting this lock is not supported by Android.
         */
        ret = SIM_BLOCKED;
    } else {
        /* Unknown locks should not exist. Defaulting to "sim absent" */
        ret = SIM_ABSENT;
    }

exit:
    at_response_free(atresponse);
    return ret;
}

/**
 * Get the current card status.
 *
 * This must be freed using freeCardStatus.
 * @return: On success returns RIL_E_SUCCESS.
 */
static int getCardStatus(RIL_CardStatus **pp_card_status) 
{
     static RIL_AppStatus app_status_array[] = {
        // SIM_ABSENT = 0
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_NOT_READY = 1
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_READY = 2
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_PIN = 3
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
        // SIM_PUK = 4
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED, RIL_PINSTATE_UNKNOWN },
        // SIM_NETWORK_PERSONALIZATION = 5
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_SUBSCRIPTION_PERSO, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN }
    };
    RIL_CardState card_state;
    int num_apps;
	unsigned int i=0;
	//reinit card riltype and perso
	while(i<(sizeof(app_status_array)/sizeof(app_status_array[0])))
	{
		app_status_array[i].app_type = ril_apptype;
		app_status_array[i].perso_substate = ril_persosubstate_network;
		
		i++;
	}

    int sim_status = getSIMStatus();
    if (sim_status == SIM_ABSENT) {
        card_state = RIL_CARDSTATE_ABSENT;
        num_apps = 0;
    } else {
        card_state = RIL_CARDSTATE_PRESENT;
        num_apps = 1;
    }

    // Allocate and initialize base card status.
    RIL_CardStatus *p_card_status = malloc(sizeof(RIL_CardStatus));
    p_card_status->card_state = card_state;
    p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
    p_card_status->gsm_umts_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->num_applications = num_apps;

    // Initialize application status
    for (i = 0; i < RIL_CARD_MAX_APPS; i++) {
        p_card_status->applications[i] = app_status_array[SIM_ABSENT];
    }

    // Pickup the appropriate application status
    // that reflects sim_status for gsm.
    if (num_apps != 0) {
        // Only support one app, gsm
        p_card_status->num_applications = 1;
        p_card_status->gsm_umts_subscription_app_index = 0;
	 p_card_status->cdma_subscription_app_index = 0;
        // Get the correct app status
        p_card_status->applications[0] = app_status_array[sim_status];
    }
    *pp_card_status = p_card_status;
     return RIL_E_SUCCESS;
}

/**
 * Free the card status returned by getCardStatus.
 */
static void freeCardStatus(RIL_CardStatus *p_card_status) {
   if(p_card_status == NULL ) return ;
    free(p_card_status);
}

/**
 * SIM ready means any commands that access the SIM will work, including:
 *  AT+CPIN, AT+CSMS, AT+CNMI, AT+CRSM
 *  (all SMS-related commands).
 */
void pollSIMState(void *param)
{
    if (sState != radio_state_not_ready &&
		sState != radio_state_locked_or_absent) {//sState != RADIO_STATE_SIM_NOT_READY && 
        // no longer valid to poll
        return;
    }

    switch(getSIMStatus()) {
		case SIM_ABSENT:
		case SIM_PIN:
		case SIM_PUK:
		case SIM_NETWORK_PERSO:
		case SIM_NETWORK_SUBSET_PERSO:
		case SIM_SERVICE_PROVIDER_PERSO:
		case SIM_CORPORATE_PERSO:
		case SIM_SIM_PERSO:
		case SIM_STERICSSON_LOCK:
		case SIM_BLOCKED:
		case SIM_PERM_BLOCKED:
		case SIM_NETWORK_PERSO_PUK:
		case SIM_NETWORK_SUBSET_PERSO_PUK:
		case SIM_SERVICE_PROVIDER_PERSO_PUK:
		case SIM_CORPORATE_PERSO_PUK:
        default:
            setRadioState(radio_state_locked_or_absent);
        return;

        case SIM_NOT_READY:
			enqueueRILEvent(pollSIMState, NULL, &TIMEVAL_SIMPOLL);
        return;

		case SIM_PIN2:
		case SIM_PUK2:
		case SIM_PUK2_PERM_BLOCKED:
		case SIM_READY:
            setRadioState(radio_state_ready);
        return;
    }
}

/** 
 * RIL_REQUEST_GET_SIM_STATUS
 *
 * Requests status of the SIM interface and the SIM card.
 * 
 * Valid errors:
 *  Must never fail. 
 */

void requestGetSimStatus(void *data, size_t datalen, RIL_Token t)
{
    RIL_CardStatus *p_card_status;
    char *p_buffer;
    int buffer_size;

    int result = getCardStatus(&p_card_status);
    if (result == RIL_E_SUCCESS) {
           p_buffer = (char *)p_card_status;
           buffer_size = sizeof(*p_card_status);
    } else {
             p_buffer = NULL;
             buffer_size = 0;
     }
    RIL_onRequestComplete(t, result, p_buffer, buffer_size);
    freeCardStatus(p_card_status);
}


/**
 * RIL_REQUEST_SIM_IO
 *
 * Request SIM I/O operation.
 * This is similar to the TS 27.007 "restricted SIM" operation
 * where it assumes all of the EF selection will be done by the
 * callee.
 ok
 */
void  requestSIM_IO(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *p_response = NULL;
    RIL_SIM_IO_Response sr;
    int err;
    char *cmd = NULL;
    RIL_SIM_IO *p_args;
    char *line;
	char prop_value[PROPERTY_VALUE_MAX];
	property_get("ril.sim.io",prop_value,"off");
	if(!strcmp(prop_value,"off")){
		RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
		return;
	}
	if(rilhw->prefer_net==kPREFER_NETWORK_TYPE_CDMA_EVDV){
	    RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
		return;
	}
    memset(&sr, 0, sizeof(sr));

    p_args = (RIL_SIM_IO *)data;

    /* FIXME handle pin2 */

    if (p_args->data == NULL) {
        asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d",
                    p_args->command, p_args->fileid,
                    p_args->p1, p_args->p2, p_args->p3);
    } else {
        asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d,%s",
                    p_args->command, p_args->fileid,
                    p_args->p1, p_args->p2, p_args->p3, p_args->data);
    }

    err = at_send_command_singleline(cmd, "+CRSM:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw1));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw2));
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        err = at_tok_nextstr(&line, &(sr.simResponse));
        if (err < 0) goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &sr, sizeof(sr));
    at_response_free(p_response);
    free(cmd);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
    free(cmd);

}

/**
 * RIL_REQUEST_CHANGE_SIM_PIN
 * RIL_REQUEST_CHANGE_SIM_PIN2
*/
static void requestChangePassword(char *facility, void *data, size_t datalen,
						   RIL_Token t)
{
	int err = 0;
	char *oldPassword = NULL;
	char *newPassword = NULL;
	char *cmd = NULL;
	ATResponse *atresponse = NULL;
	int num_retries = -1;
	RIL_Errno errorril = RIL_E_GENERIC_FAILURE;
	ATCmeError cme_error_code;

	if (datalen != 2 * sizeof(char **) || strlen(facility) != 2) {
		goto error;
	}

	oldPassword = ((char **) data)[0];
	newPassword = ((char **) data)[1];

	asprintf(&cmd, "AT+CPWD=\"%s\",\"%s\",\"%s\"", facility, oldPassword,
			 newPassword);

	err = at_send_command(cmd, &atresponse);
	free(cmd);

	num_retries = 3;

	if (err < 0) {
		goto error;
	}
	if (atresponse->success == 0) {
		if (at_get_cme_error(atresponse, &cme_error_code)) {
			switch (cme_error_code) {
			case CME_INCORRECT_PASSWORD: /* CME ERROR 16: "Incorrect password" */
				WARN("%s(): Incorrect password", __func__);
				errorril = RIL_E_PASSWORD_INCORRECT;
				break;
			case CME_SIM_PUK2_REQUIRED: /* CME ERROR 18: "SIM PUK2 required" happens when wrong
				PIN2 is used 3 times in a row */
				WARN("%s(): PIN2 locked, change PIN2 with PUK2", __func__);
				num_retries = 0;/* PUK2 required */
				errorril = RIL_E_SIM_PUK2;
				break;
			default: /* some other error */
				break;
			}
		}
		goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &num_retries, sizeof(int *));
finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, errorril, &num_retries, sizeof(int *));
	goto finally;
}



/**
 * Enter SIM PIN, might be PIN, PIN2, PUK, PUK2, etc.
 *
 * Data can hold pointers to one or two strings, depending on what we
 * want to enter. (PUK requires new PIN, etc.).
 *
 * FIXME: Do we need to return remaining tries left on error as well?
 *        Also applies to the rest of the requests that got the retries
 *        in later commits to ril.h.
 ok
 */
void  requestEnterSimPin(void*  data, size_t  datalen, RIL_Token  t)
{
	int errorcode=RIL_E_GENERIC_FAILURE;
	ATResponse *atresponse = NULL;
    ATCmeError cme_error_code;	
	int 		  err;
	char*		  cmd = NULL;
	int num_retries = -1;
	const char**  strings = (const char**)data;
		
	if ( datalen == sizeof(char*) ) {
		asprintf(&cmd, "AT+CPIN=%s", strings[0]);
	} else if ( datalen == 2*sizeof(char*) ) {
		asprintf(&cmd, "AT+CPIN=%s,%s", strings[0], strings[1]);
	} else
		goto error;

	err = at_send_command(cmd, &atresponse);								
	if(cmd) free(cmd);
	if(err<0)goto error;
    if (atresponse->success == 0) {
        if (at_get_cme_error(atresponse, &cme_error_code)) {
            switch (cme_error_code) {
            case CME_SIM_PIN_REQUIRED:
            case CME_SIM_PUK_REQUIRED:
            case CME_INCORRECT_PASSWORD:
            case CME_SIM_PIN2_REQUIRED:
            case CME_SIM_PUK2_REQUIRED:
            case CME_SIM_FAILURE:
                num_retries = 1;
                RIL_onRequestComplete(t, RIL_E_PASSWORD_INCORRECT, &num_retries, sizeof(int *));
                goto finally;

            default:
                break;
            }
        }
        goto error;
    }	

    /* Got OK, return success and wait for *EPEV to trigger poll of SIM state. */
    num_retries = 1;
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &num_retries, sizeof(int *));
	/* Notify that SIM is ready */			
	//init again 
	modem_init();
	setRadioState(RADIO_STATE_SIM_READY);
	
finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;

}

void requestChangeSimPin(void *data, size_t datalen, RIL_Token t)
{
	requestChangePassword("SC",data, datalen, t);
}
void requestChangeSimPin2(void *data, size_t datalen, RIL_Token t)
{
	requestChangePassword("P2",data, datalen, t);
}

