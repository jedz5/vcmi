/*
 * CGarrisonInt.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CGarrisonInt.h"

#include "../gui/CGuiHandler.h"

#include "../CGameInfo.h"
#include "../CPlayerInterface.h"
#include "../widgets/Buttons.h"
#include "../widgets/TextControls.h"
#include "../windows/CCreatureWindow.h"
#include "../windows/GUIClasses.h"

#include "../../CCallback.h"

#include "../../lib/CGeneralTextHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include "../../lib/CGameState.h"

#ifdef VCMI_MAC
#define SDL_SCANCODE_LCTRL SDL_SCANCODE_LGUI
#endif

void CGarrisonSlot::setHighlight(bool on)
{
	if (on)
		selectionImage->enable(); //show
	else
		selectionImage->disable(); //hide
}

void CGarrisonSlot::hover (bool on)
{
	////Hoverable::hover(on);
	if(on)
	{
		std::string temp;
		if(creature)
		{
			if(owner->getSelection())
			{
				if(owner->getSelection() == this)
				{
					temp = CGI->generaltexth->tcommands[4]; //View %s
					boost::algorithm::replace_first(temp,"%s",creature->nameSing);
				}
				else if (owner->getSelection()->creature == creature)
				{
					temp = CGI->generaltexth->tcommands[2]; //Combine %s armies
					boost::algorithm::replace_first(temp,"%s",creature->nameSing);
				}
				else if (owner->getSelection()->creature)
				{
					temp = CGI->generaltexth->tcommands[7]; //Exchange %s with %s
					boost::algorithm::replace_first(temp,"%s",owner->getSelection()->creature->nameSing);
					boost::algorithm::replace_first(temp,"%s",creature->nameSing);
				}
				else
				{
					logGlobal->warn("Warning - shouldn't be - highlighted void slot %d", owner->getSelection()->ID.getNum());
					logGlobal->warn("Highlighted set to nullptr");
					owner->selectSlot(nullptr);
				}
			}
			else
			{
				if(upg == EGarrisonType::UP)
				{
					temp = CGI->generaltexth->tcommands[12]; //Select %s (in garrison)
				}
				else if(owner->armedObjs[0] && (owner->armedObjs[0]->ID == Obj::TOWN || owner->armedObjs[0]->ID == Obj::HERO))
				{
					temp = CGI->generaltexth->tcommands[32]; //Select %s (visiting)
				}
				else
				{
					temp = CGI->generaltexth->allTexts[481]; //Select %s
				}
				boost::algorithm::replace_first(temp,"%s",creature->nameSing);
			}
		}
		else
		{
			if(owner->getSelection())
			{
				const CArmedInstance *highl = owner->getSelection()->getObj();
				if(  highl->needsLastStack()		//we are moving stack from hero's
				  && highl->stacksCount() == 1	//it's only stack
				  && owner->getSelection()->upg != upg	//we're moving it to the other garrison
				  )
				{
					temp = CGI->generaltexth->tcommands[5]; //Cannot move last army to garrison
				}
				else
				{
					temp = CGI->generaltexth->tcommands[6]; //Move %s
					boost::algorithm::replace_first(temp,"%s",owner->getSelection()->creature->nameSing);
				}
			}
			else
			{
				temp = CGI->generaltexth->tcommands[11]; //Empty
			}
		}
		GH.statusbar->setText(temp);
	}
	else
	{
		GH.statusbar->clear();
	}
}

const CArmedInstance * CGarrisonSlot::getObj() const
{
	return 	owner->armedObjs[upg];
}

/// @return Whether the unit in the slot belongs to the current player.
bool CGarrisonSlot::our() const
{
	return owner->owned[upg];
}

/// @return Whether the unit in the slot belongs to an ally but not to the current player.
bool CGarrisonSlot::ally() const
{
	if(!getObj())
		return false;

	return PlayerRelations::ALLIES == LOCPLINT->cb->getPlayerRelations(LOCPLINT->playerID, getObj()->tempOwner);
}

/// The creature slot has been clicked twice, therefore the creature info should be shown
/// @return Whether the view should be refreshed
bool CGarrisonSlot::viewInfo()
{
	UpgradeInfo pom;
	LOCPLINT->cb->getUpgradeInfo(getObj(), ID, pom);

	bool canUpgrade = getObj()->tempOwner == LOCPLINT->playerID && pom.oldID>=0; //upgrade is possible
	bool canDismiss = getObj()->tempOwner == LOCPLINT->playerID && (getObj()->stacksCount()>1  || !getObj()->needsLastStack());
	std::function<void(CreatureID)> upgr = nullptr;
	std::function<void()> dism = nullptr;
	if(canUpgrade) upgr = [=] (CreatureID newID) { LOCPLINT->cb->upgradeCreature(getObj(), ID, newID); };
	if(canDismiss) dism = [=](){ LOCPLINT->cb->dismissCreature(getObj(), ID); };

	owner->selectSlot(nullptr);
	owner->setSplittingMode(false);

	for(auto & elem : owner->splitButtons)
		elem->block(true);

	redraw();
	GH.pushIntT<CStackWindow>(myStack, dism, pom, upgr);
	return true;
}

/// The selection is empty, therefore the creature should be moved
/// @return Whether the view should be refreshed
bool CGarrisonSlot::highlightOrDropArtifact()
{
	bool artSelected = false;
	if (CWindowWithArtifacts* chw = dynamic_cast<CWindowWithArtifacts*>(GH.topInt().get())) //dirty solution
	{
		const std::shared_ptr<CArtifactsOfHero::SCommonPart> commonInfo = chw->getCommonPart();
		const CArtifactInstance * art = nullptr;
		if(commonInfo)
			art = commonInfo->src.art;

		if(art)
		{
			const CGHeroInstance *srcHero = commonInfo->src.AOH->getHero();
			artSelected = true;
			if (myStack) // try dropping the artifact only if the slot isn't empty
			{
				ArtifactLocation src(srcHero, commonInfo->src.slotID);
				ArtifactLocation dst(myStack, ArtifactPosition::CREATURE_SLOT);
				if (art->canBePutAt(dst, true))
				{	//equip clicked stack
					if(dst.getArt())
					{
						//creature can wear only one active artifact
						//if we are placing a new one, the old one will be returned to the hero's backpack
						LOCPLINT->cb->swapArtifacts(dst, ArtifactLocation(srcHero, dst.getArt()->firstBackpackSlot(srcHero)));
					}
					LOCPLINT->cb->swapArtifacts(src, dst);
				}
			}
		}
	}
	if (!artSelected && creature)
	{
		owner->selectSlot(this);
		if(creature)
		{
			for(auto & elem : owner->splitButtons)
				elem->block(!our());
		}
	}
	redraw();
	return true;
}

/// The creature is only being partially moved
/// @return Whether the view should be refreshed
bool CGarrisonSlot::split()
{
	const CGarrisonSlot * selection = owner->getSelection();
	owner->p2 = ID;   // store the second stack pos
	owner->pb = upg;  // store the second stack owner (up or down army)
	owner->setSplittingMode(false);

	int minLeft=0, minRight=0;

	if(upg != selection->upg) // not splitting within same army
	{
		if(selection->getObj()->stacksCount() == 1 // we're splitting away the last stack
			&& selection->getObj()->needsLastStack() )
		{
			minLeft = 1;
		}
		// destination army can't be emptied, unless we're rebalancing two stacks of same creature
		if(getObj()->stacksCount() == 1
			&& selection->creature == creature
			&& getObj()->needsLastStack() )
		{
			minRight = 1;
		}
	}

	int countLeft = selection->myStack ? selection->myStack->count : 0;
	int countRight = myStack ? myStack->count : 0;

	GH.pushIntT<CSplitWindow>(selection->creature, std::bind(&CGarrisonInt::splitStacks, owner, _1, _2),
		minLeft, minRight, countLeft, countRight);
	return true;
}

/// If certain creates cannot be moved, the selection should change
/// Force reselection in these cases
///     * When attempting to take creatures from ally
///     * When attempting to swap creatures with an ally
///     * When attempting to take unremovable units
/// @return Whether reselection must be done
bool CGarrisonSlot::mustForceReselection() const
{
	const CGarrisonSlot * selection = owner->getSelection();
	bool withAlly = selection->our() ^ our();
	if (!creature || !selection->creature)
		return false;
	// Attempt to take creatures from ally (select theirs first)
	if (!selection->our())
		return true;
	// Attempt to swap creatures with ally (select ours first)
	if (selection->creature != creature && withAlly)
		return true;
	if (!owner->removableUnits)
	{
		if (selection->upg == EGarrisonType::UP)
			return true;
		else
			return creature || upg == EGarrisonType::UP;
	}
	return false;
}

void CGarrisonSlot::clickRight(tribool down, bool previousState)
{
	if(creature && down)
	{
		GH.pushIntT<CStackWindow>(myStack, true);
	}
}

void CGarrisonSlot::clickLeft(tribool down, bool previousState)
{
	if(down)
	{
		bool refr = false;
		const CGarrisonSlot * selection = owner->getSelection();
		if(!selection)
		{
			refr = highlightOrDropArtifact();
			handleSplittingShortcuts();
		}
		else if(selection == this)
			refr = viewInfo();
		// Re-highlight if troops aren't removable or not ours.
		else if (mustForceReselection())
		{
			if(creature)
				owner->selectSlot(this);
			redraw();
			refr = true;
		}
		else
		{
			const CArmedInstance * selectedObj = owner->armedObjs[selection->upg];
			bool lastHeroStackSelected = false;
			if(selectedObj->stacksCount() == 1
				&& owner->getSelection()->upg != upg
				&& dynamic_cast<const CGHeroInstance*>(selectedObj))
			{
				lastHeroStackSelected = true;
			}

			if((owner->getSplittingMode() || LOCPLINT->shiftPressed()) // split window
				&& (!creature || creature == selection->creature ))
			{
				refr = split();
			}
			else if(!creature && lastHeroStackSelected) // split all except last creature
				LOCPLINT->cb->splitStack(selectedObj, owner->armedObjs[upg], selection->ID, ID, selection->myStack->count - 1);
			else if(creature != selection->creature) // swap
				LOCPLINT->cb->swapCreatures(owner->armedObjs[upg], selectedObj, ID, selection->ID);
			else if(lastHeroStackSelected) // merge last stack to other hero stack
				refr = split();
			else // merge
				LOCPLINT->cb->mergeStacks(selectedObj, owner->armedObjs[upg], selection->ID, ID);
		}
		if(refr)
		{
			// Refresh Statusbar
			hover(false);
			hover(true);
		}
	}
}

void CGarrisonSlot::update()
{
	if(getObj() != nullptr)
	{
		addUsedEvents(LCLICK | RCLICK | HOVER);
		myStack = getObj()->getStackPtr(ID);
		creature = myStack ? myStack->type : nullptr;
	}
	else
	{
		removeUsedEvents(LCLICK | RCLICK | HOVER);
		myStack = nullptr;
		creature = nullptr;
	}

	if(creature)
	{
		creatureImage->enable();
		creatureImage->setFrame(creature->iconIndex);

		stackCount->enable();
		stackCount->setText(boost::lexical_cast<std::string>(myStack->count));
	}
	else
	{
		creatureImage->disable();
		stackCount->disable();
	}
}

CGarrisonSlot::CGarrisonSlot(CGarrisonInt * Owner, int x, int y, SlotID IID, CGarrisonSlot::EGarrisonType Upg, const CStackInstance * Creature)
	: ID(IID),
	owner(Owner),
	myStack(Creature),
	creature(nullptr),
	upg(Upg)
{
	OBJECT_CONSTRUCTION_CAPTURING(255 - DISPOSE);

	pos.x += x;
	pos.y += y;

	std::string imgName = owner->smallIcons ? "cprsmall" : "TWCRPORT";

	creatureImage =  std::make_shared<CAnimImage>(imgName, 0);
	creatureImage->disable();

	selectionImage = std::make_shared<CAnimImage>(imgName, 1);
	selectionImage->disable();

	if(Owner->smallIcons)
	{
		pos.w = 32;
		pos.h = 32;
	}
	else
	{
		pos.w = 58;
		pos.h = 64;
	}

	stackCount = std::make_shared<CLabel>(pos.w, pos.h, owner->smallIcons ? FONT_TINY : FONT_MEDIUM, BOTTOMRIGHT, Colors::WHITE);
	//update();
}

void CGarrisonSlot::splitIntoParts(CGarrisonSlot::EGarrisonType type, int amount, int maxOfSplittedSlots)
{
	owner->pb = type;
	for(CGarrisonSlot * slot : owner->getEmptySlots(type))
	{
		owner->p2 = slot->ID;
		owner->splitStacks(1, amount);
		maxOfSplittedSlots--;
		if(!maxOfSplittedSlots || owner->getSelection()->myStack->count <= 1)
			break;
	}
}

void CGarrisonSlot::handleSplittingShortcuts()
{
	const Uint8 * state = SDL_GetKeyboardState(NULL);
	if(owner->getSelection() && owner->getEmptySlots(owner->getSelection()->upg).size() && owner->getSelection()->myStack->count > 1)
	{
		if(state[SDL_SCANCODE_LCTRL] && state[SDL_SCANCODE_LSHIFT])
			splitIntoParts(owner->getSelection()->upg, 1, 7);
		else if(state[SDL_SCANCODE_LCTRL])
			splitIntoParts(owner->getSelection()->upg, 1, 1);
		else if(state[SDL_SCANCODE_LSHIFT])
			splitIntoParts(owner->getSelection()->upg, owner->getSelection()->myStack->count/2 , 1);
		else
			return;
		owner->selectSlot(nullptr);
	}
}

void CGarrisonInt::addSplitBtn(std::shared_ptr<CButton> button)
{
	addChild(button.get());
	button->recActions &= ~DISPOSE;
	splitButtons.push_back(button);
	button->block(getSelection() == nullptr);
}

void CGarrisonInt::createSlots()
{
	OBJECT_CONSTRUCTION_CAPTURING(255 - DISPOSE);
	int distance = interx + (smallIcons ? 32 : 58);
	for(int i=0; i<2; i++)
	{
		std::vector<std::shared_ptr<CGarrisonSlot>> garrisonSlots;
		garrisonSlots.resize(7);
		if(armedObjs[i])
		{
			for(auto & elem : armedObjs[i]->Slots())
			{
				auto gs = std::make_shared<CGarrisonSlot>(this, i*garOffset.x + (elem.first.getNum()*distance), i*garOffset.y, elem.first, static_cast<CGarrisonSlot::EGarrisonType>(i), elem.second);
				gs->update();
				garrisonSlots[elem.first.getNum()] = gs;
			}
		}
		for(int j=0; j<7; j++)
		{
			if (!garrisonSlots[j]) {
				auto gs = std::make_shared<CGarrisonSlot>(this, i*garOffset.x + (j*distance), i*garOffset.y, SlotID(j), static_cast<CGarrisonSlot::EGarrisonType>(i), nullptr);
				gs->update();
				garrisonSlots[j] = gs;
			}
				if(twoRows && j>=4)
			{
				garrisonSlots[j]->moveBy(Point(-126, 37));
			}
		}
		vstd::concatenate(availableSlots, garrisonSlots);
	}
}

void CGarrisonInt::recreateSlots()
{
	selectSlot(nullptr);
	setSplittingMode(false);

	for(auto & elem : splitButtons)
		elem->block(true);

	for(auto slot : availableSlots)
		slot->update();
}

void CGarrisonInt::splitClick()
{
	if(!getSelection())
		return;
	setSplittingMode(!getSplittingMode());
	redraw();
}
void CGarrisonInt::splitStacks(int, int amountRight)
{
	LOCPLINT->cb->splitStack(armedObjs[getSelection()->upg], armedObjs[pb], getSelection()->ID, p2, amountRight);
}

CGarrisonInt::CGarrisonInt(int x, int y, int inx, const Point & garsOffset,
		const CArmedInstance * s1, const CArmedInstance * s2,
		bool _removableUnits, bool smallImgs, bool _twoRows)
	: highlighted(nullptr),
	inSplittingMode(false),
	interx(inx),
	garOffset(garsOffset),
	pb(false),
	smallIcons(smallImgs),
	removableUnits(_removableUnits),
	twoRows(_twoRows)
{
	
	setArmy(s1, false);
	setArmy(s2, true);
	pos.x += x;
	pos.y += y;
}

const CGarrisonSlot * CGarrisonInt::getSelection()
{
	return highlighted;
}

void CGarrisonInt::selectSlot(CGarrisonSlot *slot)
{
	if(slot != highlighted)
	{
		if(highlighted)
			highlighted->setHighlight(false);

		highlighted = slot;
		for(auto button : splitButtons)
			button->block(highlighted == nullptr || !slot->our());

		if(highlighted)
			highlighted->setHighlight(true);
	}
}

void CGarrisonInt::setSplittingMode(bool on)
{
	assert(on == false || highlighted != nullptr); //can't be in splitting mode without selection

	if(inSplittingMode || on)
	{
		auto sl = getSelection();
		for(auto slot : availableSlots)
		{
			if(slot.get() != sl)
				slot->setHighlight(  on 
					&& (slot->our() || slot->ally()) 
					&& (slot->creature == nullptr || (slot->creature == sl->creature && slot->myStack->daysCost == sl->myStack->daysCost)) 
					&& ((sl->myStack->daysCost == 0 && slot->upg) || (slot->upg == sl->upg)));
		}
		inSplittingMode = on;
	}
}

bool CGarrisonInt::getSplittingMode()
{
	return inSplittingMode;
}

std::vector<CGarrisonSlot *> CGarrisonInt::getEmptySlots(CGarrisonSlot::EGarrisonType type)
{
	std::vector<CGarrisonSlot *> emptySlots;
	for(auto slot : availableSlots)
	{
		if(type == slot->upg && ((slot->our() || slot->ally()) && slot->creature == nullptr))
			emptySlots.push_back(slot.get());
	}
	return emptySlots;
}

void CGarrisonInt::setArmy(const CArmedInstance * army, bool bottomGarrison)
{
	owned[bottomGarrison] =  army ? (army->tempOwner == LOCPLINT->playerID || army->tempOwner == PlayerColor::UNFLAGGABLE) : false;
	armedObjs[bottomGarrison] = army;
}

COutpostInt::COutpostInt(int x, int y, int inx,
	const Point & garsOffset,
	const CGOutpost * s1, const CGHeroInstance * s2,
	bool _removableUnits,
	bool smallImgs,
	bool _twoRows):CGarrisonInt(x,y,inx,garsOffset,reinterpret_cast<const CArmedInstance*>(s1), reinterpret_cast<const CArmedInstance*>(s2),_removableUnits,smallImgs,_twoRows){
	
}
	//bool _twoRows = false):CGarrisonInt(x, y, inx, garsOffset, s1, s2, _removableUnits, smallImgs, _twoRows) {}

void COutpostInt::splitClick() {
	if (!getSelection())
		return;
	setSplittingMode(!getSplittingMode());
	redraw();
}
void COutpostInt::setSplittingMode(bool on)
{
	assert(on == false || highlighted != nullptr); //can't be in splitting mode without selection

	if (inSplittingMode || on)
	{
		auto sl = getSelection();
		for (auto slot : availableSlots)
		{
			if (sl->upg && !slot->upg)
				continue;
			if (slot.get() != sl)
				slot->setHighlight((on && (slot->our() || slot->ally()) && ((slot->creature == nullptr && (sl->myStack->daysCost == 0 || sl->upg == slot->upg)) || (slot->creature == sl->creature && slot->myStack->daysCost == sl->myStack->daysCost))));
		}
		inSplittingMode = on;
	}
}
void COutpostInt::createSlots()
{
	OBJECT_CONSTRUCTION_CAPTURING(255 - DISPOSE);
	int distance = interx + (smallIcons ? 32 : 58);
	for (int i = 0; i < 2; i++)
	{
		std::vector<std::shared_ptr<CGarrisonSlot>> garrisonSlots;
		garrisonSlots.resize(7);
		if (armedObjs[i])
		{
			for (auto & elem : armedObjs[i]->Slots())
			{
				auto os = std::make_shared<COutpostSlot>(this, i*garOffset.x + (elem.first.getNum()*distance), i*garOffset.y, elem.first, static_cast<CGarrisonSlot::EGarrisonType>(i), elem.second);
				os->update();
				garrisonSlots[elem.first.getNum()] = os;
			}
		}
		for (int j = 0; j < 7; j++)
		{
			if (!garrisonSlots[j]) {
				auto os = std::make_shared<COutpostSlot>(this, i*garOffset.x + (j*distance), i*garOffset.y, SlotID(j), static_cast<CGarrisonSlot::EGarrisonType>(i), nullptr);
				os->update();
				garrisonSlots[j] = os;
			}
			if (twoRows && j >= 4)
			{
				garrisonSlots[j]->moveBy(Point(-126, 37));
			}
		}
		vstd::concatenate(availableSlots, garrisonSlots);
	}
}

COutpostSlot::COutpostSlot(COutpostInt * Owner, int x, int y, SlotID IID, CGarrisonSlot::EGarrisonType Upg, const CStackInstance * Creature)
	: CGarrisonSlot(Owner,x,y,IID,Upg,Creature)
{
	OBJECT_CONSTRUCTION_CAPTURING(255 - DISPOSE);

	stackDaysCost = std::make_shared<CLabel>(pos.w, pos.h, owner->smallIcons ? FONT_TINY : FONT_MEDIUM, TOPLEFT, Colors::GREEN);
	
}
void COutpostSlot::update()
{
	CGarrisonSlot::update();
	if (creature)
	{
		stackDaysCost->enable();
		stackDaysCost->setText(boost::lexical_cast<std::string>(myStack->daysCost));
	}
	else
	{
		stackDaysCost->disable();
	}
}
bool COutpostSlot::mustForceReselection() const
{
	const CGarrisonSlot * selection = owner->getSelection();
	bool withAlly = selection->our() ^ our();
	if (selection->upg && !upg)
		return true;
	if (!creature || !selection->creature)
		return false;
	// Attempt to take creatures from ally (select theirs first)
	//if (!selection->our())
	//	return true;
	// Attempt to swap creatures with ally (select ours first)
	//if (selection->creature != creature && withAlly)
	//	return true;
	/*if (!owner->removableUnits)
	{
		if (selection->upg == EGarrisonType::UP)
			return true;
		else
			return creature || upg == EGarrisonType::UP;
	}*/
	return false;
}
void COutpostSlot::clickLeft(tribool down, bool previousState) 
{
	if (down)
	{
		bool refr = false;
		const CGarrisonSlot * selection = owner->getSelection();
		if (!selection)
		{
			refr = highlightOrDropArtifact();
			handleSplittingShortcuts();
		}
		else if (selection == this)
			refr = viewInfo();
		// Re-highlight if troops aren't removable or not ours.
		else if (mustForceReselection())
		{
			if (creature)
				owner->selectSlot(this);
			redraw();
			refr = true;
		}
		else
		{
			const CArmedInstance * selectedObj = owner->armedObjs[selection->upg];
			/*bool lastHeroStackSelected = false;
			if (selectedObj->stacksCount() == 1
				&& owner->getSelection()->upg != upg
				&& dynamic_cast<const CGHeroInstance*>(selectedObj))
			{
				lastHeroStackSelected = true;
			}*/

			if ((owner->getSplittingMode() || LOCPLINT->shiftPressed()) // split window
				&& (creature == nullptr || (creature == selection->creature && myStack->daysCost == selection->myStack->daysCost))
				&& ((selection->myStack->daysCost == 0 && upg) || (upg == selection->upg)))
			{
				refr = split();
			}
			//else if (!creature && lastHeroStackSelected) // split all except last creature
			//	LOCPLINT->cb->splitStack(selectedObj, owner->armedObjs[upg], selection->ID, ID, selection->myStack->count - 1);
			else if ((creature != selection->creature || myStack->daysCost != selection->myStack->daysCost) 
				&& ((selection->myStack->daysCost == 0 && upg) || (upg == selection->upg))) // swap
				LOCPLINT->cb->swapCreatures(selectedObj, owner->armedObjs[upg],  selection->ID, ID);
			//else if (lastHeroStackSelected) // merge last stack to other hero stack
			//	refr = split();
			else if (!selection->upg) { //merge up/up or up/down //(selection->upg) || (selection->myStack->daysCost ==0 || myStack && (selection->myStack->daysCost == myStack->daysCost))
				if(selection->myStack->daysCost ==0 || myStack && (selection->myStack->daysCost == myStack->daysCost))// merge
					LOCPLINT->cb->mergeStacks(selectedObj, owner->armedObjs[upg], selection->ID, ID);
			}else //merge down/down
				LOCPLINT->cb->mergeStacks(selectedObj, owner->armedObjs[upg], selection->ID, ID);
		}
		if (refr)
		{
			// Refresh Statusbar
			hover(false);
			hover(true);
		}
	}
}