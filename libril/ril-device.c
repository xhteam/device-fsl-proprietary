#include <stdio.h>
#include <string.h>

#include "ril-handler.h"

static char* def_IMEI="352037030152447";
static char* def_MEID="A000001F97E1A1";


void requestGetIMSI(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;

	if((kRIL_HW_M305== rilhw->model)||(kRIL_HW_WM630== rilhw->model))
	{
		err = at_send_command_singleline("AT+CIMI","+CIMI:", &atresponse);

		if (err < 0 || atresponse->success == 0) {
		    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		} else {
		    //eat pre space ,why ZTE forget this?
		    char* line = atresponse->p_intermediates->line;
			char* imsi;
			
			err = at_tok_start(&line);
			if (err < 0)	goto error;
			
			err = at_tok_nextstr(&line, &imsi);
			if (err < 0) goto error;

			while(*imsi==' ') imsi++;
		    RIL_onRequestComplete(t, RIL_E_SUCCESS,
		                          imsi,
		                          sizeof(char *));
				
		}
		
	}
	else
	{
		err = at_send_command_numeric("AT+CIMI", &atresponse);

		if (err < 0 || atresponse->success == 0) {
		    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		} else {
		    RIL_onRequestComplete(t, RIL_E_SUCCESS,
		                          atresponse->p_intermediates->line,
		                          sizeof(char *));
		}
	}
    at_response_free(atresponse);
	return;
error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(atresponse);
	
}

/* RIL_REQUEST_DEVICE_IDENTITY
 *
 * RIL_REQUEST_DEVICE_IDENTITY
 *
 * Request the device ESN / MEID / IMEI / IMEISV.
 *
 * The request is always allowed and contains GSM and CDMA device identity;
 * it substitutes the deprecated requests RIL_REQUEST_GET_IMEI and
 * RIL_REQUEST_GET_IMEISV.
 *
 * If a NULL value is returned for any of the device id, it means that error
 * accessing the device.
 *
 * When CDMA subscription is changed the ESN/MEID may change.  The application
 * layer should re-issue the request to update the device identity in this case.
 *
 * "response" is const char **
 * ((const char **)response)[0] is IMEI if GSM subscription is available
 * ((const char **)response)[1] is IMEISV if GSM subscription is available
 * ((const char **)response)[2] is ESN if CDMA subscription is available
 * ((const char **)response)[3] is MEID if CDMA subscription is available
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  GENERIC_FAILURE
 */
 
void requestDeviceIdentity(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    char* response[4]={0};
    int err;
    int i=0;


	if(kRIL_HW_MC2716==rilhw->model||
		kRIL_HW_MC8630 ==rilhw->model )
	{
		// ESN
		err = at_send_command_numeric("AT+GSN", &atresponse);
		
		if (err < 0 || atresponse->success == 0) {
			goto error;
		} else {
			asprintf(&response[2], "%s", atresponse->p_intermediates->line);
			at_response_free(atresponse);
			atresponse = NULL;
		}
		//MEID , IMEI
		 err = at_send_command_singleline("AT^MEID", "",&atresponse);
		
		 if (err < 0 || atresponse->success == 0) {
			 DBG("requestDeviceIdentity fill with default MEID");
			 asprintf(&response[3], "%s", def_MEID);
			 at_response_free(atresponse);
			 atresponse = NULL;
			 //goto error;
		 } else {
			 asprintf(&response[0], "%s", atresponse->p_intermediates->line);
			 asprintf(&response[3], "%s", atresponse->p_intermediates->line);
			 at_response_free(atresponse);
			 atresponse = NULL;
		 }
		
	}
	else
	{
	
		/* IMEI */ 
		err = at_send_command_singleline("AT+CGSN","+CGSN:", &atresponse);
		if (err < 0 || atresponse->success == 0) 
		{			
			//try raw query
			at_response_free(atresponse);
			err = at_send_command_raw("AT+CGSN",&atresponse);
 			if(err < 0 || atresponse->success == 0) goto error;
			asprintf(&response[0], "%s", atresponse->p_intermediates->line);
		}
		else 
		{		
		
			char* line = atresponse->p_intermediates->line;
			char* imei;
			
			err = at_tok_start(&line);
			if (err < 0)	goto error;
			
			err = at_tok_nextstr(&line, &imei);
			if (err < 0) goto error;
			
			while(*imei==' ') imei++;
			asprintf(&response[0], "%s",imei);
		}

		/* CDMA not supported */
		response[2] = NULL;
		response[3] = NULL;
		
	}

    
    // IMEISV 
    asprintf(&response[1], "%02d", RIL_IMEISV_VERSION);

    
    RIL_onRequestComplete(t, RIL_E_SUCCESS,
                          &response,
                          sizeof(response));
    

finally:
	for( i=0;i<4;i++){
		if(response[i])   free(response[i]);
	}
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/* Deprecated */
/**
 * RIL_REQUEST_GET_IMEI
 *
 * Get the device IMEI, including check digit.
*/

void requestGetIMEI(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;

	if(kRIL_HW_MC2716==rilhw->model||
		kRIL_HW_MC8630==rilhw->model)
    {
    	if(RADIO_STATE_SIM_READY==radio_state_ready)
    	{
    		//return a fake IMEI 
			RIL_onRequestComplete(t, RIL_E_SUCCESS,
		                              def_IMEI,
		                              sizeof(char *));
			return;
    	
    	}
    	else
		{
		    err = at_send_command_singleline("AT^MEID", "",&atresponse);

		    if (err < 0 || atresponse->success == 0) goto error;

      }
	}
	else
	{
		err = at_send_command_singleline("AT+CGSN","+CGSN:", &atresponse);
		if (err < 0 || atresponse->success == 0) 
		{
			
			//try raw query
			at_response_free(atresponse);
			err = at_send_command_raw("AT+CGSN",&atresponse);
 			if(err < 0 || atresponse->success == 0) goto error;
			RIL_onRequestComplete(t, RIL_E_SUCCESS,
								  atresponse->p_intermediates->line,
								  sizeof(char *));
		}
		else 
		{		
		
			char* line = atresponse->p_intermediates->line;
			char* imei;
			
			err = at_tok_start(&line);
			if (err < 0)	goto error;
			
			err = at_tok_nextstr(&line, &imei);
			if (err < 0) goto error;
			
			while(*imei==' ') imei++;
			RIL_onRequestComplete(t, RIL_E_SUCCESS,
								  imei,
								  sizeof(char *));			
		}
	}

	at_response_free(atresponse);
	return;
error:
	DBG("requestGetIMEI fill with default IMEI");	
	RIL_onRequestComplete(t, RIL_E_SUCCESS,
						  def_IMEI,
						  sizeof(char *));
	at_response_free(atresponse);
}

void requestGetIMEISV(void *data, size_t datalen, RIL_Token t)
{
	char* response;
    // IMEISV 
    asprintf(&response, "%02d", RIL_IMEISV_VERSION);

	RIL_onRequestComplete(t, RIL_E_SUCCESS,
						  response,
						  sizeof(char *));
	free(response);
	
}

/**
 * RIL_REQUEST_BASEBAND_VERSION
 *
 * Return string value indicating baseband version, eg
 * response from AT+CGMR.
*/

void requestBasebandVersion(void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *atresponse = NULL;
    char *line;

    err = at_send_command_singleline("AT+CGMR", "+CGMR:", &atresponse);

    if (err < 0 || 
        atresponse->success == 0 || 
        atresponse->p_intermediates == NULL) {
        //try another ugly query pattern,ZTE bug?
		at_response_free(atresponse);
		err = at_send_command_raw("AT+CGMR",&atresponse);
		if(err<0||
			atresponse->success == 0|| 
        	atresponse->p_intermediates == NULL)
		{
			goto error;
		
		}		
	    line = atresponse->p_intermediates->line;
	    RIL_onRequestComplete(t, RIL_E_SUCCESS, line, sizeof(char *));
	    at_response_free(atresponse);
	    return;
    }
	else
	{

	    line = atresponse->p_intermediates->line;

	    RIL_onRequestComplete(t, RIL_E_SUCCESS, line+sizeof("+CGMR:"), sizeof(char *));
	    at_response_free(atresponse);
	    return;
	}

error:
    ERROR("Error in requestBasebandVersion()");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(atresponse);
}

static int device_vendor(const char* ident)
{
    int err;
    ATResponse *atresponse = NULL;
    char *line=NULL,*find=NULL;
    int ret = 0;

    int i=0;
    for(i=0;i<3 && ret==0;i++)
	{
    	
    	err = at_send_command_singleline("AT+CGMI", "",&atresponse);

		if (err < 0 || 
       		 atresponse->success == 0 || 
       		 atresponse->p_intermediates == NULL) 
		{
			at_response_free(atresponse);
			atresponse = NULL;
			sleep(1);
			continue;
		}

   	 line = atresponse->p_intermediates->line;
   	 find = strcasestr(line,ident);
   	 ret = (find != NULL) ? 1 : 0;
   	 at_response_free(atresponse);
   	 atresponse = NULL;
    }

    at_response_free(atresponse);
    return ret;
	
}
int is_ztemt_device()
{
	return device_vendor("ZTEMT");
}

int is_qualcomm_device()
{
	return device_vendor("QUALCOMM");
}
