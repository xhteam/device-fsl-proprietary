#ifndef _FAKE_RIL_H_
#define _FAKE_RIL_H_

#include <telephony/ril.h>

int fake_ril_init(RIL_RadioState state);
void fake_ril_on_request (int request, void *data, size_t datalen, RIL_Token t);

RIL_RadioState fake_ril_getState(void);



#endif

