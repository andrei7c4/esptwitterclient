#include <os_type.h>
#include <osapi.h>
#include "contikijson/jsonparse.h"
#include "contikijson/jsontree.h"
#include "parsejson.h"
#include "common.h"
#include "config.h"
#include "conv.h"


LOCAL int ICACHE_FLASH_ATTR jumpToNextType(struct jsonparse_state *state, char *buf, int bufSize, int depth, int type, const char *name)
{
	int json_type;
	while((json_type = jsonparse_next(state)) != 0)
	{
		if (depth == state->depth && json_type == type)
		{
			if (name)
			{
				jsonparse_copy_value(state, buf, bufSize);
				if (!os_strncmp(buf, name, bufSize))
				{
					return TRUE;
				}
			}
			else
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}


int ICACHE_FLASH_ATTR parseTweetText(char *json, int jsonLen, char *text, int textSize)
{
	char buf[20];
	struct jsonparse_state state;
	jsonparse_setup(&state, json, jsonLen);

	if (!jumpToNextType(&state, buf, sizeof(buf),
			1, JSON_TYPE_PAIR_NAME, "text"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_STRING)
		return ERROR;

	jsonparse_copy_value(&state, text, textSize);

	if (jumpToNextType(&state, buf, sizeof(buf),
			1, JSON_TYPE_PAIR_NAME, "extended_tweet"))
	{
		if (jumpToNextType(&state, buf, sizeof(buf),
				2, JSON_TYPE_PAIR_NAME, "full_text"))
		{
			if (jsonparse_next(&state) == JSON_TYPE_STRING)
			{
				jsonparse_copy_value(&state, text, textSize);
			}
		}
	}

	return OK;
}

int ICACHE_FLASH_ATTR parseTweetUserInfo(char *json, int jsonLen,
		char *idStr, int idStrSize,
		char *name, int nameSize,
		char *screenName, int screenNameSize,
		int fromTweet)
{
	char buf[12];
	struct jsonparse_state state;
	jsonparse_setup(&state, json, jsonLen);

	int depth = 1;
	if (fromTweet)
	{
		if (!jumpToNextType(&state, buf, sizeof(buf),
				1, JSON_TYPE_PAIR_NAME, "user"))
			return ERROR;
		depth = 2;
	}

	if (!jumpToNextType(&state, buf, sizeof(buf),
			depth, JSON_TYPE_PAIR_NAME, "id_str"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_STRING)
		return ERROR;

	jsonparse_copy_value(&state, idStr, idStrSize);

	if (!jumpToNextType(&state, buf, sizeof(buf),
			depth, JSON_TYPE_PAIR_NAME, "name"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_STRING)
		return ERROR;

	jsonparse_copy_value(&state, name, nameSize);

	if (!jumpToNextType(&state, buf, sizeof(buf),
			depth, JSON_TYPE_PAIR_NAME, "screen_name"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_STRING)
		return ERROR;

	jsonparse_copy_value(&state, screenName, screenNameSize);
	return OK;
}

int ICACHE_FLASH_ATTR parseCounters(char *json, int jsonLen, int *retweetCount, int *favoriteCount)
{
	char buf[20];
	struct jsonparse_state state;
	jsonparse_setup(&state, json, jsonLen);

	int depth = 1;
	// check if this is a retweet
	if (jumpToNextType(&state, buf, sizeof(buf),
		1, JSON_TYPE_PAIR_NAME, "retweeted_status"))
	{
		depth = 2;	// get retweet counters
	}
	else
	{
		jsonparse_setup(&state, json, jsonLen);		
	}
	
	if (!jumpToNextType(&state, buf, sizeof(buf),
			depth, JSON_TYPE_PAIR_NAME, "retweet_count"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_NUMBER)
		return ERROR;

	jsonparse_copy_value(&state, buf, sizeof(buf));
	*retweetCount = strtoint(buf);
	
	
	if (!jumpToNextType(&state, buf, sizeof(buf),
			depth, JSON_TYPE_PAIR_NAME, "favorite_count"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_NUMBER)
		return ERROR;

	jsonparse_copy_value(&state, buf, sizeof(buf));
	*favoriteCount = strtoint(buf);
	
	return OK;
}

int ICACHE_FLASH_ATTR parseTweetId(char *json, int jsonLen, char *idStr, int idStrSize)
{
	char buf[12];
	struct jsonparse_state state;
	jsonparse_setup(&state, json, jsonLen);

	if (!jumpToNextType(&state, buf, sizeof(buf),
			1, JSON_TYPE_PAIR_NAME, "id_str"))
		return ERROR;

	if (jsonparse_next(&state) != JSON_TYPE_STRING)
		return ERROR;

	jsonparse_copy_value(&state, idStr, idStrSize);
	return OK;
}

