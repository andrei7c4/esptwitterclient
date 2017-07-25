#include <os_type.h>
#include <osapi.h>
#include <ip_addr.h>
#include <lwip/err.h>
#include <lwip/dns.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>
#include <sntp.h>
#include "typedefs.h"
#include "oauth.h"
#include "config.h"
#include "debug.h"
#include "httpreq.h"

LOCAL const char *twitterStatusUrl = "/1.1/statuses/update.json";
LOCAL const char *twitterStreamUrl = "/1.1/user.json";
LOCAL const char *twitterVerifyUrl = "/1.1/account/verify_credentials.json";
LOCAL const char *twitterNewDMUrl = "/1.1/direct_messages/new.json";
LOCAL const char *twitterRetweeetUrl = "/1.1/statuses/retweet/%s.json";
LOCAL const char *twitterFavoritesUrl = "/1.1/favorites/create.json";

#define HTTP_REQ_MAX_LEN	1024
char httpRequest[HTTP_REQ_MAX_LEN];

extern struct espconn espConn;


LOCAL ParamItem* ICACHE_FLASH_ATTR paramListAppend(ParamList *list, const char *param, const char *value)
{
	ParamItem *newItem = (ParamItem*)os_malloc(sizeof(ParamItem));
	if (!newItem)
	{
		return NULL;
	}
	newItem->param = param;
	newItem->value = value;
	newItem->valueEncoded = NULL;
	newItem->paramLen = os_strlen(param);
	newItem->valueLen = os_strlen(value);
	newItem->next = NULL;

	if (list->last)
	{
		list->last->next = newItem;
	}
	else	// this is a first item
	{
		list->first = newItem;
	}
	list->last = newItem;
	list->count++;
	return newItem;
}

LOCAL void ICACHE_FLASH_ATTR paramListClear(ParamList *list)
{
	ParamItem *item = list->first;
	ParamItem *next;
	while (item)
	{
		next = item->next;
		os_free(item);
		item = next;
	}
	list->count = 0;
}


LOCAL int ICACHE_FLASH_ATTR paramListStrLen(ParamList *list)
{
	int length = 0;
	ParamItem *item = list->first;
	while (item)
	{
		length += (item->paramLen + item->valueLen + 2);
		item = item->next;
	}
	if (length > 0)
	{
		length--;	// remove last '&'
	}
	return length;
}

LOCAL int paramListEncodeValues(ParamList *list)
{
	ParamItem *param = list->first;
	int len;
	while (param)
	{
		len = percentEncodedStrLen(param->value, param->valueLen);
		param->valueEncoded = (char*)os_malloc(len+1);
		if (!param->valueEncoded)
		{
			return ERROR;
		}
		if (percentEncode(param->value, param->valueLen, param->valueEncoded, len+1) != len)
		{
			return ERROR;
		}
		param->valueLen = len;
		param = param->next;
	}
	return OK;
}

LOCAL void paramListClearEncodedValues(ParamList *list)
{
	ParamItem *param = list->first;
	while (param)
	{
		os_free(param->valueEncoded);
		param->valueEncoded = NULL;
		param = param->next;
	}
}



LOCAL int appendParams(char *dst, int dstSize, const ParamList *paramList)
{
	int len = 0;
	char *pDst = dst;

	ParamItem *param = paramList->first;
	while (param)
	{
		len = ets_snprintf(pDst, dstSize, "%s=%s&", param->param, param->valueEncoded);
		if (len < 0 || len >= dstSize) return 0;
		pDst += len;
		dstSize -= len;
		param = param->next;
	}

	if (len > 0)
	{
		dstSize++;
		pDst--;
		*pDst = '\0';	// remove last '&'
	}
	return pDst - dst;
}

LOCAL int ICACHE_FLASH_ATTR formHttpRequest(char *dst, int dstSize,
		HttpMethod httpMethod, const char *host, const char *url,
		ParamList *paramList)
{
	int requestLen = 0;
	int baseurlSize = sizeof("https://") + os_strlen(host) + os_strlen(url) + 1;
	char *baseurl = (char*)os_malloc(baseurlSize);
	if (!baseurl) goto out;
	int len = ets_snprintf(baseurl, baseurlSize, "https://%s%s", host, url);
	if (len < 0 || len >= baseurlSize) goto out;

	if (paramListEncodeValues(paramList) != OK)
	{
		goto out;
	}

	const char *method;
	switch (httpMethod)
	{
	case httpGET:
		method = "GET";
		break;
	case httpPOST:
		method = "POST";
		break;
	case httpPUT:
		method = "PUT";
		break;
	default: goto out;
	}

	char nonce[43];
	randomAlphanumericString(nonce, 42);

	char timestamp[11];
	ets_snprintf(timestamp, sizeof(timestamp), "%u", sntp_get_current_timestamp());


	char *pDst = dst;
	len = ets_snprintf(pDst, dstSize, "%s %s", method, url);
	if (len < 0 || len >= dstSize) goto out;
	pDst += len;
	dstSize -= len;

	if (httpMethod == httpGET && paramList->count > 0)
	{
		*pDst = '?';
		pDst++;
		dstSize--;
		len = appendParams(pDst, dstSize, paramList);
		if (len == 0) goto out;
		pDst += len;
		dstSize -= len;
	}

	len = ets_snprintf(pDst, dstSize,
			" HTTP/1.1\r\n"
			"Accept: */*\r\n"
			//"Connection: close\r\n"
			"Connection: keep-alive\r\n"
			"User-Agent: ESP8266\r\n"
			"Content-Type: application/x-www-form-urlencoded\r\n"
			"Authorization: OAuth "
			"oauth_consumer_key=\"%s\", "
			"oauth_nonce=\"%s\", "
			"oauth_signature=\"",
			config.consumer_key, nonce);
	if (len < 0 || len >= dstSize) goto out;
	pDst += len;
	dstSize -= len;

	len = createSignature(pDst, dstSize, method, baseurl, nonce, timestamp, paramList);
	if (len == 0) goto out;
	pDst += len;
	dstSize -= len;

	int contentLen = 0;
	if (httpMethod == httpPOST || httpMethod == httpPUT)
	{
		contentLen = paramListStrLen(paramList);
	}
    
	len = ets_snprintf(pDst, dstSize,
			"\", "		// oauth_signature="",
			"oauth_signature_method=\"HMAC-SHA1\", "
			"oauth_timestamp=\"%s\", "
			"oauth_token=\"%s\", "
			"oauth_version=\"1.0\"\r\n"
			"Content-Length: %d\r\n"
			"Host: %s\r\n\r\n",
			timestamp, config.access_token,
			contentLen, host);
	if (len < 0 || len >= dstSize) goto out;
	pDst += len;
	dstSize -= len;

	if (dstSize < contentLen)
	{
		goto out;
	}

	if (contentLen > 0)
	{
		len = appendParams(pDst, dstSize, paramList);
		if (len == 0) goto out;
		pDst += len;
		dstSize -= len;
	}

	requestLen = pDst - dst;
	debug("\nrequestLen %d\n", requestLen);
	debug("%s\n\n", dst);

out:
	os_free(baseurl);
	paramListClearEncodedValues(paramList);
	return requestLen;
}


int ICACHE_FLASH_ATTR twitterGetUserInfo(const char *host)
{
    int rv = ERROR;    
	ParamList params;
	os_memset(&params, 0, sizeof(ParamList));
	int requestLen = formHttpRequest(httpRequest, HTTP_REQ_MAX_LEN,
			httpGET, host, twitterVerifyUrl, &params);
	if (requestLen > 0)
	{
		if (espconn_secure_send(&espConn, (uint8*)httpRequest, requestLen) == OK)
        {
            rv = OK;            
        }
	}
	else
	{
		debug("getUserInfo formHttpRequest failed\n");
	}
	paramListClear(&params);
    return rv;
}

int ICACHE_FLASH_ATTR twitterRequestStream(const char *host, const char *track, const char *language, const char *filter)
{
    int rv = ERROR;
	ParamList params;
	os_memset(&params, 0, sizeof(ParamList));
	// parameters must be added in alphabetical order
	if (filter && *filter)
	{
		paramListAppend(&params, "filter_level", filter);
	}
	if (language && *language)
	{
		paramListAppend(&params, "language", language);
	}
	if (track && *track)
	{
		paramListAppend(&params, "track", track);
	}
	int requestLen = formHttpRequest(httpRequest, HTTP_REQ_MAX_LEN,
			httpGET, host, twitterStreamUrl, &params);
	if (requestLen > 0)
	{
		if (espconn_secure_send(&espConn, (uint8*)httpRequest, requestLen) == OK)
        {
            rv = OK;            
        }
	}
	else
	{
		debug("requestStream formHttpRequest failed\n");
	}
	paramListClear(&params);
    return rv;
}

int ICACHE_FLASH_ATTR twitterSendDirectMsg(const char *host, const char *text, const char *userId)
{
    int rv = ERROR;
	ParamList params;
	os_memset(&params, 0, sizeof(ParamList));
	paramListAppend(&params, "text", text);
	paramListAppend(&params, "user_id", userId);
	int requestLen = formHttpRequest(httpRequest, HTTP_REQ_MAX_LEN,
			httpPOST, host, twitterNewDMUrl, &params);
	if (requestLen > 0)
	{
		if (espconn_secure_send(&espConn, (uint8*)httpRequest, requestLen) == OK)
        {
            rv = OK;
        }
	}
	else
	{
		debug("sendDirectMsg formHttpRequest failed\n");
	}
	paramListClear(&params);
    return OK;
}

int ICACHE_FLASH_ATTR twitterRetweetTweet(const char *host, const char *tweetId)
{
    int rv = ERROR;
	char url[80];
	int len = ets_snprintf(url, sizeof(url), twitterRetweeetUrl, tweetId);
	if (len < 0 || len >= sizeof(url)) return ERROR;

	ParamList params;
	os_memset(&params, 0, sizeof(ParamList));
	int requestLen = formHttpRequest(httpRequest, HTTP_REQ_MAX_LEN,
			httpPOST, host, url, &params);
	if (requestLen > 0)
	{
		if (espconn_secure_send(&espConn, (uint8*)httpRequest, requestLen) == OK)
        {
            rv = OK;
        }
	}
	else
	{
		debug("retweetTweet formHttpRequest failed\n");
	}
	paramListClear(&params);
    return rv;
}

int ICACHE_FLASH_ATTR twitterLikeTweet(const char *host, const char *tweetId)
{
    int rv = ERROR;
	ParamList params;
	os_memset(&params, 0, sizeof(ParamList));
	paramListAppend(&params, "id", tweetId);
	int requestLen = formHttpRequest(httpRequest, HTTP_REQ_MAX_LEN,
			httpPOST, host, twitterFavoritesUrl, &params);
	if (requestLen > 0)
	{
		if (espconn_secure_send(&espConn, (uint8*)httpRequest, requestLen) == OK)
        {
            rv = OK;
        }
	}
	else
	{
		debug("likeTweet formHttpRequest failed\n");
	}
	paramListClear(&params);
    return rv;
}

int ICACHE_FLASH_ATTR twitterPostTweet(const char *host, const char *text)
{
    int rv = ERROR;
	ParamList params;
	os_memset(&params, 0, sizeof(ParamList));
	paramListAppend(&params, "status", text);
	int requestLen = formHttpRequest(httpRequest, HTTP_REQ_MAX_LEN,
			httpPOST, host, twitterStatusUrl, &params);
	if (requestLen > 0)
	{
		if (espconn_secure_send(&espConn, (uint8*)httpRequest, requestLen) == OK)
        {
            rv = OK;
        }
	}
	else
	{
		debug("formHttpRequest failed\n");
	}
	paramListClear(&params);
    return rv;
}

