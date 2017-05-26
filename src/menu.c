#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include <mem.h>

#include "common.h"
#include "debug.h"
#include "fonts.h"
#include "menu.h"
#include "graphics.h"
#include "display.h"


extern void menu1execCb(void *arg);
extern void menu2execCb(void *arg);
extern void shareCurrentTweet(void);
extern void retweetCurrentTweet(void);
extern void likeCurrentTweet(void);
extern void drawCurTweetUserName(void);

extern os_timer_t scrollTmr;

#define SCROLL_INTERVAL		10


typedef struct{
	const char *text;
	void *arg;
	const char *okText;
	const char *failedText;
}MenuItem;

typedef struct{
	const char *title;
	const char *delimeter;
	MenuItem *items;
	int count;
	int selected;
	int selectedPos;
	void (*exec)(void *);
}Menu;


LOCAL MenuItem menu1items[] = {
	{" Share ", shareCurrentTweet, "Tweet shared", "Unable to share"},
	{" Retweet ", retweetCurrentTweet, NULL, "Unable to retweet"},
	{" Like ", likeCurrentTweet, NULL, "Unable to like"}
};

LOCAL MenuItem menu2items[] = {
	{" 30m ", (void*)30, NULL, NULL},
	{" 1h ", (void*)60, NULL, NULL},
	{" 8h ", (void*)480, NULL, NULL},
	{" 24h ", (void*)1440, NULL, NULL},
	{" Off ", 0, NULL, NULL}
};

LOCAL Menu menu1 = {"", "  |  ", menu1items, NELEMENTS(menu1items), 0, 0, menu1execCb};
LOCAL Menu menu2 = {"Mute: ", " | ", menu2items, NELEMENTS(menu2items), 0, 0, menu2execCb};
LOCAL Menu *curMenu = &menu1;
LOCAL Button selectButton = Button1;
LOCAL Button okButton = Button2;

MenuState menuState = MenuHidden;
LOCAL os_timer_t menuTmr;


LOCAL void ICACHE_FLASH_ATTR menuDraw(Menu *menu, MemBufType memBuf)
{
	dispSetActiveMemBuf(memBuf);
	dispFillMem(0, TITLE_HEIGHT);

	int xPos = drawStr_Latin(&arial10, 0, 1, menu->title, -1);
	int i;
	for (i = 0; i < menu->count; i++)
	{
		if (i == menu->selected)
		{
			menu->selectedPos = xPos;
			xPos += drawStrHighlight_Latin(&arial10b, xPos, 0, menu->items[i].text);
		}
		else
		{
			xPos += drawStr_Latin(&arial10b, xPos+1, 1, menu->items[i].text, -1);
			xPos += 2;
		}
		if (i < (menu->count-1))
		{
			xPos += drawStr_Latin(&arial10b, xPos, 0, menu->delimeter, -1);
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR menuIncSelection(Menu *menu)
{
	menu->selected++;
	if (menu->selected >= menu->count)
	{
		menu->selected = 0;
	}
}

LOCAL void ICACHE_FLASH_ATTR menuExecSelection(Menu *menu)
{
	menu->exec(menu->items[menu->selected].arg);
}

LOCAL void ICACHE_FLASH_ATTR scrollMenuIn(void *arg)
{
	if (dispTitleScrollStep((int)arg))
	{
		os_timer_disarm(&scrollTmr);
	}
	dispUpdateTitle();
}

LOCAL void ICACHE_FLASH_ATTR scrollMenuOut(void *arg)
{
	if (dispTitleScrollStep((int)arg))
	{
		os_timer_disarm(&scrollTmr);
	}
	drawStrHighlight_Latin(&arial10b, curMenu->selectedPos, 0, curMenu->items[curMenu->selected].text);
	dispUpdateTitle();
}

LOCAL void ICACHE_FLASH_ATTR scrollUserName(void *arg)
{
	if (dispTitleScrollStep((int)arg))
	{
		os_timer_disarm(&scrollTmr);
		menuState = MenuHidden;
	}
	dispUpdateTitle();
}

LOCAL void ICACHE_FLASH_ATTR startScroll(os_timer_func_t *func)
{
	func((void*)TRUE);
	os_timer_setfn(&scrollTmr, func, FALSE);
	os_timer_arm(&scrollTmr, SCROLL_INTERVAL, 1);
}

LOCAL void ICACHE_FLASH_ATTR menuHide(void)
{
	dispSetActiveMemBuf(SecondaryMemBuf);
	dispFillMem(0, TITLE_HEIGHT);
	drawCurTweetUserName();
	startScroll(scrollUserName);
}

LOCAL void ICACHE_FLASH_ATTR menuDelayedHide(int delay)
{
	os_timer_disarm(&menuTmr);
	os_timer_setfn(&menuTmr, menuHide, NULL);
	os_timer_arm(&menuTmr, delay, 0);
}

LOCAL void ICACHE_FLASH_ATTR scrollStatus(void *arg)
{
	if (dispTitleScrollStep((int)arg))
	{
		os_timer_disarm(&scrollTmr);
		menuDelayedHide(3000);
	}
	dispUpdateTitle();
}



void ICACHE_FLASH_ATTR menuStateMachine(Button buttons)
{
	if (buttons == NotPressed) return;

	switch (menuState)
	{
	case MenuHidden:
		switch (buttons)
		{
		case Button1:
			curMenu = &menu1;
			selectButton = Button1;
			okButton = Button2;
			break;
		case Button2:
			curMenu = &menu2;
			selectButton = Button2;
			okButton = Button1;
			break;
		}

		menuState = MenuShow;
		curMenu->selected = 0;
		os_timer_disarm(&scrollTmr);
		menuDraw(curMenu, SecondaryMemBuf);
		startScroll(scrollMenuIn);

		// automatically hide menu after 10s
		menuDelayedHide(10000);
		break;
	case MenuShow:
		if (buttons == selectButton)
		{
			debug("selectButton\n");
			os_timer_disarm(&scrollTmr);
			menuIncSelection(curMenu);
			menuDraw(curMenu, MainMemBuf);
			dispUpdateTitle();

			menuDelayedHide(10000);
		}
		else if (buttons == okButton)
		{
			debug("okButton\n");
			menuState = MenuExec;
			os_timer_disarm(&scrollTmr);
			dispSetActiveMemBuf(SecondaryMemBuf);
			dispFillMem(0, TITLE_HEIGHT);
			dispSetActiveMemBuf(MainMemBuf);
			startScroll(scrollMenuOut);

			// do not hide menu while executing
			os_timer_disarm(&menuTmr);

			menuExecSelection(curMenu);
		}
		break;
	}
}

void ICACHE_FLASH_ATTR menu1execDone(int rc)
{
	const char *text = rc == OK ? menu1.items[menu1.selected].okText :
								  menu1.items[menu1.selected].failedText;
	if (text)
	{
		menuState = MenuExecDone;

		dispSetActiveMemBuf(SecondaryMemBuf);
		dispFillMem(0, TITLE_HEIGHT);
		drawStr_Latin(&arial10b, 0, 0, text, -1);
		startScroll(scrollStatus);
	}
	else
	{
		// no result text to show, new tweet will hide menu
		menuState = MenuHidden;
	}
}

void ICACHE_FLASH_ATTR menu2execDone(int rc)
{
	dispSetActiveMemBuf(MainMemBuf);
	dispFillMem(0, TITLE_HEIGHT);
	drawCurTweetUserName();
	dispUpdateTitle();
	menuState = MenuHidden;
}
