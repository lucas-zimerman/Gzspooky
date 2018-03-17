#ifndef _ASPOOKY
#define _ASPOOKY

enum
{
	OtherIWAD,
	warningIntro,
	storyIntro,
	waitEnter,
	titlemapMenu,
	ingameMenu,
	gameoverMenu
};

int GetSpookyMenu();
int SetSpookyMenu(int spookyMenu);
int GetSpookyMenuOpenRequest();
void TriggerSpookyMenuOpenRequest();
int GetSpookyMenuLock();
void SetSpookyMenuLock(int lock);

#endif