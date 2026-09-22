#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "globalobjects_api.h"
#include <overlays/actors/ovl_Bg_Icefloe/z_bg_icefloe.h>
#include <overlays/actors/ovl_En_Arrow/z_en_arrow.h>
#include <libc/math.h>

#define ICEFLOE_MAX_TRACKED_INSTANCES 32

// Function declarations for functions from z_bg_icefloe.c
void BgIcefloe_Init(Actor* thisx, PlayState* play);
void BgIcefloe_Destroy(Actor* thisx, PlayState* play);
void BgIcefloe_Update(Actor* thisx, PlayState* play);

void func_80AC4A80(BgIcefloe* this, PlayState* play);
void func_80AC4C18(BgIcefloe* this);
void func_80AC4D2C(BgIcefloe* this, PlayState* play);
void func_80AC4C34(BgIcefloe* this, PlayState* play);
void func_80AC4CF0(BgIcefloe* this);

// Function declaration for function from z_en_arrow.c
void func_8088B6B0(EnArrow* this, PlayState* play);

// Function declaration for function from z_malloc.c
void* ZeldaArena_Malloc(size_t size);

// Function declarations for functions from z_actor.c
void Actor_AddToCategory(ActorContext* actorCtx, Actor* actor, u8 actorCategory);
void Actor_Init(Actor* actor, PlayState* play);
ActorProfile* Actor_LoadOverlay(ActorContext* actorCtx, s16 index);
void Actor_FreeOverlay(ActorOverlay* entry);

// Function declarations for new functions related to ice floe instance management.
static s32 BgIcefloe_CountActiveInstances(void);
static void BgIcefloe_CompactSpawnList(void);
static BgIcefloe* BgIcefloe_GetOldestNonMeltingInstance(void);
static void BgIcefloe_EnforceMaxInstances(PlayState* play);

// Function declarations for new functions related to ice floe global actor management.
void BgIcefloe_DynaPolyActor_LoadMesh(Actor* thisx, PlayState* play);
static s32 BgIcefloe_GetObjectSlot(PlayState* play);
Actor* BgIceFloe_Actor_SpawnAsChildAndCutscene(ActorContext* actorCtx, PlayState* play, s16 index, f32 x, f32 y, f32 z, s16 rotX, s16 rotY, s16 rotZ, s32 params, u32 csId, u32 halfDaysBits, Actor* parent);

// Tracks all active ice floes so the runtime limit can change dynamically.
static BgIcefloe* sSpawnedInstances[ICEFLOE_MAX_TRACKED_INSTANCES] = { NULL };
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
        if (sSpawnedInstances[i] != NULL && sSpawnedInstances[i]->actionFunc != func_80AC4D2C) {
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
static BgIcefloe* BgIcefloe_GetOldestNonMeltingInstance(void) {
    for (s32 i = 0; i < sSpawnedCount; i++) {
        if (sSpawnedInstances[i] != NULL && sSpawnedInstances[i]->actionFunc != func_80AC4D2C) {
            return sSpawnedInstances[i];
        }
    }

    return NULL;
}

// Enforces the current runtime cap and trims excess floes if needed.
static void BgIcefloe_EnforceMaxInstances(PlayState* play) {
    // Config makes sure this value is between 0 and 10.
    s32 maxInstances = (s32)recomp_get_config_u32("max_instances");

    // Only evaluate once per frame.
    if (sLastLimitCheckFrame == play->gameplayFrames) {
        return;
    }
    sLastLimitCheckFrame = play->gameplayFrames;

    BgIcefloe_CompactSpawnList();

    // Keep removing oldest active floes until we're back under the cap.
    while (BgIcefloe_CountActiveInstances() > maxInstances) {
        BgIcefloe* oldest = BgIcefloe_GetOldestNonMeltingInstance();
        if (oldest == NULL) {
            break;
        }
        func_80AC4CF0(oldest);
    }
}

// Loads the collision mesh for the Icefloe dyna poly actor.
void BgIcefloe_DynaPolyActor_LoadMesh(Actor* thisx, PlayState* play) {
    BgIcefloe* this = (BgIcefloe*)thisx;
    void* obj;
    CollisionHeader* col;

    // Get the globally loaded Icefloe object.
    obj = GlobalObjects_getGlobalObject(OBJECT_ICEFLOE);

    // Get the collision header from the global object.
    col = SEGMENTED_TO_GLOBAL_PTR(obj, (CollisionHeader*)0x06000C90);

    // Resolve the collision header's segmented pointers.
    col->vtxList = SEGMENTED_TO_GLOBAL_PTR(obj, col->vtxList);
    col->polyList = SEGMENTED_TO_GLOBAL_PTR(obj, col->polyList);
    col->surfaceTypeList = SEGMENTED_TO_GLOBAL_PTR(obj, col->surfaceTypeList);
    col->bgCamList = SEGMENTED_TO_GLOBAL_PTR(obj, col->bgCamList);

    // Register the collision mesh.
    this->dyna.bgId = DynaPoly_SetBgActor(
        play,
        &play->colCtx.dyna,
        &this->dyna.actor,
        col
    );
}

RECOMP_PATCH void BgIcefloe_Init(Actor* thisx, PlayState* play) {
    BgIcefloe* this = (BgIcefloe*)thisx;

    Actor_ProcessInitChain(&this->dyna.actor, sInitChain);
    DynaPolyActor_Init(&this->dyna, 0);
    BgIcefloe_DynaPolyActor_LoadMesh(&this->dyna.actor, play);

    BgIcefloe_CompactSpawnList();

    // Register this floe in the tracking list.
    if (sSpawnedCount < ICEFLOE_MAX_TRACKED_INSTANCES) {
        sSpawnedInstances[sSpawnedCount++] = this;
    }

    this->dyna.actor.world.pos.y = this->dyna.actor.home.pos.y + 10.0f;
    func_80AC4A80(this, play);

    BgIcefloe_EnforceMaxInstances(play);
}

RECOMP_PATCH void func_80AC4C18(BgIcefloe* this) {
    this->timer = 0;
    this->actionFunc = func_80AC4C34;
}

RECOMP_PATCH void func_80AC4C34(BgIcefloe* this, PlayState* play) {
    WaterBox* waterBox;

    // Timer now counts upward instead of downward, which allows for dynamic lifetime adjustments.
    this->timer++;

    // 0 = infinite lifetime.
    u32 infinite_lifetime = (recomp_get_config_u32("icefloe_infinite_lifetime") == 0);

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
    if (!WaterBox_GetSurface1_2(play, &play->colCtx, this->dyna.actor.world.pos.x, this->dyna.actor.world.pos.z,
                                &this->dyna.actor.home.pos.y, &waterBox)) {
        func_80AC4CF0(this);
    } else {
        this->dyna.actor.world.pos.y =
            (Math_SinF(this->timer * (M_PIf / 30)) * 3.0f) + (this->dyna.actor.home.pos.y + 10.0f);
    }
}

RECOMP_PATCH void BgIcefloe_Update(Actor* thisx, PlayState* play) {
    BgIcefloe* this = (BgIcefloe*)thisx;

    // React to config changes during gameplay.
    BgIcefloe_EnforceMaxInstances(play);

    if (!Play_InCsMode(play)) {
        this->actionFunc(this, play);
    }
}

RECOMP_PATCH void BgIcefloe_Destroy(Actor* thisx, PlayState* play) {
    BgIcefloe* this = (BgIcefloe*)thisx;

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

// Retrieves the object slot for the Icefloe object, creating a synthetic slot if necessary.
static s32 BgIcefloe_GetObjectSlot(PlayState* play) {
    ObjectContext* objectCtx = &play->objectCtx;
    void* object;
    s32 slot;

    // Get the globally loaded Icefloe object.
    object = GlobalObjects_getGlobalObject(OBJECT_ICEFLOE);
    if (object == NULL) {
        return OBJECT_SLOT_NONE;
    }

    // Check for an existing slot.
    for (slot = objectCtx->numEntries; slot < ARRAY_COUNT(objectCtx->slots); slot++) {
        if (objectCtx->slots[slot].id == OBJECT_ICEFLOE) {
            return slot;
        }
    }

    // No room for a synthetic slot, return OBJECT_SLOT_NONE.
    if (objectCtx->numEntries >= ARRAY_COUNT(objectCtx->slots)) {
        return OBJECT_SLOT_NONE;
    }

    // Add the global object as a synthetic slot.
    slot = objectCtx->numEntries;

    objectCtx->slots[slot].id = OBJECT_ICEFLOE;
    objectCtx->slots[slot].segment = object;

    // Don't increment numEntries as this isn't a scene-loaded object.
    return slot;
}

Actor* BgIceFloe_Actor_SpawnAsChildAndCutscene(ActorContext* actorCtx, PlayState* play, s16 index, f32 x, f32 y, f32 z, s16 rotX,
                                     s16 rotY, s16 rotZ, s32 params, u32 csId, u32 halfDaysBits, Actor* parent) {
    Actor* actor;
    ActorProfile* profile;
    s32 objectSlot;
    ActorOverlay* overlayEntry;

    if (actorCtx->totalLoadedActors >= 255) {
        return NULL;
    }

    profile = Actor_LoadOverlay(actorCtx, index);
    if (profile == NULL) {
        return NULL;
    }

    objectSlot = BgIcefloe_GetObjectSlot(play);
    if (objectSlot <= OBJECT_SLOT_NONE) {
        Actor_FreeOverlay(&gActorOverlayTable[index]);
        return NULL;
    }

    actor = ZeldaArena_Malloc(profile->instanceSize);
    if (actor == NULL) {
        Actor_FreeOverlay(&gActorOverlayTable[index]);
        return NULL;
    }

    overlayEntry = &gActorOverlayTable[index];
    if (overlayEntry->vramStart != NULL) {
        overlayEntry->numLoaded++;
    }

    bzero(actor, profile->instanceSize);
    actor->overlayEntry = overlayEntry;
    actor->id = profile->id;
    actor->flags = profile->flags;

    // No need to check for profile->id == ACTOR_EN_PART

    actor->objectSlot = objectSlot;

    actor->init = profile->init;
    actor->destroy = profile->destroy;
    actor->update = profile->update;
    actor->draw = profile->draw;

    if (parent != NULL) {
        actor->room = parent->room;
        actor->parent = parent;
        parent->child = actor;
    } else {
        actor->room = play->roomCtx.curRoom.num;
    }

    actor->home.pos.x = x;
    actor->home.pos.y = y;
    actor->home.pos.z = z;
    actor->home.rot.x = rotX;
    actor->home.rot.y = rotY;
    actor->home.rot.z = rotZ;
    actor->params = params & 0xFFFF;
    actor->csId = csId & 0x7F;

    if (actor->csId == 0x7F) {
        actor->csId = CS_ID_NONE;
    }

    if (halfDaysBits != 0) {
        actor->halfDaysBits = halfDaysBits;
    } else {
        actor->halfDaysBits = HALFDAYBIT_ALL;
    }

    Actor_AddToCategory(actorCtx, actor, profile->type);

    {
        uintptr_t prevSeg = gSegments[0x06];

        Actor_Init(actor, play);
        gSegments[0x06] = prevSeg;
    }

    return actor;
}

RECOMP_PATCH void func_8088AA98(EnArrow* this, PlayState* play) {
    WaterBox* waterBox;
    f32 sp50 = this->actor.world.pos.y;
    Vec3f sp44;
    f32 temp_f0;

    if (WaterBox_GetSurface1(play, &play->colCtx, this->actor.world.pos.x, this->actor.world.pos.z, &sp50, &waterBox) &&
        (this->actor.world.pos.y < sp50) && !(this->actor.bgCheckFlags & BGCHECKFLAG_WATER)) {
        this->actor.bgCheckFlags |= BGCHECKFLAG_WATER;

        Math_Vec3f_Diff(&this->actor.world.pos, &this->actor.home.pos, &sp44);

        if (sp44.y != 0.0f) {
            temp_f0 = sqrtf(SQ(sp44.x) + SQ(sp44.z));
            if (temp_f0 != 0.0f) {
                temp_f0 = (((sp50 - this->actor.home.pos.y) / sp44.y) * temp_f0) / temp_f0;
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

        if ((this->actor.params == ARROW_TYPE_ICE) || (this->actor.params == ARROW_TYPE_FIRE)) {
            if ((this->actor.params == ARROW_TYPE_ICE) && (func_8088B6B0 != this->actionFunc)) {
                if (recomp_get_config_u32("allow_anywhere") == 0)
                {
                    // Allow Icefloe to spawn in any scene.
                    BgIceFloe_Actor_SpawnAsChildAndCutscene(&play->actorCtx, play, ACTOR_BG_ICEFLOE, sp44.x, sp44.y, sp44.z, 0, 0, 0, 300, CS_ID_NONE, HALFDAYBIT_ALL, NULL);
                } else 
                {
                    // Icefloe will only spawn in vanilla-allowed scenes.
                    Actor_Spawn(&play->actorCtx, play, ACTOR_BG_ICEFLOE, sp44.x, sp44.y, sp44.z, 0, 0, 0, 300);
                }

                Actor_Kill(&this->actor);
                return;
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
