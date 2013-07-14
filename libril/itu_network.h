#ifndef _ITU_NETWORK_H
#define _ITU_NETWORK_H

typedef struct itu_operator itu_operator;
struct itu_operator
{
  char* short_alpha;
  char* long_alpha;
  char* numeric;
};

int network_query_operator(const char* imsi,itu_operator* opr);


#endif
