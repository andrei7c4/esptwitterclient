#include <os_type.h>
#include <osapi.h>
#include <mem.h>
#include "oauth.h"
#include "config.h"
#include "debug.h"
#include "httpreq.h"

int hmac_sha1(const u8 *key, size_t key_len, const u8 *data, size_t data_len, u8 *mac);
unsigned char *base64_encode(const unsigned char *src, size_t len, size_t *out_len);

int ICACHE_FLASH_ATTR base64encode(const char *src, int srcLen, char *dst, int dstSize)
{
	unsigned char *tempmem = (char*)os_malloc(80);
	unsigned char *result;
	int len;

	if (!tempmem)
	{
		return 0;
	}

	// fake rom malloc init
	mem_init(tempmem);
	result = base64_encode(src, 20, &len);
	if (len > dstSize)
	{
		os_free(tempmem);
		return 0;
	}
	len--;
	os_memcpy(dst, result, len);
	dst[len] = '\0';
	os_free(tempmem);
	return len;
}


LOCAL void ICACHE_FLASH_ATTR asciiHex(unsigned char byte, char *ascii)
{
	const char *hex = "0123456789ABCDEF";
	ascii[0] = hex[byte >> 4];
	ascii[1] = hex[byte & 0x0F];
}

LOCAL int ICACHE_FLASH_ATTR charNeedEscape(char ch)
{
	if ((ch >= '0' && ch <= '9') ||
		(ch >= 'A' && ch <= 'Z') ||
		(ch >= 'a' && ch <= 'z') ||
		 ch == '-' || ch == '.'  ||
		 ch == '_' || ch == '~')
	{
		return 0;
	}
	return 1;
}

int ICACHE_FLASH_ATTR percentEncode(const char *src, int srcLen, char *dst, int dstSize)
{
	char ch;
	int len = 0;
	while (srcLen--)
	{
		ch = *src++;
		if (charNeedEscape(ch))
		{
			if ((len+2) < dstSize)
			{
				*dst = '%';
				dst++;
				asciiHex(ch, dst);
				dst += 2;
				len += 3;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			if (len < dstSize)
			{
				*dst = ch;
				dst++;
				len++;
			}
			else
			{
				return 0;
			}
		}
	}
	if (len >= dstSize)
	{
		return 0;
	}
	*dst = '\0';
	return len;
}

int ICACHE_FLASH_ATTR percentEncodedStrLen(const char *str, int strLen)
{
	int length = 0;
	while (*str && strLen)
	{
		length += charNeedEscape(*str) ? 3 : 1;
		str++;
		strLen--;
	}
	return length;
}


LOCAL int ICACHE_FLASH_ATTR random(int min, int max)
{
   return (rand() % (max - min + 1)) + min;
}

LOCAL char ICACHE_FLASH_ATTR randomAlphanumeric(void)
{
	char ranges[3][2] = {
		{'0','9'},
		{'A','Z'},
		{'a','z'}};

	int range = rand() % 3;
	return random(ranges[range][0], ranges[range][1]);
}

void ICACHE_FLASH_ATTR randomAlphanumericString(char *str, int len)
{
	static int randInit = FALSE;
	if (!randInit)
	{
		srand(sntp_get_current_timestamp());
		randInit = TRUE;
	}

	while (len--)
	{
		*str = randomAlphanumeric();
		str++;
	}
	*str = '\0';
}

LOCAL int ICACHE_FLASH_ATTR appendParamPercentEncode(char *dst, int dstSize, const char *param, const char *value, int valueLen)
{
	char *pDst = dst;
	int len = ets_snprintf(pDst, dstSize, "%s%%3D", param);
	if (len < 0 || len >= dstSize) return 0;
	pDst += len;
	dstSize -= len;

	len = percentEncode(value, valueLen, pDst, dstSize);
	if (len == 0 && valueLen > 0) return 0;
	pDst += len;
	dstSize -= len;

	len = ets_snprintf(pDst, dstSize, "%%26");
	if (len < 0 || len >= dstSize) return 0;
	pDst += len;

	return pDst - dst;
}

LOCAL int ICACHE_FLASH_ATTR createSignatureParamStr(char *dst, int dstSize,
	const char *nonce, const char *timestamp,
	const ParamList *paramList,
	const char *consumer_key, const char *oauth_token)
{
	char *pDst = dst;
	int len = 0;

	ParamItem *param = paramList->first;
	while (param)
	{
		if (param->param[0] >= 'o')
		{
			break;
		}
		len = appendParamPercentEncode(pDst, dstSize, param->param, param->valueEncoded, param->valueLen);
		if (len == 0) return 0;
		pDst += len;
		dstSize -= len;
		param = param->next;
	}

	len = appendParamPercentEncode(pDst, dstSize, "oauth_consumer_key", consumer_key, os_strlen(consumer_key));
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	len = ets_snprintf(pDst, dstSize, "oauth_nonce%%3D%s%%26", nonce);
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	len = ets_snprintf(pDst, dstSize, "oauth_signature_method%%3DHMAC-SHA1%%26");
	if (len < 0 || len >= dstSize) return 0;
	pDst += len;
	dstSize -= len;

	len = ets_snprintf(pDst, dstSize, "oauth_timestamp%%3D%s%%26", timestamp);
	if (len < 0 || len >= dstSize) return 0;
	pDst += len;
	dstSize -= len;

	len = appendParamPercentEncode(pDst, dstSize, "oauth_token", oauth_token, os_strlen(oauth_token));
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	len = ets_snprintf(pDst, dstSize, "oauth_version%%3D1.0%%26");
	if (len < 0 || len >= dstSize) return 0;
	pDst += len;
	dstSize -= len;

	while (param)
	{
		len = appendParamPercentEncode(pDst, dstSize, param->param, param->valueEncoded, param->valueLen);
		if (len == 0) return 0;
		pDst += len;
		dstSize -= len;
		param = param->next;
	}

	// remove last '&'
	pDst -= 3;
	return pDst - dst;
}

LOCAL int ICACHE_FLASH_ATTR createSignatureBaseStr(char *dst, int dstSize,
	const char *httpMethod, const char *baseUrl,
	const char *nonce, const char *timestamp,
	const ParamList *paramList)
{
	char *pDst = dst;
	int len = ets_snprintf(pDst, dstSize, "%s&", httpMethod);
	if (len < 0 || len >= dstSize) return 0;
	pDst += len;
	dstSize -= len;

	len = percentEncode(baseUrl, os_strlen(baseUrl), pDst, dstSize);
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	if (dstSize < 1) return 0;
	*pDst++ = '&';
	dstSize--;

	len = createSignatureParamStr(pDst, dstSize, nonce, timestamp, paramList, config.consumer_key, config.access_token);
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	if (dstSize < 1) return 0;
	*pDst = '\0';

	return pDst - dst;
}

LOCAL int ICACHE_FLASH_ATTR createSignatureKey(char *dst, int dstSize)
{
	char *pDst = dst;
	int len = percentEncode(config.consumer_secret, os_strlen(config.consumer_secret), pDst, dstSize);
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	if (dstSize < 1) return 0;
	*pDst++ = '&';
	dstSize--;

	len = percentEncode(config.token_secret, os_strlen(config.token_secret), pDst, dstSize);
	if (len == 0) return 0;
	pDst += len;
	dstSize -= len;

	if (dstSize < 1) return 0;
	*pDst = '\0';

	return pDst - dst;
}

int ICACHE_FLASH_ATTR createSignature(char *dst, int dstSize,
		const char *httpMethod, const char *baseUrl,
		const char *nonce, const char *timestamp,
		const ParamList *paramList)
{
	int signatureLen = 0;
	int baseStrSize = 1024;
	char *baseStr = (char*)os_malloc(baseStrSize);
	if (!baseStr)
	{
		return 0;
	}
	int len = createSignatureBaseStr(
				baseStr, baseStrSize,
				httpMethod, baseUrl,
				nonce, timestamp,
				paramList);
	if (len == 0)
	{
		debug("createSignatureBaseStr failed\n");
		os_free(baseStr);
		return 0;
	}

	//debug("baseStr len %d\n", len);
	//debug("%s\n", baseStr);
    

	int signKeySize = 256;
	char *signKey = (char*)os_malloc(signKeySize);
	if (!signKey)
	{
		os_free(baseStr);
		return 0;
	}

	len = createSignatureKey(signKey, signKeySize);
	if (len == 0)
	{
		debug("createSignatureKey failed\n");
		goto out;
	}

	//debug("signKey len %d\n", len);
	//debug("%s\n", signKey);


	char sha1result[20];
	char base64str[40];

	int rv = hmac_sha1(signKey, os_strlen(signKey), baseStr, os_strlen(baseStr), sha1result);
	if (rv != OK)
	{
		debug("hmac_sha1 failed\n");
		goto out;
	}

//	int i;
//	for (i = 0; i < 20; i++)
//	{
//		debug("%02X ", sha1result[i]);
//	}
//	debug("\n");

	len = base64encode(sha1result, sizeof(sha1result), base64str, sizeof(base64str));
	if (len == 0)
	{
		debug("base64encode failed\n");
		goto out;
	}
	//debug("base64 len %d\n", len);
	//debug("base64:\n%s\n", base64str);

	signatureLen = percentEncode(base64str, len, dst, dstSize);

out:
	os_free(baseStr);
	os_free(signKey);
	return signatureLen;
}
