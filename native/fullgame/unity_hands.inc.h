/* Your hands reaching into the game, touching its objects. Included after unity_room.inc.h (in surface_load.c), whose
 * hand triangles (sm64unity_game_hands, game coordinates) it reads: nothing new for the host to send. There is no
 * grip, only touch.
 *
 * - Touch to collect: a coin, cap or 1-up your hand touches is collected as if Mario had touched it (it is put on
 *   his list of things he collided with this frame, which the game's own interactions then handle). Not stars or
 *   keys: those are Mario's to reach.
 * - Squash with your palm: a hand slapped down onto a Goomba (or anything Mario can stomp) from above squashes it, as
 *   Mario's stomp does: it is attacked from above, and its own code does the rest (flattened, its coin dropped).
 *
 * UNITY_HANDS_TOUCH turns touch-to-collect off. */
#include "game/interaction.h"
#include "engine/math_util.h"

#define UNITY_HANDS_TOUCH 1

extern int unity_mario_carried(void);

static void unity_hand_vertex(const s16 *triangles, int triangle, int corner, float out[3]) {
    const s16 *v = triangles + triangle * 9 + corner * 3;
    out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
}

/* A point on a triangle (its nine corner coordinates) from its two barycentric weights (on b and on c) */
static void unity_corners_point(const float *t, float u, float v, float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = t[i] + u * (t[3 + i] - t[i]) + v * (t[6 + i] - t[i]);
}

/* Is it a floor (as read_surface_data decides, from its winding), and over it, where on it (u, v) and how high? */
static int unity_hand_floor_under(const s16 *triangles, int triangle, float x, float z, float *u, float *v, float *height) {
    float a[3], b[3], c[3];
    unity_hand_vertex(triangles, triangle, 0, a);
    unity_hand_vertex(triangles, triangle, 1, b);
    unity_hand_vertex(triangles, triangle, 2, c);
    float nx = (b[1] - a[1]) * (c[2] - b[2]) - (b[2] - a[2]) * (c[1] - b[1]);
    float ny = (b[2] - a[2]) * (c[0] - b[0]) - (b[0] - a[0]) * (c[2] - b[2]);
    float nz = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
    float n = sqrtf(nx * nx + ny * ny + nz * nz);
    if (n < 1e-3f || ny / n <= 0.01f) return 0;
    /* barycentric in the flat (x, z) plane */
    float e1x = b[0] - a[0], e1z = b[2] - a[2], e2x = c[0] - a[0], e2z = c[2] - a[2];
    float det = e1x * e2z - e2x * e1z;
    if (fabsf(det) < 1e-3f) return 0;
    float px = x - a[0], pz = z - a[2];
    float uu = (px * e2z - e2x * pz) / det;
    float vv = (e1x * pz - px * e1z) / det;
    if (uu < 0 || vv < 0 || uu + vv > 1) return 0;
    *u = uu; *v = vv;
    *height = a[1] + uu * (b[1] - a[1]) + vv * (c[1] - a[1]);
    return 1;
}

/* The closest point on a triangle to p (Ericson, Real-Time Collision Detection 5.1.5) */
static void unity_closest_on_triangle(const float p[3], const float a[3], const float b[3], const float c[3], float out[3]) {
    float ab[3], ac[3], ap[3], bp[3], cp[3];
    for (int i = 0; i < 3; i++) { ab[i] = b[i] - a[i]; ac[i] = c[i] - a[i]; ap[i] = p[i] - a[i]; }
#define UDOT(x, y) ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
    float d1 = UDOT(ab, ap), d2 = UDOT(ac, ap);
    if (d1 <= 0 && d2 <= 0) { vec3f_copy(out, (float *) a); return; }
    for (int i = 0; i < 3; i++) bp[i] = p[i] - b[i];
    float d3 = UDOT(ab, bp), d4 = UDOT(ac, bp);
    if (d3 >= 0 && d4 <= d3) { vec3f_copy(out, (float *) b); return; }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float t = d1 / (d1 - d3);
        for (int i = 0; i < 3; i++) out[i] = a[i] + t * ab[i];
        return;
    }
    for (int i = 0; i < 3; i++) cp[i] = p[i] - c[i];
    float d5 = UDOT(ab, cp), d6 = UDOT(ac, cp);
    if (d6 >= 0 && d5 <= d6) { vec3f_copy(out, (float *) c); return; }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float t = d2 / (d2 - d6);
        for (int i = 0; i < 3; i++) out[i] = a[i] + t * ac[i];
        return;
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (int i = 0; i < 3; i++) out[i] = b[i] + t * (c[i] - b[i]);
        return;
    }
    float denom = 1.f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    for (int i = 0; i < 3; i++) out[i] = a[i] + ab[i] * v + ac[i] * w;
#undef UDOT
}

static int unity_object_usable(struct Object *o) {
    /* (far from Mario is fine: it's out of sight unless a diorama draws it, and moves all the same) */
    return o != gMarioObject && (o->activeFlags & ACTIVE_FLAG_ACTIVE)
        && (o->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) && !(o->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE);
}

/* The hands' bounds this frame, to pass over objects nowhere near them */
static float unity_hand_min[3], unity_hand_max[3];
static int unity_hand_bounds(void) {
    if (unity_game_hand_count <= 0) return 0;
    for (int i = 0; i < 3; i++) { unity_hand_min[i] = 1e9f; unity_hand_max[i] = -1e9f; }
    for (int i = 0; i < unity_game_hand_count * 3; i++) {
        const s16 *v = unity_game_hand_triangles + i * 3;
        for (int j = 0; j < 3; j++) {
            if (v[j] < unity_hand_min[j]) unity_hand_min[j] = v[j];
            if (v[j] > unity_hand_max[j]) unity_hand_max[j] = v[j];
        }
    }
    return 1;
}
static int unity_near_hands(struct Object *o, float margin) {
    float bottom = o->oPosY - o->hitboxDownOffset, top = bottom + o->hitboxHeight;
    return o->oPosX + margin >= unity_hand_min[0] && o->oPosX - margin <= unity_hand_max[0]
        && o->oPosZ + margin >= unity_hand_min[2] && o->oPosZ - margin <= unity_hand_max[2]
        && top + margin >= unity_hand_min[1] && bottom - margin <= unity_hand_max[1];
}

/* ---- Touch to collect ---- */

static int unity_is_1up(struct Object *o) {
    const BehaviorScript *b = o->behavior;
    return b == segmented_to_virtual(bhv1Up) || b == segmented_to_virtual(bhv1upWalking)
        || b == segmented_to_virtual(bhv1upRunningAway) || b == segmented_to_virtual(bhv1upSliding)
        || b == segmented_to_virtual(bhv1upJumpOnApproach) || b == segmented_to_virtual(bhvHidden1up)
        || b == segmented_to_virtual(bhvHidden1upInPole);
}
static int unity_collectable(struct Object *o) {
    if (o->oIntangibleTimer != 0 || (o->oInteractStatus & INT_STATUS_INTERACTED)) return 0;
    if (o->oInteractType & (INTERACT_COIN | INTERACT_CAP)) return 1;
    return unity_is_1up(o);
}

static int unity_hand_touches(struct Object *o) {
    float radius = o->hitboxRadius > 40.f ? o->hitboxRadius : 40.f;
    float height = o->hitboxHeight > 40.f ? o->hitboxHeight : 40.f;
    float bottom = o->oPosY - o->hitboxDownOffset, top = bottom + height;
    float centre[3] = { o->oPosX, (bottom + top) * 0.5f, o->oPosZ };
    for (int i = 0; i < unity_game_hand_count; i++) {
        float a[3], b[3], c[3], p[3];
        unity_hand_vertex(unity_game_hand_triangles, i, 0, a);
        unity_hand_vertex(unity_game_hand_triangles, i, 1, b);
        unity_hand_vertex(unity_game_hand_triangles, i, 2, c);
        unity_closest_on_triangle(centre, a, b, c, p);
        float dx = p[0] - centre[0], dz = p[2] - centre[2];
        if (dx * dx + dz * dz <= radius * radius && p[1] >= bottom - 10.f && p[1] <= top + 10.f) return 1;
    }
    return 0;
}

/* ---- Squash with your palm ---- */

/* The hands' triangles last frame, to tell how fast each part of them is coming down */
static s16 unity_last_hand_triangles[UNITY_HAND_SURFACES*9];
static int unity_last_hand_count;
static void unity_hands_remember(void) {
    unity_last_hand_count = unity_game_hand_count;
    if (unity_game_hand_count > 0)
        memcpy(unity_last_hand_triangles, unity_game_hand_triangles, unity_game_hand_count * 9 * sizeof(s16));
}

#define UNITY_SLAP_SPEED 12.f /* game units a frame, down: a slap, not a hand lowered gently onto it (which it climbs) */
/* How far a part of a hand can have gone in a frame and still be found again: a slap covers a few hundred units, and
 * can go right through something (and the ground under it) between two frames */
#define UNITY_SLAP_TRAVEL 800.f
/* Came down through its top between last frame and this one, fast enough? */
static int unity_slap_through(float then, float now, float top) {
    return then >= top - 10.f && now <= top && then - now >= UNITY_SLAP_SPEED && then - now <= UNITY_SLAP_TRAVEL;
}
static int unity_stompable(struct Object *o) {
    return (o->oInteractType & (INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 | INTERACT_KOOPA))
        && o->oIntangibleTimer == 0 && !(o->oInteractStatus & INT_STATUS_INTERACTED);
}

/* Where a point of a hand triangle (at u, v on it) was last frame: on the triangle then nearest it. 0 if none was */
static int unity_last_point(const s16 *now, float u, float v, float out[3]) {
    int was = -1;
    float nearest = UNITY_SLAP_TRAVEL;
    for (int k = 0; k < unity_last_hand_count; k++) {
        const s16 *l = unity_last_hand_triangles + k * 9;
        float d = 0;
        for (int i = 0; i < 9; i++) d += (float) (now[i] - l[i]) * (now[i] - l[i]);
        d = sqrtf(d / 3);
        if (d < nearest) { nearest = d; was = k; }
    }
    if (was < 0) return 0;
    float last[9];
    for (int i = 0; i < 9; i++) last[i] = unity_last_hand_triangles[was * 9 + i];
    unity_corners_point(last, u, v, out);
    return 1;
}

/* Does a part of a hand over it come down through its top this frame, fast? */
static int unity_hand_slaps(struct Object *o) {
    float radius = o->hitboxRadius > 40.f ? o->hitboxRadius : 40.f;
    float height = o->hitboxHeight > 40.f ? o->hitboxHeight : 40.f;
    float top = o->oPosY - o->hitboxDownOffset + height;
    /* its middle and round its edge, under the palm */
    float spots[5][2] = { { 0, 0 }, { 0.6f, 0 }, { -0.6f, 0 }, { 0, 0.6f }, { 0, -0.6f } };
    for (int t = 0; t < unity_game_hand_count; t++) {
        for (int s = 0; s < 5; s++) {
            float u, v, h, then[3];
            float x = o->oPosX + spots[s][0] * radius, z = o->oPosZ + spots[s][1] * radius;
            if (!unity_hand_floor_under(unity_game_hand_triangles, t, x, z, &u, &v, &h)) continue;
            if (!unity_last_point(unity_game_hand_triangles + t * 9, u, v, then)) continue;
            if (unity_slap_through(then[1], h, top)) return 1;
        }
    }
    return 0;
}

static void unity_hands_squash(void) {
    if ((gTimeStopState & TIME_STOP_ACTIVE) || unity_room_depth || !unity_hand_bounds()) return;
    if (unity_last_hand_count <= 0) return;
    static const int lists[] = { OBJ_LIST_PUSHABLE, OBJ_LIST_GENACTOR, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_DEFAULT };
    for (int l = 0; l < (int) (sizeof(lists) / sizeof(lists[0])); l++) {
        struct ObjectNode *head = &gObjectLists[lists[l]];
        for (struct ObjectNode *node = head->next; node != head; node = node->next) {
            struct Object *o = (struct Object *) node;
            /* (a slap can have gone right past it, below it, by now) */
            if (!unity_object_usable(o) || !unity_stompable(o) || !unity_near_hands(o, UNITY_SLAP_TRAVEL)) continue;
            if (!unity_hand_slaps(o)) continue;
            /* as Mario's stomp (interaction.c, attack_object with INT_HIT_FROM_ABOVE) */
            o->oInteractStatus = ATTACK_FROM_ABOVE + (INT_STATUS_INTERACTED | INT_STATUS_WAS_ATTACKED);
        }
    }
}

/* After detect_object_collisions: what your hand touches, Mario touches, so his interactions (or a 1-up's own
 * check for him) collect it this frame */
void unity_hands_touch(void) {
    unity_hands_squash();
    if (!UNITY_HANDS_TOUCH || !gMarioObject || unity_mario_carried() || unity_room_depth) return;
    if ((gTimeStopState & TIME_STOP_ACTIVE) || !unity_hand_bounds()) return;
    static const int lists[] = { OBJ_LIST_LEVEL, OBJ_LIST_GENACTOR, OBJ_LIST_DEFAULT };
    for (int l = 0; l < (int) (sizeof(lists) / sizeof(lists[0])); l++) {
        struct ObjectNode *head = &gObjectLists[lists[l]];
        for (struct ObjectNode *node = head->next; node != head; node = node->next) {
            struct Object *o = (struct Object *) node;
            if (gMarioObject->numCollidedObjs >= 4) return;
            if (!unity_object_usable(o) || !unity_collectable(o) || !unity_near_hands(o, 80.f)) continue;
            if (obj_check_if_collided_with_object(o, gMarioObject) || o->numCollidedObjs >= 4) continue;
            if (!unity_hand_touches(o)) continue;
            gMarioObject->collidedObjs[gMarioObject->numCollidedObjs++] = o;
            o->collidedObjs[o->numCollidedObjs++] = gMarioObject;
            gMarioObject->collidedObjInteractTypes |= o->oInteractType;
            o->collidedObjInteractTypes |= gMarioObject->oInteractType;
        }
    }
}

/* At the end of the objects' frame (before unload_deactivated_objects), once everything has moved */
void unity_hands_after_objects(void) {
    unity_hands_remember(); /* (for next frame: slaps) */
}
