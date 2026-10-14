// =====================================================================================================
//  ISAAC MOD -- Isaac from The Binding of Isaac as a plant in Plants vs. Zombies
//
//  One Isaac per level, controlled like in the Minecraft mod (2D, top-down style):
//    WASD        walk anywhere on the lawn            Arrow keys   shoot tears up / down / left / right
//    E           drop a bomb (2 second fuse)          Q            take the pill you're carrying
//  Hearts (red / soul / black), coins, bombs and keys are shown on the left; the pill bottom right.
//  Zombies drop TBOI pickups with the Minecraft mod's mob loot table, and the levels that used to give
//  a new plant give an item ("upgrade") on an altar instead. Everything carries over from level to
//  level until Isaac dies, which ends the run (like TBOI). Sprites and sounds live in the 'isaac' folder
//  next to the game.
// =====================================================================================================
#ifndef __ISAACMOD_H__
#define __ISAACMOD_H__

#include <cstdint>
#include <string>
#include <vector>
#include <random>
#include "../ConstEnums.h"

class Board;
class LawnApp;
class Zombie;
namespace Sexy { class Graphics; class Image; }

enum class IsaacPickup : int
{
	PENNY, NICKEL, DIME, KEY, RED_HEART, HALF_RED_HEART, SOUL_HEART, BLACK_HEART, BOMB, PILL
};

enum class IsaacUpgrade : int
{
	HOLY_MANTLE, BRIMSTONE, PYRO, SKATOLE, STIGMATA, BREAKFAST, LUNCH, DINNER, COUNT
};

enum class IsaacPill : int
{
	HEALTH_UP, HEALTH_DOWN, SPEED_UP, SPEED_DOWN, LUCK_UP, LUCK_DOWN, BOMBS_ARE_KEY, EXPLOSIVE_DIARRHEA,
	FULL_HEALTH, BAD_TRIP, RANGE_UP, RANGE_DOWN, DAMAGE_UP, DAMAGE_DOWN, COUNT
};

/** Everything that carries over from level to level (saved in isaac_run.txt). Reset when Isaac dies. */
struct IsaacRun
{
	int mCoins = 0, mKeys = 0, mBombs = 1;
	int mRedHearts = 6, mMaxRedHearts = 6, mSoulHearts = 0, mBlackHearts = 0;   // in half hearts
	float mSpeed = 1.0f, mTears = 2.73f, mDamage = 3.5f, mRange = 6.5f, mShotSpeed = 1.0f, mLuck = 0.0f;
	std::vector<int> mItems;            // upgrades held, in pickup order
	std::vector<int> mItemsSeen;        // upgrades that already appeared this run (each only once, meals aside)
	int mPill = -1;                     // pill colour carried, -1 = none
	uint32_t mSeed = 1;                 // decides which pill colour does what
	std::vector<int> mPillsIdentified;  // colours whose effect is known
	std::vector<int> mPillsSpawned;     // how many of each colour dropped (max 6 each)

	static constexpr int MAX_HEART_HALVES = 24;   // two rows of six hearts
	int  ExtraHeartRoom() const { return MAX_HEART_HALVES - mMaxRedHearts - mSoulHearts - mBlackHearts; }
	int  TotalHealth() const { return mRedHearts + mSoulHearts + mBlackHearts; }
	bool Has(IsaacUpgrade theUpgrade) const;
	void AddHeartContainer();
	void RemoveHeartContainer();
	void AddRedHearts(int theHalves);
	void AddSoulHearts(int theHalves);
	void AddBlackHearts(int theHalves);
	int  TakeDamage(int theHalves);     // returns how many black heart halves were used up
	IsaacPill PillEffect(int theColour) const;
	bool PillKnown(int theColour) const;

	void Load(const std::string& thePath);
	void Save(const std::string& thePath) const;
};

class IsaacMod
{
public:
	explicit IsaacMod(Board* theBoard);
	~IsaacMod();

	/** Should Isaac be in this level at all (not in the Zen Garden, Vasebreaker, I, Zombie...)? */
	static bool		EnabledFor(LawnApp* theApp);
	/** Adventure mode with Isaac: no new plants (only the Peashooter); levels give items instead. */
	static bool		NoNewPlants(LawnApp* theApp);
	/** Would this Adventure level have given a new plant? */
	static bool		LevelGivesPlant(int theLevel);

	void			Update();                       // every game tick while the level is being played
	void			Draw(Sexy::Graphics* g);        // Isaac, tears, bombs, pickups, altar (over the lawn)
	void			DrawHUD(Sexy::Graphics* g);     // hearts, coins, bombs, keys, pill, announcements
	void			OnZombieKilled(int theX, int theY);
	/** The level's last zombie died on a level that gives a plant: put an item altar there instead. */
	void			SpawnAltar(int theX, int theY);
	bool			Active() const { return mSpawned; }

private:
	struct Tear { float x, y, vx, vy, travelled, range; int damage; bool dead; int fade; };
	struct Bomb { float x, y; int fuse; bool isaacs; };
	struct Drop { IsaacPickup type; int variant; float x, y, vx, vy, groundY; int age; int collected; bool dead; };
	struct Beam { int dir; float x, y, length; int age; int damage; };
	struct Blast { float x, y; int age; };

	Board*			mBoard;
	LawnApp*		mApp;
	IsaacRun		mRun;
	std::mt19937	mRand;
	std::string		mSavePath;

	bool			mSpawned = false;
	float			mX = 0, mY = 0, mVelX = 0, mVelY = 0;   // feet, in board pixels
	int				mBodyDir = 0;                         // 0 down, 1 right, 2 up, 3 left
	int				mHeadDir = 0;
	float			mWalkAnim = 0;
	bool			mMoving = false;
	int				mFireCooldown = 0;
	int				mShootFrame = 0;                      // ticks left showing the "crying" head
	int				mLookTimer = 0;                       // ticks left looking the way he shot
	int				mLastArrow = -1;
	int				mInvincible = 0;                      // ticks of invincibility after a hit
	int				mHurtTimer = 0;
	int				mDeathTimer = -1;                     // >= 0 once he's dying
	int				mHoldTimer = 0;                       // holding a new item over his head
	int				mHoldItem = -1;
	int				mMantleReadyAt = 0;                   // tick the Holy Mantle comes back
	int				mTick = 0;
	int				mBrimCharge = -1;                     // ticks charged, -1 = not charging
	int				mBrimDir = 0;
	int				mBrimFiring = 0;
	int				mDiarrheaBombs = 0, mDiarrheaTimer = 0;
	bool			mKeyE = false, mKeyQ = false;

	std::vector<Tear>	mTears;
	std::vector<Bomb>	mBombs;
	std::vector<Drop>	mDrops;
	std::vector<Beam>	mBeams;
	std::vector<Blast>	mBlasts;
	bool			mAltar = false, mAltarTaken = false;
	float			mAltarX = 0, mAltarY = 0;
	int				mAltarItem = -1;

	std::string		mAnnounce, mAnnounceSub;
	int				mAnnounceTimer = 0;

	void			Spawn();
	void			UpdateMovement();
	void			UpdateShooting();
	void			UpdateTears();
	void			UpdateBombs();
	void			UpdateDrops();
	void			UpdateBeams();
	void			UpdateDamage();
	void			UpdateAltar();
	void			Hurt(int theHalves);
	void			Die();
	void			Explode(float theX, float theY);
	void			AddDrop(IsaacPickup theType, float theX, float theY);
	void			TakePill();
	void			ApplyPill(IsaacPill thePill);
	void			GiveItem(int theItem);
	int				PickItem();
	void			Announce(const std::string& theTitle, const std::string& theSub = "");
	int				TearDamage() const;
	int				ChargeTicks() const;
	void			RowBounds(float theX, float& theTop, float& theBottom);
	bool			KeyDown(int theKey) const;
	void			Play(int theSound);
	void			DamageZombiesIn(float theX, float theY, float theW, float theH, int theDamage);

	void			DrawIsaac(Sexy::Graphics* g);
	void			DrawHead(Sexy::Graphics* g, int theFeetX, int theFeetY);
	void			DrawDrop(Sexy::Graphics* g, const Drop& theDrop);
	void			DrawTextCentered(Sexy::Graphics* g, const std::string& theText, int theX, int theY);
};

#endif
