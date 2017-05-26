#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include <spi_flash.h>
#include "config.h"

extern void createTrackList(const char *trackStr);
extern void connectToStreamHost(void);
extern void connectToApiHost(void);


Config config;

void ICACHE_FLASH_ATTR configInit(Config *config)
{
	os_memset(config, 0, sizeof(Config));
	config->magic = VALID_MAGIC_NUMBER;
	os_strcpy(config->ssid, DEFAULT_SSID);
	os_strcpy(config->pass, DEFAULT_PASS);

	os_strcpy(config->consumer_key, DEFALUT_CONSUMER_KEY);
	os_strcpy(config->access_token, DEFAULT_ACCESS_TOKEN);
	os_strcpy(config->consumer_secret, DEFAULT_CONSUMER_SECRET);
	os_strcpy(config->token_secret, DEFAULT_TOKEN_SECRET);

	os_strcpy(config->trackStr, DEFAULT_TRACK);
	os_strcpy(config->filter, DEFAULT_FILTER);
	os_strcpy(config->language, DEFAULT_LANGUAGE);

	config->dispScrollEn = TRUE;
	config->titleScrollEn = TRUE;
    
    config->debugEn = FALSE;
}

void ICACHE_FLASH_ATTR configRead(Config *config)
{
	spi_flash_read(CONFIG_SAVE_FLASH_ADDR, (uint*)config, sizeof(Config));
	if (config->magic != VALID_MAGIC_NUMBER)
	{
		os_printf("no valid config in flash\n");
		configInit(config);
		configWrite(config);
	}
	else
	{
		os_printf("valid config found\n");
	}
}

void ICACHE_FLASH_ATTR configWrite(Config *config)
{
	spi_flash_erase_sector(CONFIG_SAVE_FLASH_SECTOR);
	spi_flash_write(CONFIG_SAVE_FLASH_ADDR, (uint*)config, sizeof(Config));
}

LOCAL int ICACHE_FLASH_ATTR resetConfig(const char *value, uint valueLen)
{
	configInit(&config);
	os_printf("OK\n");
	configWrite(&config);
	system_restart();
	return OK;
}


LOCAL int ICACHE_FLASH_ATTR setParam(char *param, uint paramSize, const char *value, uint valueLen)
{
	if (!value || valueLen > paramSize-1)
	{
		return ERROR;
	}
	os_memset(param, 0, paramSize);
	os_memcpy(param, value, valueLen);
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setBoolParam(int *param, const char *value, uint valueLen)
{
	if (!value || !*value || !valueLen)
		return ERROR;

	switch (value[0])
	{
	case '0':
		*param = FALSE;
		return OK;
	case '1':
		*param = TRUE;
		return OK;
	default:
		return ERROR;
	}
}


LOCAL int ICACHE_FLASH_ATTR setSsid(const char *value, uint valueLen)
{
	if (setParam(config.ssid, sizeof(config.ssid), value, valueLen) != OK)
	{
		return ERROR;
	}
	os_printf("OK\n");
	configWrite(&config);
	system_restart();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setPass(const char *value, uint valueLen)
{
	if (setParam(config.pass, sizeof(config.pass), value, valueLen) != OK)
	{
		return ERROR;
	}
	os_printf("OK\n");
	configWrite(&config);
	system_restart();
	return OK;
}


LOCAL int ICACHE_FLASH_ATTR setConsumerKey(const char *value, uint valueLen)
{
	if (setParam(config.consumer_key, sizeof(config.consumer_key), value, valueLen) != OK)
	{
		return ERROR;
	}
	connectToApiHost();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setAccessToken(const char *value, uint valueLen)
{
	if (setParam(config.access_token, sizeof(config.access_token), value, valueLen) != OK)
	{
		return ERROR;
	}
	connectToApiHost();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setConsumerSecret(const char *value, uint valueLen)
{
	if (setParam(config.consumer_secret, sizeof(config.consumer_secret), value, valueLen) != OK)
	{
		return ERROR;
	}
	connectToApiHost();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setTokenSecret(const char *value, uint valueLen)
{
	if (setParam(config.token_secret, sizeof(config.token_secret), value, valueLen) != OK)
	{
		return ERROR;
	}
	connectToApiHost();
	return OK;
}


LOCAL int ICACHE_FLASH_ATTR setTrack(const char *value, uint valueLen)
{
	if (setParam(config.trackStr, sizeof(config.trackStr), value, valueLen) != OK)
	{
		return ERROR;
	}
	createTrackList(config.trackStr);
	connectToStreamHost();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setFilter(const char *value, uint valueLen)
{
	if (setParam(config.filter, sizeof(config.filter), value, valueLen) != OK)
	{
		return ERROR;
	}
	connectToStreamHost();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setLanguage(const char *value, uint valueLen)
{
	if (setParam(config.language, sizeof(config.language), value, valueLen) != OK)
	{
		return ERROR;
	}
	connectToStreamHost();
	return OK;
}

LOCAL int ICACHE_FLASH_ATTR setDispScroll(const char *value, uint valueLen)
{
	return setBoolParam(&config.dispScrollEn, value, valueLen);
}

LOCAL int ICACHE_FLASH_ATTR setTitleScroll(const char *value, uint valueLen)
{
	return setBoolParam(&config.titleScrollEn, value, valueLen);
}

LOCAL int ICACHE_FLASH_ATTR setDebug(const char *value, uint valueLen)
{
	return setBoolParam(&config.debugEn, value, valueLen);
}


typedef struct
{
    const char *cmd;
    int (*func)(const char *value, uint valueLen);
}CmdEntry;

CmdEntry commands[] = {
	{"ssid", setSsid},
	{"pass", setPass},
	{"consumer_key", setConsumerKey},
	{"access_token", setAccessToken},
	{"consumer_secret", setConsumerSecret},
	{"token_secret", setTokenSecret},
	{"track", setTrack},
	{"filter", setFilter},
	{"language", setLanguage},
	{"disp_scroll", setDispScroll},
	{"title_scroll", setTitleScroll},
	{"debug", setDebug},
	{"reset", resetConfig},
};

void ICACHE_FLASH_ATTR onUartCmdReceived(char* command, int length)
{
	if (length < 5)
		return;

	char *sep = os_strchr(command, ':');
	if (!sep)
		return;
	*sep = '\0';

	char *value = sep+1;
	int valueLen = os_strlen(value);

	uint i;
	uint nrCmds = sizeof(commands)/sizeof(commands[0]);
	for (i = 0; i < nrCmds; i++)
	{
		if (!os_strcmp(command, commands[i].cmd))
		{
			if (commands[i].func(value, valueLen) == OK)
			{
				os_printf("OK\n");
				configWrite(&config);
			}
			else
			{
				os_printf("invalid parameter\n");
			}
			return;
		}
	}
	os_printf("command not supported\n");
}
