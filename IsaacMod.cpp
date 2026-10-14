// Isaac from The Binding of Isaac as a plant in Plants vs. Zombies. See IsaacMod.h for the controls.
// Numbers follow the Minecraft mod: its 20 ticks a second are 100 ticks here (x5), one Minecraft block
// is one lawn tile (80 px), and TBOI damage is scaled so a basic tear (3.5) hits as hard as a pea (20).

#include "IsaacMod.h"
#include "Board.h"
#include "Zombie.h"
#include "Coin.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "../GameConstants.h"
#include "Common.h"
#include "graphics/Graphics.h"
#include "graphics/GLImage.h"
#include "graphics/Color.h"
#include "misc/KeyCodes.h"
#include "widget/WidgetManager.h"
#include "sound/SoundManager.h"
#include "../PvzpLib/PvzpParticle.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdio>

using namespace Sexy;

// ----------------------------------------------------------------------------------------------------
//  Tuning
// ----------------------------------------------------------------------------------------------------
namespace
{
	constexpr int   TICKS_PER_SECOND = 100;
	constexpr int   MC = 5;                        // one Minecraft tick in game ticks
	constexpr float TILE = 80.0f;
	constexpr float WALK_SPEED = 1.6f;             // board px per tick at speed 1.0
	constexpr float TEAR_SPEED = 5.0f;             // board px per tick at shot speed 1.0
	constexpr float TEAR_HEIGHT = 40.0f;           // tears fly at Isaac's eye height
	constexpr int   INVINCIBLE_TICKS = 20 * MC;    // like the Minecraft mod: 1 second after a hit
	constexpr int   HURT_TICKS = 10 * MC;
	constexpr int   BOMB_FUSE = 40 * MC;           // 2 seconds
	constexpr float BOMB_RADIUS = 1.5f * TILE;     // the Minecraft mod's 3 block blast, as tiles across
	constexpr int   BOMB_DAMAGE = 343;             // TBOI bomb (60) at the tear scale
	constexpr int   BLACK_HEART_DAMAGE = 143;      // the Minecraft mod's 25 damage blast, same scale
	constexpr int   MANTLE_RECHARGE = 1200 * MC;   // a minute
	constexpr int   DEATH_TICKS = 6 * MC;          // per death frame
	constexpr int   HOLD_TICKS = 30 * MC;          // holding a new item up
	constexpr int   BRIM_LIFETIME = 12 * MC;
	constexpr int   MAX_PICKUPS = 99;
	constexpr int   PILL_COLOURS = 13, MAX_PER_COLOUR = 6;
	constexpr float STAT_STEP = 0.5f, LUCK_STEP = 1.0f;
	constexpr int   DIARRHEA_BOMBS = 5, DIARRHEA_INTERVAL = 5 * MC;

	// Drops (the Minecraft mod's mob loot table)
	constexpr float DROP_CHANCE = 1.0f / 10.0f;
	constexpr float NICKEL_CHANCE = 1.0f / 30.0f, DIME_CHANCE = 1.0f / 80.0f, HALF_HEART_CHANCE = 0.35f;

	// Sprite sheet (isaac/isaac.png, everything at 2x): {x, y, w, h}
	constexpr int S = 2;
	constexpr int HEAD_W = 28 * S, HEAD_H = 26 * S, HEAD_Y = 9 * S;
	constexpr int BODY_W = 24 * S, VBODY_Y = 32 * S, VBODY_H = 15 * S, HBODY_Y = 48 * S, HBODY_H = 14 * S;
	constexpr int WALK_FRAMES = 10;
	constexpr int POSE_HURT[4] = {136 * S, 64 * S, 28 * S, 30 * S};
	constexpr int POSE_DEATH[3][4] = {{0, 64 * S, 28 * S, 33 * S}, {104 * S, 64 * S, 28 * S, 32 * S}, {32 * S, 64 * S, 36 * S, 27 * S}};
	constexpr int POSE_PICKUP[4] = {72 * S, 64 * S, 28 * S, 31 * S};
	// head frames: 0 front, 2 right, 4 back, 6 left (+1 = shooting)
	constexpr int HEAD_FOR_DIR[4] = {0, 2, 4, 6};   // dir: 0 down (front), 1 right, 2 up (back), 3 left

	struct ItemInfo { const char* id; const char* name; const char* desc; int quality; bool meal; };
	const ItemInfo ITEMS[] = {
		{"holy_mantle", "HOLY MANTLE", "Holy shield!", 4, false},
		{"brimstone", "BRIMSTONE", "Blood laser barrage", 4, false},
		{"pyro", "PYRO", "99 bombs", 3, false},
		{"skatole", "SKATOLE", "Fly love", 0, false},
		{"stigmata", "STIGMATA", "DMG + HP up", 2, false},
		{"breakfast", "BREAKFAST", "HP up", 1, true},
		{"lunch", "LUNCH", "HP up", 1, true},
		{"dinner", "DINNER", "HP up", 1, true},
	};
	constexpr int QUALITY_WEIGHT[6] = {2, 40, 30, 16, 8, 2};
	// Costume slot of each item (-1 none): 0 cheek, 1 horn, 2 hat
	constexpr int COSTUME_SLOT[] = {-1, -1, 1, 2, 0, -1, -1, -1};
	constexpr int COSTUME_FRAMES[] = {1, 1, 2, 1, 1, 1, 1, 1};

	const char* PILL_NAMES[] = {"Health Up", "Health Down", "Speed Up", "Speed Down", "Luck Up", "Luck Down",
		"Bombs Are Key", "Explosive Diarrhea", "Full Health", "Bad Trip", "Range Up", "Range Down", "Damage Up", "Damage Down"};

	// ---- Assets (loaded once) ----
	struct Assets
	{
		bool loaded = false;
		Image* isaac = nullptr; Image* brimHeads = nullptr; Image* laser = nullptr; Image* laserSide = nullptr;
		Image* tear = nullptr; Image* bomb = nullptr; Image* bombWhite = nullptr; Image* altar = nullptr;
		Image* pickups = nullptr; Image* hud = nullptr; Image* pills = nullptr; Image* glow = nullptr; Image* mantleHud = nullptr;
		Image* items[(int)IsaacUpgrade::COUNT] = {};
		Image* costume[(int)IsaacUpgrade::COUNT] = {};
		Image* costumeBrim[(int)IsaacUpgrade::COUNT] = {};
		int sTear[2] = {-1, -1}, sTearHit = -1, sHurt[3] = {-1, -1, -1}, sDie[2] = {-1, -1}, sBoom[3] = {-1, -1, -1};
		int sPenny = -1, sNickel = -1, sDime = -1, sHeart = -1, sSoul = -1, sKey = -1, sMantle = -1, sLaser = -1, sTreasure = -1;
	};
	Assets gA;

	Image* LoadImg(LawnApp* theApp, const std::string& theName)
	{
		return theApp->GetImage("isaac/" + theName + ".png");
	}

	int LoadSnd(LawnApp* theApp, const std::string& theName)
	{
		if (!theApp->mSoundManager)
			return -1;
		return (int)theApp->mSoundManager->LoadSound("isaac/sounds/" + theName);
	}

	void LoadAssets(LawnApp* theApp)
	{
		if (gA.loaded)
			return;
		gA.loaded = true;
		gA.isaac = LoadImg(theApp, "isaac");
		gA.brimHeads = LoadImg(theApp, "brimstone_heads");
		gA.laser = LoadImg(theApp, "brimstone_laser");
		gA.laserSide = LoadImg(theApp, "brimstone_laser_side");
		gA.tear = LoadImg(theApp, "tear");
		gA.bomb = LoadImg(theApp, "bomb");
		gA.bombWhite = LoadImg(theApp, "bomb_white");
		gA.altar = LoadImg(theApp, "altar");
		gA.pickups = LoadImg(theApp, "pickups");
		gA.hud = LoadImg(theApp, "hud");
		gA.pills = LoadImg(theApp, "pills_hud");
		gA.glow = LoadImg(theApp, "mantle_glow");
		gA.mantleHud = LoadImg(theApp, "mantle_hud");
		for (int i = 0; i < (int)IsaacUpgrade::COUNT; i++)
		{
			gA.items[i] = LoadImg(theApp, std::string("item_") + ITEMS[i].id);
			if (COSTUME_SLOT[i] >= 0)
			{
				gA.costume[i] = LoadImg(theApp, std::string("costume_") + ITEMS[i].id);
				gA.costumeBrim[i] = LoadImg(theApp, std::string("costume_") + ITEMS[i].id + "_brimstone");
			}
		}
		gA.sTear[0] = LoadSnd(theApp, "tear_fire_4");
		gA.sTear[1] = LoadSnd(theApp, "tear_fire_5");
		gA.sTearHit = LoadSnd(theApp, "tear_block");
		for (int i = 0; i < 3; i++)
		{
			gA.sHurt[i] = LoadSnd(theApp, "hurt_grunt_" + std::to_string(i));
			gA.sBoom[i] = LoadSnd(theApp, "explosion_debris" + std::to_string(i + 1));
		}
		gA.sDie[0] = LoadSnd(theApp, "isaac_dies_0");
		gA.sDie[1] = LoadSnd(theApp, "isaac_dies_1");
		gA.sPenny = LoadSnd(theApp, "penny_pickup_1");
		gA.sNickel = LoadSnd(theApp, "nickel_pickup");
		gA.sDime = LoadSnd(theApp, "pickup_dime_01");
		gA.sHeart = LoadSnd(theApp, "heart");
		gA.sSoul = LoadSnd(theApp, "soul_heart");
		gA.sKey = LoadSnd(theApp, "key_pickup_gauntlet_4");
		gA.sMantle = LoadSnd(theApp, "holy_mantle_break");
		gA.sLaser = LoadSnd(theApp, "blood_laser");
		gA.sTreasure = LoadSnd(theApp, "treasure_room");
	}

	bool Overlaps(float ax, float ay, float aw, float ah, const Rect& r)
	{
		return ax < r.mX + r.mWidth && ax + aw > r.mX && ay < r.mY + r.mHeight && ay + ah > r.mY;
	}

	// Zombies Isaac can hit and be hit by: alive, not mind-controlled, on (or just above) the lawn
	constexpr unsigned int HIT_MASK = (1u << DamageRangeFlags::DAMAGES_GROUND) | (1u << DamageRangeFlags::DAMAGES_FLYING) |
		(1u << DamageRangeFlags::DAMAGES_OFF_GROUND) | (1u << DamageRangeFlags::DAMAGES_DOG);
}

// ----------------------------------------------------------------------------------------------------
//  The run (hearts, pickups, items, stats) -- like IsaacPlayerData / IsaacStats in the Minecraft mod
// ----------------------------------------------------------------------------------------------------
bool IsaacRun::Has(IsaacUpgrade theUpgrade) const
{
	return std::find(mItems.begin(), mItems.end(), (int)theUpgrade) != mItems.end();
}

void IsaacRun::AddHeartContainer()
{
	// a new container comes filled; once the two rows are full there's no room for more
	if (ExtraHeartRoom() < 2)
		return;
	mMaxRedHearts += 2;
	mRedHearts += 2;
}

void IsaacRun::RemoveHeartContainer()
{
	mMaxRedHearts = std::max(2, mMaxRedHearts - 2);
	mRedHearts = std::min(mRedHearts, mMaxRedHearts);
}

void IsaacRun::AddRedHearts(int theHalves)
{
	mRedHearts = std::clamp(mRedHearts + theHalves, 0, mMaxRedHearts);
}

void IsaacRun::AddSoulHearts(int theHalves)
{
	mSoulHearts = std::max(0, mSoulHearts + std::min(theHalves, ExtraHeartRoom()));
}

void IsaacRun::AddBlackHearts(int theHalves)
{
	mBlackHearts = std::max(0, mBlackHearts + std::min(theHalves, ExtraHeartRoom()));
}

int IsaacRun::TakeDamage(int theHalves)
{
	// black hearts go first, then soul hearts, then red hearts
	int aBlackBefore = mBlackHearts;
	int aAbsorbed = std::min(theHalves, mBlackHearts);
	mBlackHearts -= aAbsorbed;
	theHalves -= aAbsorbed;
	aAbsorbed = std::min(theHalves, mSoulHearts);
	mSoulHearts -= aAbsorbed;
	theHalves -= aAbsorbed;
	mRedHearts = std::max(0, mRedHearts - theHalves);
	// whole black hearts used up
	return (aBlackBefore + 1) / 2 - (mBlackHearts + 1) / 2;
}

IsaacPill IsaacRun::PillEffect(int theColour) const
{
	// which colour does what is shuffled once per run
	std::vector<int> aEffects;
	for (int i = 0; i < (int)IsaacPill::COUNT; i++)
		aEffects.push_back(i);
	std::mt19937 aRand(mSeed);
	std::shuffle(aEffects.begin(), aEffects.end(), aRand);
	return (IsaacPill)aEffects[std::clamp(theColour, 0, PILL_COLOURS - 1)];
}

bool IsaacRun::PillKnown(int theColour) const
{
	return std::find(mPillsIdentified.begin(), mPillsIdentified.end(), theColour) != mPillsIdentified.end();
}

static void ReadList(std::istringstream& theIn, std::vector<int>& theList)
{
	theList.clear();
	int aCount = 0;
	theIn >> aCount;
	for (int i = 0; i < aCount && theIn; i++)
	{
		int aValue = 0;
		theIn >> aValue;
		theList.push_back(aValue);
	}
}

static void WriteList(std::ostream& theOut, const char* theKey, const std::vector<int>& theList)
{
	theOut << theKey << " " << theList.size();
	for (int aValue : theList)
		theOut << " " << aValue;
	theOut << "\n";
}

void IsaacRun::Load(const std::string& thePath)
{
	std::ifstream aFile(Sexy::PathFromU8(thePath));
	if (!aFile)
		return;
	std::string aLine;
	while (std::getline(aFile, aLine))
	{
		std::istringstream aIn(aLine);
		std::string aKey;
		aIn >> aKey;
		if (aKey == "coins") aIn >> mCoins;
		else if (aKey == "keys") aIn >> mKeys;
		else if (aKey == "bombs") aIn >> mBombs;
		else if (aKey == "hearts") aIn >> mRedHearts >> mMaxRedHearts >> mSoulHearts >> mBlackHearts;
		else if (aKey == "stats") aIn >> mSpeed >> mTears >> mDamage >> mRange >> mShotSpeed >> mLuck;
		else if (aKey == "items") ReadList(aIn, mItems);
		else if (aKey == "seen") ReadList(aIn, mItemsSeen);
		else if (aKey == "pill") aIn >> mPill;
		else if (aKey == "seed") aIn >> mSeed;
		else if (aKey == "identified") ReadList(aIn, mPillsIdentified);
		else if (aKey == "spawned") ReadList(aIn, mPillsSpawned);
	}
	if (TotalHealth() <= 0)
		*this = IsaacRun();   // a finished run: start over
}

void IsaacRun::Save(const std::string& thePath) const
{
	std::ofstream aFile(Sexy::PathFromU8(thePath));
	if (!aFile)
		return;
	aFile << "coins " << mCoins << "\nkeys " << mKeys << "\nbombs " << mBombs << "\n";
	aFile << "hearts " << mRedHearts << " " << mMaxRedHearts << " " << mSoulHearts << " " << mBlackHearts << "\n";
	aFile << "stats " << mSpeed << " " << mTears << " " << mDamage << " " << mRange << " " << mShotSpeed << " " << mLuck << "\n";
	WriteList(aFile, "items", mItems);
	WriteList(aFile, "seen", mItemsSeen);
	aFile << "pill " << mPill << "\nseed " << mSeed << "\n";
	WriteList(aFile, "identified", mPillsIdentified);
	WriteList(aFile, "spawned", mPillsSpawned);
}

// ----------------------------------------------------------------------------------------------------
//  Where Isaac plays
// ----------------------------------------------------------------------------------------------------
bool IsaacMod::EnabledFor(LawnApp* theApp)
{
	GameMode aMode = theApp->mGameMode;
	if (aMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || aMode == GameMode::GAMEMODE_TREE_OF_WISDOM ||
		aMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM || aMode == GameMode::GAMEMODE_UPSELL ||
		aMode == GameMode::GAMEMODE_INTRO || aMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
		aMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST)
		return false;
	if (theApp->IsIZombieLevel() || theApp->IsScaryPotterLevel())
		return false;
	return true;
}

bool IsaacMod::NoNewPlants(LawnApp* theApp)
{
	return theApp->IsAdventureMode();
}

bool IsaacMod::LevelGivesPlant(int theLevel)
{
	if (theLevel >= 50 || theLevel % 10 == 9)
		return false;
	return theLevel != 4 && theLevel != 14 && theLevel != 24 && theLevel != 34 && theLevel != 44;
}

IsaacMod::IsaacMod(Board* theBoard) : mBoard(theBoard), mApp(theBoard->mApp)
{
	mRand.seed((uint32_t)std::time(nullptr) ^ (uint32_t)(uintptr_t)this);
	mSavePath = GetAppDataPath("isaac_run.txt");
	mRun.Load(mSavePath);
	if (mRun.mSeed == 1)
		mRun.mSeed = mRand();
}

IsaacMod::~IsaacMod() = default;

bool IsaacMod::KeyDown(int theKey) const
{
	return mApp->mWidgetManager && theKey >= 0 && theKey < 0xFF && mApp->mWidgetManager->mKeyDown[theKey];
}

void IsaacMod::Play(int theSound)
{
	if (theSound >= 0)
		mApp->PlaySample(theSound);
}

void IsaacMod::RowBounds(float theX, float& theTop, float& theBottom)
{
	int aRows = mBoard->StageHas6Rows() ? 6 : 5;
	int aGridX = mBoard->PixelToGridXKeepOnBoard((int)theX, 300);
	int aRowH = (mBoard->StageHasPool() || mBoard->StageHasRoof()) ? 85 : 100;
	theTop = (float)mBoard->GridToPixelY(aGridX, 0) + 45.0f;
	theBottom = (float)mBoard->GridToPixelY(aGridX, aRows - 1) + aRowH - 5.0f;
}

void IsaacMod::Spawn()
{
	LoadAssets(mApp);
	mSpawned = true;
	mX = LAWN_XMIN + TILE * 1.5f;
	float aTop, aBottom;
	RowBounds(mX, aTop, aBottom);
	mY = (aTop + aBottom) / 2.0f;
	mInvincible = INVINCIBLE_TICKS;
}

int IsaacMod::TearDamage() const
{
	return std::max(1, (int)std::lround(mRun.mDamage / 3.5f * 20.0f));
}

int IsaacMod::ChargeTicks() const
{
	// TBOI's Brimstone: fire rate / 3, tear delay = 30 / fire rate - 1, charge (s) = Roundup(tear delay) / 30
	float aFireRate = std::max(0.05f, mRun.mTears) / 3.0f;
	float aDelay = 30.0f / aFireRate - 1.0f;
	float aSeconds = std::ceil(aDelay) / 30.0f;
	return std::max(1, (int)std::lround(aSeconds * TICKS_PER_SECOND));
}

void IsaacMod::Announce(const std::string& theTitle, const std::string& theSub)
{
	mAnnounce = theTitle;
	mAnnounceSub = theSub;
	mAnnounceTimer = 3 * TICKS_PER_SECOND;
}

// ----------------------------------------------------------------------------------------------------
//  Update
// ----------------------------------------------------------------------------------------------------
void IsaacMod::Update()
{
	if (!mSpawned)
	{
		if (!EnabledFor(mApp))
			return;
		Spawn();
	}
	mTick++;
	if (mAnnounceTimer > 0)
		mAnnounceTimer--;

	if (mDeathTimer >= 0)
	{
		mDeathTimer++;
		if (mDeathTimer == 3 * DEATH_TICKS + TICKS_PER_SECOND)
			mBoard->ZombiesWon(nullptr);
		UpdateTears();
		UpdateBombs();
		return;
	}

	if (mInvincible > 0) mInvincible--;
	if (mHurtTimer > 0) mHurtTimer--;
	if (mShootFrame > 0) mShootFrame--;
	if (mLookTimer > 0) mLookTimer--;
	if (mHoldTimer > 0) mHoldTimer--;

	UpdateMovement();
	UpdateShooting();

	// E: bomb (one press, one bomb)
	bool aE = KeyDown('E');
	if (aE && !mKeyE && mRun.mBombs > 0)
	{
		mRun.mBombs--;
		mBombs.push_back({mX, mY, BOMB_FUSE, true});
	}
	mKeyE = aE;
	// Q: take the pill
	bool aQ = KeyDown('Q');
	if (aQ && !mKeyQ)
		TakePill();
	mKeyQ = aQ;

	// Explosive Diarrhea: a bomb every quarter second
	if (mDiarrheaBombs > 0 && --mDiarrheaTimer <= 0)
	{
		mBombs.push_back({mX, mY, BOMB_FUSE, true});
		mDiarrheaBombs--;
		mDiarrheaTimer = DIARRHEA_INTERVAL;
	}

	UpdateTears();
	UpdateBeams();
	UpdateBombs();
	UpdateDrops();
	UpdateAltar();
	UpdateDamage();

	if (mTick % TICKS_PER_SECOND == 0)
		mRun.Save(mSavePath);
}

void IsaacMod::UpdateMovement()
{
	float aDX = (KeyDown('D') ? 1.0f : 0.0f) - (KeyDown('A') ? 1.0f : 0.0f);
	float aDY = (KeyDown('S') ? 1.0f : 0.0f) - (KeyDown('W') ? 1.0f : 0.0f);
	if (mHoldTimer > HOLD_TICKS - 20)
		aDX = aDY = 0;   // a moment frozen while lifting a new item up
	float aLen = std::sqrt(aDX * aDX + aDY * aDY);
	float aSpeed = WALK_SPEED * mRun.mSpeed;
	float aWantX = aLen > 0 ? aDX / aLen * aSpeed : 0.0f;
	float aWantY = aLen > 0 ? aDY / aLen * aSpeed : 0.0f;
	// a little slide, like TBOI
	mVelX += (aWantX - mVelX) * 0.25f;
	mVelY += (aWantY - mVelY) * 0.25f;
	mX += mVelX;
	mY += mVelY;

	float aTop, aBottom;
	mX = std::clamp(mX, (float)LAWN_XMIN + 10.0f, (float)BOARD_WIDTH - 20.0f);
	RowBounds(mX, aTop, aBottom);
	mY = std::clamp(mY, aTop, aBottom);

	mMoving = aLen > 0;
	if (mMoving)
	{
		// body faces the way he walks (sideways wins on diagonals, like TBOI)
		if (aDX != 0) mBodyDir = aDX > 0 ? 1 : 3;
		else mBodyDir = aDY > 0 ? 0 : 2;
		mWalkAnim += 0.2f * mRun.mSpeed;
	}
	else
	{
		mWalkAnim = 0;
	}
}

void IsaacMod::UpdateShooting()
{
	// Arrow keys: the newest one pressed wins
	static const int ARROWS[4] = {KEYCODE_DOWN, KEYCODE_RIGHT, KEYCODE_UP, KEYCODE_LEFT};
	static bool sWasDown[4] = {false, false, false, false};
	int aDir = -1;
	for (int i = 0; i < 4; i++)
	{
		bool aDown = KeyDown(ARROWS[i]);
		if (aDown && !sWasDown[i])
			mLastArrow = i;
		sWasDown[i] = aDown;
	}
	if (mLastArrow >= 0 && KeyDown(ARROWS[mLastArrow]))
		aDir = mLastArrow;
	else
		for (int i = 0; i < 4; i++)
			if (KeyDown(ARROWS[i])) { aDir = i; break; }

	if (mFireCooldown > 0)
		mFireCooldown--;

	if (mRun.Has(IsaacUpgrade::BRIMSTONE))
	{
		// Brimstone: hold to charge, let go when it's full to fire the laser
		if (aDir >= 0)
		{
			if (mBrimCharge < 0) mBrimCharge = 0;
			mBrimCharge++;
			mBrimDir = aDir;
			mHeadDir = aDir;
			mLookTimer = 12 * MC;
		}
		else if (mBrimCharge >= 0)
		{
			if (mBrimCharge >= ChargeTicks())
			{
				float aMouthY = mY - TEAR_HEIGHT;
				mBeams.push_back({mBrimDir, mX, aMouthY, 0.0f, 0, TearDamage()});
				mBrimFiring = BRIM_LIFETIME;
				Play(gA.sLaser);
			}
			mBrimCharge = -1;
		}
		if (mBrimFiring > 0)
			mBrimFiring--;
		return;
	}
	mBrimCharge = -1;

	if (aDir < 0 || mFireCooldown > 0)
		return;
	static const float DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0};
	float aSpeed = TEAR_SPEED * mRun.mShotSpeed;
	Tear aTear;
	aTear.x = mX + DX[aDir] * 10.0f;
	aTear.y = mY - TEAR_HEIGHT + DY[aDir] * 6.0f;
	aTear.vx = DX[aDir] * aSpeed + mVelX * 0.5f;   // tears carry some of Isaac's movement, like TBOI
	aTear.vy = DY[aDir] * aSpeed + mVelY * 0.5f;
	aTear.travelled = 0;
	aTear.range = mRun.mRange * TILE;
	aTear.damage = TearDamage();
	aTear.dead = false;
	aTear.fade = 0;
	mTears.push_back(aTear);
	mFireCooldown = std::max(1, (int)std::lround(TICKS_PER_SECOND / std::max(0.5f, mRun.mTears)));
	mHeadDir = aDir;
	mShootFrame = 3 * MC;
	mLookTimer = 12 * MC;
	Play(gA.sTear[mRand() % 2]);
}

void IsaacMod::UpdateTears()
{
	for (Tear& aTear : mTears)
	{
		if (aTear.dead)
		{
			aTear.fade++;
			continue;
		}
		aTear.x += aTear.vx;
		aTear.y += aTear.vy;
		aTear.travelled += std::sqrt(aTear.vx * aTear.vx + aTear.vy * aTear.vy);
		bool aHit = false;
		for (Zombie* aZombie : mBoard->mZombies)
		{
			if (aZombie->mDead || !aZombie->EffectedByDamage(HIT_MASK))
				continue;
			if (Overlaps(aTear.x - 8, aTear.y - 8, 16, 16, aZombie->GetZombieRect()))
			{
				aZombie->TakeDamage(aTear.damage, 0);
				aHit = true;
				break;
			}
		}
		float aTop, aBottom;
		RowBounds(aTear.x, aTop, aBottom);
		bool aOut = aTear.x < 0 || aTear.x > BOARD_WIDTH || aTear.y < aTop - 60 || aTear.y > aBottom + 10;
		if (aHit || aOut || aTear.travelled >= aTear.range)
		{
			aTear.dead = true;
			Play(gA.sTearHit);
		}
	}
	mTears.erase(std::remove_if(mTears.begin(), mTears.end(), [](const Tear& t) { return t.dead && t.fade > 10; }), mTears.end());
}

void IsaacMod::DamageZombiesIn(float theX, float theY, float theW, float theH, int theDamage)
{
	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (aZombie->mDead || !aZombie->EffectedByDamage(HIT_MASK))
			continue;
		if (Overlaps(theX, theY, theW, theH, aZombie->GetZombieRect()))
			aZombie->TakeDamage(theDamage, 0);
	}
}

void IsaacMod::UpdateBeams()
{
	// The Minecraft mod's laser: 12 ticks long, damaging on 8 of them, piercing everything in its way
	static const int DAMAGE_TICKS[] = {0, 3, 5, 6, 7, 9, 10, 11};
	for (Beam& aBeam : mBeams)
	{
		// it follows Isaac's mouth
		aBeam.x = mX;
		aBeam.y = mY - TEAR_HEIGHT;
		float aTop, aBottom;
		RowBounds(mX, aTop, aBottom);
		switch (aBeam.dir)
		{
		case 0: aBeam.length = aBottom + 20 - aBeam.y; break;
		case 2: aBeam.length = aBeam.y - (aTop - 70); break;
		case 1: aBeam.length = BOARD_WIDTH - aBeam.x; break;
		default: aBeam.length = aBeam.x; break;
		}
		aBeam.length = std::max(0.0f, aBeam.length);
		for (int t : DAMAGE_TICKS)
		{
			if (aBeam.age == t * MC)
			{
				const float aHalf = 28.0f;
				if (aBeam.dir == 1) DamageZombiesIn(aBeam.x, aBeam.y - aHalf, aBeam.length, aHalf * 2, aBeam.damage);
				else if (aBeam.dir == 3) DamageZombiesIn(aBeam.x - aBeam.length, aBeam.y - aHalf, aBeam.length, aHalf * 2, aBeam.damage);
				else if (aBeam.dir == 0) DamageZombiesIn(aBeam.x - aHalf, aBeam.y, aHalf * 2, aBeam.length, aBeam.damage);
				else DamageZombiesIn(aBeam.x - aHalf, aBeam.y - aBeam.length, aHalf * 2, aBeam.length, aBeam.damage);
			}
		}
		aBeam.age++;
	}
	mBeams.erase(std::remove_if(mBeams.begin(), mBeams.end(), [](const Beam& b) { return b.age >= BRIM_LIFETIME; }), mBeams.end());
}

void IsaacMod::UpdateBombs()
{
	std::vector<Bomb> aExploding;
	for (Bomb& aBomb : mBombs)
	{
		if (--aBomb.fuse <= 0)
			aExploding.push_back(aBomb);
	}
	mBombs.erase(std::remove_if(mBombs.begin(), mBombs.end(), [](const Bomb& b) { return b.fuse <= 0; }), mBombs.end());
	for (const Bomb& aBomb : aExploding)
		Explode(aBomb.x, aBomb.y);
	for (Blast& aBlast : mBlasts)
		aBlast.age++;
	mBlasts.erase(std::remove_if(mBlasts.begin(), mBlasts.end(), [](const Blast& b) { return b.age > 40; }), mBlasts.end());
}

void IsaacMod::Explode(float theX, float theY)
{
	Play(gA.sBoom[mRand() % 3]);
	mApp->AddPvzpParticle(theX, theY - 30, Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0), ParticleEffect::PARTICLE_POWIE);
	mBoard->ShakeBoard(3, -4);
	mBlasts.push_back({theX, theY, 0});
	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (aZombie->mDead || !aZombie->EffectedByDamage(HIT_MASK))
			continue;
		Rect aRect = aZombie->GetZombieRect();
		float aCX = std::clamp(theX, (float)aRect.mX, (float)(aRect.mX + aRect.mWidth));
		float aCY = std::clamp(theY - 30, (float)aRect.mY, (float)(aRect.mY + aRect.mHeight));
		if ((aCX - theX) * (aCX - theX) + (aCY - theY + 30) * (aCY - theY + 30) <= BOMB_RADIUS * BOMB_RADIUS)
			aZombie->TakeDamage(BOMB_DAMAGE, 0);
	}
	// bombs hurt Isaac too: a whole heart
	if (mDeathTimer < 0 && std::hypot(mX - theX, mY - theY) <= BOMB_RADIUS * 0.75f)
	{
		if (mInvincible <= 0)
			Hurt(2);
	}
}

void IsaacMod::UpdateDamage()
{
	if (mInvincible > 0 || mDeathTimer >= 0)
		return;
	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (aZombie->mDead || aZombie->IsDeadOrDying() || !aZombie->EffectedByDamage(HIT_MASK))
			continue;
		if (!Overlaps(mX - 14, mY - 50, 28, 46, aZombie->GetZombieRect()))
			continue;
		// half a heart; the big ones (Gargantuars, Dr. Zomboss) take a whole heart
		bool aBig = aZombie->mZombieType == ZombieType::ZOMBIE_GARGANTUAR || aZombie->mZombieType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR ||
			aZombie->mZombieType == ZombieType::ZOMBIE_BOSS;
		Hurt(aBig ? 2 : 1);
		return;
	}
}

void IsaacMod::Hurt(int theHalves)
{
	mInvincible = INVINCIBLE_TICKS;
	// Holy Mantle: blocks the hit, then gone for a minute
	if (mRun.Has(IsaacUpgrade::HOLY_MANTLE) && mTick >= mMantleReadyAt)
	{
		mMantleReadyAt = mTick + MANTLE_RECHARGE;
		Play(gA.sMantle);
		return;
	}
	int aBlackUsed = mRun.TakeDamage(theHalves);
	for (int i = 0; i < aBlackUsed; i++)
	{
		// a black heart running out hurts every zombie on the lawn
		for (Zombie* aZombie : mBoard->mZombies)
			if (!aZombie->mDead && aZombie->EffectedByDamage(HIT_MASK))
				aZombie->TakeDamage(BLACK_HEART_DAMAGE, 0);
	}
	mHurtTimer = HURT_TICKS;
	if (mRun.TotalHealth() <= 0)
	{
		Die();
		return;
	}
	Play(gA.sHurt[mRand() % 3]);
}

void IsaacMod::Die()
{
	mDeathTimer = 0;
	Play(gA.sDie[mRand() % 2]);
	// the run is over: next level starts a new one
	IsaacRun aFresh;
	aFresh.mSeed = mRand();
	aFresh.Save(mSavePath);
	mRun = aFresh;
	mRun.mRedHearts = 0;   // still dead for this level
}

// ----------------------------------------------------------------------------------------------------
//  Pickups
// ----------------------------------------------------------------------------------------------------
void IsaacMod::OnZombieKilled(int theX, int theY)
{
	if (!mSpawned)
		return;
	std::uniform_real_distribution<float> aRoll(0.0f, 1.0f);
	if (aRoll(mRand) >= DROP_CHANCE)
		return;
	float r = aRoll(mRand);
	IsaacPickup aType;
	if (r < 0.02f)
		aType = IsaacPickup::PILL;
	else if (r < 0.45f)
	{
		float c = aRoll(mRand);
		aType = c < DIME_CHANCE ? IsaacPickup::DIME : c < DIME_CHANCE + NICKEL_CHANCE ? IsaacPickup::NICKEL : IsaacPickup::PENNY;
	}
	else if (r < 0.80f)
		aType = aRoll(mRand) < HALF_HEART_CHANCE ? IsaacPickup::HALF_RED_HEART : IsaacPickup::RED_HEART;
	else
		aType = IsaacPickup::BOMB;
	AddDrop(aType, (float)theX, (float)theY);
}

void IsaacMod::AddDrop(IsaacPickup theType, float theX, float theY)
{
	Drop aDrop;
	aDrop.type = theType;
	aDrop.variant = 0;
	if (theType == IsaacPickup::PILL)
	{
		// only 6 of each pill colour per run; once they're all out, a penny instead
		mRun.mPillsSpawned.resize(PILL_COLOURS, 0);
		std::vector<int> aFree;
		for (int c = 0; c < PILL_COLOURS; c++)
			if (mRun.mPillsSpawned[c] < MAX_PER_COLOUR)
				aFree.push_back(c);
		if (aFree.empty())
			aDrop.type = IsaacPickup::PENNY;
		else
		{
			aDrop.variant = aFree[mRand() % aFree.size()];
			mRun.mPillsSpawned[aDrop.variant]++;
		}
	}
	std::uniform_real_distribution<float> aSpread(-1.5f, 1.5f);
	aDrop.x = theX;
	aDrop.y = theY;
	aDrop.vx = aSpread(mRand);
	aDrop.vy = -4.0f;
	float aTop, aBottom;
	RowBounds(theX, aTop, aBottom);
	aDrop.groundY = std::clamp(theY + 40.0f, aTop, aBottom);
	aDrop.age = 0;
	aDrop.collected = -1;
	aDrop.dead = false;
	mDrops.push_back(aDrop);
}

void IsaacMod::UpdateDrops()
{
	for (size_t i = 0; i < mDrops.size(); i++)
	{
		Drop& aDrop = mDrops[i];
		aDrop.age++;
		if (aDrop.collected >= 0)
		{
			if (++aDrop.collected > 8 * MC)
				aDrop.dead = true;
			continue;
		}
		if (aDrop.y < aDrop.groundY || aDrop.vy < 0)
		{
			aDrop.vy += 0.25f;
			aDrop.x += aDrop.vx;
			aDrop.y = std::min(aDrop.y + aDrop.vy, aDrop.groundY + 0.01f);
			if (aDrop.y >= aDrop.groundY && aDrop.vy > 0)
			{
				aDrop.y = aDrop.groundY;
				aDrop.vy = 0;
				aDrop.vx = 0;
			}
			aDrop.x = std::clamp(aDrop.x, (float)LAWN_XMIN, (float)BOARD_WIDTH - 20.0f);
			continue;
		}
		if (aDrop.collected == -2 || mDeathTimer >= 0 || std::hypot(aDrop.x - mX, aDrop.y - mY) > 36.0f)
			continue;

		bool aTaken = true;
		switch (aDrop.type)
		{
		case IsaacPickup::PENNY: case IsaacPickup::NICKEL: case IsaacPickup::DIME:
			mRun.mCoins = std::min(MAX_PICKUPS, mRun.mCoins + (aDrop.type == IsaacPickup::DIME ? 20 : aDrop.type == IsaacPickup::NICKEL ? 5 : 1));
			Play(aDrop.type == IsaacPickup::DIME ? gA.sDime : aDrop.type == IsaacPickup::NICKEL ? gA.sNickel : gA.sPenny);
			break;
		case IsaacPickup::KEY:
			mRun.mKeys = std::min(MAX_PICKUPS, mRun.mKeys + 1);
			Play(gA.sKey);
			break;
		case IsaacPickup::BOMB:
			mRun.mBombs = std::min(MAX_PICKUPS, mRun.mBombs + 1);
			break;
		case IsaacPickup::RED_HEART: case IsaacPickup::HALF_RED_HEART:
			if (mRun.mRedHearts >= mRun.mMaxRedHearts) { aTaken = false; break; }   // full: leave it
			mRun.AddRedHearts(aDrop.type == IsaacPickup::RED_HEART ? 2 : 1);
			Play(gA.sHeart);
			break;
		case IsaacPickup::SOUL_HEART: case IsaacPickup::BLACK_HEART:
			if (mRun.ExtraHeartRoom() <= 0) { aTaken = false; break; }
			if (aDrop.type == IsaacPickup::SOUL_HEART) { mRun.AddSoulHearts(2); Play(gA.sSoul); }
			else { mRun.AddBlackHearts(2); Play(gA.sHeart); }
			break;
		case IsaacPickup::PILL:
			if (mRun.mPill >= 0)
			{
				// carrying one already: swap them
				Drop aOld = aDrop;
				aOld.variant = mRun.mPill;
				aOld.age = 0;
				aOld.x = mX + 30;
				aOld.y = aOld.groundY = mY;
				aOld.collected = -2;   // can't be picked straight back up
				mRun.mPill = aDrop.variant;
				mDrops.push_back(aOld);
			}
			else
				mRun.mPill = mDrops[i].variant;
			Play(gA.sKey);
			break;
		}
		if (aTaken)
			mDrops[i].collected = 0;
	}
	// a swapped pill becomes collectable again once Isaac steps away
	for (Drop& aDrop : mDrops)
		if (aDrop.collected == -2 && std::hypot(aDrop.x - mX, aDrop.y - mY) > 50.0f)
			aDrop.collected = -1;
	mDrops.erase(std::remove_if(mDrops.begin(), mDrops.end(), [](const Drop& d) { return d.dead; }), mDrops.end());
}

void IsaacMod::TakePill()
{
	if (mRun.mPill < 0 || mDeathTimer >= 0)
		return;
	int aColour = mRun.mPill;
	mRun.mPill = -1;
	IsaacPill aEffect = mRun.PillEffect(aColour);
	if (!mRun.PillKnown(aColour))
		mRun.mPillsIdentified.push_back(aColour);
	Announce(PILL_NAMES[(int)aEffect]);
	ApplyPill(aEffect);
}

void IsaacMod::ApplyPill(IsaacPill thePill)
{
	switch (thePill)
	{
	case IsaacPill::HEALTH_UP: mRun.AddHeartContainer(); break;
	case IsaacPill::HEALTH_DOWN: mRun.RemoveHeartContainer(); break;
	case IsaacPill::SPEED_UP: mRun.mSpeed = std::clamp(mRun.mSpeed + STAT_STEP, 0.1f, 2.0f); break;
	case IsaacPill::SPEED_DOWN: mRun.mSpeed = std::clamp(mRun.mSpeed - STAT_STEP, 0.1f, 2.0f); break;
	case IsaacPill::LUCK_UP: mRun.mLuck += LUCK_STEP; break;
	case IsaacPill::LUCK_DOWN: mRun.mLuck -= LUCK_STEP; break;
	case IsaacPill::RANGE_UP: mRun.mRange += STAT_STEP; break;
	case IsaacPill::RANGE_DOWN: mRun.mRange = std::max(1.0f, mRun.mRange - STAT_STEP); break;
	case IsaacPill::DAMAGE_UP: mRun.mDamage += STAT_STEP; break;
	case IsaacPill::DAMAGE_DOWN: mRun.mDamage = std::max(0.0f, mRun.mDamage - STAT_STEP); break;
	case IsaacPill::BOMBS_ARE_KEY: std::swap(mRun.mBombs, mRun.mKeys); break;
	case IsaacPill::FULL_HEALTH: mRun.mRedHearts = mRun.mMaxRedHearts; break;
	case IsaacPill::EXPLOSIVE_DIARRHEA: mDiarrheaBombs = DIARRHEA_BOMBS; mDiarrheaTimer = 0; break;
	case IsaacPill::BAD_TRIP:
		// a whole heart of damage -- unless that would kill him, then it's Full Health
		if (mRun.TotalHealth() <= 2)
			mRun.mRedHearts = mRun.mMaxRedHearts;
		else
		{
			mRun.TakeDamage(2);
			mHurtTimer = HURT_TICKS;
			Play(gA.sHurt[0]);
		}
		break;
	default: break;
	}
}

// ----------------------------------------------------------------------------------------------------
//  Items
// ----------------------------------------------------------------------------------------------------
int IsaacMod::PickItem()
{
	// like the Minecraft mod: each item once per run, weighted by quality; the meals once nothing's left
	std::vector<int> aPool;
	for (int i = 0; i < (int)IsaacUpgrade::COUNT; i++)
		if (ITEMS[i].meal || std::find(mRun.mItemsSeen.begin(), mRun.mItemsSeen.end(), i) == mRun.mItemsSeen.end())
			aPool.push_back(i);
	bool aOnlyMeals = std::all_of(aPool.begin(), aPool.end(), [](int i) { return ITEMS[i].meal; });
	if (aOnlyMeals)
		return (int)IsaacUpgrade::BREAKFAST + (int)(mRand() % 3);
	int aTotal = 0;
	auto aWeight = [](int i) { return ITEMS[i].meal ? std::max(1, QUALITY_WEIGHT[ITEMS[i].quality] / 3) : QUALITY_WEIGHT[ITEMS[i].quality]; };
	for (int i : aPool) aTotal += aWeight(i);
	int aRoll = (int)(mRand() % (unsigned)std::max(1, aTotal));
	for (int i : aPool)
	{
		aRoll -= aWeight(i);
		if (aRoll < 0) return i;
	}
	return aPool.back();
}

void IsaacMod::SpawnAltar(int theX, int theY)
{
	if (!mSpawned)
		return;
	mAltar = true;
	mAltarTaken = false;
	mAltarX = std::clamp((float)theX, (float)LAWN_XMIN + 40.0f, (float)BOARD_WIDTH - 60.0f);
	float aTop, aBottom;
	RowBounds(mAltarX, aTop, aBottom);
	mAltarY = std::clamp((float)theY + 40.0f, aTop, aBottom);
	mAltarItem = PickItem();
	if (!ITEMS[mAltarItem].meal)
		mRun.mItemsSeen.push_back(mAltarItem);
	Play(gA.sTreasure);
}

void IsaacMod::UpdateAltar()
{
	if (!mAltar || mAltarTaken || mDeathTimer >= 0)
		return;
	if (std::abs(mX - mAltarX) < 30 && std::abs(mY - mAltarY) < 30)
	{
		mAltarTaken = true;
		GiveItem(mAltarItem);
		// and now the level can end: the money bag to click
		mApp->PlayFoley(FoleyType::FOLEY_SPAWN_SUN);
		mBoard->AddCoin((int)mAltarX - 20, (int)mAltarY - 80, CoinType::COIN_AWARD_MONEY_BAG, CoinMotion::COIN_MOTION_COIN);
	}
}

void IsaacMod::GiveItem(int theItem)
{
	if (std::find(mRun.mItems.begin(), mRun.mItems.end(), theItem) == mRun.mItems.end() || ITEMS[theItem].meal)
		mRun.mItems.push_back(theItem);
	switch ((IsaacUpgrade)theItem)
	{
	case IsaacUpgrade::PYRO:
		mRun.mBombs = MAX_PICKUPS;
		break;
	case IsaacUpgrade::STIGMATA: case IsaacUpgrade::BREAKFAST: case IsaacUpgrade::LUNCH: case IsaacUpgrade::DINNER:
		mRun.AddHeartContainer();
		mRun.AddRedHearts(2);
		mRun.mDamage += 0.3f;
		break;
	default:
		break;
	}
	mHoldItem = theItem;
	mHoldTimer = HOLD_TICKS;
	Announce(ITEMS[theItem].name, ITEMS[theItem].desc);
	mRun.Save(mSavePath);
}

// ----------------------------------------------------------------------------------------------------
//  Drawing
// ----------------------------------------------------------------------------------------------------
void IsaacMod::DrawHead(Graphics* g, int theFeetX, int theFeetY)
{
	int aDir = (mLookTimer > 0 || mBrimCharge >= 0 || mBrimFiring > 0) ? mHeadDir : (mMoving ? mBodyDir : 0);
	int aHeadBottom = theFeetY - HEAD_Y;
	int aAnim = (mTick / 20) % 2;

	// costumes worn: per slot only the newest item, drawn in pickup order (the last one on top)
	std::vector<int> aCostumes;
	{
		int aBySlot[3] = {-1, -1, -1};
		for (int aItem : mRun.mItems)
			if (COSTUME_SLOT[aItem] >= 0)
				aBySlot[COSTUME_SLOT[aItem]] = aItem;
		for (int aItem : mRun.mItems)
			if (COSTUME_SLOT[aItem] >= 0 && aBySlot[COSTUME_SLOT[aItem]] == aItem &&
				std::find(aCostumes.begin(), aCostumes.end(), aItem) == aCostumes.end())
				aCostumes.push_back(aItem);
	}

	if (mRun.Has(IsaacUpgrade::BRIMSTONE) && gA.brimHeads)
	{
		// Brimstone heads (128 px cells): rows front / side / back, charge frames across; firing rows below
		int aRow = aDir == 0 ? 0 : aDir == 2 ? 2 : 1;
		bool aMirror = aDir == 3;
		int aCol = 0;
		int aDrawRow = aRow;
		if (mBrimFiring > 0)
		{
			aDrawRow = 3 + aRow;
			aCol = (mTick / 10) % 3;
		}
		else if (mBrimCharge >= 0)
		{
			float aCharge = mBrimCharge / (float)ChargeTicks();
			aCol = aCharge >= 1.0f ? ((mTick / 15) % 2 == 0 ? 6 : 5) : std::min(5, (int)(aCharge * 6));
		}
		Rect aDest(theFeetX - 64, aHeadBottom - 94, 128, 128);
		g->DrawImageMirror(gA.brimHeads, aDest, Rect(aCol * 128, aDrawRow * 128, 128, 128), aMirror);
		for (int aItem : aCostumes)
		{
			Image* aImg = gA.costumeBrim[aItem];
			if (aImg)
				g->DrawImageMirror(aImg, aDest, Rect(((mTick / 20) % COSTUME_FRAMES[aItem]) * 128, aRow * 128, 128, 128), aMirror);
		}
		return;
	}

	int aHead = HEAD_FOR_DIR[aDir] + (mShootFrame > 0 ? 1 : 0);
	int aX = theFeetX - HEAD_W / 2, aY = aHeadBottom - HEAD_H;
	g->DrawImage(gA.isaac, aX, aY, Rect(aHead * 32 * S, 0, HEAD_W, HEAD_H));
	// costume cells are 128x128 with the head frame 32 px in from the left and 64 down
	for (int aItem : aCostumes)
	{
		Image* aImg = gA.costume[aItem];
		if (aImg)
			g->DrawImage(aImg, aX - 32, aY - 64, Rect(aHead * 128, (aAnim % COSTUME_FRAMES[aItem]) * 128, 128, 128));
	}
}

void IsaacMod::DrawIsaac(Graphics* g)
{
	if (!gA.isaac)
		return;
	int aFX = (int)mX, aFY = (int)mY;

	// Holy Mantle glow
	if (mRun.Has(IsaacUpgrade::HOLY_MANTLE) && mTick >= mMantleReadyAt && mDeathTimer < 0 && gA.glow)
		g->DrawImage(gA.glow, aFX - 64, aFY - 30 - 64);

	if (mDeathTimer >= 0)
	{
		const int* aPose = POSE_DEATH[std::min(2, mDeathTimer / DEATH_TICKS)];
		g->DrawImage(gA.isaac, aFX - aPose[2] / 2, aFY - aPose[3], Rect(aPose[0], aPose[1], aPose[2], aPose[3]));
		return;
	}

	// flashing: red after a hit, see-through while invincible
	bool aFlash = mInvincible > 0 && (mInvincible / 6) % 2 == 0;
	if (mHurtTimer > 0 || aFlash)
	{
		g->SetColorizeImages(true);
		g->SetColor(mHurtTimer > 0 ? Color(255, 112, 112) : Color(255, 255, 255, 140));
	}

	if (mHoldTimer > 0 && mHoldItem >= 0)
	{
		// holding the new item up over his head
		g->DrawImage(gA.isaac, aFX - POSE_PICKUP[2] / 2, aFY - POSE_PICKUP[3], Rect(POSE_PICKUP[0], POSE_PICKUP[1], POSE_PICKUP[2], POSE_PICKUP[3]));
		Image* aItemImg = gA.items[mHoldItem];
		if (aItemImg)
			g->DrawImage(aItemImg, aFX - aItemImg->mWidth / 2, aFY - POSE_PICKUP[3] - aItemImg->mHeight + 6);
	}
	else if (mHurtTimer > 0)
	{
		g->DrawImage(gA.isaac, aFX - POSE_HURT[2] / 2, aFY - POSE_HURT[3], Rect(POSE_HURT[0], POSE_HURT[1], POSE_HURT[2], POSE_HURT[3]));
	}
	else
	{
		// body: walking direction (up/down cycle, or the sideways one -- the sheet faces right -- mirrored for left)
		int aFrame = mMoving ? ((int)mWalkAnim) % WALK_FRAMES : 0;
		if (mBodyDir == 2 && mMoving)
			aFrame = WALK_FRAMES - 1 - aFrame;   // walking away plays the cycle backwards
		bool aSide = mMoving && (mBodyDir == 1 || mBodyDir == 3);
		if (aSide)
			g->DrawImageMirror(gA.isaac, aFX - BODY_W / 2, aFY - HBODY_H, Rect(aFrame * BODY_W, HBODY_Y, BODY_W, HBODY_H), mBodyDir == 3);
		else
			g->DrawImage(gA.isaac, aFX - BODY_W / 2, aFY - VBODY_H, Rect(aFrame * BODY_W, VBODY_Y, BODY_W, VBODY_H));
		DrawHead(g, aFX, aFY);
	}
	g->SetColorizeImages(false);
}

void IsaacMod::DrawDrop(Graphics* g, const Drop& theDrop)
{
	if (!gA.pickups)
		return;
	// pickups.png: 128 px cells (2x), feet at (64, 80)
	int aCol = 0, aRow = 0;
	float t = (float)theDrop.age / MC;   // in Minecraft ticks, like the Minecraft mod's animations
	switch (theDrop.type)
	{
	case IsaacPickup::PENNY: case IsaacPickup::NICKEL: case IsaacPickup::DIME:
	{
		int aIdle = theDrop.type == IsaacPickup::PENNY ? 0 : theDrop.type == IsaacPickup::NICKEL ? 3 : 6;
		if (theDrop.collected >= 0)
		{
			if (theDrop.collected > 4 * MC) return;
			aRow = aIdle + 2;
			aCol = std::min(7, theDrop.collected * 8 / (4 * MC));
			if (theDrop.type == IsaacPickup::DIME) { aRow = 7; aCol = std::min(4, theDrop.collected * 5 / (4 * MC)); }
			if (theDrop.type == IsaacPickup::NICKEL) { aRow = 5; aCol = std::min(3, theDrop.collected * 4 / (4 * MC)); }
		}
		else if (theDrop.type != IsaacPickup::DIME && t < 16)
		{
			aRow = aIdle + 1;
			aCol = std::min(7, (int)(t / 16 * 8));
		}
		else
		{
			float aPhase = std::fmod(t - 16, 40.0f);
			aRow = aIdle;
			aCol = (aPhase >= 0 && aPhase < 12) ? std::min(5, (int)(aPhase / 12 * 6)) : 0;
		}
		break;
	}
	case IsaacPickup::KEY: if (theDrop.collected >= 0) return; aCol = 0; aRow = 8; break;
	case IsaacPickup::BOMB: if (theDrop.collected >= 0) return; aCol = 3; aRow = 8; break;
	case IsaacPickup::HALF_RED_HEART: if (theDrop.collected >= 0) return; aCol = 6; aRow = 8; break;
	case IsaacPickup::RED_HEART: case IsaacPickup::SOUL_HEART: case IsaacPickup::BLACK_HEART:
	{
		if (theDrop.collected >= 0) return;
		bool aBeat = std::fmod(t, 20.0f) < 5.0f;   // a heartbeat once a second
		if (theDrop.type == IsaacPickup::RED_HEART) { aCol = aBeat ? 5 : 4; aRow = 8; }
		else if (theDrop.type == IsaacPickup::SOUL_HEART) { aCol = aBeat ? 6 : 0; aRow = 9; }
		else { aCol = aBeat ? 7 : 1; aRow = 9; }
		break;
	}
	case IsaacPickup::PILL:
		if (theDrop.collected >= 0) return;
		aCol = theDrop.variant % 8;
		aRow = 10 + theDrop.variant / 8;
		break;
	}
	g->DrawImage(gA.pickups, (int)theDrop.x - 64, (int)theDrop.y - 80, Rect(aCol * 128, aRow * 128, 128, 128));
}

void IsaacMod::Draw(Graphics* g)
{
	if (!mSpawned)
		return;
	g->PushState();

	// altar with the item bobbing on it
	if (mAltar && gA.altar)
	{
		g->DrawImage(gA.altar, (int)mAltarX - gA.altar->mWidth / 2, (int)mAltarY - gA.altar->mHeight);
		if (!mAltarTaken && mAltarItem >= 0 && gA.items[mAltarItem])
		{
			Image* aImg = gA.items[mAltarItem];
			int aBob = (int)std::lround(std::sin(mTick * 0.05) * 4.0);
			g->DrawImage(aImg, (int)mAltarX - aImg->mWidth / 2, (int)mAltarY - gA.altar->mHeight - aImg->mHeight + 8 + aBob);
		}
	}

	for (const Drop& aDrop : mDrops)
		DrawDrop(g, aDrop);

	// bombs: pulse white faster and faster as the fuse runs down
	for (const Bomb& aBomb : mBombs)
	{
		if (!gA.bomb) break;
		float aDone = 1.0f - aBomb.fuse / (float)BOMB_FUSE;
		float aRate = 1.5f + (7.0f - 1.5f) * aDone;
		float aPhase = std::fmod(aDone * BOMB_FUSE / TICKS_PER_SECOND * aRate, 1.0f);
		Image* aImg = (aPhase < 0.35f && gA.bombWhite) ? gA.bombWhite : gA.bomb;
		g->DrawImage(aImg, (int)aBomb.x - aImg->mWidth / 2, (int)aBomb.y - aImg->mHeight);
	}

	// Isaac: behind his tears when shooting down, in front otherwise
	DrawIsaac(g);

	for (const Tear& aTear : mTears)
	{
		if (!gA.tear) break;
		if (aTear.dead)
		{
			// a little splash: the tear grows and fades
			int aSize = 24 + aTear.fade * 2;
			g->SetColorizeImages(true);
			g->SetColor(Color(255, 255, 255, std::max(0, 255 - aTear.fade * 25)));
			g->DrawImage(gA.tear, (int)aTear.x - aSize / 2, (int)aTear.y - aSize / 2, aSize, aSize);
			g->SetColorizeImages(false);
			continue;
		}
		g->DrawImage(gA.tear, (int)aTear.x - 12, (int)aTear.y - 12);
	}

	for (const Beam& aBeam : mBeams)
	{
		int aLen = (int)aBeam.length;
		if (aLen <= 0) continue;
		const int W = 56, TILE_LEN = 320;   // the laser strip at 2x
		if (aBeam.dir == 1 || aBeam.dir == 3)
		{
			if (!gA.laserSide) continue;
			for (int aDone = 0; aDone < aLen; aDone += TILE_LEN)
			{
				int aPiece = std::min(TILE_LEN, aLen - aDone);
				int aX = aBeam.dir == 1 ? (int)aBeam.x + aDone : (int)aBeam.x - aDone - aPiece;
				g->DrawImage(gA.laserSide, aX, (int)aBeam.y - W / 2, Rect(0, 0, aPiece, W));
			}
		}
		else
		{
			if (!gA.laser) continue;
			for (int aDone = 0; aDone < aLen; aDone += TILE_LEN)
			{
				int aPiece = std::min(TILE_LEN, aLen - aDone);
				int aY = aBeam.dir == 0 ? (int)aBeam.y + aDone : (int)aBeam.y - aDone - aPiece;
				g->DrawImage(gA.laser, (int)aBeam.x - W / 2, aY, Rect(0, 0, W, aPiece));
			}
		}
	}
	g->PopState();
}

void IsaacMod::DrawTextCentered(Graphics* g, const std::string& theText, int theX, int theY)
{
	int aW = g->StringWidth(theText);
	g->SetColor(Color(0, 0, 0, 180));
	g->DrawString(theText, theX - aW / 2 + 2, theY + 2);
	g->SetColor(Color(255, 255, 255));
	g->DrawString(theText, theX - aW / 2, theY);
}

void IsaacMod::DrawHUD(Graphics* g)
{
	if (!mSpawned || !gA.hud)
		return;
	g->PushState();
	const int LEFT = 8, TOP = 92;

	// Hearts: red containers, then soul, then black; two rows of six
	{
		int aIndex = 0;
		auto aHeart = [&](int theCel)
		{
			int aX = LEFT + (aIndex % 6) * 24, aY = TOP + (aIndex / 6) * 22;
			g->DrawImage(gA.hud, aX, aY, Rect(theCel * 32, 0, 30, 28));
			aIndex++;
		};
		for (int i = 0; i < mRun.mMaxRedHearts; i += 2)
			aHeart(mRun.mRedHearts >= i + 2 ? 0 : mRun.mRedHearts == i + 1 ? 1 : 2);
		for (int i = 0; i < mRun.mSoulHearts; i += 2)
			aHeart(mRun.mSoulHearts >= i + 2 ? 3 : 4);
		for (int i = 0; i < mRun.mBlackHearts; i += 2)
			aHeart(mRun.mBlackHearts >= i + 2 ? 5 : 6);
		if (mRun.Has(IsaacUpgrade::HOLY_MANTLE) && mTick >= mMantleReadyAt && gA.mantleHud)
			g->DrawImage(gA.mantleHud, LEFT + (aIndex % 6) * 24 + 4, TOP + (aIndex / 6) * 22 + 2);
	}

	// Coins, bombs, keys with two-digit counts
	g->SetFont(Sexy::FONT_CONTINUUMBOLD14OUTLINE);
	auto aCounter = [&](const Rect& theIcon, int theValue, int theY)
	{
		g->DrawImage(gA.hud, LEFT + (40 - theIcon.mWidth) / 2, theY + (32 - theIcon.mHeight) / 2, theIcon);
		char aBuf[8];
		snprintf(aBuf, sizeof(aBuf), "%02d", theValue);
		g->SetColor(Color(255, 255, 255));
		g->DrawString(aBuf, LEFT + 44, theY + 23);
	};
	int aRows = (mRun.mMaxRedHearts / 2 + (mRun.mSoulHearts + 1) / 2 + (mRun.mBlackHearts + 1) / 2 + 5) / 6;
	int aY = TOP + std::max(1, aRows) * 22 + 12;
	aCounter(Rect(0, 32, 36, 26), mRun.mCoins, aY);
	aCounter(Rect(48, 32, 40, 42), mRun.mBombs, aY + 34);
	aCounter(Rect(128, 80, 28, 40), mRun.mKeys, aY + 72);

	// The pill bottom right, with its name once it's known
	if (mRun.mPill >= 0 && gA.pills)
	{
		int aX = BOARD_WIDTH - 44, aPY = BOARD_HEIGHT - 44;
		g->DrawImage(gA.pills, aX, aPY, Rect(mRun.mPill * 36, 0, 36, 36));
		std::string aName = mRun.PillKnown(mRun.mPill) ? PILL_NAMES[(int)mRun.PillEffect(mRun.mPill)] : "???";
		g->SetColor(Color(255, 255, 255));
		g->DrawString(aName, aX - g->StringWidth(aName) - 6, aPY + 24);
	}

	// Announcements (item names, pills) across the middle
	if (mAnnounceTimer > 0)
	{
		g->SetFont(Sexy::FONT_BRIANNETOD32);
		DrawTextCentered(g, mAnnounce, BOARD_WIDTH / 2, 170);
		if (!mAnnounceSub.empty())
		{
			g->SetFont(Sexy::FONT_BRIANNETOD16);
			DrawTextCentered(g, mAnnounceSub, BOARD_WIDTH / 2, 200);
		}
	}

	// Brimstone charge: a little bar over Isaac's head
	if (mBrimCharge >= 0 && mDeathTimer < 0)
	{
		float aCharge = std::min(1.0f, mBrimCharge / (float)ChargeTicks());
		int aX = (int)mX - 20, aBY = (int)mY - 110;
		g->SetColor(Color(0, 0, 0, 160));
		g->FillRect(aX - 1, aBY - 1, 42, 7);
		g->SetColor(aCharge >= 1.0f ? Color(255, 60, 60) : Color(180, 0, 0));
		g->FillRect(aX, aBY, (int)(40 * aCharge), 5);
	}
	g->PopState();
}
