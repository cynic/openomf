#include <math.h>
#include "controller/ai_controller.h"
#include "game/objects/har.h"
#include "game/objects/scrap.h"
#include "game/objects/projectile.h"
#include "game/protos/intersect.h"
#include "game/protos/object_specializer.h"
#include "game/scenes/arena.h"
#include "game/game_state.h"
#include "game/utils/serial.h"
#include "formats/pilot.h"
#include "resources/af_loader.h"
#include "resources/ids.h"
#include "resources/animation.h"
#include "controller/controller.h"
#include "utils/allocator.h"
#include "utils/log.h"
#include "utils/vec.h"
#include "utils/random.h"

/* times thrown before we AI learns its lesson */
#define MAX_TIMES_THROWN 3
/* times shot before we AI learns its lesson */
#define MAX_TIMES_SHOT 4
/* base likelihood to change movement action (lower is more likely) */
#define BASE_ACT_THRESH 90
/* base timer before we can consider changing movement action */
#define BASE_ACT_TIMER 28
/* base likelihood to keep moving (lower is more likely) */
#define BASE_MOVE_THRESH 16
/* base likelihood to move forwards (lower is more likely) */
#define BASE_FWD_THRESH 50
/* base likelihood to jump while moving forwards (lower is more likely) */
#define BASE_FWD_JUMP_THRESH 76
/* base likelihood to jump while moving backwards (lower is more likely) */
#define BASE_BACK_JUMP_THRESH 82
/* base likelihood to jump while standing still (lower is more likely) */
#define BASE_STILL_JUMP_THRESH 95
/* number of move ticks before bailing on tactic */
#define TACTIC_MOVE_TIMER_MAX 5
/* number of attack attempt ticks before bailing on tactic */
#define TACTIC_ATTACK_TIMER_MAX 2

typedef struct move_stat_t {
    int max_hit_dist;
    int min_hit_dist;
    int value;
    int attempts;
    int consecutive;
    int last_dist;
} move_stat;

typedef struct tactic_state_t {
    int tactic_type;
    int last_tactic;
    int move_type;
    int move_timer;
    int attack_type;
    int attack_id;
    int attack_timer;
    int attack_on;
    int chain_hit_on;
    int chain_hit_tactic;
} tactic_state;

typedef struct ai_t {
    int har_event_hooked;
    int difficulty;
    int act_timer;
    int cur_act;
    int input_lag; // number of ticks to wait per input
    int input_lag_timer;

    // move stats
    af_move *selected_move;
    int last_move_id;
    int move_str_pos;
    move_stat move_stats[70];
    int blocked;
    int thrown; // times thrown by enemy
    int shot; // times shot by enemy

    // tactical state
    tactic_state *tactic;

    sd_pilot *pilot;

    // all projectiles currently on screen (vector of projectile object*)
    vector active_projectiles;
} ai;

enum {
    TACTIC_ESCAPE = 1, // escape from enemy
    TACTIC_TURTLE, // block attacks
    TACTIC_GRAB, // charge and grab enemy
    TACTIC_SPAM, // spam the same attack
    TACTIC_SHOOT, // shoot a projectile
    TACTIC_TRIP, // trip enemy
    TACTIC_QUICK, // quick attack
    TACTIC_CLOSE, // close with the enemy
    TACTIC_FLY, // fly towards the enemy
    TACTIC_PUSH, // spam power moves to push them back
    TACTIC_COUNTER // block then attack
};

enum {
    MOVE_CLOSE = 1, // close distance
    MOVE_AVOID, // gain distance
    MOVE_JUMP, // jump towards
    MOVE_BLOCK // hold block
};

enum {
    ATTACK_ID = 1, // attack by id
    ATTACK_TRIP, // trip attack
    ATTACK_GRAB, // grab/throw attack
    ATTACK_LIGHT, // light/quick attack
    ATTACK_HEAVY, // heavy/power attack
    ATTACK_JUMP, // jumping attack
    ATTACK_RANGED, // ranged attack
    ATTACK_CHARGE, // charge attack
    ATTACK_PUSH, // push attack
    ATTACK_RANDOM // random attack
};

enum {
    RANGE_CRAMPED,
    RANGE_CLOSE,
    RANGE_MID,
    RANGE_FAR
};

int char_to_act(int ch, int direction) {
    switch(ch) {
        case '8': return ACT_UP;
        case '2': return ACT_DOWN;
        case '6':
            if(direction == OBJECT_FACE_LEFT) {
                return ACT_LEFT;
            } else {
                return ACT_RIGHT;
            }
        case '4':
            if(direction == OBJECT_FACE_LEFT) {
                return ACT_RIGHT;
            } else {
                return ACT_LEFT;
            }
        case '7':
            if(direction == OBJECT_FACE_LEFT) {
                return ACT_UP|ACT_RIGHT;
            } else {
                return ACT_UP|ACT_LEFT;
            }
        case '9':
            if(direction == OBJECT_FACE_LEFT) {
                return ACT_UP|ACT_LEFT;
            } else {
                return ACT_UP|ACT_RIGHT;
            }
        case '1':
            if(direction == OBJECT_FACE_LEFT) {
                return ACT_DOWN|ACT_RIGHT;
            } else {
                return ACT_DOWN|ACT_LEFT;
            }
        case '3':
            if(direction == OBJECT_FACE_LEFT) {
                return ACT_DOWN|ACT_LEFT;
            } else {
                return ACT_DOWN|ACT_RIGHT;
            }
        case 'K': return ACT_KICK;
        case 'P': return ACT_PUNCH;
        case '5': return ACT_STOP;
        default:
            break;
    }
    return ACT_STOP;
}

/** 
 * \brief Convenience method to roll '1 in x' chance.
 *
 * \param roll_x An integer indicating number of numbers in roll.
 *
 * \return A boolean indicating whether the roll passed.
 */
bool roll_chance(int roll_x) {
    return roll_x <= 1 ? true : rand_int(roll_x) == 1;
}

/** 
 * \brief Roll chance for pilot preference.
 *
 * \param pref_val The value of the pilot preference (-400 to 400)
 *
 * \return A boolean indicating whether the preference is confirmed.
 */
bool roll_pref(int pref_val) {
    int rand_roll = rand_int(800);
    int pref_thresh = pref_val + 400;
    return rand_roll <= pref_thresh;
}

/** 
 * \brief Determine whether the AI is smart enough to usually go ahead with an action.
 *
 * \param a The AI instance.
 *
 * \return A boolean indicating whether the AI is smart enough.
 */
bool smart_usually(const ai *a) {
    if (a->difficulty == 6) {
        // at highest difficulty 92% chance to be smart
        return !roll_chance(12);
    } else if (a->difficulty >= 3) {
        return roll_chance(7 - a->difficulty);
    } else {
        return false;
    }
}

/** 
 * \brief Determine whether the AI is dumb enough to usually go ahead with an action.
 *
 * \param a The AI instance.
 *
 * \return A boolean indicating whether the AI is dumb enough.
 */
bool dumb_usually(const ai *a) {
    if (a->difficulty == 1) {
        // at lowest difficulty 92% chance to be dumb
        return !roll_chance(12);
    } if (a->difficulty <= 2) {
        return roll_chance(a->difficulty + 1);
    } else {
        return false;
    }
}

/** 
 * \brief Determine whether the AI is smart enough to sometimes go ahead with an action.
 *
 * \param a The AI instance.
 *
 * \return A boolean indicating whether the AI is smart enough.
 */
bool smart_sometimes(const ai *a) {
    if (a->difficulty >= 2) {
        return roll_chance(10 - a->difficulty);
    } else {
        return false;
    }
}

/** 
 * \brief Determine whether the AI is dumb enough to sometimes go ahead with an action.
 *
 * \param a The AI instance.
 *
 * \return A boolean indicating whether the AI is dumb enough.
 */
bool dumb_sometimes(const ai *a) {
    if (a->difficulty <= 2) {
        return roll_chance(a->difficulty + 2);
    } else {
        return false;
    }
}

/** 
 * \brief Determine whether AI will proceed with an action using exponentially scaling roll.
 *
 * \param a The AI instance.
 *
 * \return A boolean indicating whether the AI should proceed with an action.
 */
bool diff_scale(const ai *a) {
    int roll = rand_int(36);
    return roll <= (a->difficulty * a->difficulty);
}

/** 
 * \brief Determine the current range classification.
 *
 * \param ctrl The AI controller instance.
 *
 * \return A boolean indicating range classification.
 */
bool get_enemy_range(const controller *ctrl) {
    object *o = ctrl->har;
    har *h = object_get_userdata(o);
    object *o_enemy = game_state_get_player(o->gs, h->player_id == 1 ? 0 : 1)->har;

    int range_units = abs(o_enemy->pos.x - o->pos.x) / 50;
    switch (range_units) {
        case 0:
        case 1:
            return RANGE_CRAMPED;
        case 2:
            return RANGE_CLOSE;
        case 3:
        case 4:
            return RANGE_MID;
        default:
            return RANGE_FAR;
    }
}

/** 
 * \brief Convenience method to check whether the provided move is a special move.
 *
 * \param move The move instance.
 *
 * \return A boolean indicating whether the move is a special move.
 */
bool is_special_move(const af_move *move) {
    if (
        !str_equal_c(&move->move_string, "K") ||
        !str_equal_c(&move->move_string, "K1") ||
        !str_equal_c(&move->move_string, "K2") ||
        !str_equal_c(&move->move_string, "K3") ||
        !str_equal_c(&move->move_string, "K4") ||
        !str_equal_c(&move->move_string, "K6") ||
        !str_equal_c(&move->move_string, "P") ||
        !str_equal_c(&move->move_string, "P1") ||
        !str_equal_c(&move->move_string, "P2") ||
        !str_equal_c(&move->move_string, "P3") ||
        !str_equal_c(&move->move_string, "P4") ||
        !str_equal_c(&move->move_string, "P6")
    ) {
        return false;
    }
    return true;
}

/** 
 * \brief Convenience method to check whether a HAR has projectiles.
 *
 * \param har_id An integer identifying the HAR.
 *
 * \return Boolean indicating whether HAR supports projectiles.
 */
bool har_has_projectiles(int har_id) {
    switch (har_id) {
        case HAR_JAGUAR:
        case HAR_SHADOW:
        case HAR_ELECTRA:
        case HAR_SHREDDER:
        case HAR_CHRONOS:
        case HAR_NOVA:
            return true;
    }

    return false;
}

/** 
 * \brief Convenience method to check whether a HAR has a charge attack.
 *
 * \param har_id An integer identifying the HAR.
 *
 * \return Boolean indicating whether HAR has a charge attack.
 */
bool har_has_charge(int har_id) {
    switch (har_id) {
        case HAR_JAGUAR:
        case HAR_SHADOW:
        case HAR_KATANA:
        case HAR_FLAIL:
        case HAR_THORN:
        case HAR_PYROS:
        case HAR_ELECTRA:
        case HAR_SHREDDER:
        case HAR_CHRONOS:
        case HAR_GARGOYLE:
            return true;
    }
    return false;
}

/** 
 * \brief Convenience method to check whether a HAR has a push attack.
 *
 * \param har_id An integer identifying the HAR.
 *
 * \return Boolean indicating whether HAR has a push attack.
 */
bool har_has_push(int har_id) {
    switch (har_id) {
        case HAR_JAGUAR:
        case HAR_KATANA:
        case HAR_FLAIL:
        case HAR_THORN:
        case HAR_PYROS:
        case HAR_ELECTRA:
        case HAR_NOVA:
            return true;
    }
    return false;
}

/** 
 * \brief Determine whether the AI would like to use the specified tactic.
 *
 * \param ctrl Controller instance.
 * \param tactic_type An integer identifying the tactic.
 *
 * \return Boolean indicating whether the AI would like to use the tactic..
 */
bool likes_tactic(const controller *ctrl, int tactic_type) {
    ai *a = ctrl->data;

    object *o = ctrl->har;
    har *h = object_get_userdata(o);
    sd_pilot *pilot = a->pilot;

    if (
        (a->tactic->last_tactic == tactic_type && roll_chance(2)) ||
        h->state == STATE_JUMPING
     ) {
        return false;
    }

    bool enemy_close = h->close;
    int enemy_range = get_enemy_range(ctrl);
    bool wall_close = h->is_wallhugging;
        
    switch (tactic_type) {
        case TACTIC_SHOOT:
            if (
                har_has_projectiles(h->id) &&
                (
                    (roll_pref(a->pilot->ap_special)) ||
                    (pilot->att_def && roll_chance(6)) ||
                    (pilot->att_sniper && roll_chance(3)) ||
                    (wall_close && roll_chance(3))
                ) &&
                (
                    (h->id != HAR_SHREDDER && !enemy_close) ||
                    (
                        h->id == HAR_SHREDDER &&
                        (
                            (enemy_range <= RANGE_MID && smart_usually(a)) ||
                            dumb_sometimes(a)
                        )
                    ) // shredder prefers to be close-mid range
                )
            ) {
                return true;
            }
            break;
        case TACTIC_CLOSE:
            if (
                !enemy_close &&
                (
                    (har_has_charge(h->id) && smart_usually(a)) ||
                    (pilot->att_hyper && roll_chance(4)) ||
                    roll_chance(6)
                )
            ) {
                return true;
            }
            break;
        case TACTIC_QUICK:
            if (
                ((pilot->att_hyper || pilot->att_sniper) && roll_chance(5)) ||
                roll_chance(10)
            ) {
                return true;
            }
            break;
        case TACTIC_GRAB:
            if (
                (a->thrown <= MAX_TIMES_THROWN || roll_chance(2)) && (
                    (pilot->att_hyper && roll_chance(3)) ||
                    ((h->id == HAR_FLAIL || h->id == HAR_THORN) && roll_chance(3)) ||
                    roll_chance(6)
                )
            ) {
                return true;
            }
            break;
        case TACTIC_TURTLE:
            if (
                 a->thrown <= MAX_TIMES_THROWN && (
                    (pilot->att_def && roll_chance(3)) ||
                    roll_chance(10)
                )
            ) {
                return true;
            }
            break;
        case TACTIC_COUNTER:
            if (
                a->thrown < MAX_TIMES_THROWN && (
                    (pilot->att_def && roll_chance(3)) ||
                    roll_chance(6)
                )
            ) {
                return true;
            }
            break;
        case TACTIC_ESCAPE:
            if (
                (pilot->att_jump && roll_chance(3)) ||
                roll_chance(6)
            ) {
                return true;
            }
            break;
        case TACTIC_FLY:
            if (
                (
                    roll_pref(a->pilot->pref_jump) ||
                    (h->id == HAR_GARGOYLE || h->id == HAR_PYROS)
                ) &&
                (
                    pilot->att_jump ||
                    wall_close ||
                    roll_chance(4)
                ) 
            ) {
                return true;
            }
            break;
        case TACTIC_PUSH:
            if (
                enemy_range <= RANGE_MID &&
                (
                    (har_has_push(h->id) && smart_usually(a)) ||
                    (pilot->att_def && roll_chance(3)) ||
                    (wall_close && roll_chance(3)) ||
                    roll_chance(6)
                )
            ) {
                return true;
            }
            break;
        case TACTIC_TRIP:
            if (enemy_range <= RANGE_MID && roll_chance(3)) {
                return true;
            }
            break;
        case TACTIC_SPAM:
            if (
                (enemy_close || dumb_usually(a)) &&
                (
                    wall_close || roll_chance(6)
                ) &&
                roll_chance(3)
            ){
                return true;
            }
            break;
    }

    return false;
}

/** 
 * \brief Queue the specified tactic in AI tactical state object.
 *
 * \param ctrl Controller instance.
 * \param tactic_type An integer identifying the tactic.
 *
 * \return Void.
 */
void queue_tactic(controller *ctrl, int tactic_type) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    a->tactic->last_tactic = a->tactic->tactic_type > 0 ? a->tactic->tactic_type : 0;
    a->tactic->tactic_type = tactic_type;

    // log when we queue a tactic
    switch (tactic_type) {
        case TACTIC_GRAB:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mGRAB\e[0m");
            break;
        case TACTIC_TRIP:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mTRIP\e[0m");
            break;
        case TACTIC_QUICK:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mQUICK\e[0m");
            break;
        case TACTIC_CLOSE:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mCLOSE\e[0m");
            break;
        case TACTIC_FLY:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mFLY\e[0m");
            break;
        case TACTIC_SHOOT:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mSHOOT\e[0m");
            break;
        case TACTIC_PUSH:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mPUSH\e[0m");
            break;
        case TACTIC_SPAM:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mSPAM\e[0m");
            break;
        case TACTIC_ESCAPE:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mESCAPE\e[0m");
            break;
        case TACTIC_TURTLE:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mTURTLE\e[0m");
            break;
        case TACTIC_COUNTER:
            DEBUG("\e[33mQueue tactic:\e[0m \e[32mCOUNTER\e[0m");
            break;
    }

    bool enemy_close = h->close;
    bool wall_close = h->is_wallhugging;
    int enemy_range = get_enemy_range(ctrl);

    bool do_charge = false;

    // set move tactic
    switch (tactic_type) {
        // aggressive tactics
        case TACTIC_GRAB:
        case TACTIC_TRIP:
        case TACTIC_QUICK:
        case TACTIC_CLOSE:
            if (enemy_close) {
                a->tactic->move_type = 0;
            } else if (
                (
                    tactic_type == TACTIC_CLOSE ||
                    (tactic_type == TACTIC_QUICK && roll_chance(3))
                ) &&
                smart_usually(a) &&
                har_has_charge(h->id)
            ) {
                // smart AI will try to use charge attacks
                a->tactic->move_type = 0;
                do_charge = true;
            } else if (
                smart_usually(a) &&
                roll_pref(a->pilot->pref_jump)
            ) {
                // smart AI that likes to jump will close via jump
                a->tactic->move_type = MOVE_JUMP;
            } else {
                a->tactic->move_type = MOVE_CLOSE;
            }
            break;
        // jumping tactics
        case TACTIC_FLY:
            a->tactic->move_type = MOVE_JUMP;
            break;
        // ranged tactics
        case TACTIC_SHOOT:
            a->tactic->move_type = (enemy_close && !wall_close) ? MOVE_AVOID : 0;
            break;
        // stalling tactics
        case TACTIC_PUSH:
        case TACTIC_SPAM:
            a->tactic->move_type = 0;
            break;
        // evasive tactics
        case TACTIC_ESCAPE:
            a->tactic->move_type = wall_close? MOVE_JUMP : MOVE_AVOID;
            break;
        // goading tactics
        case TACTIC_TURTLE:
            if (enemy_range == RANGE_CRAMPED) {
                // at this range they might grab/throw so we need to use escape tactic
                a->tactic->move_type = wall_close? MOVE_JUMP : MOVE_AVOID;
            } else {
                a->tactic->move_type = MOVE_BLOCK;
            }
            break;
        case TACTIC_COUNTER:
            a->tactic->move_type = enemy_range > RANGE_CRAMPED ? MOVE_BLOCK : 0;
            break;
    }

    // set tactic move timer
    if (a->tactic->move_type > 0) {
        a->tactic->move_timer = TACTIC_MOVE_TIMER_MAX;
    }

    if (do_charge) {
        // set charge attack
        a->tactic->attack_type = ATTACK_CHARGE;
        a->tactic->attack_id = 0;
    } else {
        // set attack tactics
        switch (tactic_type) {
            // aggressive tactics
            case TACTIC_GRAB:
                a->tactic->attack_type = ATTACK_GRAB;
                a->tactic->attack_id = 0;
                break;
            case TACTIC_TRIP:
                a->tactic->attack_type = ATTACK_TRIP;
                a->tactic->attack_id = 0;
                // if we are jumping we wait for land to trip
                if (a->tactic->move_type == MOVE_JUMP) {
                    a->tactic->attack_on = HAR_EVENT_LAND;
                }
                break;
            case TACTIC_QUICK:
                a->tactic->attack_type = ATTACK_LIGHT;
                a->tactic->attack_id = 0;
                break;
            case TACTIC_FLY:
                // smart AI will try for a jumping attack
                a->tactic->attack_type = smart_usually(a) ? ATTACK_JUMP : 0;
                a->tactic->attack_id = 0;
                break;
            case TACTIC_SHOOT:
                a->tactic->attack_type = ATTACK_RANGED;
                a->tactic->attack_id = 0;
                break;
            case TACTIC_PUSH:
                if (har_has_push(h->id)) { // && smart_sometimes(a)
                    a->tactic->attack_type = ATTACK_PUSH;
                } else {
                    a->tactic->attack_type = ATTACK_HEAVY;
                }
                a->tactic->attack_id = 0;
                break;
            case TACTIC_SPAM:
                if (a->last_move_id > 0) {
                    a->tactic->attack_type = ATTACK_ID;
                    a->tactic->attack_id = a->last_move_id;
                } else {
                    a->tactic->attack_type = ATTACK_LIGHT;
                    a->tactic->attack_id = 0;
                }
                break;
            case TACTIC_COUNTER:
                a->tactic->attack_type = roll_chance(3) ? ATTACK_TRIP : ATTACK_HEAVY;
                // we only wait for block if they're not in range to grab/throw
                if (enemy_range > RANGE_CRAMPED) {
                    a->tactic->attack_on = HAR_EVENT_BLOCK;
                }
                break;
            case TACTIC_CLOSE:
                a->tactic->attack_type = ATTACK_RANDOM;
                a->tactic->attack_id = 0;
                break;
            case TACTIC_ESCAPE:
            case TACTIC_TURTLE:
                a->tactic->attack_type = 0;
                a->tactic->attack_id = 0;
        }
    }

    // set tactic attack timer
    if (a->tactic->attack_type > 0) {
        a->tactic->attack_timer = TACTIC_ATTACK_TIMER_MAX;
    }
}

void reset_tactic_state(ai *a) {
    a->tactic->last_tactic = a->tactic->tactic_type ? a->tactic->tactic_type : 0;
    a->tactic->tactic_type = 0;
    a->tactic->move_type = 0;
    a->tactic->move_timer = 0;
    a->tactic->attack_type = 0;
    a->tactic->attack_id = 0;
    a->tactic->attack_timer = 0;
    a->tactic->attack_on = 0;
    a->tactic->chain_hit_on = 0;
    a->tactic->chain_hit_tactic = 0;
}

/** 
 * \brief Reset the base movement act timer.
 *
 * \param a The AI instance.
 *
 * \return Void.
 */
void reset_act_timer(ai *a) {
    a->act_timer = BASE_ACT_TIMER - (a->difficulty * 2) - rand_int(3);
}

/** 
 * \brief Determine whether a pilot dislikes a move.
 *
 * \param a The AI instance.
 * \param selected_move The move instance.
 *
 * \return A boolean indicating whether move was disliked.
 */
bool dislikes_move(const ai *a, const af_move *move) {
    // check for non-projectile special moves
    if (is_special_move(move)) {
        // pilots with bad special ability dislike special moves
        return !roll_pref(a->pilot->ap_special);
    }

    switch(move->category) {
        case CAT_BASIC:
            // smart AI dislike basic moves
            return smart_usually(a);
        case CAT_LOW:
            // pilots with bad low ability dislike low moves 
            return !roll_pref(a->pilot->ap_low);
        case CAT_MEDIUM:
            // pilots with bad middle ability dislike middle moves
            return !roll_pref(a->pilot->ap_middle);
        case CAT_HIGH:
            // pilots with bad high ability dislike high moves
            return !roll_pref(a->pilot->ap_high);
        case CAT_THROW:
        case CAT_CLOSE:
            // non-hyper pilots with bad throw ability dislike throw moves
            return !a->pilot->att_hyper && !roll_pref(a->pilot->ap_throw);
        case CAT_JUMPING:
            // non-jumper pilots with bad jump ability dislike jump moves
            return !a->pilot->att_jump && !roll_pref(a->pilot->ap_jump);
        case CAT_PROJECTILE:
            // non-sniper pilots with bad special ability dislike projectile moves
            return !a->pilot->att_sniper && !roll_pref(a->pilot->ap_special);
    }

    return false;
}

/** 
 * \brief Determine whether a move is too powerful for AI difficutly.
 *
 * \param a The AI instance.
 * \param selected_move The move instance.
 *
 * \return A boolean indicating whether move is considered too powerful.
 */
bool move_too_powerful(const ai *a, const af_move *move) {
    return is_special_move(move) && dumb_usually(a);
}

int ai_har_event(controller *ctrl, har_event event) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);
    sd_pilot *pilot = a->pilot;
    move_stat *ms;

    bool has_queued_tactic = a->tactic->tactic_type > 0;

    if (has_queued_tactic) {
        switch(event.type) {
            case HAR_EVENT_BLOCK:
                if (
                    a->tactic->tactic_type != TACTIC_COUNTER && 
                    a->tactic->tactic_type != TACTIC_TURTLE && 
                    a->tactic->tactic_type != TACTIC_TRIP && 
                    a->tactic->tactic_type != TACTIC_PUSH &&
                    a->tactic->tactic_type != TACTIC_SPAM &&
                    a->tactic->tactic_type != TACTIC_FLY &&
                    (a->tactic->tactic_type != TACTIC_GRAB || roll_chance(2)) &&
                    (a->tactic->chain_hit_on == 0 || a->tactic->chain_hit_on != event.move->category)
                ) {
                    reset_tactic_state(a);
                    has_queued_tactic = false;
                    DEBUG("\e[90mReset tactic queue: EVENT_BLOCK\e[0m");
                }
                break;
            case HAR_EVENT_TAKE_HIT:
                reset_tactic_state(a);
                has_queued_tactic = false;
                DEBUG("\e[90mReset tactic queue: EVENT_TAKE_HIT\e[0m");
                break;
            case HAR_EVENT_ENEMY_STUN:
                if (
                    a->tactic->tactic_type != TACTIC_GRAB && 
                    a->tactic->tactic_type != TACTIC_CLOSE && 
                    a->tactic->tactic_type != TACTIC_TRIP && 
                    a->tactic->tactic_type != TACTIC_SHOOT
                ) {
                    reset_tactic_state(a);
                    has_queued_tactic = false;
                    DEBUG("\e[90mReset tactic queue: EVENT_ENEMY_STUN\e[0m");
                }
            break;
        }
    }

    switch(event.type) {
        case HAR_EVENT_ENEMY_STUN:
        case HAR_EVENT_BLOCK:
        case HAR_EVENT_LAND:
        case HAR_EVENT_HIT_WALL:
        case HAR_EVENT_TAKE_HIT:
        case HAR_EVENT_RECOVER:
        break;
        case HAR_EVENT_ATTACK:
        case HAR_EVENT_ENEMY_BLOCK:
        case HAR_EVENT_LAND_HIT:
            a->selected_move = NULL;
        break;
    }

    switch(event.type) {
        case HAR_EVENT_LAND_HIT:
            ms = &a->move_stats[event.move->id];

            if (ms->max_hit_dist == -1 || ms->last_dist > ms->max_hit_dist){
                ms->max_hit_dist = ms->last_dist;
            }

            if (ms->min_hit_dist == -1 || ms->last_dist < ms->min_hit_dist){
                ms->min_hit_dist = ms->last_dist;
            }

            ms->value++;
            if(ms->value > 10) {
                ms->value = 10;
            }

            a->last_move_id = event.move->id;

            if (a->tactic->chain_hit_on == event.move->category) {
                DEBUG("\e[33mQueueing chained tactic\e[0m");
                queue_tactic(ctrl, a->tactic->chain_hit_tactic);
                break;
            }

            if (has_queued_tactic || !smart_usually(a)) break;

            if (likes_tactic(ctrl, TACTIC_QUICK)) {
                queue_tactic(ctrl, TACTIC_QUICK);
            } else if (likes_tactic(ctrl, TACTIC_TRIP)) {
                queue_tactic(ctrl, TACTIC_TRIP);
            } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                queue_tactic(ctrl, TACTIC_GRAB);
            } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                queue_tactic(ctrl, TACTIC_PUSH);
            } else if (likes_tactic(ctrl, TACTIC_CLOSE)) {
                queue_tactic(ctrl, TACTIC_CLOSE);
            } else if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                queue_tactic(ctrl, TACTIC_SHOOT);
            } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                queue_tactic(ctrl, TACTIC_TURTLE);
            } else if (likes_tactic(ctrl, TACTIC_SPAM)) {
                queue_tactic(ctrl, TACTIC_SPAM);
            }

            break;

        case HAR_EVENT_ENEMY_BLOCK:
            ms = &a->move_stats[event.move->id];
            if(!a->blocked) {
                a->blocked = 1;
                ms->value--;

                a->last_move_id = event.move->id;

                if (has_queued_tactic || !smart_usually(a)) break;

                if (likes_tactic(ctrl, TACTIC_GRAB)) {
                    queue_tactic(ctrl, TACTIC_GRAB);
                } else if (likes_tactic(ctrl, TACTIC_TRIP)) {
                    queue_tactic(ctrl, TACTIC_TRIP);
                } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                    queue_tactic(ctrl, TACTIC_PUSH);
                } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                    queue_tactic(ctrl, TACTIC_COUNTER);
                } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                    queue_tactic(ctrl, TACTIC_TURTLE);
                } else if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                    queue_tactic(ctrl, TACTIC_ESCAPE);
                } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                    queue_tactic(ctrl, TACTIC_FLY);
                } else if (likes_tactic(ctrl, TACTIC_QUICK)) {
                    queue_tactic(ctrl, TACTIC_QUICK);
                } else if (likes_tactic(ctrl, TACTIC_SPAM)) {
                    queue_tactic(ctrl, TACTIC_SPAM);
                }
            }

            break;

        case HAR_EVENT_BLOCK:

            if (has_queued_tactic && a->tactic->attack_on == HAR_EVENT_BLOCK) {
                // do the attack now
                DEBUG("\e[94mAttempting counter move\e[0m");
                a->tactic->move_timer = 0;
                break;
            } else if (has_queued_tactic || !smart_usually(a)) break;

                if (event.move->category == CAT_PROJECTILE) {
                    if (likes_tactic(ctrl, TACTIC_FLY)) {
                        queue_tactic(ctrl, TACTIC_FLY);
                    } else if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                        queue_tactic(ctrl, TACTIC_SHOOT);
                    } else if (likes_tactic(ctrl, TACTIC_CLOSE)) {
                        queue_tactic(ctrl, TACTIC_CLOSE);
                    } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                        queue_tactic(ctrl, TACTIC_TURTLE);
                    } 
                } else {
                    if (likes_tactic(ctrl, TACTIC_TRIP)) {
                        queue_tactic(ctrl, TACTIC_TRIP);
                    } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                        queue_tactic(ctrl, TACTIC_PUSH);
                    } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                        queue_tactic(ctrl, TACTIC_TURTLE);
                    } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                        queue_tactic(ctrl, TACTIC_GRAB);
                    } else if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                        queue_tactic(ctrl, TACTIC_ESCAPE);
                    } else if (likes_tactic(ctrl, TACTIC_QUICK)) {
                        queue_tactic(ctrl, TACTIC_QUICK);
                    } else if (likes_tactic(ctrl, TACTIC_SPAM)) {
                        queue_tactic(ctrl, TACTIC_SPAM);
                    } 
                }

            break;

        case HAR_EVENT_LAND:

            if (has_queued_tactic && a->tactic->attack_on == HAR_EVENT_LAND && h->state == STATE_STANDING) {
                // do the attack now
                a->tactic->move_timer = 0;
                DEBUG("\e[94mAttempting landing move\e[0m");
                break;
            } else if (has_queued_tactic || !smart_usually(a)) break;

            if (likes_tactic(ctrl, TACTIC_TRIP)) {
                queue_tactic(ctrl, TACTIC_TRIP);
            } else if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                queue_tactic(ctrl, TACTIC_SHOOT);
            } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                queue_tactic(ctrl, TACTIC_TURTLE);
            } else if (likes_tactic(ctrl, TACTIC_QUICK)) {
                queue_tactic(ctrl, TACTIC_QUICK);
            } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                queue_tactic(ctrl, TACTIC_GRAB);
            } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                queue_tactic(ctrl, TACTIC_PUSH);
            } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                queue_tactic(ctrl, TACTIC_COUNTER);
            } else if (likes_tactic(ctrl, TACTIC_CLOSE)) {
                queue_tactic(ctrl, TACTIC_CLOSE);
            }

            break;

        case HAR_EVENT_ATTACK:
            break;

        case HAR_EVENT_HIT_WALL:

            if (has_queued_tactic || !smart_usually(a)) break;

            if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                queue_tactic(ctrl, TACTIC_SHOOT);
            } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                queue_tactic(ctrl, TACTIC_PUSH);
            } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                queue_tactic(ctrl, TACTIC_TURTLE);
            } else if (likes_tactic(ctrl, TACTIC_TRIP)) {
                queue_tactic(ctrl, TACTIC_TRIP);
            } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                queue_tactic(ctrl, TACTIC_FLY);
            } else if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                queue_tactic(ctrl, TACTIC_ESCAPE);
            } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                queue_tactic(ctrl, TACTIC_COUNTER);
            } else if (likes_tactic(ctrl, TACTIC_CLOSE)) {
                queue_tactic(ctrl, TACTIC_CLOSE);
            }

            break;

        case HAR_EVENT_TAKE_HIT:

            // if enemy is cheesing the AI will try to adjust
            if (event.move->category == CAT_THROW || event.move->category == CAT_CLOSE) {
                // keep track of how many times we have been thrown
                a->thrown++;
                if (smart_usually(a) && a->thrown >= MAX_TIMES_THROWN) {
                    DEBUG("\e[33mAI adjusting in response to repeated throws.\e[0m");
                    // turn off defensive personality
                    pilot->att_def = 0;
                    // turn on sniper personality
                    pilot->att_sniper = 1;
                    // turn on jumper personality
                    pilot->att_jump = 1;
                    // favor jumping movement
                    pilot->pref_jump += 50;
                    // favor backwards movement
                    if (pilot->pref_back < 200) pilot->pref_back += 50;
                    if (pilot->pref_fwd > -200) pilot->pref_fwd -= 50;
                }
            } else if (event.move->category == CAT_PROJECTILE) {
                // keep track of how many times we have been shot
                a->shot++;
                if (smart_usually(a) && a->shot >= MAX_TIMES_SHOT) {
                    DEBUG("\e[33mAI adjusting in response to repeated projectiles.\e[0m");
                    // turn off defensive personality
                    pilot->att_def = 0;
                    // turn on aggressive personality
                    pilot->att_hyper = 1;
                    // turn on jumper personality
                    pilot->att_jump = 1;
                    // favor forwards movement
                    if (pilot->pref_fwd < 200) pilot->pref_fwd += 50;
                    if (pilot->pref_back > -200) pilot->pref_back -= 50;
                }
            }

            if (has_queued_tactic || !smart_usually(a)) break;

            if (event.move->category == CAT_THROW || event.move->category == CAT_CLOSE) {
                // distance gaining tactics
                if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                    queue_tactic(ctrl, TACTIC_ESCAPE);
                } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                    queue_tactic(ctrl, TACTIC_PUSH);
                } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                    queue_tactic(ctrl, TACTIC_FLY);
                }
            } else if (event.move->category == CAT_PROJECTILE) {
                // aggressive tactics
                if (likes_tactic(ctrl, TACTIC_CLOSE)) {
                    queue_tactic(ctrl, TACTIC_CLOSE);
                } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                    queue_tactic(ctrl, TACTIC_FLY);
                } else if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                    queue_tactic(ctrl, TACTIC_SHOOT);
                } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                    queue_tactic(ctrl, TACTIC_GRAB);
                }
            } else {
                // defensive tactics
                if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                    queue_tactic(ctrl, TACTIC_COUNTER);
                } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                    queue_tactic(ctrl, TACTIC_TURTLE);
                } else if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                    queue_tactic(ctrl, TACTIC_ESCAPE);
                } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                    queue_tactic(ctrl, TACTIC_PUSH);
                } else if (likes_tactic(ctrl, TACTIC_TRIP)) {
                    queue_tactic(ctrl, TACTIC_TRIP);
                } else if (likes_tactic(ctrl, TACTIC_QUICK)) {
                    queue_tactic(ctrl, TACTIC_QUICK);
                } else if (likes_tactic(ctrl, TACTIC_SPAM)) {
                    queue_tactic(ctrl, TACTIC_SPAM);
                }
            }

            break;
        case HAR_EVENT_RECOVER:

            if (has_queued_tactic || !smart_usually(a)) break;

            if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                queue_tactic(ctrl, TACTIC_SHOOT);
            } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                queue_tactic(ctrl, TACTIC_COUNTER);
            } else if (likes_tactic(ctrl, TACTIC_TURTLE)) {
                queue_tactic(ctrl, TACTIC_TURTLE);
            } else if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                queue_tactic(ctrl, TACTIC_ESCAPE);
            } 

            break;
        case HAR_EVENT_ENEMY_STUN:

            if (has_queued_tactic || !smart_usually(a)) break;

            if (roll_chance(2)) {
                queue_tactic(ctrl, TACTIC_GRAB);
            } else {
                queue_tactic(ctrl, TACTIC_CLOSE);
            }
        default:
            break;
    }

    return 0;
}


void ai_controller_free(controller *ctrl) {
    ai *a = ctrl->data;
    vector_free(&a->active_projectiles);
<<<<<<< HEAD
    omf_free(a);
=======
    free(a->tactic);
    free(a);
>>>>>>> 7287d2c8... AI Rework:
}

/** 
 * \brief Check whether a move is valid and can be initiated.
 *
 * \param move The move instance.
 * \param h The HAR instance.
 *
 * \return A boolean indicating whether the move is valid
 */
bool is_valid_move(const af_move *move, const har *h, bool force_allow_projectile) {
    // If category is any of these, and bot is not close, then
    // do not try to execute any of them. This attempts
    // to make the HARs close up instead of standing in place
    // wawing their hands towards each other. Not a perfect solution.
    switch(move->category) {
        case CAT_CLOSE:
        case CAT_LOW:
        case CAT_MEDIUM:
        case CAT_HIGH:
            // Only allow handwaving if close or jumping
            if(!h->close && h->state != STATE_JUMPING) {
                return false;
            }
    }
    if(move->category == CAT_JUMPING && h->state != STATE_JUMPING) {
        // not jumping but trying to execute a jumping move
        return false;
    }
    if(move->category != CAT_JUMPING && h->state == STATE_JUMPING) {
        // jumping but this move is not a jumping move
        return false;
    }
    if(move->category == CAT_SCRAP && h->state != STATE_VICTORY) {
        return false;
    }
    if(move->category == CAT_DESTRUCTION && h->state != STATE_SCRAP) {
        return false;
    }

    // XXX check for chaining?

    int move_str_len = str_size(&move->move_string);
    char tmp;
    for(int i = 0; i < move_str_len; i++) {
        tmp = str_at(&move->move_string, i);
        if(!((tmp >= '1' && tmp <= '9') || tmp == 'K' || tmp == 'P')) {
            if (force_allow_projectile && move->category == CAT_PROJECTILE) return true; // projectile is always true
            return false;
        }
    }

    if((move->damage > 0
        || move->category == CAT_PROJECTILE
        || move->category == CAT_SCRAP
        || move->category == CAT_DESTRUCTION) && move_str_len > 0) 
    {
        return true;
    }

    return false;
}

/** 
 * \brief Sets the selected move.
 *
 * \param ctrl Controller instance.
 * \param selected_move The move instance.
 *
 * \return Void.
 */
void set_selected_move(controller *ctrl, af_move *selected_move) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    a->move_stats[selected_move->id].attempts++;
    a->move_stats[selected_move->id].consecutive++;

    // do the move
    a->selected_move = selected_move;
    a->move_str_pos = str_size(&selected_move->move_string)-1;
    object *o_enemy = game_state_get_player(o->gs, h->player_id == 1 ? 0 : 1)->har;
    a->move_stats[a->selected_move->id].last_dist = fabsf(o->pos.x - o_enemy->pos.x);
    a->blocked = 0;
    // DEBUG("AI selected move %s", str_c(&selected_move->move_string));
}

/** 
 * \brief Assigns a move by category identifier. Ignores move_stat learning.
 *
 * \param ctrl Controller instance.
 * \param category An integer identifying the desired category of move.
 * \param highest_damage A boolean indicating whether to pick the highest damage move of category.
 *
 * \return A boolean indicating whether move was assigned.
 */
bool assign_move_by_cat(controller *ctrl, int category, bool highest_damage) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    af_move *selected_move = NULL;
    int top_value = 0;

    // Attack
    for(int i = 0; i < 70; i++) {
        af_move *move = NULL;
        if((move = af_get_move(h->af_data, i))) {
            // category filter
            if (category != move->category) {
                continue;
            }

            move_stat *ms = &a->move_stats[i];
            if(is_valid_move(move, h, true)) {
                int value;
                if (highest_damage) {
                    // evaluate the move based purely on damage
                    value = (int) move->damage * 10;
                } else {
                    // evaluate the move based on learning reinforcement
                    value = ms->value + rand_int(10);
                    if (ms->min_hit_dist != -1){
                        if (ms->last_dist < ms->max_hit_dist+5 && ms->last_dist > ms->min_hit_dist+5){
                            value += 2;
                        } else if (ms->last_dist > ms->max_hit_dist+10){
                            value -= 3;
                        }
                    }

                    // smart AI will slightly favor high damage moves
                    if (smart_usually(a)) {
                        value += ((int) move->damage / 4);
                    }

                    value -= ms->attempts/2;
                    value -= ms->consecutive*2;
                }

                if (selected_move == NULL){
                    selected_move = move;
                    top_value = value;
                } else if (value > top_value) {
                    selected_move = move;
                    top_value = value;
                }
            }
        }
    }

    if(selected_move) {
        for(int i = 0; i < 70; i++) {
            a->move_stats[i].consecutive /= 2;
        }

        set_selected_move(ctrl, selected_move);
        return true;
    }


    return false;
}

/** 
 * \brief Assigns a move by move_id.
 *
 * \param ctrl Controller instance.
 * \param move_id An integer identifying the desired move.
 *
 * \return A boolean indicating whether move was assigned.
 */
bool assign_move_by_id(controller *ctrl, int move_id) {
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    for(int i = 0; i < 70; i++) {
        af_move *move = NULL;
        if((move = af_get_move(h->af_data, i))) {
            if(is_valid_move(move, h, true)) {
                // move_id filter
                if (move_id != move->id) {
                    continue;
                }
                
                // DEBUG("=== assign_move_by_id === id %d", move_id);
                set_selected_move(ctrl, move);
                return true;
            }
        }
    }

    return false;
}

/** 
 * \brief Assigns a move by move_id.
 *
 * \param ctrl Controller instance.
 * \param move_id An integer identifying the desired move.
 *
 * \return A boolean indicating whether move was assigned.
 */
int ai_block_har(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);
    object *o_enemy = game_state_get_player(o->gs, h->player_id == 1 ? 0 : 1)->har;
    har *h_enemy = object_get_userdata(o_enemy);

    // XXX TODO get maximum move distance from the animation object
    if(fabsf(o_enemy->pos.x - o->pos.x) < 100 && h_enemy->executing_move &&
       smart_usually(a)) {
        if(har_is_crouching(h_enemy)) {
            a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT);
            controller_cmd(ctrl, a->cur_act, ev);
        } else {
            a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT);
            controller_cmd(ctrl, a->cur_act, ev);
        }
        return 1;
    }
    return 0;
}

int ai_block_projectile(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;

    iterator it;
    object **o_tmp;
    vector_iter_begin(&a->active_projectiles, &it);
    while((o_tmp = iter_next(&it)) != NULL) {
        object *o_prj = *o_tmp;
        if(projectile_get_owner(o_prj) == o)  {
            continue;
        }
        if(o_prj->cur_sprite && smart_usually(a)) {
            vec2i pos_prj = vec2i_add(object_get_pos(o_prj), o_prj->cur_sprite->pos);
            vec2i size_prj = object_get_size(o_prj);
            if (object_get_direction(o_prj) == OBJECT_FACE_LEFT) {
                pos_prj.x = object_get_pos(o_prj).x + ((o_prj->cur_sprite->pos.x * -1) - size_prj.x);
            }
            if(fabsf(pos_prj.x - o->pos.x) < 120) {
                a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT);
                controller_cmd(ctrl, a->cur_act, ev);
                return 1;
            }
        }

    }

    return 0;
}

/** 
 * \brief Process the current selected move.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Void.
 */
void process_selected_move(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;

    if(a->input_lag_timer > 0) {
        a->input_lag_timer--;
    } else {
        a->move_str_pos--;
        if(a->move_str_pos <= 0) {
            a->move_str_pos = 0;
        }
        a->input_lag_timer = a->input_lag;
    }


    int ch = str_at(&a->selected_move->move_string, a->move_str_pos);
    controller_cmd(ctrl, char_to_act(ch, o->direction), ev);

    if (a->move_str_pos == 0) {
        a->selected_move = NULL;
    }
}

/** 
 * \brief Handle the AI's movement.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Void.
 */
void handle_movement(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    // Change action after act_timer runs out
    int jump_thresh = 0;
    if(a->act_timer <= 0 && rand_int(100) > (BASE_ACT_THRESH - (a->difficulty * 3))) {
        int p_move_roll = rand_int(100);

        int p_move_thresh = BASE_MOVE_THRESH - (a->difficulty * 2);

        if (p_move_roll > p_move_thresh) {

            int p_fwd_roll = rand_int(100);

            int p_fwd_thresh = BASE_FWD_THRESH - ((a->difficulty - 1) * 2);
            if (a->pilot->pref_fwd > a->pilot->pref_back) {
                p_fwd_thresh -= roll_pref(a->pilot->pref_fwd) ? 8 : 4;
            } else if (a->pilot->pref_back > a->pilot->pref_fwd) {
                p_fwd_thresh += roll_pref(a->pilot->pref_back) ? 4 : 2;
            }

            if (h->id == HAR_FLAIL || h->id == HAR_THORN || h->id == HAR_NOVA) p_fwd_thresh -= 4; // flail/thorn/nova
            if (a->pilot->att_hyper) p_fwd_thresh -= 4; // pilot hyper

            // DEBUG("p_fwd_thresh %d", p_fwd_thresh);
            
            if (p_fwd_roll >= p_fwd_thresh) {
                // walk forward
                a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT);
                jump_thresh = BASE_FWD_JUMP_THRESH - (a->difficulty * 2);
            } else {
                // walk backward
                a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT);
                jump_thresh = BASE_BACK_JUMP_THRESH - (a->difficulty * 2);
            }
        } else {
            if (smart_sometimes(a)) {
                // crouch and block
                a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT);
            } else {
                // do nothing
                a->cur_act = ACT_STOP;
                jump_thresh = BASE_STILL_JUMP_THRESH - a->difficulty;
            }             
        }

        reset_act_timer(a);

        controller_cmd(ctrl, a->cur_act, ev);
    }

    // 5% more chance of jumping if pilot personality likes it
    if (jump_thresh > 0 && a->pilot->att_jump) {
        jump_thresh -= 5;
    }

    // Jump once in a while if they like to jump
    if(jump_thresh > 0 && rand_int(100) >= jump_thresh && roll_pref(a->pilot->pref_jump)){
        if(o->vel.x < 0) {
            controller_cmd(ctrl, ACT_UP|ACT_LEFT, ev);
        } else if(o->vel.x > 0) {
            controller_cmd(ctrl, ACT_UP|ACT_RIGHT, ev);
        } else {
            controller_cmd(ctrl, ACT_UP, ev);
        }
    }
}

/** 
 * \brief Attempt to select a random attack.
 *
 * \param ctrl Controller instance.
 * \param highest_damage A boolean indicating whether to pick the highest damage move that is valid.
 *
 * \return Boolean indicating whether an attack was selected.
 */
bool attempt_attack(controller *ctrl, bool highest_damage) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    int enemy_range = get_enemy_range(ctrl);
    bool in_attempt_range = (
        enemy_range <= RANGE_CLOSE ||
        (enemy_range == RANGE_MID && dumb_sometimes(a))
    );

    af_move *selected_move = NULL;
    int top_value = 0;

    // Attack
    for(int i = 0; i < 70; i++) {
        af_move *move = NULL;
        if((move = af_get_move(h->af_data, i))) {
            move_stat *ms = &a->move_stats[i];
            if(is_valid_move(move, h, false)) {
                // smart AI will bail-out unless close enough to hit
                if (!in_attempt_range &&
                    (
                        move->category == CAT_BASIC ||
                        move->category == CAT_LOW ||
                        move->category == CAT_MEDIUM ||
                        move->category == CAT_HIGH ||
                        move->category == CAT_LOW
                    )
                ) {
                    continue;
                }

                int value;
                if (highest_damage) {
                    // evaluate the move based purely on damage
                    value = (int) move->damage * 10;
                } else {
                    // evaluate the move based on learning reinforcement
                    value = ms->value + rand_int(10);
                    if (ms->min_hit_dist != -1){
                        if (ms->last_dist < ms->max_hit_dist+5 && ms->last_dist > ms->min_hit_dist+5){
                            value += 2;
                        } else if (ms->last_dist > ms->max_hit_dist+10){
                            value -= 3;
                        }
                    }

                    // AI is less likely to use exact same move as last attack
                    if (a->last_move_id > 0 && a->last_move_id == move->id) {
                        value -= rand_int(10);
                    }

                    // smart AI will slightly favor high damage moves
                    if (smart_usually(a)) {
                        value += ((int) move->damage / 4);
                    }

                    // AI is less likely to use disliked moves
                    if (dislikes_move(a, move)) {
                        value -= rand_int(10);
                    }

                    value -= ms->attempts/2;
                    value -= ms->consecutive*2;

                    // sometimes skip move if it is too powerful for difficulty
                    if (move_too_powerful(a, move)) {
                        DEBUG("skipping move %s because of difficulty", str_c(&move->move_string));
                        continue;
                    }
                }

                if (selected_move == NULL){
                    selected_move = move;
                    top_value = value;
                } else if (value > top_value) {
                    selected_move = move;
                    top_value = value;
                }
            }
        }
    }

    if(selected_move) {
        for(int i = 0; i < 70; i++) {
            a->move_stats[i].consecutive /= 2;
        }

        // DEBUG("\e[35mRandom attack\e[0m %d", selected_move->id);
        set_selected_move(ctrl, selected_move);
        return true;
    }

    return false;
}

/** 
 * \brief Attempt to initiate a charge atack using direct keyboard combinations.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Boolean indicating whether an attack was initiated.
 */
bool attempt_charge_attack(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    int enemy_range = get_enemy_range(ctrl);

    switch (h->state) {
        case STATE_WALKTO:
        case STATE_WALKFROM:
        case STATE_CROUCHBLOCK:
        case STATE_CROUCHING:
            controller_cmd(ctrl, ACT_STOP, ev);
            break;
        case STATE_STANDING:
            break;
        default:
            return false;
    }

    switch (h->id) {
        case HAR_JAGUAR:
            DEBUG("\e[35mJaguar move:\e[0m Leap");
            if (enemy_range >= RANGE_MID && smart_usually(a)) {
                // Shadow Leap : B,D,F+P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
            }
            // Jaguar Leap : D,F+P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_SHADOW:
            // Shadow Grab       : D,D+P
            DEBUG("\e[35mShadow move:\e[0m Shadow Grab");
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, ACT_STOP, ev);
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, ACT_DOWN|ACT_PUNCH, ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_KATANA:
            if (roll_chance(2) && roll_pref(a->pilot->ap_low)) {
                // Trip-Slide attack : D+B+K
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
                controller_cmd(ctrl, ACT_KICK, ev);
                DEBUG("\e[35mKatana move:\e[0m Trip-slide");
            } else {
                if (enemy_range >= RANGE_MID && roll_chance(2)) {
                    DEBUG("\e[35mKatana move:\e[0m Foward Razor Spin");
                    // Foward Razor Spin : D,F+K
                    controller_cmd(ctrl, ACT_DOWN, ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_KICK : ACT_LEFT|ACT_KICK), ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                } else {
                    DEBUG("\e[35mKatana move:\e[0m Rising Blade ");
                    if (enemy_range > RANGE_CRAMPED && smart_usually(a)) {
                        // Triple Blade : B,D,F+P
                        controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                        controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
                    }
                    // Rising Blade : D,F+P
                    controller_cmd(ctrl, ACT_DOWN, ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
                    controller_cmd(ctrl, ACT_PUNCH, ev);
                }
            }
            break;
        case HAR_FLAIL:
            DEBUG("\e[35mFlail move:\e[0m Charging Punch");
            if (enemy_range >= RANGE_MID && smart_usually(a)) {
                // Shadow Punch : D,B,B,P
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
            }
            // Charging Punch : B,B,P
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_PUNCH : ACT_RIGHT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_THORN:
            // Spike-Charge : F,F+P
            DEBUG("\e[35mThorn move:\e[0m Spike-charge");
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_PYROS:
            DEBUG("\e[35mPyros move:\e[0m Thrust");
            if (enemy_range >= RANGE_MID && smart_usually(a)) {
                // Shadow Thrust : F,F,F+P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                controller_cmd(ctrl, ACT_STOP, ev);
            }
            // Super Thrust : F,F+P
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, ACT_STOP, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_ELECTRA:
            DEBUG("\e[35mElectra move:\e[0m Rolling Thunder");
            if (enemy_range >= RANGE_MID && smart_usually(a)) {
                // Super R.T. : B,D,F,F+P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                controller_cmd(ctrl, ACT_DOWN, ev);
            }
            // Rolling Thunder : F,F+P
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, ACT_STOP, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_CHRONOS:
           if (enemy_range == RANGE_FAR || (smart_usually(a) && roll_pref(a->pilot->ap_special))) {
                DEBUG("\e[35mChronos move:\e[0m Teleport");
                // Teleportation : D,P 
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, ACT_STOP, ev);
                controller_cmd(ctrl, ACT_PUNCH, ev);
            } else {
                DEBUG("\e[35mChronos move\e[0m: Trip-slide");
                // Trip-Slide attack : D,B+K
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
                controller_cmd(ctrl, ACT_KICK, ev);
            }
            break;
        case HAR_SHREDDER:
           if (enemy_range == RANGE_FAR || (smart_usually(a) && roll_pref(a->pilot->ap_jump))) {
                // Flip Kick : D,D+K
                DEBUG("\e[35mShredder move:\e[0m Flip-kick");
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, ACT_STOP, ev);
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, ACT_DOWN|ACT_KICK, ev);
                controller_cmd(ctrl, ACT_KICK, ev);
            } else {
                DEBUG("\e[35mShredder move:\e[0m Head-butt");
                if (enemy_range >= RANGE_MID && smart_usually(a)) {
                    // Shadow Head-Butt : B,D,F+P 
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
                }
                // Head-Butt : D,F+P 
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
                controller_cmd(ctrl, ACT_PUNCH, ev);
            }
            break;
        case HAR_GARGOYLE:
            if (enemy_range == RANGE_FAR || (smart_usually(a) && roll_pref(a->pilot->ap_jump))) {
                DEBUG("\e[35mGargoyle move:\e[0m Wing-charge");
                // Wing Charge : F,F,P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
                controller_cmd(ctrl, ACT_PUNCH, ev);
            } else {
                DEBUG("\e[35mGargoyle move:\e[0m Talon");
                if (enemy_range == RANGE_MID && smart_usually(a)) {
                    // Shadow Talon : B,D,F,P
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
                }
                // Flying Talon : D,F,P
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
                controller_cmd(ctrl, ACT_PUNCH, ev);
            }
            break;
    }

    return true;
}

/** 
 * \brief Attempt to initiate a push atack using direct keyboard combinations.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Boolean indicating whether an attack was initiated.
 */
bool attempt_push_attack(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    int enemy_range = get_enemy_range(ctrl);

    switch (h->state) {
        case STATE_WALKTO:
        case STATE_WALKFROM:
        case STATE_CROUCHBLOCK:
        case STATE_CROUCHING:
            controller_cmd(ctrl, ACT_STOP, ev);
            break;
        case STATE_STANDING:
            break;
        default:
            return false;
    }

    switch (h->id) {
        case HAR_JAGUAR:
            DEBUG("\e[35mJaguar move:\e[0m High Kick");
            // High Kick : B+K
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_KICK : ACT_RIGHT|ACT_KICK), ev);
            controller_cmd(ctrl, ACT_KICK, ev);
            break;
        case HAR_KATANA:
            DEBUG("\e[35mKatana move:\e[0m Rising Blade");
            if (enemy_range > RANGE_CRAMPED && smart_usually(a)) {
                // Triple Blade : B,D,F+P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
            }
            // Rising Blade : D,F+P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_FLAIL:
            if (roll_chance(3)) {
                DEBUG("\e[35mFlail move:\e[0m Slow Swing Chains");
                // Slow Swing Chain : D,K
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, ACT_STOP, ev);
                controller_cmd(ctrl, ACT_KICK, ev);
            } else {
                DEBUG("\e[35mFlail move:\e[0m Swinging Chains");
                // Swinging Chains : D,P
                controller_cmd(ctrl, ACT_DOWN, ev);
                controller_cmd(ctrl, ACT_STOP, ev);
                controller_cmd(ctrl, ACT_PUNCH, ev);
            }
            break;
        case HAR_THORN:
            DEBUG("\e[35mThorn move:\e[0m Speed Kick");
            if (enemy_range > RANGE_CRAMPED && smart_usually(a)) {
                // Shadow Kick : B,D,F+K
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_DOWN : ACT_RIGHT|ACT_DOWN), ev);
            }
            // Speed Kick : D,F+K
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_KICK : ACT_LEFT|ACT_KICK), ev);
            controller_cmd(ctrl, ACT_KICK, ev);
            break;
        case HAR_PYROS:
            DEBUG("\e[35mPyros move:\e[0m Fire Spin");
            // Fire Spin : D+P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, ACT_STOP, ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_ELECTRA:
            DEBUG("\e[35mElectra move:\e[0m Electric Shards");
            // Electric Shards : D,F+P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT|ACT_PUNCH : ACT_LEFT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_NOVA:
            DEBUG("\e[35mNova move:\e[0m Earthquake Slam");
            // Earthquake Slam : D,D,P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, ACT_STOP, ev);
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
    }

    return true;
}

/** 
 * \brief Attempt to initiate a push atack using direct keyboard combinations.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Boolean indicating whether an attack was initiated.
 */
bool attempt_trip_attack(controller *ctrl, ctrl_event **ev) {
   // ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    //int enemy_range = get_enemy_range(ctrl);

    switch (h->state) {
        case STATE_WALKTO:
        case STATE_WALKFROM:
        case STATE_CROUCHBLOCK:
        case STATE_CROUCHING:
            controller_cmd(ctrl, ACT_STOP, ev);
            break;
        case STATE_STANDING:
            break;
        default:
            return false;
    }

    DEBUG("\e[35mHar move:\e[0m Trip");
    // Standard Trip : D+B+K
    controller_cmd(ctrl, ACT_DOWN, ev);
    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
    controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_KICK : ACT_RIGHT|ACT_KICK), ev);
    controller_cmd(ctrl, ACT_KICK, ev);

    return true;
}

/** 
 * \brief Attempt to initiate a projectile atack using direct keyboard combinations.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Boolean indicating whether an attack was initiated.
 */
bool attempt_projectile_attack(controller *ctrl, ctrl_event **ev) {
    object *o = ctrl->har;
    har *h = object_get_userdata(o);

    if (h->state == STATE_WALKTO || h->state == STATE_WALKFROM || h->state == STATE_CROUCHBLOCK) {
        controller_cmd(ctrl, ACT_STOP, ev);
    }

    switch (h->id) {
        case HAR_JAGUAR: // Concussion Cannon : D,B+P
        case HAR_ELECTRA: // Ball Lightning : D, B+P
        case HAR_SHREDDER: // Flying Hands : D, B+P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_PUNCH : ACT_RIGHT|ACT_PUNCH), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_SHADOW:
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            if (roll_chance(2)) {
                // Shadow Punch : D,B+P 
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_PUNCH : ACT_RIGHT|ACT_PUNCH), ev);
                controller_cmd(ctrl, ACT_PUNCH, ev);
            } else {
                // Shadow Kick : D,B+K
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT|ACT_KICK : ACT_RIGHT|ACT_KICK), ev);
                controller_cmd(ctrl, ACT_KICK, ev);
            }
            break;
        case HAR_CHRONOS: // Stasis : D, B, P
            controller_cmd(ctrl, ACT_DOWN, ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
            controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
        case HAR_NOVA: 
            controller_cmd(ctrl, ACT_DOWN, ev);
            if (roll_chance(3)) {
                // Mini-Grenade : D, B, P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT), ev);
            } else {
                // Missile : D, F, P
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_RIGHT : ACT_DOWN|ACT_LEFT), ev);
                controller_cmd(ctrl, (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT), ev);
            }
            controller_cmd(ctrl, ACT_PUNCH, ev);
            break;
    }

    return true;
}

/** 
 * \brief Handle the next phase of the currently queued tactic.
 *
 * \param ctrl Controller instance.
 * \param ev The current controller event.
 *
 * \return Boolean indicating whether AI moved or attacked.
 */
bool handle_queued_tactic(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    har *h = object_get_userdata(o);
    tactic_state *tactic = a->tactic;
    int enemy_close = h->close;
    int enemy_range = get_enemy_range(ctrl);
    bool wall_close = h->is_wallhugging;

    bool acted = false;
    if (tactic->move_type > 0 && tactic->move_timer > 0) {
        acted = true;
        // handle movement phase of tactic
        switch (tactic->move_type) {
            case MOVE_CLOSE:
                if (!enemy_close) {
                    // take a step closer
                    a->cur_act = o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT;
                    controller_cmd(ctrl, a->cur_act, ev);
                    tactic->move_timer--;
                } else {
                    tactic->move_timer = 0;
                    DEBUG("\e[34mMovement close success\e[0m: %d", h->id);
                }
                break;
            case MOVE_AVOID:
                if (enemy_range == RANGE_FAR) {
                    tactic->move_timer = 0;
                    acted = true;
                } else {
                    if (enemy_range == RANGE_CRAMPED || !roll_pref(a->pilot->pref_jump)) {
                        // take a step away
                        a->cur_act = o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT;
                    } else {
                        if (smart_usually(a)) {
                            // do super jump
                            controller_cmd(ctrl, ACT_DOWN, ev);
                        }
                        // jump away
                        a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT)|ACT_UP;
                    }

                    controller_cmd(ctrl, a->cur_act, ev);
                    tactic->move_timer--;
                }

                if (tactic->move_timer == 0) DEBUG("\e[34mMovement avoid finished\e[0m: %d", h->id);
                break;
            case MOVE_JUMP:
                if (!enemy_close) {
                    if (enemy_range == RANGE_FAR && smart_usually(a)) {
                        // do super jump
                        controller_cmd(ctrl, ACT_DOWN, ev);
                    }
                    // jump closer
                    a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT)|ACT_UP;
                    controller_cmd(ctrl, a->cur_act, ev);
                    if (roll_pref(a->pilot->pref_jump)) {
                        tactic->move_timer--;
                    } else {
                        tactic->move_timer = 0;
                    }
                } else if (tactic->tactic_type == TACTIC_FLY) {
                    if (smart_sometimes(a)) {
                        // do super jump
                        controller_cmd(ctrl, ACT_DOWN, ev);
                    }
                    // jump over enemy
                    a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_RIGHT : ACT_LEFT)|ACT_UP;
                    controller_cmd(ctrl, a->cur_act, ev);
                    tactic->move_timer = 0;
                } else {
                    tactic->move_timer = 0;
                }

                if (tactic->move_timer == 0) DEBUG("\e[34mMovement jump finished\e[0m: %d", h->id);
                break;
            case MOVE_BLOCK:
                if (wall_close || har_is_crouching(h)) {
                    // crouch & block
                    a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_DOWN|ACT_LEFT : ACT_DOWN|ACT_RIGHT);
                } else {
                    // retreat & block
                    a->cur_act = (o->direction == OBJECT_FACE_RIGHT ? ACT_LEFT : ACT_RIGHT)|ACT_UP;
                }

                controller_cmd(ctrl, a->cur_act, ev);
                tactic->move_timer--;

                if (tactic->move_timer == 0) DEBUG("\e[34mMovement block finished\e[0m: %d", h->id);
                break;
            default:
                DEBUG("\e[31mFlushing invalid move type\e[0m: %d", h->id);
                tactic->move_type = 0;
                tactic->move_timer = 0;
                acted = false;
        }
    } else if (tactic->attack_type > 0 && tactic->attack_timer > 0) {
        // handle attack phase of tactic
        bool in_attempt_range = (
            enemy_close || 
            (enemy_range <= RANGE_MID && dumb_sometimes(a))
        );
        acted = true;
        tactic->attack_timer--;
        int attack_cat = 0;
        switch (tactic->attack_type) {
            case ATTACK_ID:
                if (!in_attempt_range) break;

                if (assign_move_by_id(ctrl, tactic->attack_id)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mSpecific attack success\e[0m: %d", h->id);
                }
                break;
            case ATTACK_TRIP:
                if (attempt_trip_attack(ctrl, ev)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mTrip attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_sometimes(a)) {
                        if (likes_tactic(ctrl, TACTIC_ESCAPE)) {
                            // set chain tactic to shoot if low attack hits
                            a->tactic->chain_hit_on = CAT_LOW;
                            a->tactic->chain_hit_tactic = TACTIC_ESCAPE;
                        } else if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                            // set chain tactic to escape if low attack hits
                            a->tactic->chain_hit_on = CAT_LOW;
                            a->tactic->chain_hit_tactic = TACTIC_SHOOT;
                        }
                    }
                }
                break;
            case ATTACK_GRAB:
                if (!enemy_close) break;

                if (assign_move_by_cat(ctrl, CAT_THROW, false)) {
                    attack_cat = CAT_THROW;
                } else if (assign_move_by_cat(ctrl, CAT_CLOSE, true)) {
                    attack_cat = CAT_CLOSE;
                }

                if (attack_cat > 0) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mGrab attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_sometimes(a)) {
                        if (likes_tactic(ctrl, TACTIC_PUSH)) {
                            // set chain tactic to push if attack hits
                            a->tactic->chain_hit_on = attack_cat;
                            a->tactic->chain_hit_tactic = TACTIC_PUSH;
                        } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                            // set chain tactic to fly if attack hits
                            a->tactic->chain_hit_on = attack_cat;
                            a->tactic->chain_hit_tactic = TACTIC_FLY;
                        } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                            // set chain tactic to counter if attack hits
                            a->tactic->chain_hit_on = attack_cat;
                            a->tactic->chain_hit_tactic = TACTIC_COUNTER;
                        } else if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                            // set chain tactic to shoot if attack hits
                            a->tactic->chain_hit_on = attack_cat;
                            a->tactic->chain_hit_tactic = TACTIC_SHOOT;
                        }
                    }
                }
                break;
            case ATTACK_LIGHT:
                if (!in_attempt_range) break;

                int light_cat = roll_chance(2) ? CAT_BASIC : CAT_MEDIUM;
                if (assign_move_by_cat(ctrl, light_cat, false)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mLight attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_sometimes(a)) {
                        if (likes_tactic(ctrl, TACTIC_PUSH)) {
                            // set chain tactic to push if attack hits
                            a->tactic->chain_hit_on = light_cat;
                            a->tactic->chain_hit_tactic = TACTIC_PUSH;
                        } else if (likes_tactic(ctrl, TACTIC_TRIP)) {
                            // set chain tactic to trip if attack hits
                            a->tactic->chain_hit_on = light_cat;
                            a->tactic->chain_hit_tactic = TACTIC_TRIP;
                        } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                            // set chain tactic to fly if attack hits
                            a->tactic->chain_hit_on = light_cat;
                            a->tactic->chain_hit_tactic = TACTIC_FLY;
                        }
                    }
                }
                break;
            case ATTACK_HEAVY:
                if (!in_attempt_range) break;

                int heavy_cat = roll_chance(2) ? CAT_MEDIUM : CAT_HIGH;
                if (assign_move_by_cat(ctrl, heavy_cat, true)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mHeavy attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_sometimes(a)) {
                        if (likes_tactic(ctrl, TACTIC_TRIP)) {
                            // set chain tactic to trip if attack hits
                            a->tactic->chain_hit_on = heavy_cat;
                            a->tactic->chain_hit_tactic = TACTIC_TRIP;
                        } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                            // set chain tactic to counter if attack hits
                            a->tactic->chain_hit_on = heavy_cat;
                            a->tactic->chain_hit_tactic = TACTIC_COUNTER;
                        } else if (likes_tactic(ctrl, TACTIC_QUICK)) {
                            // set chain tactic to quick if attack hits
                            a->tactic->chain_hit_on = heavy_cat;
                            a->tactic->chain_hit_tactic = TACTIC_QUICK;
                        }
                    }
                }
                break;
            case ATTACK_JUMP:
                if (!in_attempt_range && a->tactic->attack_timer > 0) {
                    DEBUG("\e[35mWaiting for jump attack range\e[0m");
                    // when not in range we wait until last tick of attack timer
                    // that way the attack won't fizzle out before we reach them
                    return acted;
                }

                if (attempt_attack(ctrl, false)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mJump attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_usually(a)) {
                        if (likes_tactic(ctrl, TACTIC_TRIP)) {
                            // set chain tactic to counter if jumping attack hits
                            a->tactic->chain_hit_on = a->selected_move->category;
                            a->tactic->chain_hit_tactic = TACTIC_TRIP;
                        } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                            // set chain tactic to counter if jumping attack hits
                            a->tactic->chain_hit_on = a->selected_move->category;
                            a->tactic->chain_hit_tactic = TACTIC_GRAB;
                        } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                            // set chain tactic to counter if jumping attack hits
                            a->tactic->chain_hit_on = a->selected_move->category;
                            a->tactic->chain_hit_tactic = TACTIC_PUSH;
                        }
                    }
                }
                break;
            case ATTACK_RANGED:
                if (attempt_projectile_attack(ctrl, ev)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mRanged attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_sometimes(a)) {
                        if (a->pilot->att_sniper && likes_tactic(ctrl, TACTIC_SHOOT)) {
                            // set chain tactic to shoot if projectile hits
                            a->tactic->chain_hit_on = CAT_PROJECTILE;
                            a->tactic->chain_hit_tactic = TACTIC_SHOOT;
                        } else if (likes_tactic(ctrl, TACTIC_FLY)) {
                            // set chain tactic to fly if projectile hits
                            a->tactic->chain_hit_on = CAT_PROJECTILE;
                            a->tactic->chain_hit_tactic = TACTIC_FLY;
                        } else if (likes_tactic(ctrl, TACTIC_COUNTER)) {
                            // set chain tactic to counter if projectile hits
                            a->tactic->chain_hit_on = CAT_PROJECTILE;
                            a->tactic->chain_hit_tactic = TACTIC_COUNTER;
                        }
                    }
                }
                break;
            case ATTACK_CHARGE:
                DEBUG("\e[35mCharge attempt\e[0m");
                if (attempt_charge_attack(ctrl, ev)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mCharge attack success\e[0m: %d", h->id);

                    if (h->id == HAR_SHADOW) {
                        if (likes_tactic(ctrl, TACTIC_SHOOT)) {
                            queue_tactic(ctrl, TACTIC_SHOOT);
                        } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                            queue_tactic(ctrl, TACTIC_GRAB);
                        } else {
                            queue_tactic(ctrl, TACTIC_FLY);
                        }
                    }
                }
                break;
            case ATTACK_PUSH:
                if (attempt_push_attack(ctrl, ev)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mPush attack success\e[0m: %d", h->id);
                }
                break;
            case ATTACK_RANDOM:
                if (attempt_attack(ctrl, false)) {
                    reset_tactic_state(a);
                    DEBUG("\e[32mRandom attack success\e[0m: %d", h->id);

                    // chain another tactic
                    if (smart_usually(a)) {
                        if (likes_tactic(ctrl, TACTIC_TRIP)) {
                            // set chain tactic to counter if jumping attack hits
                            a->tactic->chain_hit_on = a->selected_move->category;
                            a->tactic->chain_hit_tactic = TACTIC_TRIP;
                        } else if (likes_tactic(ctrl, TACTIC_GRAB)) {
                            // set chain tactic to counter if jumping attack hits
                            a->tactic->chain_hit_on = a->selected_move->category;
                            a->tactic->chain_hit_tactic = TACTIC_GRAB;
                        } else if (likes_tactic(ctrl, TACTIC_PUSH)) {
                            // set chain tactic to counter if jumping attack hits
                            a->tactic->chain_hit_on = a->selected_move->category;
                            a->tactic->chain_hit_tactic = TACTIC_PUSH;
                        }
                    }
                }
                break;
            default:
                DEBUG("\e[31mFlushing invalid attack type\e[0m: %d", h->id);
                tactic->attack_type = 0;
                tactic->attack_timer = 0;
        }
    } else {
        // reset queued tactic
        reset_tactic_state(a);
        DEBUG("\e[31mFlushing failed tactic queue\e[0m: %d", h->id);
    }

    return acted;
}

int ai_controller_poll(controller *ctrl, ctrl_event **ev) {
    ai *a = ctrl->data;
    object *o = ctrl->har;
    if (!o) {
        return 1;
    }

    har *h = object_get_userdata(o);

    // Do not run AI while the game is paused
    if(game_state_is_paused(o->gs)) { return 0; }

    // Do not run AI while match is starting or ending
    // XXX this prevents the AI from doing scrap/destruction moves
    // XXX this could be fixed by providing a "scene changed" event
    if(is_arena(game_state_get_scene(o->gs)->id) &&
       arena_get_state(game_state_get_scene(o->gs)) != ARENA_STATE_FIGHTING) {

        // null out selected move to fix the "AI not moving problem"
        a->selected_move = NULL;
        return 0;
    }

    // decrement act_timer
    a->act_timer--;

    // Grab all projectiles on screen
    vector_clear(&a->active_projectiles);
    game_state_get_projectiles(o->gs, &a->active_projectiles);

    // Try to block har
    if(ai_block_har(ctrl, ev)) {
        return 0;
    }

    // Try to block projectiles
    if(ai_block_projectile(ctrl, ev)) {
        return 0;
    }

    // handle selected move
    if(a->selected_move) {
        // finish doing the selected move first
        process_selected_move(ctrl, ev);
        // DEBUG("=== POLL === process_selected_move");
        return 0;
    }

    bool can_move = (h->state == STATE_STANDING || h->state == STATE_WALKTO || h->state == STATE_WALKFROM || h->state == STATE_CROUCHBLOCK);    
    bool can_interupt_tactic = (
        a->tactic->tactic_type == 0 || !(
            a->tactic->attack_type == ATTACK_CHARGE ||
            a->tactic->attack_type == ATTACK_PUSH ||
            a->tactic->attack_type == ATTACK_TRIP
        )
    );

    // be wary of repeated throws while attempting to complete a tactic
    if (can_move && can_interupt_tactic && a->thrown > 1 && a->difficulty > 2) {
        // attempt a quick attack to disrupt their grab/throw
        int enemy_range = get_enemy_range(ctrl);
        if (
            (
                enemy_range == RANGE_CRAMPED ||
                (enemy_range == RANGE_CLOSE && a->thrown >= 2)
            ) &&
            (assign_move_by_cat(ctrl, CAT_LOW, false) || attempt_attack(ctrl, false))
        ) {
            DEBUG("\e[35mSpamming random attacks to avoid being thrown\e[0m");
            reset_tactic_state(a);
            return 0;
        }
    }

    // attempt queued tactic
    if (a->tactic->tactic_type > 0 && (can_move || (a->tactic->tactic_type == TACTIC_FLY && h->state == STATE_JUMPING))) {
        bool acted = handle_queued_tactic(ctrl, ev);
        // check if tactic is complete
        if (a->tactic->tactic_type == 0) {
            // reset movement act timer
            reset_act_timer(a);
        }

        if (acted) return 0; // wait for next poll
    }

    // attempt a random attack
    if (diff_scale(a) && attempt_attack(ctrl, false)) {
        // reset movement act timer
        reset_act_timer(a);
        return 0;
    }

    // handle movement
    handle_movement(ctrl, ev);
    // DEBUG("=== POLL === handle_movement");

    // queue a random tactic for next poll
    if (
        (
            a->last_move_id > 0 ||
            h->close
        ) &&
        smart_sometimes(a) &&
        a->tactic->tactic_type == 0 &&
        can_move &&
        roll_chance(6)
     ) {
        if (roll_chance(4) && likes_tactic(ctrl, TACTIC_CLOSE)) {
            DEBUG("\e[35mQueue random tactic:\e[32m CLOSE\e[0m");
            queue_tactic(ctrl, TACTIC_CLOSE);
        } else if (roll_chance(4) && likes_tactic(ctrl, TACTIC_PUSH)) {
            DEBUG("\e[35mQueue random tactic\e[32m PUSH\e[0m");
            queue_tactic(ctrl, TACTIC_PUSH);
        } else if (roll_chance(4) && likes_tactic(ctrl, TACTIC_TRIP)) {
            DEBUG("\e[35mQueue random tactic\e[32m TRIP\e[0m");
            queue_tactic(ctrl, TACTIC_TRIP);
        } else if (roll_chance(8) && likes_tactic(ctrl, TACTIC_SHOOT)) {
            DEBUG("\e[35mQueue random tactic:\e[32m SHOOT\e[0m");
            queue_tactic(ctrl, TACTIC_SHOOT);
        } else if (roll_chance(6) && likes_tactic(ctrl, TACTIC_GRAB)) {
            DEBUG("\e[35mQueue random tactic\e[32m GRAB\e[0m");
            queue_tactic(ctrl, TACTIC_GRAB);
        } else if (roll_chance(6) && likes_tactic(ctrl, TACTIC_FLY)) {
            DEBUG("\e[35mQueue random tactic\e[32m FLY\e[0m");
            queue_tactic(ctrl, TACTIC_FLY);
        } else if (roll_chance(6) && likes_tactic(ctrl, TACTIC_QUICK)) {
            DEBUG("\e[35mQueue random tactic\e[32m QUICK\e[0m");
            queue_tactic(ctrl, TACTIC_QUICK);
        }
    }

    return 0;
}

<<<<<<< HEAD
void ai_controller_create(controller *ctrl, int difficulty) {
    ai *a = omf_calloc(1, sizeof(ai));
=======
/** 
 * \brief Populate pilot preferences according to perceived personality.
 *
 * \param pilot The pilot details.
 * \param pilot_id An integer identifying the pilot.
 *
 * \return Void.
 */
void populate_pilot_prefs(sd_pilot *pilot, int pilot_id) {
    switch (pilot_id) {
        case 0:
            // crystal
            // determined and independent
            pilot->pref_fwd = 150;
            pilot->att_hyper = 1;
            pilot->ap_throw = 150;
            pilot->ap_special = 50;
            break;
        case 1:
            // stefan
            // young and skillful
            pilot->att_normal = 1;
            pilot->pref_fwd = 50;
            pilot->ap_special = 200;
            pilot->ap_jump = 100;
            break;
        case 2:
            // milano
            // fast kickboxer
            pilot->att_jump = 1;
            pilot->pref_fwd = 100;
            pilot->ap_special = -150;
            pilot->ap_jump = 300;
            pilot->pref_jump = 100;
            break;
        case 3:
            // christian
            // aggressive
            pilot->att_hyper = 1;
            pilot->pref_fwd = 250;
            pilot->ap_special = 150;
            break;
        case 4:
            // shirro
            // slow but powerful
            pilot->att_normal = 1;
            pilot->ap_jump = -100;
            pilot->pref_jump = -100;
            pilot->ap_throw = 300;
            pilot->ap_special = -50;
            break;
        case 5:
            // jean-paul
            // well rounded & calculating
            pilot->att_sniper = 1;
            pilot->pref_back = 50;
            pilot->ap_low = 100;
            pilot->ap_jump = 100;
            pilot->ap_special = 200;
            break;
        case 6:
            // ibrahim
            // patience
            pilot->att_def = 1;
            pilot->pref_back = 100;
            pilot->ap_special = 100;
            pilot->ap_throw = 100;
            break;
        case 7:
            // angel
            // mysterious
            pilot->att_sniper = 1;
            pilot->pref_jump = 50;
            pilot->pref_fwd = 150;
            pilot->ap_special = 300;
            break;
        case 8:
            // cosette
            // defensive / cautious
            pilot->att_def = 1;
            pilot->ap_low = 100;
            pilot->ap_special = -50;
            pilot->pref_jump = -100;
            pilot->ap_jump = -50;
            break;
        case 9:
            // raven
            // that's so raven
            pilot->att_hyper = 1;
            pilot->pref_jump = 200;
            pilot->ap_jump = 400;
            pilot->ap_special = 300;
            break;
        case 10:
            // kreissack
            // special
            pilot->att_normal = 1;
            pilot->ap_throw = 100;
            pilot->ap_special = 350;
            break;
    }
}

void ai_controller_create(controller *ctrl, int difficulty, sd_pilot *pilot, int pilot_id) {
    ai *a = malloc(sizeof(ai));
>>>>>>> 7287d2c8... AI Rework:
    a->har_event_hooked = 0;
    a->difficulty = difficulty+1;
    a->act_timer = 0;
    a->cur_act = 0;
    a->input_lag = 3;
    a->input_lag_timer = a->input_lag;
    a->selected_move = NULL;
    a->last_move_id = 0;
    a->move_str_pos = 0;
    memset(a->move_stats, 0, sizeof(a->move_stats));
    for(int i = 0;i < 70;i++) {
        a->move_stats[i].max_hit_dist = -1;
        a->move_stats[i].min_hit_dist = -1;
        a->move_stats[i].last_dist = -1;
    }
    a->blocked = 0;
    a->thrown = 0;
    
    vector_create(&a->active_projectiles, sizeof(object*));
    a->pilot = pilot;

    // set pilot prefs manually until we start reading them from binary
    populate_pilot_prefs(pilot, pilot_id);

    // set initial tactical state
    tactic_state *tactic = calloc(1, sizeof(tactic_state));
    tactic->tactic_type = 0;
    tactic->last_tactic = 0;
    tactic->move_type = 0;
    tactic->move_timer = 0;
    tactic->attack_type = 0;
    tactic->attack_id = 0;
    tactic->attack_timer = 0;
    tactic->attack_on = 0;
    a->tactic = tactic;
    reset_tactic_state(a);

    ctrl->data = a;
    ctrl->type = CTRL_TYPE_AI;
    ctrl->poll_fun = &ai_controller_poll;
    ctrl->har_hook = &ai_har_event;
}
