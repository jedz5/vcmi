#include "StdInc.h"
#include "COutpostInterface.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "CAdvmapInterface.h"
#include "CHeroWindow.h"
#include "CTradeWindow.h"
#include "GUIClasses.h"
#include "QuickRecruitmentWindow.h"

#include "../CBitmapHandler.h"
#include "../CGameInfo.h"
#include "../CMessage.h"
#include "../CMusicHandler.h"
#include "../CPlayerInterface.h"
#include "../Graphics.h"
#include "../gui/CGuiHandler.h"
#include "../gui/SDL_Extensions.h"
#include "../windows/InfoWindows.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/CComponent.h"

#include "../../CCallback.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CGeneralTextHandler.h"
#include "../../lib/CModHandler.h"
#include "../../lib/CTownHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapping/CCampaignHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
COutpostInterface::COutpostInterface(const CGOutpost * oop, const CGHeroInstance* hero, const CGTownInstance * to) :
	CStatusbarWindow(PLAYER_COLORED | BORDERED),
	visitingHero(hero)
{
	OBJECT_CONSTRUCTION_CAPTURING(255 - DISPOSE);
	op = const_cast<CGOutpost*>(oop);
	op->updateDaysCost(LOCPLINT->cb.get());
	addUsedEvents(KEYBOARD);
	town = to;
	//builds = std::make_shared<CCastleBuildings>(town);
	background = std::make_shared<CPicture>(town->town->clientInfo.townBackground);
	panel = std::make_shared<CPicture>("TOWNSCRN", 0, background->pos.h);
	panel->colorize(LOCPLINT->playerID);
	pos.w = panel->pos.w;
	pos.h = background->pos.h + panel->pos.h;
	center();
	updateShadow();
	//type will be set during initialization
	garr = std::make_shared<COutpostInt>(305, 387, 4, Point(0, 96), op, visitingHero); //std::const_pointer_cast<CArmedInstance *>(army1)
	garr->createSlots();
	garr->type |= REDRAW_PARENT;
	heroes = std::make_shared<HeroSlots>(town, Point(241, 387), Point(241, 483), garr, true);
	title = std::make_shared<CLabel>(85, 387, FONT_MEDIUM, TOPLEFT, Colors::WHITE, town->name);
	income = std::make_shared<CLabel>(195, 443, FONT_SMALL, CENTER);
	icon = std::make_shared<CAnimImage>("ITPT", 0, 0, 15, 387);

	exit = std::make_shared<CButton>(Point(744, 544), "TSBTNS", CButton::tooltip(CGI->generaltexth->tcommands[8]), [&]() {close(); }, SDLK_RETURN);
	exit->assignedKeys.insert(SDLK_ESCAPE);
	exit->setImageOrder(4, 5, 6, 7);

	auto split = std::make_shared<CButton>(Point(744, 382), "TSBTNS", CButton::tooltip(CGI->generaltexth->tcommands[3]), [&]()
	{
		if (garr->getSelection())
			garr->splitClick();
		//heroes->splitClicked();
	});
	garr->addSplitBtn(split);
	Rect barRect(9, 182, 732, 18);
	auto statusbarBackground = std::make_shared<CPicture>(*(panel.get()), barRect, 9, 555, false);
	statusbar = CGStatusBar::create(statusbarBackground);
	resdatabar = std::make_shared<CResDataBar>("ARESBAR", 3, 575, 32, 2, 85, 85);

	townlist = std::make_shared<CTownList2>(3, Point(744, 414), "IAM014", "IAM015");
	//if (from)
	//	townlist->select(from);

	townlist->select(town); //this will scroll list to select current town
	townlist->onSelect = std::bind(&COutpostInterface::townChange, this);

	recreateIcons();
	CCS->musich->playMusic(town->town->clientInfo.musicTheme, true);
}

COutpostInterface::~COutpostInterface()
{
	//if (LOCPLINT->castleInt == this)
	//	LOCPLINT->castleInt = nullptr;
	visitingHero = nullptr;
}

void COutpostInterface::updateGarrisons()
{
	garr->recreateSlots();
	//garr2->recreateSlots();
}

void COutpostInterface::close()
{
	if(visitingHero)
		adventureInt->select(visitingHero);
	CWindowObject::close();
}


void COutpostInterface::townChange()
{
	//TODO: do not recreate window
	const CGTownInstance * dest = LOCPLINT->towns[townlist->getSelectedIndex()];
	const CGTownInstance * town = this->town;// "this" is going to be deleted
	if (dest == town)
		return;
	close();
	GH.pushIntT<COutpostInterface>(op, visitingHero, dest);
}


void COutpostInterface::recreateIcons()
{
	OBJECT_CONSTRUCTION_CUSTOM_CAPTURING(255 - DISPOSE);
	size_t iconIndex = town->town->clientInfo.icons[town->hasFort()][town->builded >= CGI->modh->settings.MAX_BUILDING_PER_TURN];

	icon->setFrame(iconIndex);
	TResources townIncome = town->dailyIncome();
	income->setText(boost::lexical_cast<std::string>(townIncome[Res::GOLD]));

	//hall = std::make_shared<CTownInfo>(80, 413, town, true);
	//fort = std::make_shared<CTownInfo>(122, 413, town, false);

	fastArmyPurhase = std::make_shared<CButton>(Point(122, 413), "itmcl.def", CButton::tooltip(), [&]() {GH.pushIntT<QuickRecruitmentWindow>(town, background->pos,op);});
	fastArmyPurhase->setImageOrder(town->fortLevel() - 1, town->fortLevel() - 1, town->fortLevel() - 1, town->fortLevel() - 1);
	fastArmyPurhase->setAnimateLonelyFrame(true);

	creainfo.clear();

	for (size_t i = 0; i < 4; i++)
		creainfo.push_back(std::make_shared<CCreaInfo>(Point(14 + 55 * i, 459), town, i));

	for (size_t i = 0; i < 4; i++)
		creainfo.push_back(std::make_shared<CCreaInfo>(Point(14 + 55 * i, 507), town, i + 4));
}

void COutpostInterface::keyPressed(const SDL_KeyboardEvent & key)
{
	if (key.state != SDL_PRESSED) return;

	switch (key.keysym.sym)
	{
	case SDLK_UP:
		townlist->selectPrev();
		break;
	case SDLK_DOWN:
		townlist->selectNext();
		break;
	case SDLK_SPACE:
		heroes->swapArmies();
		break;
	default:
		break;
	}
}