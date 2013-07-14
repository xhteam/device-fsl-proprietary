#include <cutils/properties.h>
#include "ril-handler.h"
#include "ril-fake.h"
static RIL_RadioState sFakeRilState = RADIO_STATE_UNAVAILABLE;

static void fake_ril_set_radio_state(RIL_RadioState newState)
{
	if (sFakeRilState != newState)
	{
		sFakeRilState = newState;
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
	}
}

static void fake_ril_request_radio_power(void *data, size_t datalen, RIL_Token t)
{
	int onOff;

	onOff = ((int *)data)[0];
	if ((onOff == 0 || onOff == 4)&& sFakeRilState != RADIO_STATE_OFF)
	{
		DBG("%s --> Set radio off",__func__);
		fake_ril_set_radio_state(RADIO_STATE_OFF);
	}
	else if (onOff > 0 && sFakeRilState == RADIO_STATE_OFF)
	{
		DBG("%s --> Set radio on",__func__);
		fake_ril_set_radio_state(RADIO_STATE_SIM_NOT_READY);
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void fake_ril_on_request (int request, void *data, size_t datalen, RIL_Token t)
{
    char imei_str[32] = {'\0'};

	switch (request)
	{
	case RIL_REQUEST_RADIO_POWER:
		fake_ril_request_radio_power(data, datalen, t);
		break;

	case RIL_REQUEST_GET_CURRENT_CALLS:
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		break;

	case RIL_REQUEST_GET_IMEI:
		property_get("persist.fakeril.imei", imei_str, "000000000000000");
		RIL_onRequestComplete(t, RIL_E_SUCCESS, imei_str, sizeof(char *));
		break;
	case RIL_REQUEST_GET_IMEISV:
		{
			char* response;
			// IMEISV 
			asprintf(&response, "%d", 1);

			RIL_onRequestComplete(t, RIL_E_SUCCESS,
							  response,
							  sizeof(char *));
			free(response);
		}
		break;

	default:
		RIL_onRequestComplete(t, RIL_E_CANCELLED, NULL, 0);
		break;
	}
}

RIL_RadioState fake_ril_getState(void)
{
	return sFakeRilState;
}


int fake_ril_init(RIL_RadioState state)
{
	fake_ril_set_radio_state (state);
	return 0;
}




