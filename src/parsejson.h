#ifndef INCLUDE_PARSEJSON_H_
#define INCLUDE_PARSEJSON_H_

#include "common.h"

int parseTweetText(char *json, int jsonLen, char *text, int textSize);
int parseTweetUserInfo(char *json, int jsonLen,
		char *idStr, int idStrSize,
		char *name, int nameSize,
		char *screenName, int screenNameSize,
		int fromTweet);
int parseCounters(char *json, int jsonLen, int *retweetCount, int *favoriteCount);
int parseTweetId(char *json, int jsonLen, char *idStr, int idStrSize);


#endif /* INCLUDE_PARSEJSON_H_ */
