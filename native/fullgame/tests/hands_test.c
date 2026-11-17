/* Headless test of your hands touching the game (unity_hands.inc.h), from the hand triangles alone
 * (sm64unity_game_hands): touch a coin to collect it, slap a Goomba to squash it.
 * Run by run_headless_test.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include "sm64.h"
#include "types.h"
#include "object_fields.h"
#include "model_ids.h"
#include "behavior_data.h"
#include "game/object_list_processor.h"
#include "game/spawn_object.h"
#include "game/level_update.h"
#include "game/area.h"
#include "game/object_helpers.h"
#include "game/mario.h"
#include "game/interaction.h"
#include "pc/cliopts.h"

int sm64unity_init(const char *);
int sm64unity_tick(uint16_t, int8_t, int8_t);
int sm64unity_stage(void);
void sm64unity_cursor(int, float, float);
void sm64unity_mr_geometry(int);
int sm64unity_room_mesh(const s16 *, int);
float sm64unity_room_floor(float, float, float);
int sm64unity_render_current(void);
int sm64unity_game_hands(const s16 *, int);
int sm64unity_carry(int, float, float, float, float, float, float);
int sm64unity_carry_mode(void);

static int failures;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); failures++; } printf("\n"); } while (0)

static void tick(uint16_t b) { sm64unity_tick(b, 0, 0); sm64unity_render_current(); }
static void ticks(int n) { for (int i = 0; i < n; i++) tick(0); }

/* A flat rectangle (two floor triangles, wound up) at height y */
static int quad(s16 *t, float x0, float z0, float x1, float z1, float y) {
    s16 v[] = { x0, y, z0, x0, y, z1, x1, y, z1, x0, y, z0, x1, y, z1, x1, y, z0 };
    memcpy(t, v, sizeof(v));
    return 2;
}

/* Through the title screen and the file select into Castle Grounds */
static int into_castle_grounds(int argc, char **argv) {
    gCLIOpts.SkipIntro = 1;
    if (!sm64unity_init(argc > 1 ? argv[1] : ".")) { printf("init failed\n"); return 0; }
    sm64unity_mr_geometry(1);
    for (int i = 0; i < 2000 && sm64unity_stage() != 2; i++) tick(i % 20 < 2 ? START_BUTTON : 0);
    for (int i = 0; i < 2000 && (sm64unity_stage() != 3 || !gMarioObject || gCurrLevelNum != LEVEL_CASTLE_GROUNDS); i++) {
        sm64unity_cursor(1, 62, 77);
        tick(i % 20 < 2 ? A_BUTTON : 0);
    }
    sm64unity_cursor(0, 0, 0);
    if (!gMarioObject || gCurrLevelNum != LEVEL_CASTLE_GROUNDS) { printf("never got into Castle Grounds\n"); return 0; }
    ticks(200);
    printf("Castle Grounds, Mario at %.0f %.0f %.0f\n", gMarioObject->oPosX, gMarioObject->oPosY, gMarioObject->oPosZ);
    return 1;
}

static struct Object *spawn(const BehaviorScript *bhv, int model, float dx, float dz) {
    struct Object *o = spawn_object(gMarioObject, model, bhv);
    o->oPosX = gMarioObject->oPosX + dx; o->oPosY = gMarioObject->oPosY + 50; o->oPosZ = gMarioObject->oPosZ + dz;
    o->oHomeX = o->oPosX; o->oHomeY = o->oPosY; o->oHomeZ = o->oPosZ;
    o->parentObj = o;
    o->oBehParams = 0; o->oBehParams2ndByte = 0;
    return o;
}

/* A palm 160 units square, flat, centred on (x, z) at height y */
static void palm(float x, float y, float z) {
    s16 t[18];
    quad(t, x - 80, z - 80, x + 80, z + 80, y);
    sm64unity_game_hands(t, 2);
}

/* Brought straight down over (x, z) from y + 300 to y - 100, `step` units a frame. 1 if o was gone within 5 s */
static int bring_down(struct Object *o, float x, float y, float z, float step) {
    for (float h = y + 300; h > y - 100; h -= step) { palm(x, h, z); tick(0); }
    sm64unity_game_hands(NULL, 0);
    for (int i = 0; i < 150 && (o->activeFlags & ACTIVE_FLAG_ACTIVE); i++) tick(0);
    return !(o->activeFlags & ACTIVE_FLAG_ACTIVE);
}

int main(int argc, char **argv) {
    if (!into_castle_grounds(argc, argv)) return 1;

    printf("Touch to collect:\n");
    struct Object *coin = spawn(bhvYellowCoin, MODEL_YELLOW_COIN, 400, 0);
    ticks(2);
    s16 coins = gMarioState->numCoins;
    for (int i = 0; i < 10; i++) { palm(coin->oPosX, coin->oPosY + 10, coin->oPosZ); tick(0); }
    sm64unity_game_hands(NULL, 0);
    ticks(5);
    CHECK(gMarioState->numCoins == coins + 1, "a coin your hand touches is Mario's (%d coins, were %d)", gMarioState->numCoins, coins);
    struct Object *far = spawn(bhvYellowCoin, MODEL_YELLOW_COIN, -400, 0);
    ticks(2);
    coins = gMarioState->numCoins;
    for (int i = 0; i < 10; i++) { palm(far->oPosX + 400, far->oPosY + 10, far->oPosZ); tick(0); }
    sm64unity_game_hands(NULL, 0);
    ticks(5);
    CHECK(gMarioState->numCoins == coins && (far->activeFlags & ACTIVE_FLAG_ACTIVE), "one it doesn't touch isn't");

    printf("Not stars or keys (Mario's to reach):\n");
    struct Object *star = spawn(bhvStar, MODEL_STAR, 0, 400);
    ticks(2);
    u32 action = gMarioState->action;
    for (int i = 0; i < 10; i++) { palm(star->oPosX, star->oPosY + 10, star->oPosZ); tick(0); }
    sm64unity_game_hands(NULL, 0);
    ticks(5);
    CHECK((star->oInteractType & INTERACT_STAR_OR_KEY) && (star->activeFlags & ACTIVE_FLAG_ACTIVE)
          && gMarioState->action == action, "a star your hand touches stays where it is");

    printf("Squash with your palm:\n");
    struct Object *g = spawn(bhvGoomba, MODEL_GOOMBA, 0, 900);
    ticks(30);
    CHECK(!bring_down(g, g->oPosX, g->oPosY, g->oPosZ, 4), "lowered gently onto a Goomba, it isn't squashed");
    ticks(30);
    CHECK(bring_down(g, g->oPosX, g->oPosY, g->oPosZ, 40), "slapped down onto it, it is");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures != 0;
}
