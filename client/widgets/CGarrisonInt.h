/*
 * CGarrisonInt.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../windows/CWindowObject.h"

class CGarrisonInt;
class CButton;
class CArmedInstance;
class CAnimImage;
class CCreatureSet;
class CGarrisonSlot;
class CStackInstance;
class CLabel;
class CGOutpost;
/// A single garrison slot which holds one creature of a specific amount
class CGarrisonSlot : public CIntObject
{

	
	std::shared_ptr<CAnimImage> creatureImage;
	std::shared_ptr<CAnimImage> selectionImage; // image for selection, not always visible
	std::shared_ptr<CLabel> stackCount;

public:
	SlotID ID; //for identification
	CGarrisonInt *owner;
	/// Type of Garrison for slot (up or down)
	enum EGarrisonType
	{
		UP = 0,  ///< 0 - up garrison (Garrisoned)
		DOWN,  ///< 1 - down garrison (Visiting)
	} upg; ///< Flag indicating if it is the up or down garrison

	const CStackInstance * myStack; //nullptr if slot is empty
	const CCreature * creature;
	bool highlightOrDropArtifact();
	virtual bool mustForceReselection() const;
	virtual bool viewInfo();
	virtual bool split();
	void setHighlight(bool on);
	virtual void hover (bool on) override; //call-in
	const CArmedInstance * getObj() const;
	bool our() const;
	bool ally() const;
	virtual void clickRight(tribool down, bool previousState) override;
	virtual void clickLeft(tribool down, bool previousState) override;
	virtual void update();
	CGarrisonSlot(CGarrisonInt *Owner, int x, int y, SlotID IID, EGarrisonType Upg=EGarrisonType::UP, const CStackInstance * Creature=nullptr);

	virtual void splitIntoParts(EGarrisonType type, int amount, int maxOfSplittedSlots);
	virtual void handleSplittingShortcuts();

	friend class CGarrisonInt;
};

/// Class which manages slots of upper and lower garrison, splitting of units
class CGarrisonInt :public CIntObject
{
public:
	/// Chosen slot. Should be changed only via selectSlot.
	CGarrisonSlot * highlighted;
	bool inSplittingMode;
	std::vector<std::shared_ptr<CGarrisonSlot>> availableSlots;  ///< Slots of upper and lower garrison

	int interx;  ///< Space between slots
	Point garOffset;  ///< Offset between garrisons (not used if only one hero)
	std::vector<std::shared_ptr<CButton>> splitButtons;  ///< May be empty if no buttons

	SlotID p2; ///< TODO: comment me
	bool pb,
		 smallIcons,      ///< true - 32x32 imgs, false - 58x64
		 removableUnits,  ///< player Can remove units from up
		 twoRows,         ///< slots Will be placed in 2 rows
		 owned[2];        ///< player Owns up or down army ([0] upper, [1] lower)

	virtual void createSlots();
	virtual void selectSlot(CGarrisonSlot * slot); ///< @param slot null = deselect
	const CGarrisonSlot * getSelection();

	virtual void setSplittingMode(bool on);
	virtual bool getSplittingMode();

	std::vector<CGarrisonSlot *> getEmptySlots(CGarrisonSlot::EGarrisonType type);

	const CArmedInstance * armedObjs[2];  ///< [0] is upper, [1] is down

	virtual void setArmy(const CArmedInstance * army, bool bottomGarrison);
	virtual void addSplitBtn(std::shared_ptr<CButton> button);

	virtual void recreateSlots();

	virtual void splitClick();  ///< handles click on split button
	virtual void splitStacks(int amountLeft, int amountRight);  ///< TODO: comment me

	/// Constructor
	/// @param x, y Position
	/// @param inx Distance between slots;
	/// @param garsOffset
	/// @param s1, s2 Top and bottom armies
	/// @param _removableUnits You can take units from top
	/// @param smallImgs Units images size 64x58 or 32x32
	/// @param _twoRows Display slots in 2 row (1st row = 4 slots, 2nd = 3 slots)
	CGarrisonInt(int x, int y, int inx,
			 const Point & garsOffset,
			 const CArmedInstance * s1, const CArmedInstance * s2 = nullptr,
			 bool _removableUnits = true,
			 bool smallImgs = false,
			 bool _twoRows = false);
};

class CGarrisonHolder
{
public:
	virtual void updateGarrisons() = 0;
};

class COutpostInt : public CGarrisonInt {
public:
	void splitClick() override;
	void setSplittingMode(bool on) override;
	void createSlots() override;
	COutpostInt(int x, int y, int inx,
		const Point & garsOffset,
		const CGOutpost * s1, const CGHeroInstance * s2 = nullptr,
		bool _removableUnits = true,
		bool smallImgs = false,
		bool _twoRows = false);
};
class COutpostSlot : public CGarrisonSlot {
public:
	std::shared_ptr<CLabel> stackDaysCost;
	COutpostSlot(COutpostInt *Owner, int x, int y, SlotID IID, EGarrisonType Upg = EGarrisonType::UP, const CStackInstance * Creature = nullptr);
	void update() override;
	virtual void clickLeft(tribool down, bool previousState) override;
	virtual bool mustForceReselection() const override;
};