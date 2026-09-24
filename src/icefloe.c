#include "modding.h"
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

// Function declaration for function from z_object.c
s32 Object_GetSlot(ObjectContext* objectCtx, s16 objectId);

// Function declarations for new functions related to ice floe instance management.
static s32 BgIcefloe_CountActiveInstances(void);
static void BgIcefloe_CompactSpawnList(void);
static BgIcefloe* BgIcefloe_GetOldestNonMeltingInstance(void);
static void BgIcefloe_EnforceMaxInstances(PlayState* play);

// Function declarations for new functions related to global object slot synthesis.
void BgIcefloe_SynthesizeGlobalObjectSlot(PlayState* play);
void before_func_8088AA98(EnArrow* this, PlayState* play);

extern CollisionHeader gIcefloePlatformCol;

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
        BgIcefloe* oldest = BgIcefloe_GetOldestNonMeltingInstance();
        if (oldest == NULL) {
            break;
        }
        func_80AC4CF0(oldest);
    }
}

RECOMP_PATCH void BgIcefloe_Init(Actor* thisx, PlayState* play) {
    BgIcefloe* this = (BgIcefloe*)thisx;

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
        // Bobbing animation for the ice floe on water.
        this->dyna.actor.world.pos.y =
            (Math_SinF(this->timer * (-M_PIf / 30)) * 3.0f) + (this->dyna.actor.home.pos.y + 10.0f);
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

// Synthesizes a slot for the ice floe global object if it isn't already loaded in the scene.
void BgIcefloe_SynthesizeGlobalObjectSlot(PlayState* play) {
    void* object;
    s32 slot;

    // Only add a slot if OBJECT_ICEFLOE isn't already loaded for the scene.
    if (Object_GetSlot(&play->objectCtx, OBJECT_ICEFLOE) <= OBJECT_SLOT_NONE) {
        // Get the globally loaded Icefloe object.
        object = GlobalObjects_getGlobalObject(OBJECT_ICEFLOE);
        if (object == NULL) {
            recomp_printf("IcePlatformUtilities: Failed to get global object for OBJECT_ICEFLOE\n");
            return;
        }

        // Make sure there is room for the synthetic object entry.
        if (play->objectCtx.numEntries >= ARRAY_COUNT(play->objectCtx.slots)) {
            recomp_printf("IcePlatformUtilities: No free object slots for OBJECT_ICEFLOE\n");
            return;
        }

        // Expose the global object through the scene's object context.
        slot = play->objectCtx.numEntries;
        play->objectCtx.slots[slot].id = OBJECT_ICEFLOE;
        play->objectCtx.slots[slot].segment = object;
        play->objectCtx.numEntries++;

        recomp_printf("IcePlatformUtilities: Adding synthetic slot for OBJECT_ICEFLOE in slot=%d, sceneId = %d\n", slot, play->sceneId);
    } else {
        recomp_printf("IcePlatformUtilities: OBJECT_ICEFLOE already has a slot\n");
    }
}

RECOMP_HOOK("func_8088AA98") void before_func_8088AA98(EnArrow* this, PlayState* play) {
    if (recomp_get_config_u32("allow_anywhere") == 0) {
        // If the "allow_anywhere" config is enabled, synthesize an object slot for the global ice floe object.
        BgIcefloe_SynthesizeGlobalObjectSlot(play);
        return;
    }
}
