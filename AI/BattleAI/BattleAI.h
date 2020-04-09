/*
 * BattleAI.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "../../lib/AI_Base.h"
#include "PossibleSpellcast.h"
#include "PotentialTargets.h"

class CSpell;
class EnemyInfo;

/*
struct CurrentOffensivePotential
{
	std::map<const CStack *, PotentialTargets> ourAttacks;
	std::map<const CStack *, PotentialTargets> enemyAttacks;

	CurrentOffensivePotential(ui8 side)
	{
		for(auto stack : cbc->battleGetStacks())
		{
			if(stack->side == side)
				ourAttacks[stack] = PotentialTargets(stack);
			else
				enemyAttacks[stack] = PotentialTargets(stack);
		}
	}

	int potentialValue()
	{
		int ourPotential = 0, enemyPotential = 0;
		for(auto &p : ourAttacks)
			ourPotential += p.second.bestAction().attackValue();

		for(auto &p : enemyAttacks)
			enemyPotential += p.second.bestAction().attackValue();

		return ourPotential - enemyPotential;
	}
};
*/ // These lines may be usefull but they are't used in the code.

class CBattleAI : public CBattleGameInterface
{
protected:
	int side;
	std::shared_ptr<CBattleCallback> cb;

	//Previous setting of cb
	bool wasWaitingForRealize, wasUnlockingGs;

public:
	//************************************
	// Method:    CBattleAI
	// FullName:  CBattleAI::CBattleAI
	// Access:    public 
	// Returns:   
	// Qualifier:
	//************************************
	CBattleAI();
	~CBattleAI();

	void init(std::shared_ptr<CBattleCallback> CB) override; 
	void attemptCastingSpell();

	void evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps); //for offensive damaging spells only

	BattleAction activeStack(const CStack * stack) override; //called when it's turn of that stack
	BattleAction goTowards(const CStack * stack, BattleHex hex );

	boost::optional<BattleAction> considerFleeingOrSurrendering();

	static int distToNearestNeighbour(BattleHex hex, const ReachabilityInfo::TDistances& dists, BattleHex *chosenHex = nullptr);
	static bool isCloser(const EnemyInfo & ei1, const EnemyInfo & ei2, const ReachabilityInfo::TDistances & dists);

	void print(const std::string &text) const;
	BattleAction useCatapult(const CStack *stack);
	void battleStart(const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, bool Side) override;
};
class CGeniusAI : public CBattleAI
{

public:
	CGeniusAI();
	~CGeniusAI();

	BattleAction activeStack(const CStack * stack) override; //called when it's turn of that stack
	BattleAction goTowards(const CStack * stack, BattleHex hex);
	void attemptCastingSpell();
};