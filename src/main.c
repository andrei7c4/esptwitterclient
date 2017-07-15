#include <os_type.h>
#include <osapi.h>
#include <ip_addr.h>
#include <lwip/err.h>
#include <lwip/dns.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>
#include <sntp.h>
#include <gpio.h>
#include "drivers/uart.h"
#include "drivers/spi.h"
#include "config.h"
#include "debug.h"
#include "httpreq.h"
#include "oauth.h"
#include "common.h"
#include "parsejson.h"
#include "fonts.h"
#include "icons.h"
#include "strlib.h"
#include "graphics.h"
#include "display.h"
#include "menu.h"
#include "mpu6500.h"



typedef struct
{
	char idStr[50];
	int idStrLen;
	ushort *name;
	int nameLen;
	char screenName[50];
	int screenNameLen;
}UserInfo;
UserInfo curUser = {0};

typedef struct
{
	UserInfo user;
	char idStr[50];
	int idStrLen;
	int retweetCount;
	int favoriteCount;
}TweetInfo;
TweetInfo curTweet = {{0},0,0,0,0,0,0};

LOCAL ushort *trackWstr = NULL;
LOCAL StrList trackList = {NULL, 0};
void createTrackList(const char *trackStr);

typedef enum{
	titleStateNoTitle,
	titleStateNewTweet,
	titleStateDrawCounters,
	titleStateDrawName,
}TitleState;
TitleState titleState = titleStateNoTitle;
#define TITLE_STATE_INTERVAL		3000
LOCAL os_timer_t titleStateTmr;

LOCAL os_timer_t gpTmr;
LOCAL os_timer_t httpRxTmr;
LOCAL os_timer_t buttonsTmr;
LOCAL os_timer_t screenSaverTmr;
LOCAL os_timer_t accelTmr;
extern os_timer_t scrollTmr;

LOCAL int mutePeriod = 0;
LOCAL uint lastTweetRecvTs = 0;

#define HTTP_RX_BUF_SIZE	8192
char httpMsgRxBuf[HTTP_RX_BUF_SIZE];
int httpRxMsgCurLen = 0;

const uint dnsCheckInterval = 100;

struct espconn espConn = {0};
LOCAL struct _esp_tcp espConnTcp = {0};


LOCAL void getUserInfo(void);
LOCAL void parseApiReply(void);
LOCAL void requestStream(void);
LOCAL void parseStreamReply(void);

typedef struct
{
	const char *host;
	ip_addr_t ip;
	void (*requestFunc)(void);
	void (*parserFunc)(void);
}ConnParams;
ConnParams apiConnParams = {"api.twitter.com", {0}, getUserInfo, parseApiReply};
ConnParams streamConnParams = {"userstream.twitter.com", {0}, requestStream, parseStreamReply};

LOCAL int disconnExpected = FALSE;
LOCAL int reconnCbCalled = FALSE;

LOCAL void connectToWiFiAP(void);
LOCAL void checkWiFiConnStatus(void);
LOCAL void checkSntpSync(void);
LOCAL void connectToHost(ConnParams *params);
LOCAL void connectFirstTime(ConnParams *params);
LOCAL void checkDnsStatus(void *arg);
LOCAL void getHostByNameCb(const char *name, ip_addr_t *ipaddr, void *arg);
LOCAL void onTcpConnected(void *arg);
LOCAL void onTcpDataSent(void *arg);
LOCAL void onTcpDataRecv(void *arg, char *pusrdata, unsigned short length);
LOCAL void onTcpDisconnected(void *arg);
LOCAL void reconnect(void);
LOCAL void onTcpReconnCb(void *arg, sint8 err);
LOCAL void titleTmrCb(void);
LOCAL void buttonsScanTmrCb(void);
LOCAL void drawTwitterLogo(void);
LOCAL void wakeupDisplay(void);
LOCAL void screenSaverTmrCb(void);
LOCAL void accelTmrCb(void);


typedef enum{
	stateInit,
	stateConnectToAp,
    stateConnectToHost,
    stateConnected,
	stateMuted
}AppState;
AppState appState = stateInit;

LOCAL void ICACHE_FLASH_ATTR setAppState(AppState newState)
{
	if (appState != newState)
	{
		appState = newState;
		debug("appState %d\n", (int)appState);
	}
}


void user_init(void)
{
	os_timer_disarm(&gpTmr);
	os_timer_disarm(&httpRxTmr);

	os_timer_disarm(&titleStateTmr);
	os_timer_setfn(&titleStateTmr, (os_timer_func_t*)titleTmrCb, NULL);
	os_timer_disarm(&scrollTmr);

	os_timer_disarm(&buttonsTmr);
	os_timer_setfn(&buttonsTmr, (os_timer_func_t*)buttonsScanTmrCb, NULL);

	os_timer_disarm(&screenSaverTmr);
	os_timer_setfn(&screenSaverTmr, (os_timer_func_t*)screenSaverTmrCb, NULL);

	os_timer_disarm(&accelTmr);
	os_timer_setfn(&accelTmr, (os_timer_func_t*)accelTmrCb, NULL);

	//uart_init(BIT_RATE_115200, BIT_RATE_115200);
	uart_init(BIT_RATE_921600, BIT_RATE_921600);

	dispSetActiveMemBuf(MainMemBuf);
	dispFillMem(0, DISP_HEIGHT);

//configInit(&config);
//configWrite(&config);
	configRead(&config);

	os_memset(&trackList, 0, sizeof(StrList));
	createTrackList(config.trackStr);
	
	debug("Built on %s %s\n", __DATE__, __TIME__);
	debug("SDK version %s\n", system_get_sdk_version());
	debug("free heap %d\n", system_get_free_heap_size());
    
	gpio_init();

	spi_init(HSPI, 20, 5, FALSE);	// spi clock = 800 kHz

	// same spi settings for SSD1322 and MPU6500
	// data is valid on clock trailing edge
	// clock is high when inactive
	spi_mode(HSPI, 1, 1);

	if (mpu6500_init() == OK)
	{
		// accelerometer found -> read values twice per second
		os_timer_arm(&accelTmr, 500, 1);
	}

	SSD1322_init();

	drawTwitterLogo();
	dispUpdate(Page0);
	wakeupDisplay();
	
	setAppState(stateConnectToAp);
	wifi_set_opmode(STATION_MODE);
	connectToWiFiAP();
    
	// enable buttons scan
	os_timer_arm(&buttonsTmr, 100, 1);
}

LOCAL void ICACHE_FLASH_ATTR connectToWiFiAP(void)
{
	struct station_config stationConf;
	stationConf.bssid_set = 0;	// mac address not needed
	os_memcpy(stationConf.ssid, config.ssid, sizeof(stationConf.ssid));
	os_memcpy(stationConf.password, config.pass, sizeof(stationConf.password));
	wifi_station_set_config(&stationConf);

	checkWiFiConnStatus();
}

LOCAL void ICACHE_FLASH_ATTR checkWiFiConnStatus(void)
{
	struct ip_info ipconfig;
	memset(&ipconfig, 0, sizeof(ipconfig));

	// check current connection status and own ip address
	wifi_get_ip_info(STATION_IF, &ipconfig);
	uint8 connStatus = wifi_station_get_connect_status();
	if (connStatus == STATION_GOT_IP && ipconfig.ip.addr != 0)
	{
		// connection with AP established -> sync time
        // TODO: make addresses configurable
		sntp_setservername(0, "europe.pool.ntp.org");
		sntp_setservername(1, "us.pool.ntp.org");
		sntp_set_timezone(0);
		sntp_init();
		checkSntpSync();
	}
	else
	{
		if (connStatus == STATION_WRONG_PASSWORD ||
			connStatus == STATION_NO_AP_FOUND	 ||
			connStatus == STATION_CONNECT_FAIL)
		{
			debug("Failed to connect to AP (status: %u)\n", connStatus);
		}
		else
		{
			// not yet connected, recheck later
			os_timer_setfn(&gpTmr, (os_timer_func_t*)checkWiFiConnStatus, NULL);
			os_timer_arm(&gpTmr, 100, 0);
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR checkSntpSync(void)
{
	uint ts = sntp_get_current_timestamp();
	if (ts < 1483228800)	// 1.1.2017
	{
		// not yet synced, recheck later
		os_timer_setfn(&gpTmr, (os_timer_func_t*)checkSntpSync, NULL);
		os_timer_arm(&gpTmr, 100, 0);
		return;
	}

	// time synced -> connect to Twitter
	connectFirstTime(&apiConnParams);
}

LOCAL void ICACHE_FLASH_ATTR connectToHost(ConnParams *params)
{
    setAppState(stateConnectToHost);
        
	espConn.reverse = params;
	if (espConn.state == ESPCONN_CONNECT)		// if we are currently connected to some host -> disconnect
	{											// and try to connect when disconnection occurs
		disconnExpected = TRUE;
		// disconnect should not be called directly from here
		os_timer_setfn(&gpTmr, (os_timer_func_t*)espconn_secure_disconnect, &espConn);
		os_timer_arm(&gpTmr, 100, 0);
	}
	else	// we are currently not connected to any host
	{
		if (params->ip.addr)	// we have ip of the host
		{
			// use this ip and try to connect
			os_memcpy(&espConn.proto.tcp->remote_ip, &params->ip.addr, 4);
			reconnect();
		}
		else	// we don't yet have ip of the host
		{
			connectFirstTime(params);
		}
	}
}

void ICACHE_FLASH_ATTR connectToStreamHost(void)	// called from config.c
{
	connectToHost(&streamConnParams);
}

void ICACHE_FLASH_ATTR connectToApiHost(void)	// called from config.c
{
	if (config.consumer_key[0] && config.access_token[0] &&
		config.consumer_secret[0] && config.token_secret[0])
	{
		connectToHost(&apiConnParams);
	}
}

LOCAL void ICACHE_FLASH_ATTR connectFirstTime(ConnParams *params)
{
	// connection with AP established -> get openweathermap server ip
	//setAppState(stateGetTwitterIp);

	espConn.proto.tcp = &espConnTcp;
	espConn.type = ESPCONN_TCP;
	espConn.state = ESPCONN_NONE;
	espConn.reverse = params;
	//serverIp.addr = 0;
	//espconn_gethostbyname(&tcpSock, twitterApiHost, &serverIp, getHostByNameCb);
	//espconn_gethostbyname(&tcpSock, twitterStreamHost, &serverIp, getHostByNameCb);

	// register callbacks
	espconn_regist_connectcb(&espConn, onTcpConnected);
	espconn_regist_reconcb(&espConn, onTcpReconnCb);

	espconn_gethostbyname(&espConn, params->host, &params->ip, getHostByNameCb);

	os_timer_setfn(&gpTmr, (os_timer_func_t*)checkDnsStatus, params);
	os_timer_arm(&gpTmr, dnsCheckInterval, 0);
}

LOCAL void ICACHE_FLASH_ATTR checkDnsStatus(void *arg)
{
    //struct espconn *pespconn = arg;
	ConnParams *params = arg;
    //if (appState == stateGetTwitterIp)
    {
		//espconn_gethostbyname(pespconn, twitterApiHost, &serverIp, getHostByNameCb);
		espconn_gethostbyname(&espConn, params->host, &params->ip, getHostByNameCb);
		os_timer_arm(&gpTmr, dnsCheckInterval, 0);
    }
}

LOCAL void ICACHE_FLASH_ATTR getHostByNameCb(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;
    ConnParams *params = pespconn->reverse;
	os_timer_disarm(&gpTmr);

	if (params->ip.addr != 0)
	{
		debug("getHostByNameCb serverIp != 0\n");
		return;
	}
	if (ipaddr == NULL || ipaddr->addr == 0)
	{
		debug("getHostByNameCb ip NULL\n");
		return;
	}
	debug("getHostByNameCb ip: "IPSTR"\n", IP2STR(ipaddr));
    
	// connect to host
	params->ip.addr = ipaddr->addr;
	os_memcpy(pespconn->proto.tcp->remote_ip, &ipaddr->addr, 4);
	pespconn->proto.tcp->remote_port = 443;	// use HTTPS port
	pespconn->proto.tcp->local_port = espconn_port();	// get next free local port number
	
	espconn_secure_set_size(ESPCONN_CLIENT, 8192);
	int rv = espconn_secure_connect(pespconn);	// tcp SSL connect
	debug("espconn_secure_connect %d\n", rv);

	os_timer_setfn(&gpTmr, (os_timer_func_t*)reconnect, 0);
	os_timer_arm(&gpTmr, 10000, 0);
}

LOCAL void ICACHE_FLASH_ATTR onTcpConnected(void *arg)
{
    setAppState(stateConnected);
    
	debug("onTcpConnected\n");
	struct espconn *pespconn = arg;
	ConnParams *params = pespconn->reverse;
	os_timer_disarm(&gpTmr);
	// register callbacks
	espconn_regist_recvcb(pespconn, onTcpDataRecv);
	espconn_regist_sentcb(pespconn, onTcpDataSent);
	espconn_regist_disconcb(pespconn, onTcpDisconnected);

	params->requestFunc();
	reconnCbCalled = FALSE;
}

LOCAL void ICACHE_FLASH_ATTR onTcpDataSent(void *arg)
{
	debug("onTcpDataSent\n");
}


LOCAL char* ICACHE_FLASH_ATTR findNewline(char *str, int length)
{
	while (length > 1)
	{
		if (*str == '\r' && *(str+1) == '\n')
		{
			return str;
		}
		str++;
		length--;
	}
	return NULL;
}

LOCAL int ICACHE_FLASH_ATTR copyFilterNewlines(char *dst, int dstSize, char *src, int srcLen)
{
	char *pDst = dst;
	char *prevNewline = src;
	while (dstSize > 0 && srcLen > 0)
	{
		char *newline = findNewline(src, srcLen);
		if (newline)
		{
			int diff = newline-prevNewline;
			if (diff <= 6)
			{
				src += (diff+2);
				srcLen -= (diff+2);
			}
			else
			{
				int bytes = MIN(dstSize, diff);
				os_memcpy(pDst, src, bytes);
				pDst += bytes;
				src += bytes;
				dstSize -= bytes;
				srcLen -= bytes;
			}
			prevNewline = newline;
		}
		else	// no more newlines, copy rest
		{
			int bytes = MIN(dstSize, srcLen);
			os_memcpy(pDst, src, bytes);
			pDst += bytes;
			src += bytes;
			dstSize -= bytes;
			srcLen -= bytes;
		}
	}
	return (pDst-dst);
}

LOCAL void ICACHE_FLASH_ATTR onTcpDataRecv(void *arg, char *pusrdata, unsigned short length)
{
	debug("onTcpDataRecv %d\n", length);
	os_timer_disarm(&httpRxTmr);
	
	ConnParams *params = espConn.reverse;
	int spaceLeft = (HTTP_RX_BUF_SIZE-httpRxMsgCurLen-1);
	httpRxMsgCurLen += copyFilterNewlines(httpMsgRxBuf+httpRxMsgCurLen, spaceLeft, pusrdata, length);
	if (httpRxMsgCurLen > 0)
	{
		if (httpRxMsgCurLen < (HTTP_RX_BUF_SIZE-1))
		{			
			os_timer_setfn(&httpRxTmr, (os_timer_func_t*)params->parserFunc, NULL);
			os_timer_arm(&httpRxTmr, 1000, 0);
		}
		else
		{
			debug("httpMsgRxBuf full\n");
			params->parserFunc();
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR onTcpDisconnected(void *arg)
{
	// on unexpected disconnection the following might happen:
	// usually: only onTcpReconnCb is called
	// sometimes: only onTcpDisconnected is called
	// rarely: both onTcpReconnCb and onTcpDisconnected are called

	struct espconn *pespconn = arg;
	ConnParams *params = pespconn->reverse;
	debug("onTcpDisconnected\n");
	debug("disconnExpected %d\n", disconnExpected);
	if (!disconnExpected)	// we got unexpectedly disconnected
	{
		debug("reconnCbCalled %d\n", reconnCbCalled);
		if (reconnCbCalled)		// if onTcpReconnCb was also called
		{						// just ignore this callback
			reconnCbCalled = FALSE;
			return;
		}

		// onTcpReconnCb was not called -> try to reconnect from here
		if (params != &streamConnParams)	// but only if we are reading stream
		{
			return;
		}
	}
	else
	{
		disconnExpected = FALSE;
	}

	if (appState == stateMuted)		// don't reconnect when muted
	{
		return;
	}

	// in all other cases -> try to reconnect
	if (params->ip.addr)	// we have ip of the host
	{
		// use this ip and try to connect
		os_memcpy(&espConn.proto.tcp->remote_ip, &params->ip.addr, 4);
		reconnect();
	}
	else	// we don't yet have ip of the host
	{
		connectFirstTime(params);
	}
}

LOCAL void ICACHE_FLASH_ATTR onTcpReconnCb(void *arg, sint8 err)
{
	debug("onTcpReconnCb\n");

	// ok, something went wrong and we got disconnected
	// try to reconnect in 5 sec
	os_timer_disarm(&gpTmr);
	os_timer_setfn(&gpTmr, (os_timer_func_t*)reconnect, 0);
	os_timer_arm(&gpTmr, 5000, 0);
	reconnCbCalled = TRUE;
}

LOCAL void ICACHE_FLASH_ATTR reconnect(void)
{
	debug("reconnect\n");
	int rv = espconn_secure_connect(&espConn);
	debug("espconn_secure_connect %d\n", rv);

	os_timer_disarm(&gpTmr);
	os_timer_setfn(&gpTmr, (os_timer_func_t*)reconnect, 0);
	os_timer_arm(&gpTmr, 10000, 0);
}



void ICACHE_FLASH_ATTR createTrackList(const char *trackStr)
{
	int trackLen = os_strlen(trackStr);
	int trackConvSize = (trackLen+1)*sizeof(ushort);
	os_free(trackWstr);
	trackWstr = (ushort*)os_malloc(trackConvSize);
	if (trackWstr)
	{
		trackLen = u8_toucs(trackWstr, trackConvSize, trackStr, trackLen);
		if (trackLen > 0)
		{
			clearStrList(&trackList);
			strSplit(trackWstr, &trackList);
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR requestStream(void)
{    
    twitterRequestStream(streamConnParams.host, 
        config.trackStr, config.language, config.filter);
}

LOCAL void ICACHE_FLASH_ATTR getUserInfo(void)
{
    twitterGetUserInfo(apiConnParams.host);
}

void ICACHE_FLASH_ATTR shareCurrentTweet(void)
{
	char msg[130];
	int len = ets_snprintf(msg, sizeof(msg), "https://twitter.com/%s/status/%s", curTweet.user.screenName, curTweet.idStr);
	if (len < 0 || len >= sizeof(msg)) return;

    twitterSendDirectMsg(apiConnParams.host, msg, curUser.idStr);
}

void ICACHE_FLASH_ATTR retweetCurrentTweet(void)
{
    twitterRetweetTweet(apiConnParams.host, curTweet.idStr);
}

void ICACHE_FLASH_ATTR likeCurrentTweet(void)
{
    twitterLikeTweet(apiConnParams.host, curTweet.idStr);
}



LOCAL void ICACHE_FLASH_ATTR drawUserName(int x, int y, const UserInfo *user)
{
	if (user->nameLen && user->screenNameLen)
	{
		int xPos = x;
		xPos += drawStr(&arial10b, x, y+1, user->name, user->nameLen);
		char *atstr = " @";
		xPos += drawStr_Latin(&arial10, xPos, y, atstr, -1);
		//drawStrLatinOnly(&arial10, xPos, 0, user->screenName, user->screenNameLen);
		drawStrHighlight_Latin(&arial10, xPos, y, user->screenName);
	}
}

void ICACHE_FLASH_ATTR drawCurTweetUserName(void)	// called from menu.c
{
	drawUserName(0, 0, &curTweet.user);
}

LOCAL void ICACHE_FLASH_ATTR drawCounter(int x, int y, int value, const uint *icon)
{
	drawImage(x, y, icon);
	int iconWidth = icon[0];
	char str[20];
	int strLen;
	char unit[2] = " ";
	if (value >= 1000)
	{
		value /= 1000;
		unit[0] = 'K';
	}
	strLen = ets_snprintf(str, sizeof(str), "%d%s", value, unit);
	if (strLen && strLen < sizeof(str))
	{
		drawStr_Latin(&arial13, x+iconWidth+5, y, str, strLen);
	}
}

LOCAL void ICACHE_FLASH_ATTR drawCounters(void)
{
	drawCounter(0, 0, curTweet.retweetCount, retweetIcon);
	drawCounter(104, 0, curTweet.favoriteCount, heartIcon);
}

LOCAL void ICACHE_FLASH_ATTR drawTwitterLogo(void)
{
	int width = twitterLogo[0];
	int x = (DISP_WIDTH/2) - (width/2);
	drawImage(x, 0, twitterLogo);
}


LOCAL int ICACHE_FLASH_ATTR parseTweet(TweetInfo *tweet, ushort **text)
{	
	char *json = (char*)os_strstr(httpMsgRxBuf, "{\"created_at\"");
	if (!json)
	{
		return ERROR;
	}
	
	int jsonLen = httpRxMsgCurLen - (json - httpMsgRxBuf);
	//debug("jsonLen %d\n", jsonLen);
	
	const int jsonValBufSize = 1024;
	char *jsonValBuf = (char*)os_malloc(jsonValBufSize);
	if (!jsonValBuf)
	{
		return ERROR;
	}

	if (parseTweetText(json, jsonLen, jsonValBuf, jsonValBufSize) != OK)
	{
		os_free(jsonValBuf);
		return ERROR;
	}
	//debug("\n%s\n", jsonValBuf);
	
	int textLen = decodeUtf8(jsonValBuf, os_strlen(jsonValBuf), text);
	if (!textLen)
	{
		os_free(jsonValBuf);
		return ERROR;
	}
	replaceLinks(*text, textLen);
	replaceHtmlEntities(*text, textLen);

	if (parseTweetUserInfo(json, jsonLen,
			tweet->user.idStr, sizeof(tweet->user.idStr),
			jsonValBuf, jsonValBufSize,
			tweet->user.screenName, sizeof(tweet->user.screenName), TRUE) == OK)
	{
		os_free(tweet->user.name);
		tweet->user.nameLen = decodeUtf8(jsonValBuf, os_strlen(jsonValBuf), &tweet->user.name);
		tweet->user.screenNameLen = os_strlen(tweet->user.screenName);
		tweet->user.idStrLen = os_strlen(tweet->user.idStr);
	}
	else
	{
		tweet->user.name[0] = '\0';
		tweet->user.nameLen = 0;
		tweet->user.screenName[0] = '\0';
		tweet->user.screenNameLen = 0;
		tweet->user.idStr[0] = '\0';
		tweet->user.idStrLen = 0;
	}
	
	if (parseCounters(json, jsonLen, &tweet->retweetCount, &tweet->favoriteCount) != OK)
	{
		tweet->retweetCount = 0;
		tweet->favoriteCount = 0;
	}
	
	if (parseTweetId(json, jsonLen, tweet->idStr, sizeof(tweet->idStr)) == OK)
	{
		tweet->idStrLen = os_strlen(tweet->idStr);
	}
	else
	{
		tweet->idStr[0] = '\0';
		tweet->idStrLen = 0;
	}

	os_free(jsonValBuf);
	return OK;
}

LOCAL void ICACHE_FLASH_ATTR showTweet(const TweetInfo *tweet, const ushort *text)
{
	os_timer_disarm(&titleStateTmr);
	//os_timer_disarm(&scrollTmr);
	dispSetActiveMemBuf(MainMemBuf);
	dispFillMem(0, DISP_HEIGHT);

	if (!drawStrWordWrapped(0, TITLE_HEIGHT, DISP_WIDTH-1, DISP_HEIGHT-1, text, &arial13, &arial13b, &trackList, FALSE))
	{
		drawStrWordWrapped(0, TITLE_HEIGHT, DISP_WIDTH-1, DISP_HEIGHT-1, text, &arial10, &arial10b, &trackList, TRUE);
	}

	drawUserName(0, 0, &tweet->user);

	uint ts = sntp_get_current_timestamp();
	if (((ts - lastTweetRecvTs) < 5) || !config.dispScrollEn)
	{
		// if tweets are coming fast -> don't animate
		dispUpdate(dispScrollCurLine == 0 ? Page0 : Page1);
		os_timer_arm(&titleStateTmr, TITLE_STATE_INTERVAL, 0);
	}
	else
	{
		scrollDisplay();
	}
	lastTweetRecvTs = ts;

	wakeupDisplay();
	titleState = titleStateNewTweet;
}


LOCAL void ICACHE_FLASH_ATTR parseStreamReply(void)
{
	if (menuState != MenuHidden)
	{
		// ignore new tweets while menu is shown
		httpRxMsgCurLen = 0;
		return;
	}

	debug("parseStreamReply, len %d\n", httpRxMsgCurLen);
	httpMsgRxBuf[httpRxMsgCurLen] = '\0';
	//debug("%s\n", httpMsgRxBuf);
    
	ushort *text = NULL;
	if (parseTweet(&curTweet, &text) == OK && text)
	{
		showTweet(&curTweet, text);
		os_free(text);
	}
	else
	{
		debug("NO TWEET FOUND\n");
		if (httpRxMsgCurLen < 200)
			debug("%s\n", httpMsgRxBuf);
	}
	debug("free heap %d\n", system_get_free_heap_size());
	
	httpRxMsgCurLen = 0;
}

LOCAL int ICACHE_FLASH_ATTR parseCurUserName(void)
{
	char *json = (char*)os_strstr(httpMsgRxBuf, "{\"");
	if (!json)
	{
		return ERROR;
	}

	int jsonLen = httpRxMsgCurLen - (json - httpMsgRxBuf);

	const int jsonValBufSize = 1024;
	char *jsonValBuf = (char*)os_malloc(jsonValBufSize);
	if (!jsonValBuf)
	{
		return ERROR;
	}

	if (parseTweetUserInfo(json, jsonLen,
			curUser.idStr, sizeof(curUser.idStr),
			jsonValBuf, jsonValBufSize,
			curUser.screenName, sizeof(curUser.screenName), FALSE) == OK)
	{
		os_free(curUser.name);
		curUser.nameLen = decodeUtf8(jsonValBuf, os_strlen(jsonValBuf), &curUser.name);
		curUser.screenNameLen = os_strlen(curUser.screenName);
		curUser.idStrLen = os_strlen(curUser.idStr);
		os_free(jsonValBuf);
		return OK;
	}

	curUser.name[0] = '\0';
	curUser.nameLen = 0;
	curUser.screenName[0] = '\0';
	curUser.screenNameLen = 0;
	curUser.idStr[0] = '\0';
	curUser.idStrLen = 0;
	os_free(jsonValBuf);
	return ERROR;
}

LOCAL void ICACHE_FLASH_ATTR showStreamReqParams(void)
{
	dispSetActiveMemBuf(MainMemBuf);
	dispFillMem(0, DISP_HEIGHT);

	int xPos = drawStr_Latin(&arial10b, 0, 1, "User: ", -1);
	drawUserName(xPos, 0, &curUser);

	xPos = drawStr_Latin(&arial10b, 0, 23, "Track: ", -1);
	if (trackWstr[0])
	{
		drawStr(&arial10, xPos, 23, trackWstr, -1);
	}
	else
	{
		drawStr_Latin(&arial10, xPos, 25, "none", -1);
	}

	xPos = drawStr_Latin(&arial10b, 0, 38, "Language: ", -1);
	drawStr_Latin(&arial10, xPos, 40, config.language[0] ? config.language : "any", -1);

	xPos = drawStr_Latin(&arial10b, 0, 53, "Filter: ", -1);
	drawStr_Latin(&arial10, xPos, 55, config.filter[0] ? config.filter : "none", -1);

	scrollDisplay();
	wakeupDisplay();
}

LOCAL void ICACHE_FLASH_ATTR parseApiReply(void)
{
	debug("parseApiReply, len %d\n", httpRxMsgCurLen);
	httpMsgRxBuf[httpRxMsgCurLen] = '\0';
	//debug("%s\n", httpMsgRxBuf);

	if (apiConnParams.requestFunc == getUserInfo)	// this is a reply to user info request
	{
		if (parseCurUserName() == OK)
		{
			showStreamReqParams();
		}
	}
	else if (apiConnParams.requestFunc == shareCurrentTweet)
	{
		char *ok = (char*)os_strstr(httpMsgRxBuf, "HTTP/1.1 200 OK");
		menu1execDone(ok ? OK : ERROR);
	}
	else if (apiConnParams.requestFunc == retweetCurrentTweet ||
			 apiConnParams.requestFunc == likeCurrentTweet)		// this is a reply to retweet or like request
	{
		ushort *text = NULL;
		if (parseTweet(&curTweet, &text) == OK && text)
		{
			showTweet(&curTweet, text);
			os_free(text);
			menu1execDone(OK);
		}
		else
		{
			debug("NO TWEET FOUND\n");
			menu1execDone(ERROR);
		}
	}
	httpRxMsgCurLen = 0;

	//connectToHost(&streamConnParams);
	os_timer_setfn(&gpTmr, (os_timer_func_t*)connectToHost, &streamConnParams);
	os_timer_arm(&gpTmr, 1000, 0);
}



LOCAL void ICACHE_FLASH_ATTR titleTmrCb(void)
{
	static int titleStateChanges = 0;
	if (titleState == titleStateNewTweet)
	{
		titleStateChanges = 1;
	}
	else
	{
		if (titleStateChanges < 6)
		{
			titleStateChanges++;
		}
		else
		{
			return;
		}
	}
	
	switch (titleState)
	{
	case titleStateNewTweet:
		titleState = titleStateDrawCounters;
		dispSetActiveMemBuf(SecondaryMemBuf);
		dispFillMem(0, TITLE_HEIGHT);
		drawCounters();
		break;
	case titleStateDrawCounters:
		titleState = titleStateDrawName;
		dispFillMem(0, TITLE_HEIGHT);
		drawUserName(0, 0, &curTweet.user);
		break;
	case titleStateDrawName:
		titleState = titleStateDrawCounters;
		dispFillMem(0, TITLE_HEIGHT);
		drawCounters();
		break;
	}
	
	if (!config.titleScrollEn)
	{
		dispCopySecMemBufToMain();
		dispUpdateTitle();
		os_timer_arm(&titleStateTmr, TITLE_STATE_INTERVAL, 0);
	}
	else
	{
		scrollTitle();
	}
}

void ICACHE_FLASH_ATTR titleScrollDone(void)
{
	os_timer_arm(&titleStateTmr, TITLE_STATE_INTERVAL, 0);
}

void ICACHE_FLASH_ATTR displayScrollDone(void)
{
	if (titleState == titleStateNewTweet)
	{
		os_timer_arm(&titleStateTmr, TITLE_STATE_INTERVAL, 0);
	}
}


LOCAL void ICACHE_FLASH_ATTR unmuteDisplay(void)
{
	os_timer_disarm(&gpTmr);
	connectToHost(&streamConnParams);
}

LOCAL void ICACHE_FLASH_ATTR unmuteTmrCb(void)
{
	mutePeriod--;
	if (mutePeriod <= 0)
	{
		unmuteDisplay();
	}
}

LOCAL void ICACHE_FLASH_ATTR muteDisplay(int period)
{
	setAppState(stateMuted);

	ConnParams *params = espConn.reverse;
	if (espConn.state != ESPCONN_NONE)
	{
		disconnExpected = TRUE;
		espconn_secure_disconnect(&espConn);
	}

	//dispSetActiveMemBuf(MainMemBuf);
	//dispFillMem(0, DISP_HEIGHT);
	//SSD1322_cpyMemBuf(dispScrollCurLine, DISP_HEIGHT);

	//curTweet.user.nameLen = 0;
	//curTweet.user.screenNameLen = 0;

	dispVerticalSqueezeStart();

	menu2execDone(OK);

	if (period > 0)
	{
		mutePeriod = period;
		os_timer_setfn(&gpTmr, (os_timer_func_t*)unmuteTmrCb, NULL);
		os_timer_arm(&gpTmr, 60000, 1);
	}
}


LOCAL Button adcValToButton(uint16 adcVal)
{
	if (adcVal >= 768) return NotPressed;
	if (adcVal >= 256) return Button1;
	return Button2;
}

LOCAL void ICACHE_FLASH_ATTR buttonsScanTmrCb(void)
{
	if (dispScrollCurLine != 0 && dispScrollCurLine != 64)
	{
		return;		// currently scrolling new tweet, ignore buttons
	}

	static Button prevButtons = NotPressed;
	uint16 adcVal = system_adc_read();
	Button buttons = adcValToButton(adcVal);
	//debug("adcVal %d, buttons %d\n", adcVal, buttons);
	if (prevButtons != buttons)
	{
		prevButtons = buttons;
		if (buttons != NotPressed)
		{
			if (appState == stateMuted)
			{
				unmuteDisplay();
			}
			else if (displayState == stateOn)
			{
				os_timer_disarm(&titleStateTmr);
				menuStateMachine(buttons);
			}
			wakeupDisplay();
		}
	}
}

void ICACHE_FLASH_ATTR menu1execCb(void *arg)
{
	apiConnParams.requestFunc = arg;
	connectToHost(&apiConnParams);
}

void ICACHE_FLASH_ATTR menu2execCb(void *arg)
{
	os_timer_setfn(&gpTmr, (os_timer_func_t*)muteDisplay, arg);
	os_timer_arm(&gpTmr, 1000, 0);
}


// TODO: make times configurable
LOCAL void ICACHE_FLASH_ATTR wakeupDisplay(void)
{
	dispUndimmStart();
	os_timer_arm(&screenSaverTmr, 5*60*1000, 0);
}

LOCAL void ICACHE_FLASH_ATTR screenSaverTmrCb(void)
{
	switch (displayState)
	{
	case stateOn:
		dispDimmingStart();
		os_timer_arm(&screenSaverTmr, 15*60*1000, 0);
		break;
	case stateDimmed:
		dispVerticalSqueezeStart();
		break;
	}
}


LOCAL void ICACHE_FLASH_ATTR accelTmrCb(void)
{
	sint16 x = accelReadX();
	if (x < -8192 && dispOrient != orient180deg)
	{
		dispSetOrientation(orient180deg);
	}
	else if (x > 8192 && dispOrient != orient0deg)
	{
		dispSetOrientation(orient0deg);
	}
}
