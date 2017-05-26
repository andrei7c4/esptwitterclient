#ifndef INCLUDE_CONFIG_H_
#define INCLUDE_CONFIG_H_

#include "typedefs.h"


#define DEFAULT_SSID	""
#define DEFAULT_PASS	""

#define DEFALUT_CONSUMER_KEY	""
#define DEFAULT_ACCESS_TOKEN	""
#define DEFAULT_CONSUMER_SECRET	""
#define DEFAULT_TOKEN_SECRET	""

#define DEFAULT_TRACK			""
#define DEFAULT_FILTER			""
#define DEFAULT_LANGUAGE		""


#define CONFIG_SAVE_FLASH_SECTOR	0x0F
#define CONFIG_SAVE_FLASH_ADDR		(CONFIG_SAVE_FLASH_SECTOR * SPI_FLASH_SEC_SIZE)
#define VALID_MAGIC_NUMBER			0xAABBCCDD

typedef struct{
	uint magic;
	char ssid[36];
	char pass[68];

	char consumer_key[128];
	char access_token[128];
	char consumer_secret[128];
	char token_secret[128];

	char trackStr[128];
	char filter[8];
	char language[32];

	int dispScrollEn;
	int titleScrollEn;
    
    int debugEn;
}Config;
extern Config config;



void configInit(Config *config);
void configRead(Config *config);
void configWrite(Config *config);



#endif /* INCLUDE_CONFIG_H_ */
