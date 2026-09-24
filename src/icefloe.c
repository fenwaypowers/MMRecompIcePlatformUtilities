#include "globalobjects_api.h"
#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"
#include <libc/math.h>
#include <overlays/actors/ovl_Bg_Icefloe/z_bg_icefloe.h>
#include <overlays/actors/ovl_En_Arrow/z_en_arrow.h>

#define ICEFLOE_MAX_TRACKED_INSTANCES 32

// Function declarations for functions from z_bg_icefloe.c

void BgIcefloe_Init(Actor *thisx, PlayState *play);
void BgIcefloe_Destroy(Actor *thisx, PlayState *play);
void BgIcefloe_Update(Actor *thisx, PlayState *play);
// Initiates the growth sequence for an ice floe instance.
void func_80AC4A80(BgIcefloe *this, PlayState *play);
void func_80AC4C18(BgIcefloe *this);
// Handles the melting behavior of the ice floe.
void func_80AC4D2C(BgIcefloe *this, PlayState *play);
void func_80AC4C34(BgIcefloe *this, PlayState *play);
// Initiates the melting sequence for an ice floe instance.
void func_80AC4CF0(BgIcefloe *this);

// Function declaration for function from z_en_arrow.c

// Handles the behavior of the arrow when it is in flight and potentially
// colliding with objects.
void func_8088B6B0(EnArrow *this, PlayState *play);

// Function declaration for function from z_object.c

s32 Object_GetSlot(ObjectContext *objectCtx, s16 objectId);

// Function declarations for new functions related to ice floe instance
// management.

static s32 BgIcefloe_CountActiveInstances(void);
static void BgIcefloe_CompactSpawnList(void);
static BgIcefloe *BgIcefloe_GetOldestNonMeltingInstance(void);
static void BgIcefloe_EnforceMaxInstances(PlayState *play);

// Function declarations for new functions related to global object slot
// synthesis.

void BgIcefloe_SynthesizeGlobalObjectSlot(PlayState *play);
void before_func_8088AA98(EnArrow *this, PlayState *play);

// Function declarations for new functions related to enforcing the dynamic
// collision limits.

static void BgIcefloe_GetDynaUsage(PlayState *play, s32 *polyCount,
                                   s32 *vtxCount);
static bool BgIcefloe_CanSpawn(PlayState *play);

extern CollisionHeader gIcefloePlatformCol;

// Tracks all active ice floes so the runtime limit can change dynamically.
static BgIcefloe *sSpawnedInstances[ICEFLOE_MAX_TRACKED_INSTANCES] = {NULL};

// Tracks the number of currently spawned ice floe instances.
static s32 sSpawnedCount = 0;

// Prevents the limit check from running more than once per frame.
static u32 sLastLimitCheckFrame = 0;

static InitChainEntry sInitChain[] = {
    ICHAIN_VEC3F_DIV1000(scale, 0, ICHAIN_STOP),
};

// Counts only floes that are still active, not ones already melting.
static s32 BgIcefloe_CountActiveInstances(void) {
  s32 count = 0;

  for (s32 i = 0; i < sSpawnedCount; i++) {
    if (sSpawnedInstances[i] != NULL &&
        sSpawnedInstances[i]->actionFunc != func_80AC4D2C) {
      count++;
    }
  }

  return count;
}

// Removes NULL gaps so the list stays compact.
static void BgIcefloe_CompactSpawnList(void) {
  s32 write = 0;

  for (s32 read = 0; read < ICEFLOE_MAX_TRACKED_INSTANCES; read++) {
    if (sSpawnedInstances[read] != NULL) {
      sSpawnedInstances[write++] = sSpawnedInstances[read];
    }
  }

  for (s32 i = write; i < ICEFLOE_MAX_TRACKED_INSTANCES; i++) {
    sSpawnedInstances[i] = NULL;
  }

  sSpawnedCount = write;
}

// Returns the oldest floe that is not already melting.
static BgIcefloe *BgIcefloe_GetOldestNonMeltingInstance(void) {
  for (s32 i = 0; i < sSpawnedCount; i++) {
    if (sSpawnedInstances[i] != NULL &&
        sSpawnedInstances[i]->actionFunc != func_80AC4D2C) {
      return sSpawnedInstances[i];
    }
  }

  return NULL;
}

// Returns the oldest floe regardless of its melting state.
static BgIcefloe *BgIcefloe_GetOldestInstance(void) {
  for (s32 i = 0; i < sSpawnedCount; i++) {
    if (sSpawnedInstances[i] != NULL) {
      return sSpawnedInstances[i];
    }
  }

  return NULL;
}

// Enforces the current runtime cap and trims excess floes if needed.
static void BgIcefloe_EnforceMaxInstances(PlayState *play) {
  // Config ensures this value is between 0 and 5.
  s32 maxInstances = (s32)recomp_get_config_u32("max_instances");

  // Only evaluate once per frame.
  if (sLastLimitCheckFrame == play->gameplayFrames) {
    return;
  }
  sLastLimitCheckFrame = play->gameplayFrames;

  BgIcefloe_CompactSpawnList();

  // Keep removing oldest active floes until we're back under the cap.
  while (BgIcefloe_CountActiveInstances() > maxInstances) {
    BgIcefloe *oldest = BgIcefloe_GetOldestNonMeltingInstance();
    if (oldest == NULL) {
      break;
    }
    func_80AC4CF0(oldest);
  }
}

// Initializes a new ice floe instance, loads its mesh, and registers it in the
// tracking list.
RECOMP_PATCH void BgIcefloe_Init(Actor *thisx, PlayState *play) {
  BgIcefloe *this = (BgIcefloe *)thisx;

  Actor_ProcessInitChain(&this->dyna.actor, sInitChain);
  DynaPolyActor_Init(&this->dyna, 0);

  DynaPolyActor_LoadMesh(play, &this->dyna, &gIcefloePlatformCol);

  BgIcefloe_CompactSpawnList();

  // Register this floe in the tracking list.
  if (sSpawnedCount < ICEFLOE_MAX_TRACKED_INSTANCES) {
    sSpawnedInstances[sSpawnedCount++] = this;
  }

  this->dyna.actor.world.pos.y = this->dyna.actor.home.pos.y + 10.0f;
  func_80AC4A80(this, play);

  BgIcefloe_EnforceMaxInstances(play);
}

// Starts the melting process for an ice floe instance.
RECOMP_PATCH void func_80AC4C18(BgIcefloe *this) {
  this->timer = 0;
  this->actionFunc = func_80AC4C34;
}

// Updates the ice floe's state each frame, handling melting and bobbing
// animation.
RECOMP_PATCH void func_80AC4C34(BgIcefloe *this, PlayState *play) {
  WaterBox *waterBox;

  // Timer now counts upward instead of downward, which allows for dynamic
  // lifetime adjustments.
  this->timer++;

  // 0 = infinite lifetime.
  u32 infinite_lifetime =
      (recomp_get_config_u32("icefloe_infinite_lifetime") == 0);

  // Lifetime is configurable in seconds, then converted to frames.
  s32 maxTime = (s32)(recomp_get_config_u32("icefloe_lifetime") * 20);

  // In infinite mode, loop the timer so it stays bounded.
  if (infinite_lifetime) {
    if (this->timer >= 60) {
      this->timer = 0;
    }
  } else if (this->timer >= maxTime) {
    func_80AC4CF0(this);
    return;
  }

  // Melt if the floe is no longer over water.
  if (!WaterBox_GetSurface1_2(play, &play->colCtx, this->dyna.actor.world.pos.x,
                              this->dyna.actor.world.pos.z,
                              &this->dyna.actor.home.pos.y, &waterBox)) {
    func_80AC4CF0(this);
  } else {
    // Bobbing animation for the ice floe on water.
    this->dyna.actor.world.pos.y =
        (Math_SinF(this->timer * (-M_PIf / 30)) * 3.0f) +
        (this->dyna.actor.home.pos.y + 10.0f);
  }
}

// Updates the ice floe actor each frame, enforcing max instances and calling
// its action function.
RECOMP_PATCH void BgIcefloe_Update(Actor *thisx, PlayState *play) {
  BgIcefloe *this = (BgIcefloe *)thisx;

  // React to config changes during gameplay.
  BgIcefloe_EnforceMaxInstances(play);

  if (!Play_InCsMode(play)) {
    this->actionFunc(this, play);
  }
}

// Destroys an ice floe instance, removing it from the tracking list and freeing
// its collision data.
RECOMP_PATCH void BgIcefloe_Destroy(Actor *thisx, PlayState *play) {
  BgIcefloe *this = (BgIcefloe *)thisx;

  DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);

  // Remove this floe from the tracking list and compact the array.
  for (s32 i = 0; i < sSpawnedCount; i++) {
    if (sSpawnedInstances[i] == this) {
      for (s32 j = i; j < sSpawnedCount - 1; j++) {
        sSpawnedInstances[j] = sSpawnedInstances[j + 1];
      }

      sSpawnedInstances[sSpawnedCount - 1] = NULL;
      sSpawnedCount--;
      break;
    }
  }
}

// Synthesizes a slot for the ice floe global object if it isn't already loaded
// in the scene.
void BgIcefloe_SynthesizeGlobalObjectSlot(PlayState *play) {
  void *object;
  s32 slot;

  // Only add a slot if OBJECT_ICEFLOE isn't already loaded for the scene.
  if (Object_GetSlot(&play->objectCtx, OBJECT_ICEFLOE) <= OBJECT_SLOT_NONE) {
    // Get the globally loaded Icefloe object.
    object = GlobalObjects_getGlobalObject(OBJECT_ICEFLOE);
    if (object == NULL) {
      return;
    }

    // Make sure there is room for the synthetic object entry.
    if (play->objectCtx.numEntries >= ARRAY_COUNT(play->objectCtx.slots)) {
      return;
    }

    slot = play->objectCtx.numEntries;
    play->objectCtx.slots[slot].id = OBJECT_ICEFLOE;
    play->objectCtx.slots[slot].segment = object;
    play->objectCtx.numEntries++;
  }
}

// Hook function called before func_8088AA98, used to synthesize the ice floe
// object slot if needed.
RECOMP_HOOK("func_8088AA98")
void before_func_8088AA98(EnArrow *this, PlayState *play) {
  if (recomp_get_config_u32("allow_anywhere") == 0) {
    // If the "allow_anywhere" config is enabled, synthesize an object slot for
    // the global ice floe object.
    BgIcefloe_SynthesizeGlobalObjectSlot(play);
    return;
  }
}

// Handles the arrow entering water, spawning ice floes if necessary, and
// triggering water effects.
RECOMP_PATCH void func_8088AA98(EnArrow *this, PlayState *play) {
  WaterBox *waterBox;
  f32 sp50 = this->actor.world.pos.y;
  Vec3f sp44;
  f32 temp_f0;

  if (WaterBox_GetSurface1(play, &play->colCtx, this->actor.world.pos.x,
                           this->actor.world.pos.z, &sp50, &waterBox) &&
      (this->actor.world.pos.y < sp50) &&
      !(this->actor.bgCheckFlags & BGCHECKFLAG_WATER)) {
    this->actor.bgCheckFlags |= BGCHECKFLAG_WATER;

    Math_Vec3f_Diff(&this->actor.world.pos, &this->actor.home.pos, &sp44);

    if (sp44.y != 0.0f) {
      temp_f0 = sqrtf(SQ(sp44.x) + SQ(sp44.z));
      if (temp_f0 != 0.0f) {
        temp_f0 =
            (((sp50 - this->actor.home.pos.y) / sp44.y) * temp_f0) / temp_f0;
      }
      sp44.x = this->actor.home.pos.x + (sp44.x * temp_f0);
      sp44.y = sp50;
      sp44.z = this->actor.home.pos.z + (sp44.z * temp_f0);
      EffectSsGSplash_Spawn(play, &sp44, NULL, NULL, 0, 300);
    }

    Actor_PlaySfx(&this->actor, NA_SE_EV_DIVE_INTO_WATER_L);

    EffectSsGRipple_Spawn(play, &sp44, 100, 500, 0);
    EffectSsGRipple_Spawn(play, &sp44, 100, 500, 4);
    EffectSsGRipple_Spawn(play, &sp44, 100, 500, 8);

    if ((this->actor.params == ARROW_TYPE_ICE) ||
        (this->actor.params == ARROW_TYPE_FIRE)) {
      if ((this->actor.params == ARROW_TYPE_ICE) &&
          (func_8088B6B0 != this->actionFunc)) {
        if (BgIcefloe_CanSpawn(play)) {
          Actor_Spawn(&play->actorCtx, play, ACTOR_BG_ICEFLOE, sp44.x, sp44.y,
                      sp44.z, 0, 0, 0, 300);
          Actor_Kill(&this->actor);
          return;
        }

        BgIcefloe *oldestFloe = BgIcefloe_GetOldestInstance();
        if (oldestFloe != NULL) {
          if (oldestFloe->actionFunc != func_80AC4D2C) {
            func_80AC4CF0(oldestFloe);
          }
        }
      }

      this->actor.params = ARROW_TYPE_NORMAL;
      this->collider.elem.atDmgInfo.dmgFlags = 0x20;

      if (this->actor.child != NULL) {
        Actor_Kill(this->actor.child);
        return;
      }

      Magic_Reset(play);
    }
  }
}

// Calculates the number of polygons and vertices currently used by dynamic
// collision objects.
static void BgIcefloe_GetDynaUsage(PlayState *play, s32 *polyCount,
                                   s32 *vtxCount) {
  DynaCollisionContext *dyna = &play->colCtx.dyna;

  *polyCount = 0;
  *vtxCount = 0;

  for (s32 bgId = 0; bgId < BG_ACTOR_MAX; bgId++) {
    if (!(dyna->bgActorFlags[bgId] & BGACTOR_IN_USE)) {
      continue;
    }

    if (dyna->bgActorFlags[bgId] & BGACTOR_COLLISION_DISABLED) {
      continue;
    }

    CollisionHeader *col = dyna->bgActors[bgId].colHeader;

    if (col == NULL) {
      continue;
    }

    *polyCount += col->numPolygons;
    *vtxCount += col->numVertices;
  }
}

// Determines if a new ice floe can be spawned based on the current dynamic
// collision usage and limits.
static bool BgIcefloe_CanSpawn(PlayState *play) {
  void *obj;
  CollisionHeader *col;
  DynaCollisionContext *dyna = &play->colCtx.dyna;
  s32 polyCount;
  s32 vtxCount;
  s32 numPolygons = 22;
  s32 numVertices = 13;

  BgIcefloe_GetDynaUsage(play, &polyCount, &vtxCount);

  obj = GlobalObjects_getGlobalObject(OBJECT_ICEFLOE);
  if (obj != NULL) {
    col = SEGMENTED_TO_GLOBAL_PTR(obj, (CollisionHeader *)0x06000C90);
    if (col != NULL) {
      numPolygons = col->numPolygons;
      numVertices = col->numVertices;
    }
  }

  return (polyCount + numPolygons <= dyna->polyListMax) &&
         (vtxCount + numVertices <= dyna->vtxListMax);
}
