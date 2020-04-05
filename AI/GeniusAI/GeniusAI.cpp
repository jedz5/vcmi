/*
* BattleAI.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "GeniusAI.h"

#include <vstd/RNG.h>

#include "StackWithBonuses.h"
#include "EnemyInfo.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/CStack.h"//todo: remove
#include "../../lib/CConfigHandler.h"
#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

class RNGStub : public vstd::RNG
{
public:
	vstd::TRandI64 getInt64Range(int64_t lower, int64_t upper) override
	{
		return [=]()->int64_t
		{
			return (lower + upper) / 2;
		};
	}

	vstd::TRand getDoubleRange(double lower, double upper) override
	{
		return [=]()->double
		{
			return (lower + upper) / 2;
		};
	}
};

enum class SpellTypes
{
	ADVENTURE, BATTLE, OTHER
};

SpellTypes spellType(const CSpell * spell)
{
	if (!spell->isCombatSpell() || spell->isCreatureAbility())
		return SpellTypes::OTHER;

	if (spell->isOffensiveSpell() || spell->hasEffects() || spell->hasBattleEffects())
		return SpellTypes::BATTLE;

	return SpellTypes::OTHER;
}

CGeniusAI::CGeniusAI()
	: side(-1), wasWaitingForRealize(false), wasUnlockingGs(false)
{
}

CGeniusAI::~CGeniusAI()
{
	if (cb)
	{
		//Restore previous state of CB - it may be shared with the main AI (like VCAI)
		cb->waitTillRealize = wasWaitingForRealize;
		cb->unlockGsWhenWaiting = wasUnlockingGs;
	}
}

void CGeniusAI::init(std::shared_ptr<CBattleCallback> CB)
{
	setCbc(CB);
	cb = CB;
	playerID = *CB->getPlayerID(); //TODO should be sth in callback
	wasWaitingForRealize = cb->waitTillRealize;
	wasUnlockingGs = CB->unlockGsWhenWaiting;
	CB->waitTillRealize = true;
	CB->unlockGsWhenWaiting = false;
}
bool start = true;
bool bank = false;
const CStack* startStack ;
BattleAction CGeniusAI::activeStack(const CStack * stack)
{
	LOG_TRACE_PARAMS(logAi, "stack: %s", stack->nodeName());
	setCbc(cb); //TODO: make solid sure that AIs always use their callbacks (need to take care of event handlers too)
	try
	{
		if (stack->type->idNumber == CreatureID::CATAPULT)
			return useCatapult(stack);
		if (stack->hasBonusOfType(Bonus::SIEGE_WEAPON) && stack->hasBonusOfType(Bonus::HEALER))
		{
			auto healingTargets = cb->battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
			std::map<int, const CStack*> woundHpToStack;
			for (auto stack : healingTargets)
				if (auto woundHp = stack->MaxHealth() - stack->getFirstHPleft())
					woundHpToStack[woundHp] = stack;
			if (woundHpToStack.empty())
				return BattleAction::makeDefend(stack);
			else
				return BattleAction::makeHeal(stack, woundHpToStack.rbegin()->second); //last element of the woundHpToStack is the most wounded stack
		}

		attemptCastingSpell();

		if (auto ret = getCbc()->battleIsFinished())
		{
			//spellcast may finish battle
			//send special preudo-action
			BattleAction cancel;
			cancel.actionType = EActionType::CANCEL;
			return cancel;
		}

		if (auto action = considerFleeingOrSurrendering())
			return *action;


		//evaluate casting spell for spellcasting stack
		boost::optional<PossibleSpellcast> bestSpellcast(boost::none);
		//TODO: faerie dragon type spell should be selected by server
		SpellID creatureSpellToCast = cb->battleGetRandomStackSpell(CRandomGenerator::getDefault(), stack, CBattleInfoCallback::RANDOM_AIMED);
		if (stack->hasBonusOfType(Bonus::SPELLCASTER) && stack->canCast() && creatureSpellToCast != SpellID::NONE)
		{
			const CSpell * spell = creatureSpellToCast.toSpell();

			if (spell->canBeCast(getCbc().get(), spells::Mode::CREATURE_ACTIVE, stack))
			{
				std::vector<PossibleSpellcast> possibleCasts;
				spells::BattleCast temp(getCbc().get(), stack, spells::Mode::CREATURE_ACTIVE, spell);
				for (auto & target : temp.findPotentialTargets())
				{
					PossibleSpellcast ps;
					ps.dest = target;
					ps.spell = spell;
					evaluateCreatureSpellcast(stack, ps);
					possibleCasts.push_back(ps);
				}

				std::sort(possibleCasts.begin(), possibleCasts.end(), [&](const PossibleSpellcast & lhs, const PossibleSpellcast & rhs) { return lhs.value > rhs.value; });
				if (!possibleCasts.empty() && possibleCasts.front().value > 0)
				{
					bestSpellcast = boost::optional<PossibleSpellcast>(possibleCasts.front());
				}
			}
		}
		//best action is from effective owner point if view, we are effective owner as we received "activeStack"		
		if (start) {    
			bank = !(stack->getPosition().getX() == 1 || stack->occupiedHex().getX() == 1);  //if in creature bank then just fight
			startStack = stack;
		}
		HypotheticBattle hb(getCbc());
		auto reachability = hb.getReachability(stack);
		auto hexes = hb.battleGetAvailableHexes(reachability, stack);
		auto goNorthWest = [=]() -> BattleAction {
			auto reachability = cb->getReachability(stack);
			if (stack->position != BattleHex(2, 0) && stack->occupiedHex() != BattleHex(2, 0) && vstd::contains(hexes, BattleHex(2, 0)))
				return BattleAction::makeMove(stack, BattleHex(2, 0));
			else if (stack->position != BattleHex(2, 1) && stack->occupiedHex() != BattleHex(2, 1) && vstd::contains(hexes, BattleHex(2, 1)))
				return BattleAction::makeMove(stack, BattleHex(2, 1));
			else if (stack->position != BattleHex(1, 1) && stack->occupiedHex() != BattleHex(1, 1) && vstd::contains(hexes, BattleHex(1, 1)))
				return BattleAction::makeMove(stack, BattleHex(1, 1));
			else if (stack->position == BattleHex(2, 0) || stack->occupiedHex() == BattleHex(2, 0)|| stack->position == BattleHex(2, 1) || stack->occupiedHex() == BattleHex(2, 1)|| stack->position == BattleHex(1, 1) || stack->occupiedHex() == BattleHex(1, 1)) 
				return BattleAction::makeDefend(stack);
			else{
				auto isNorthWester = [](BattleHex p1, BattleHex p2) -> bool {
					if (p1.getX() < p2.getX())
						return true;
					else if (p1.getX() == p2.getX())
						return p1.getY() <= p2.getY();
					else
						return false;
				};

				BattleHex dest = *std::min_element(hexes.begin(), hexes.end(), isNorthWester);
				if (dest != stack->position && stack->occupiedHex() != dest)
					return goTowards(stack, dest);
				else
					return BattleAction::makeDefend(stack);
			}


		};
		auto melee = cb->battleGetStacksIf([=](const CStack * s) {
			return s->isValidTarget(false) && s->side && !s->getCreature()->isShooting();
		});
		const bool tacticPhase = getCbc()->battleTacticDist() && getCbc()->battleGetTacticsSide() == stack->unitSide();
		if (tacticPhase) {
			if (start || stack != startStack) {
				PotentialTargets targets(stack, &hb);
				if (melee.size() == 0) {
					if (stack->isShooter()) {
						BattleAction ba;
						ba.side = side;
						ba.actionType = EActionType::NO_ACTION;
						return ba;
					}
					auto dists = getCbc()->battleGetDistances(stack, stack->getPosition());
					if (!targets.unreachableEnemies.empty())
					{
						const EnemyInfo &ei = *range::min_element(targets.unreachableEnemies, std::bind(isCloser, _1, _2, std::ref(dists)));
						if (distToNearestNeighbour(ei.s->getPosition(), dists) < GameConstants::BFIELD_SIZE)
						{
							return goTowards(stack, ei.s->getPosition());
						}
					}
				}
				else {
					if (!stack->isShooter())
						return goNorthWest();
					else {
						BattleAction ba;
						ba.side = side;
						ba.actionType = EActionType::NO_ACTION;
						return ba;
					}
				}
			}
			else {
				return BattleAction::makeEndOFTacticPhase(side);
			}
			
		}
		start = false;
		//protect archer in no bank situation
		if (!bank && melee.size() > 0 && !stack->isShooter() && stack->getCount() < 3 && stack->level() <5)
		{
			return goNorthWest();
		}
		PotentialTargets targets(stack, &hb);
		if (targets.possibleAttacks.size())
		{
			auto hlp = targets.bestAction();
			if (bestSpellcast.is_initialized() && bestSpellcast->value > hlp.damageDiff())
				return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
			if (hlp.attack.shooting) {
				if(bank)
					return BattleAction::makeShotAttack(stack, hlp.attack.defender);
				else if (!stack->moved() && !stack->waited() && hlp.attack.defender->getPosition().getX() > 7 && !hlp.attack.defender->moved() && !hlp.attack.defender->canShoot())
				{
					return BattleAction::makeWait(stack);
				}
				else
				{
					return BattleAction::makeShotAttack(stack, hlp.attack.defender);
				}
			}
			else if (bestSpellcast.is_initialized())
			{
				return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
			}
			else {

				PotentialTargets enemyTargets(hlp.attack.defender, &hb);
				auto isImportant = [=]() {
					auto important = boost::find_if(enemyTargets.possibleAttacks, [=](AttackPossibility ap) {
						return ((ap.attack.attacker->isShooter() && !getCbc()->battleHasShootingPenalty(ap.attack.attacker, ap.attack.defender->getPosition())) 
							||ap.attack.defender->isShooter() 
							|| ap.attack.attacker->getPosition().getX() <=7 )  ;
					});
					if ( important != enemyTargets.possibleAttacks.end())
						return true;
					return false;
				};
				if(bank)
					return BattleAction::makeMeleeAttack(stack, hlp.attack.defender->getPosition(), hlp.tile);
				else if ((enemyTargets.possibleAttacks.size() > 0 && isImportant())) //melee.size()==0 || 
				{
					return BattleAction::makeMeleeAttack(stack, hlp.attack.defender->getPosition(), hlp.tile);
				}
				else
				{
					if (stack->waited())
					{
						return goNorthWest();
					}
					else
					{
						if (!stack->moved())
							return BattleAction::makeWait(stack);
					}
				}
			}
		}
		else
		{
			if (stack->waited())
			{
				if (melee.size())
					return goNorthWest();
				//ThreatMap threatsToUs(stack); // These lines may be usefull but they are't used in the code.
				auto dists = getCbc()->battleGetDistances(stack, stack->getPosition());
				if (!targets.unreachableEnemies.empty())
				{
					const EnemyInfo &ei = *range::min_element(targets.unreachableEnemies, std::bind(isCloser, _1, _2, std::ref(dists)));
					if (distToNearestNeighbour(ei.s->getPosition(), dists) < GameConstants::BFIELD_SIZE)
					{
						return goTowards(stack, ei.s->getPosition());
					}
				}
			}
			else
			{
				if (!stack->moved())
					return BattleAction::makeWait(stack);
			}
		}
	}
	catch (boost::thread_interrupted &)
	{
		throw;
	}
	catch (std::exception &e)
	{
		logAi->error("Exception occurred in %s %s", __FUNCTION__, e.what());
	}
	return BattleAction::makeDefend(stack);
}

BattleAction CGeniusAI::goTowards(const CStack * stack, BattleHex destination)
{
	if (!destination.isValid())
	{
		logAi->error("CGeniusAI::goTowards: invalid destination");
		return BattleAction::makeDefend(stack);
	}

	auto reachability = cb->getReachability(stack);
	auto avHexes = cb->battleGetAvailableHexes(reachability, stack);

	if (vstd::contains(avHexes, destination))
		return BattleAction::makeMove(stack, destination);
	auto destNeighbours = destination.neighbouringTiles();
	if (vstd::contains_if(destNeighbours, [&](BattleHex n) { return stack->coversPos(destination); }))
	{
		logAi->warn("Warning: already standing on neighbouring tile!");
		//We shouldn't even be here...
		return BattleAction::makeDefend(stack);
	}
	vstd::erase_if(destNeighbours, [&](BattleHex hex) { return !reachability.accessibility.accessible(hex, stack); });
	if (!avHexes.size() || !destNeighbours.size()) //we are blocked or dest is blocked
	{
		return BattleAction::makeDefend(stack);
	}
	if (stack->hasBonusOfType(Bonus::FLYING))
	{
		// Flying stack doesn't go hex by hex, so we can't backtrack using predecessors.
		// We just check all available hexes and pick the one closest to the target.
		auto distToDestNeighbour = [&](BattleHex hex) -> int
		{
			auto nearestNeighbourToHex = vstd::minElementByFun(destNeighbours, [&](BattleHex a)
			{return BattleHex::getDistance(a, hex); });
			return BattleHex::getDistance(*nearestNeighbourToHex, hex);
		};
		auto nearestAvailableHex = vstd::minElementByFun(avHexes, distToDestNeighbour);
		return BattleAction::makeMove(stack, *nearestAvailableHex);
	}
	else
	{
		BattleHex bestNeighbor = destination;
		if (distToNearestNeighbour(destination, reachability.distances, &bestNeighbor) > GameConstants::BFIELD_SIZE)
		{
			return BattleAction::makeDefend(stack);
		}
		BattleHex currentDest = bestNeighbor;
		while (1)
		{
			if (!currentDest.isValid())
			{
				logAi->error("CGeniusAI::goTowards: internal error");
				return BattleAction::makeDefend(stack);
			}

			if (vstd::contains(avHexes, currentDest))
				return BattleAction::makeMove(stack, currentDest);
			currentDest = reachability.predecessors[currentDest];
		}
	}
}

BattleAction CGeniusAI::useCatapult(const CStack * stack)
{
	throw std::runtime_error("CGeniusAI::useCatapult is not implemented.");
}

void CGeniusAI::attemptCastingSpell()
{
	auto hero = cb->battleGetMyHero();
	if (!hero)
		return;
	auto s = settings["adventure"]["useMagic"];
	if (!s.Bool() || cb->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
		return;

	LOGL("Casting spells sounds like fun. Let's see...");
	//Get all spells we can cast
	std::vector<const CSpell*> possibleSpells;
	vstd::copy_if(VLC->spellh->objects, std::back_inserter(possibleSpells), [hero](const CSpell *s) -> bool
	{
		return s->canBeCast(getCbc().get(), spells::Mode::HERO, hero);
	});
	LOGFL("I can cast %d spells.", possibleSpells.size());

	vstd::erase_if(possibleSpells, [](const CSpell *s)
	{
		return spellType(s) != SpellTypes::BATTLE;
	});

	LOGFL("I know how %d of them works.", possibleSpells.size());

	//Get possible spell-target pairs
	std::vector<PossibleSpellcast> possibleCasts;
	for (auto spell : possibleSpells)
	{
		spells::BattleCast temp(getCbc().get(), hero, spells::Mode::HERO, spell);

		for (auto & target : temp.findPotentialTargets())
		{
			PossibleSpellcast ps;
			ps.dest = target;
			ps.spell = spell;
			possibleCasts.push_back(ps);
		}
	}
	LOGFL("Found %d spell-target combinations.", possibleCasts.size());
	if (possibleCasts.empty())
		return;

	using ValueMap = PossibleSpellcast::ValueMap;

	auto evaluateQueue = [&](ValueMap & values, const std::vector<battle::Units> & queue, HypotheticBattle * state, size_t minTurnSpan, bool * enemyHadTurnOut) -> bool
	{
		bool firstRound = true;
		bool enemyHadTurn = false;
		size_t ourTurnSpan = 0;

		bool stop = false;

		for (auto & round : queue)
		{
			if (!firstRound)
				state->nextRound(0);//todo: set actual value?
			for (auto unit : round)
			{
				if (!vstd::contains(values, unit->unitId()))
					values[unit->unitId()] = 0;

				if (!unit->alive())
					continue;

				if (state->battleGetOwner(unit) != playerID)
				{
					enemyHadTurn = true;

					if (!firstRound || state->battleCastSpells(unit->unitSide()) == 0)
					{
						//enemy could counter our spell at this point
						//anyway, we do not know what enemy will do
						//just stop evaluation
						stop = true;
						break;
					}
				}
				else if (!enemyHadTurn)
				{
					ourTurnSpan++;
				}

				state->nextTurn(unit->unitId());

				PotentialTargets pt(unit, state);

				if (!pt.possibleAttacks.empty())
				{
					AttackPossibility ap = pt.bestAction();

					auto swb = state->getForUpdate(unit->unitId());
					*swb = *ap.attackerState;

					if (ap.damageDealt > 0)
						swb->removeUnitBonus(Bonus::UntilAttack);
					if (ap.damageReceived > 0)
						swb->removeUnitBonus(Bonus::UntilBeingAttacked);

					for (auto affected : ap.affectedUnits)
					{
						swb = state->getForUpdate(affected->unitId());
						*swb = *affected;

						if (ap.damageDealt > 0)
							swb->removeUnitBonus(Bonus::UntilBeingAttacked);
						if (ap.damageReceived > 0 && ap.attack.defender->unitId() == affected->unitId())
							swb->removeUnitBonus(Bonus::UntilAttack);
					}
				}

				auto bav = pt.bestActionValue();

				//best action is from effective owner`s point if view, we need to convert to our point if view
				if (state->battleGetOwner(unit) != playerID)
					bav = -bav;
				values[unit->unitId()] += bav;
			}

			firstRound = false;

			if (stop)
				break;
		}

		if (enemyHadTurnOut)
			*enemyHadTurnOut = enemyHadTurn;

		return ourTurnSpan >= minTurnSpan;
	};

	RNGStub rngStub;

	ValueMap valueOfStack;
	ValueMap healthOfStack;

	TStacks all = cb->battleGetAllStacks(false);

	size_t ourRemainingTurns = 0;

	for (auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
		valueOfStack[unit->unitId()] = 0;

		if (cb->battleGetOwner(unit) == playerID && unit->canMove() && !unit->moved())
			ourRemainingTurns++;
	}

	LOGFL("I have %d turns left in this round", ourRemainingTurns);

	const bool castNow = ourRemainingTurns <= 1;

	if (castNow)
		print("I should try to cast a spell now");
	else
		print("I could wait better moment to cast a spell");

	auto amount = all.size();

	std::vector<battle::Units> turnOrder;

	cb->battleGetTurnOrder(turnOrder, amount, 2); //no more than 1 turn after current, each unit at least once

	{
		bool enemyHadTurn = false;

		HypotheticBattle state(cb);
		evaluateQueue(valueOfStack, turnOrder, &state, 0, &enemyHadTurn);

		if (!enemyHadTurn)
		{
			auto battleIsFinishedOpt = state.battleIsFinished();

			if (battleIsFinishedOpt)
			{
				print("No need to cast a spell. Battle will finish soon.");
				return;
			}
		}
	}

	auto evaluateSpellcast = [&](PossibleSpellcast * ps)
	{
		HypotheticBattle state(cb);

		spells::BattleCast cast(&state, hero, spells::Mode::HERO, ps->spell);
		cast.target = ps->dest;
		cast.cast(&state, rngStub);
		ValueMap newHealthOfStack;
		ValueMap newValueOfStack;

		size_t ourUnits = 0;

		for (auto unit : all)
		{
			auto unitId = unit->unitId();
			auto localUnit = state.battleGetUnitByID(unitId);

			newHealthOfStack[unitId] = localUnit->getAvailableHealth();
			newValueOfStack[unitId] = 0;

			if (state.battleGetOwner(localUnit) == playerID && localUnit->alive() && localUnit->willMove())
				ourUnits++;
		}

		size_t minTurnSpan = ourUnits / 3; //todo: tweak this

		std::vector<battle::Units> newTurnOrder;
		state.battleGetTurnOrder(newTurnOrder, amount, 2);

		const bool turnSpanOK = evaluateQueue(newValueOfStack, newTurnOrder, &state, minTurnSpan, nullptr);

		if (turnSpanOK || castNow)
		{
			int64_t totalGain = 0;

			for (auto unit : all)
			{
				auto unitId = unit->unitId();
				auto localUnit = state.battleGetUnitByID(unitId);

				auto newValue = getValOr(newValueOfStack, unitId, 0);
				auto oldValue = getValOr(valueOfStack, unitId, 0);

				auto healthDiff = newHealthOfStack[unitId] - healthOfStack[unitId];

				if (localUnit->unitOwner() != playerID)
					healthDiff = -healthDiff;

				if (healthDiff < 0)
				{
					ps->value = -1;
					return; //do not damage own units at all
				}

				totalGain += (newValue - oldValue + healthDiff);
			}

			ps->value = totalGain;
		}
		else
		{
			ps->value = -1;
		}
	};

	std::vector<std::function<void()>> tasks;

	for (PossibleSpellcast & psc : possibleCasts)
		tasks.push_back(std::bind(evaluateSpellcast, &psc));

	uint32_t threadCount = boost::thread::hardware_concurrency();

	if (threadCount == 0)
	{
		logGlobal->warn("No information of CPU cores available");
		threadCount = 1;
	}

	CStopWatch timer;

	CThreadHelper threadHelper(&tasks, threadCount);
	threadHelper.run();

	LOGFL("Evaluation took %d ms", timer.getDiff());

	auto pscValue = [](const PossibleSpellcast &ps) -> int64_t
	{
		return ps.value;
	};
	auto castToPerform = *vstd::maxElementByFun(possibleCasts, pscValue);

	if (castToPerform.value > 0)
	{
		LOGFL("Best spell is %s (value %d). Will cast.", castToPerform.spell->name % castToPerform.value);
		BattleAction spellcast;
		spellcast.actionType = EActionType::HERO_SPELL;
		spellcast.actionSubtype = castToPerform.spell->id;
		spellcast.setTarget(castToPerform.dest);
		spellcast.side = side;
		spellcast.stackNumber = (!side) ? -1 : -2;
		cb->battleMakeAction(&spellcast);
	}
	else
	{
		LOGFL("Best spell is %s. But it is actually useless (value %d).", castToPerform.spell->name % castToPerform.value);
	}
}
//Below method works only for offensive spells
void CGeniusAI::evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps)
{
	using ValueMap = PossibleSpellcast::ValueMap;

	RNGStub rngStub;
	HypotheticBattle state(getCbc());
	TStacks all = getCbc()->battleGetAllStacks(false);

	ValueMap healthOfStack;
	ValueMap newHealthOfStack;

	for (auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
	}

	spells::BattleCast cast(&state, stack, spells::Mode::CREATURE_ACTIVE, ps.spell);
	cast.target = ps.dest;
	cast.cast(&state, rngStub);

	for (auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);
		newHealthOfStack[unitId] = localUnit->getAvailableHealth();
	}

	int64_t totalGain = 0;

	for (auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);

		auto healthDiff = newHealthOfStack[unitId] - healthOfStack[unitId];

		if (localUnit->unitOwner() != getCbc()->getPlayerID())
			healthDiff = -healthDiff;

		if (healthDiff < 0)
		{
			ps.value = -1;
			return; //do not damage own units at all
		}

		totalGain += healthDiff;
	}

	ps.value = totalGain;
};
int CGeniusAI::distToNearestNeighbour(BattleHex hex, const ReachabilityInfo::TDistances &dists, BattleHex *chosenHex)
{
	int ret = 1000000;
	for (BattleHex n : hex.neighbouringTiles())
	{
		if (dists[n] >= 0 && dists[n] < ret)
		{
			ret = dists[n];
			if (chosenHex)
				*chosenHex = n;
		}
	}
	return ret;
}

void CGeniusAI::battleStart(const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, bool Side)
{
	LOG_TRACE(logAi);
	side = Side;
}

bool CGeniusAI::isCloser(const EnemyInfo &ei1, const EnemyInfo &ei2, const ReachabilityInfo::TDistances &dists)
{
	return distToNearestNeighbour(ei1.s->getPosition(), dists) < distToNearestNeighbour(ei2.s->getPosition(), dists);
}

void CGeniusAI::print(const std::string &text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.getStr(), this, text);
}

boost::optional<BattleAction> CGeniusAI::considerFleeingOrSurrendering()
{
	if (cb->battleCanSurrender(playerID))
	{
	}
	if (cb->battleCanFlee())
	{
	}
	return boost::none;
}



