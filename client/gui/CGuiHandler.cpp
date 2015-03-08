#include "StdInc.h"
#include "CGuiHandler.h"

#include "SDL_Extensions.h"
#include "CIntObject.h"
#include "../CGameInfo.h"
#include "CCursorHandler.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/CConfigHandler.h"
#include "../CMT.h"

extern std::queue<SDL_Event> events;
extern boost::mutex eventsM;

boost::thread_specific_ptr<bool> inGuiThread;

SObjectConstruction::SObjectConstruction( CIntObject *obj )
:myObj(obj)
{
	GH.createdObj.push_front(obj);
	GH.captureChildren = true;
}

SObjectConstruction::~SObjectConstruction()
{
	assert(GH.createdObj.size());
	assert(GH.createdObj.front() == myObj);
	GH.createdObj.pop_front();
	GH.captureChildren = GH.createdObj.size();
}

SSetCaptureState::SSetCaptureState(bool allow, ui8 actions)
{
	previousCapture = GH.captureChildren;
	GH.captureChildren = false;
	prevActions = GH.defActionsDef;
	GH.defActionsDef = actions;
}

SSetCaptureState::~SSetCaptureState()
{
	GH.captureChildren = previousCapture;
	GH.defActionsDef = prevActions;
}

static inline void
processList(const ui16 mask, const ui16 flag, std::list<CIntObject*> *lst, std::function<void (std::list<CIntObject*> *)> cb)
{
	if (mask & flag)
		cb(lst);
}

void CGuiHandler::processLists(const ui16 activityFlag, std::function<void (std::list<CIntObject*> *)> cb)
{
	processList(CIntObject::LCLICK,activityFlag,&lclickable,cb);
	processList(CIntObject::RCLICK,activityFlag,&rclickable,cb);
	processList(CIntObject::HOVER,activityFlag,&hoverable,cb);
	processList(CIntObject::MOVE,activityFlag,&motioninterested,cb);
	processList(CIntObject::KEYBOARD,activityFlag,&keyinterested,cb);
	processList(CIntObject::TIME,activityFlag,&timeinterested,cb);
	processList(CIntObject::WHEEL,activityFlag,&wheelInterested,cb);
	processList(CIntObject::DOUBLECLICK,activityFlag,&doubleClickInterested,cb);
}

void CGuiHandler::handleElementActivate(CIntObject * elem, ui16 activityFlag)
{
	processLists(activityFlag,[&](std::list<CIntObject*> * lst){
		lst->push_front(elem);
	});
	elem->active_m |= activityFlag;
}

void CGuiHandler::handleElementDeActivate(CIntObject * elem, ui16 activityFlag)
{
	processLists(activityFlag,[&](std::list<CIntObject*> * lst){
		auto hlp = std::find(lst->begin(),lst->end(),elem);
		assert(hlp != lst->end());
		lst->erase(hlp);
	});
	elem->active_m &= ~activityFlag;
}

void CGuiHandler::popInt( IShowActivatable *top )
{
	assert(listInt.front() == top);
	top->deactivate();
	listInt.pop_front();
	objsToBlit -= top;
	if(!listInt.empty())
		listInt.front()->activate();
	totalRedraw();
}

void CGuiHandler::popIntTotally( IShowActivatable *top )
{
	assert(listInt.front() == top);
	popInt(top);
	delete top;
	fakeMouseMove();
}

void CGuiHandler::pushInt( IShowActivatable *newInt )
{
	assert(newInt);
	assert(boost::range::find(listInt, newInt) == listInt.end()); // do not add same object twice

	//a new interface will be present, we'll need to use buffer surface (unless it's advmapint that will alter screenBuf on activate anyway)
	screenBuf = screen2;

	if(!listInt.empty())
		listInt.front()->deactivate();
	listInt.push_front(newInt);
	newInt->activate();
	objsToBlit.push_back(newInt);
	totalRedraw();
}

void CGuiHandler::popInts( int howMany )
{
	if(!howMany) return; //senseless but who knows...

	assert(listInt.size() >= howMany);
	listInt.front()->deactivate();
	for(int i=0; i < howMany; i++)
	{
		objsToBlit -= listInt.front();
		delete listInt.front();
		listInt.pop_front();
	}

	if(!listInt.empty())
	{
		listInt.front()->activate();
		totalRedraw();
	}
	fakeMouseMove();
}

IShowActivatable * CGuiHandler::topInt()
{
	if(listInt.empty())
		return nullptr;
	else
		return listInt.front();
}

void CGuiHandler::totalRedraw()
{
	for(auto & elem : objsToBlit)
		elem->showAll(screen2);
	blitAt(screen2,0,0,screen);
}

void CGuiHandler::updateTime()
{
	int ms = mainFPSmng->getElapsedMilliseconds();
	std::list<CIntObject*> hlp = timeinterested;
	for (auto & elem : hlp)
	{
		if(!vstd::contains(timeinterested,elem)) continue;
		(elem)->onTimer(ms);
	}
}

void CGuiHandler::handleEvents()
{
	while(true)
	{
		SDL_Event ev;
		boost::unique_lock<boost::mutex> lock(eventsM);
		if(events.empty())
		{
			return;
		}
		else
		{
			ev = events.front();
			events.pop();
		}
		handleEvent(&ev);
	}
}

void CGuiHandler::handleEvent(SDL_Event *sEvent)
{
	current = sEvent;
	bool prev;

	if (sEvent->type==SDL_KEYDOWN || sEvent->type==SDL_KEYUP)
	{
		SDL_KeyboardEvent key = sEvent->key;

		//translate numpad keys
		if(key.keysym.sym == SDLK_KP_ENTER)
		{
			key.keysym.sym = (SDLKey)SDLK_RETURN;
		}

		bool keysCaptured = false;
		for(auto i=keyinterested.begin(); i != keyinterested.end() && current; i++)
		{
			if((*i)->captureThisEvent(key))
			{
				keysCaptured = true;
				break;
			}
		}

		std::list<CIntObject*> miCopy = keyinterested;
		for(auto i=miCopy.begin(); i != miCopy.end() && current; i++)
			if(vstd::contains(keyinterested,*i) && (!keysCaptured || (*i)->captureThisEvent(key)))
				(**i).keyPressed(key);
	}
	else if(sEvent->type==SDL_MOUSEMOTION)
	{
		CCS->curh->cursorMove(sEvent->motion.x, sEvent->motion.y);
		handleMouseMotion(sEvent);
	}
	else if (sEvent->type==SDL_MOUSEBUTTONDOWN)
	{
		if(sEvent->button.button == SDL_BUTTON_LEFT)
		{

			if(lastClick == sEvent->motion  &&  (SDL_GetTicks() - lastClickTime) < 300)
			{
				std::list<CIntObject*> hlp = doubleClickInterested;
				for(auto i=hlp.begin(); i != hlp.end() && current; i++)
				{
					if(!vstd::contains(doubleClickInterested,*i)) continue;
					if (isItIn(&(*i)->pos,sEvent->motion.x,sEvent->motion.y))
					{
						(*i)->onDoubleClick();
					}
				}

			}

			lastClick = sEvent->motion;
			lastClickTime = SDL_GetTicks();

			std::list<CIntObject*> hlp = lclickable;
			for(auto i=hlp.begin(); i != hlp.end() && current; i++)
			{
				if(!vstd::contains(lclickable,*i)) continue;
				if (isItIn(&(*i)->pos,sEvent->motion.x,sEvent->motion.y))
				{
					prev = (*i)->pressedL;
					(*i)->pressedL = true;
					(*i)->clickLeft(true, prev);
				}
			}
		}
		else if (sEvent->button.button == SDL_BUTTON_RIGHT)
		{
			std::list<CIntObject*> hlp = rclickable;
			for(auto i=hlp.begin(); i != hlp.end() && current; i++)
			{
				if(!vstd::contains(rclickable,*i)) continue;
				if (isItIn(&(*i)->pos,sEvent->motion.x,sEvent->motion.y))
				{
					prev = (*i)->pressedR;
					(*i)->pressedR = true;
					(*i)->clickRight(true, prev);
				}
			}
		}
		else if(sEvent->button.button == SDL_BUTTON_WHEELDOWN || sEvent->button.button == SDL_BUTTON_WHEELUP)
		{
			std::list<CIntObject*> hlp = wheelInterested;
			for(auto i=hlp.begin(); i != hlp.end() && current; i++)
			{
				if(!vstd::contains(wheelInterested,*i)) continue;
				(*i)->wheelScrolled(sEvent->button.button == SDL_BUTTON_WHEELDOWN, isItIn(&(*i)->pos,sEvent->motion.x,sEvent->motion.y));
			}
		}
	}
	else if ((sEvent->type==SDL_MOUSEBUTTONUP) && (sEvent->button.button == SDL_BUTTON_LEFT))
	{
		std::list<CIntObject*> hlp = lclickable;
		for(auto i=hlp.begin(); i != hlp.end() && current; i++)
		{
			if(!vstd::contains(lclickable,*i)) continue;
			prev = (*i)->pressedL;
			(*i)->pressedL = false;
			if (isItIn(&(*i)->pos,sEvent->motion.x,sEvent->motion.y))
			{
				(*i)->clickLeft(false, prev);
			}
			else
				(*i)->clickLeft(boost::logic::indeterminate, prev);
		}
	}
	else if ((sEvent->type==SDL_MOUSEBUTTONUP) && (sEvent->button.button == SDL_BUTTON_RIGHT))
	{
		std::list<CIntObject*> hlp = rclickable;
		for(auto i=hlp.begin(); i != hlp.end() && current; i++)
		{
			if(!vstd::contains(rclickable,*i)) continue;
			prev = (*i)->pressedR;
			(*i)->pressedR = false;
			if (isItIn(&(*i)->pos,sEvent->motion.x,sEvent->motion.y))
			{
				(*i)->clickRight(false, prev);
			}
			else
				(*i)->clickRight(boost::logic::indeterminate, prev);
		}
	}
	current = nullptr;

} //event end

void CGuiHandler::handleMouseMotion(SDL_Event *sEvent)
{
	//sending active, hovered hoverable objects hover() call
	std::vector<CIntObject*> hlp;
	for(auto & elem : hoverable)
	{
		if (isItIn(&(elem)->pos,sEvent->motion.x,sEvent->motion.y))
		{
			if (!(elem)->hovered)
				hlp.push_back((elem));
		}
		else if ((elem)->hovered)
		{
			(elem)->hover(false);
			(elem)->hovered = false;
		}
	}
	for(auto & elem : hlp)
	{
		elem->hover(true);
		elem->hovered = true;
	}

	handleMoveInterested(sEvent->motion);
}

void CGuiHandler::simpleRedraw()
{
	//update only top interface a11nd draw background
	if(objsToBlit.size() > 1)
		blitAt(screen2,0,0,screen); //blit background
	objsToBlit.back()->show(screen); //blit active interface/window
}

void CGuiHandler::handleMoveInterested( const SDL_MouseMotionEvent & motion )
{
	//sending active, MotionInterested objects mouseMoved() call
	std::list<CIntObject*> miCopy = motioninterested;
	for(auto & elem : miCopy)
	{
		if ((elem)->strongInterest || isItIn(&(elem)->pos, motion.x, motion.y))
		{
			(elem)->mouseMoved(motion);
		}
	}
}

void CGuiHandler::fakeMouseMove()
{
	SDL_Event evnt;

	SDL_MouseMotionEvent sme = {SDL_MOUSEMOTION, 0, 0, 0, 0, 0, 0};
	int x, y;
	sme.state = SDL_GetMouseState(&x, &y);
	sme.x = x;
	sme.y = y;

	evnt.motion = sme;
	current = &evnt;
	handleMouseMotion(&evnt);
}

void CGuiHandler::run()
{
	setThreadName("CGuiHandler::run");
	inGuiThread.reset(new bool(true));
	try
	{
		if(settings["video"]["fullscreen"].Bool())
			CCS->curh->centerCursor();

		mainFPSmng->init(); // resets internal clock, needed for FPS manager
		while(!terminate)
		{
			if(curInt)
				curInt->update(); // calls a update and drawing process of the loaded game interface object at the moment

			mainFPSmng->framerateDelay(); // holds a constant FPS
		}
	}
	catch(const std::exception & e)
	{
        logGlobal->errorStream() << "Error: " << e.what();
		exit(EXIT_FAILURE);
	}
}

CGuiHandler::CGuiHandler()
:lastClick(-500, -500)
{
	curInt = nullptr;
	current = nullptr;
	terminate = false;
	statusbar = nullptr;

	// Creates the FPS manager and sets the framerate to 48 which is doubled the value of the original Heroes 3 FPS rate
	mainFPSmng = new CFramerateManager(48);
	//do not init CFramerateManager here --AVS
}

CGuiHandler::~CGuiHandler()
{
	delete mainFPSmng;
}

void CGuiHandler::breakEventHandling()
{
	current = nullptr;
}

void CGuiHandler::drawFPSCounter()
{
	const static SDL_Color yellow = {255, 255, 0, 0};
	static SDL_Rect overlay = { 0, 0, 64, 32};
	Uint32 black = SDL_MapRGB(screen->format, 10, 10, 10);
	SDL_FillRect(screen, &overlay, black);
	std::string fps = boost::lexical_cast<std::string>(mainFPSmng->fps);
	graphics->fonts[FONT_BIG]->renderTextLeft(screen, fps, yellow, Point(10, 10));
}

SDLKey CGuiHandler::arrowToNum( SDLKey key )
{
	switch(key)
	{
	case SDLK_DOWN:
		return SDLK_KP2;
	case SDLK_UP:
		return SDLK_KP8;
	case SDLK_LEFT:
		return SDLK_KP4;
	case SDLK_RIGHT:
		return SDLK_KP6;
	default:
		assert(0);
	}
	throw std::runtime_error("Wrong key!");
}

SDLKey CGuiHandler::numToDigit( SDLKey key )
{
	if(key >= SDLK_KP0 && key <= SDLK_KP9)
		return SDLKey(key - SDLK_KP0 + SDLK_0);

#define REMOVE_KP(keyName) case SDLK_KP_ ## keyName : return SDLK_ ## keyName;
	switch(key)
	{
		REMOVE_KP(PERIOD)
			REMOVE_KP(MINUS)
			REMOVE_KP(PLUS)
			REMOVE_KP(EQUALS)

	case SDLK_KP_MULTIPLY:
		return SDLK_ASTERISK;
	case SDLK_KP_DIVIDE:
		return SDLK_SLASH;
	case SDLK_KP_ENTER:
		return SDLK_RETURN;
	default:
		return SDLK_UNKNOWN;
	}
#undef REMOVE_KP
}

bool CGuiHandler::isNumKey( SDLKey key, bool number )
{
	if(number)
		return key >= SDLK_KP0 && key <= SDLK_KP9;
	else
		return key >= SDLK_KP0 && key <= SDLK_KP_EQUALS;
}

bool CGuiHandler::isArrowKey( SDLKey key )
{
	return key >= SDLK_UP && key <= SDLK_LEFT;
}

bool CGuiHandler::amIGuiThread()
{
	return inGuiThread.get() && *inGuiThread;
}

void CGuiHandler::pushSDLEvent(int type, int usercode)
{
	SDL_Event event;
	event.type = type;
	event.user.code = usercode;	// not necessarily used
	SDL_PushEvent(&event);
}

CFramerateManager::CFramerateManager(int rate)
{
	this->rate = rate;
	this->rateticks = (1000.0 / rate);
	this->fps = 0;
}

void CFramerateManager::init()
{
	this->lastticks = SDL_GetTicks();
}

void CFramerateManager::framerateDelay()
{
	ui32 currentTicks = SDL_GetTicks();
	timeElapsed = currentTicks - lastticks;

	// FPS is higher than it should be, then wait some time
	if (timeElapsed < rateticks)
	{
		SDL_Delay(ceil(this->rateticks) - timeElapsed);
	}
	currentTicks = SDL_GetTicks();

	fps = ceil(1000.0 / timeElapsed);

	// recalculate timeElapsed for external calls via getElapsed()
	// limit it to 1000 ms to avoid breaking animation in case of huge lag (e.g. triggered breakpoint)
	timeElapsed = std::min<ui32>(currentTicks - lastticks, 1000);
	lastticks = SDL_GetTicks();
}
