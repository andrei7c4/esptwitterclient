#ifndef SRC_HTTPREQ_H_
#define SRC_HTTPREQ_H_

typedef struct ParamItem ParamItem;
typedef struct ParamList ParamList;

struct ParamItem
{
	const char *param;
	const char *value;
	char *valueEncoded;
	int paramLen;
	int valueLen;
	ParamItem *next;
};

struct ParamList
{
	ParamItem *first;
	ParamItem *last;
	int count;
};

int twitterGetUserInfo(const char *host);
int twitterRequestStream(const char *host, const char *track, const char *language, const char *filter);
int twitterSendDirectMsg(const char *host, const char *text, const char *userId);
int twitterRetweetTweet(const char *host, const char *tweetId);
int twitterLikeTweet(const char *host, const char *tweetId);
int twitterPostTweet(const char *host, const char *text);


#endif /* SRC_HTTPREQ_H_ */
