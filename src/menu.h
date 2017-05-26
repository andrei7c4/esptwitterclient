#ifndef SRC_MENU_H_
#define SRC_MENU_H_

#include "typedefs.h"

typedef enum{
	MenuHidden,
	MenuShow,
	MenuExec,
	MenuExecDone
}MenuState;
extern MenuState menuState;

typedef enum{
	NotPressed,
	Button1,
	Button2
}Button;

void menuStateMachine(Button buttons);
void menu1execDone(int rc);
void menu2execDone(int rc);



#endif /* SRC_MENU_H_ */
