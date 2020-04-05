#pragma once

#include "../widgets/CGarrisonInt.h"
#include "../widgets/Images.h"
#include "CCastleInterface.h"
class CGOutpost;
class CTownList2;
/// Class which manages the outpost window
class COutpostInterface : public CStatusbarWindow, public CGarrisonHolder
{
	std::shared_ptr<CLabel> title;
	std::shared_ptr<CLabel> income;
	std::shared_ptr<CAnimImage> icon;

	std::shared_ptr<CPicture> panel;
	std::shared_ptr<CResDataBar> resdatabar;

	//std::shared_ptr<CTownInfo> hall;
	//std::shared_ptr<CTownInfo> fort;

	std::shared_ptr<CButton> exit;
	std::shared_ptr<CButton> split;
	std::shared_ptr<CButton> fastArmyPurhase;

	std::vector<std::shared_ptr<CCreaInfo>> creainfo;//small icons of creatures (bottom-left corner);

public:
	std::shared_ptr<CTownList2> townlist;

	//TODO: move to private
	CGOutpost * op;
	const CGTownInstance * town;
	const CGHeroInstance* visitingHero;
	std::shared_ptr<HeroSlots> heroes;
	//std::shared_ptr<CCastleBuildings> builds;

	std::shared_ptr<CGarrisonInt> garr;
	//from - previously selected castle (if any)
	COutpostInterface(const CGOutpost * op, const CGHeroInstance* hero,const CGTownInstance * to);
	~COutpostInterface();

	virtual void updateGarrisons() override;

	//void castleTeleport(int where);
	void townChange();
	void keyPressed(const SDL_KeyboardEvent & key) override;
	void close();
	//void addBuilding(BuildingID bid);
	//void removeBuilding(BuildingID bid);
	void recreateIcons();
};