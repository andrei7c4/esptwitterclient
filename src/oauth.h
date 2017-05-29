#ifndef SRC_OAUTH_H_
#define SRC_OAUTH_H_

#include "common.h"


int base64encode(const char *src, int srcLen, char *dst, int dstSize);

int percentEncode(const char *src, int srcLen, char *dst, int dstSize);
int percentEncodedStrLen(const char *str, int strLen);

void randomAlphanumericString(char *str, int len);


typedef struct ParamList ParamList;
int createSignature(char *dst, int dstSize,
	const char *httpMethod, const char *baseUrl,
	const char *nonce, const char *timestamp,
	const ParamList *paramList);


#endif /* SRC_OAUTH_H_ */
