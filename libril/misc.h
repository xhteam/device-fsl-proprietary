/** returns 1 if line starts with prefix, 0 if it does not */
#ifndef _ZTEMT_RIL_MISC_H
#define _ZTEMT_RIL_MISC_H

#ifdef __cplusplus
extern "C" {
#endif

#define PROPERTY_SET_MAX_MS_WAIT            750
#define PROPERTY_SET_CHECK_INTERVAL_MS      50

struct tlv {
    unsigned    tag;
    const char *data;
    const char *end;
};

/** Returns 1 if line starts with prefix, 0 if it does not. */
int strStartsWith(const char *line, const char *prefix);

char *getFirstElementValue(const char *document,
                           const char *elementBeginTag,
                           const char *elementEndTag,
                           char **remainingDocument);

char char2nib(char c);

int stringToBinary(const char *string,
                   size_t len,
                   unsigned char *binary);

int binaryToString(const unsigned char *binary,
                   size_t len,
                   char *string);

int parseTlv(const char *stream,
             const char *end,
             struct tlv *tlv);

int property_set_verified(const char *key, const char *value);

#define TLV_DATA(tlv, pos) (((unsigned)char2nib(tlv.data[(pos) * 2 + 0]) << 4) | \
                            ((unsigned)char2nib(tlv.data[(pos) * 2 + 1]) << 0))

#define NUM_ELEMS(x) (sizeof(x) / sizeof(x[0]))

#ifdef __cplusplus
}
#endif
#endif
