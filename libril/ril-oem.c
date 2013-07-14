#include <stdio.h>
#include "ril-handler.h"

/**
 * RIL_REQUEST_OEM_HOOK_RAW
 *
 * This request reserved for OEM-specific uses. It passes raw byte arrays
 * back and forth.
*/
void requestOEMHookRaw(void *data, size_t datalen, RIL_Token t)
{
    /* Echo back data */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, data, datalen);
}

/**
 * RIL_REQUEST_OEM_HOOK_STRINGS
 *
 * This request reserved for OEM-specific uses. It passes strings
 * back and forth.
*/
void requestOEMHookStrings(void *data, size_t datalen, RIL_Token t)
{
    int i;
    const char **cur;

    DBG("got OEM_HOOK_STRINGS: 0x%8p %lu", data, (long) datalen);

    for (i = (datalen / sizeof(char *)), cur = (const char **) data;
         i > 0; cur++, i--) {
        DBG("> '%s'", *cur);
    }

    /* Echo back strings. */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, data, datalen);
}
