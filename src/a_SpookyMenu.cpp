enum
{	
	MenuNormal,
	IngameMenu
};

int SpookyMenu = MenuNormal;
int SpookyMenuOpenRequest = false;
int SpookyMenuLock = false;


int GetSpookyMenu() {
	return SpookyMenu;
}


//Set a fixed data to informa what menu is going to be rendered
int SetSpookyMenu(int spookyMenu) {
	int success = false;
	if (spookyMenu) {
		//only signed values
		SpookyMenu = spookyMenu;
		success = true;
	}
	return success;
}

int GetSpookyMenuOpenRequest()
{
	int tmp = SpookyMenuOpenRequest;
	SpookyMenuOpenRequest = false;
	return tmp;
}

void TriggerSpookyMenuOpenRequest() {
	SpookyMenuOpenRequest = true;
}

void SetSpookyMenuLock(int lock) {
	SpookyMenuLock = lock == true;
}

int GetSpookyMenuLock() {
	return SpookyMenuLock;
}