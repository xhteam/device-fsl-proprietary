#ifndef _RIL_CONFIG_H_
#define _RIL_CONFIG_H_

#include <telephony/ril.h>

//-------------system---------

#define RIL_IMEISV_VERSION  0
#define RIL_DRIVER_VERSION "QRIL 1.1"
#define MAX_AT_RESPONSE (8 * 1024)
#define START_PPPD_TIMEOUT 	60 /*10*2=20s*/

//5s
#define PPPD_WDG_TIMEOUT		5

#define STOP_PPPD_TIMEOUT		30//s

#define SYS_NET_PATH   "/sys/class/net"
#define PPP_INTERFACE  "ppp0"

#define CDMA_MASQUERADING 1

#define MAX_DATA_CALL_COUNT 16


#if(RIL_VERSION>4)
#define RIL_REQUEST_REGISTRATION_STATE RIL_REQUEST_VOICE_REGISTRATION_STATE
#define RIL_REQUEST_GPRS_REGISTRATION_STATE RIL_REQUEST_DATA_REGISTRATION_STATE
#define RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED
#endif

#if (RIL_VERSION==5)
#define RIL_CardStatus RIL_CardStatus_v5
#define RIL_SignalStrength RIL_SignalStrength_v5
#define RIL_SIM_IO RIL_SIM_IO_v5
#elif (RIL_VERSION>5)
#define RIL_CardStatus RIL_CardStatus_v6
#define RIL_SignalStrength RIL_SignalStrength_v6
#define RIL_SIM_IO RIL_SIM_IO_v6
#endif


#endif //_ZTEMT_RIL_CONFIG_H

