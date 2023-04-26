#include "game/scenes/mechlab/lab_menu_customize.h"
#include "game/scenes/mechlab.h"
#include "game/common_defines.h"
#include "game/gui/sizer.h"
#include "game/gui/spritebutton.h"
#include "game/gui/text_render.h"
#include "game/gui/trn_menu.h"
#include "game/scenes/mechlab/button_details.h"
#include "formats/pilot.h"
#include "resources/bk.h"
#include "resources/languages.h"
#include "utils/log.h"

// negative values means the upgrade is unavailable at that level
int32_t arm_leg_prices[][10] = {
    // jaguar
    {0, 760, 2280, 5320, 9120, 13680, 22800, 38000, 57000, -1},
    // shadow
    {0, 800, 2400, 5600, 9600, 14400, 24000, 40000, 60000, 96000},
    // TODO the rest
};

int32_t stun_resistance_prices[][10] = {
    // jaguar
    {0, 1140, 3420, 7980, 13680, 20520, 34200, 57000, 85500, -1},
    // shadow
    {0, 1200, 3600, 8400, 14400, 21600, 36000, -1, -1, -1},
    // TODO the rest
};

int32_t armor_prices[][10] = {
    // jaguar
    {0, 1900, 5700, 13300, 22800, 34200, -1, -1, -1, -1},
    //shadow
    {0, 2000, 6000, 14000, 24000, 36000, 60000, 100000, -1, -1},
};

void lab_menu_customize_done(component *c, void *userdata) {
    trnmenu_finish(c->parent);
}

void lab_menu_customize_color_main(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot_set_player_color(&p1->chr->pilot, SECONDARY, (p1->chr->pilot.color_2 + 1) % 16);
    mechlab_update(s);
}

void lab_menu_customize_color_secondary(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot_set_player_color(&p1->chr->pilot, TERTIARY, (p1->chr->pilot.color_1 + 1) % 16);
    mechlab_update(s);
}

void lab_menu_customize_color_third(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot_set_player_color(&p1->chr->pilot, PRIMARY, (p1->chr->pilot.color_3 + 1) % 16);
    mechlab_update(s);
}



void lab_menu_customize_arm_power(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_power];
        if (price > 0) {
            pilot->money += price * 0.85;
            pilot->arm_power--;
            mechlab_update(s);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_power + 1];
        pilot->money -= price;
        pilot->arm_power++;
        mechlab_update(s);
    }
}

void lab_menu_customize_check_arm_power_price(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_power];
        if (price < 1) {
            component_disable(c, 1);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_power + 1];
        if (price < 0 || price > pilot->money) {
            component_disable(c, 1);
        }
    }
}


void lab_menu_customize_leg_power(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_power];
        if (price > 0) {
            pilot->money += price * 0.85;
            pilot->leg_power--;
            mechlab_update(s);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_power + 1];
        pilot->money -= price;
        pilot->leg_power++;
        mechlab_update(s);
    }
}

void lab_menu_customize_check_leg_power_price(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_power];
        if (price < 1) {
            component_disable(c, 1);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_power + 1];
        if (price < 0 || price > pilot->money) {
            component_disable(c, 1);
        }
    }
}

void lab_menu_customize_arm_speed(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_speed];
        if (price > 0) {
            pilot->money += price * 0.85;
            pilot->arm_speed--;
            mechlab_update(s);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_speed + 1];
        pilot->money -= price;
        pilot->arm_speed++;
        mechlab_update(s);
    }
}

void lab_menu_customize_check_arm_speed_price(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_speed];
        if (price < 1) {
            component_disable(c, 1);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->arm_speed + 1];
        if (price < 0 || price > pilot->money) {
            component_disable(c, 1);
        }
    }
}


void lab_menu_customize_leg_speed(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_speed];
        if (price > 0) {
            pilot->money += price * 0.85;
            pilot->leg_speed--;
            mechlab_update(s);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_speed + 1];
        pilot->money -= price;
        pilot->leg_speed++;
        mechlab_update(s);
    }
}

void lab_menu_customize_check_leg_speed_price(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_speed];
        if (price < 1) {
            component_disable(c, 1);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->leg_speed + 1];
        if (price < 0 || price > pilot->money) {
            component_disable(c, 1);
        }
    }
}

void lab_menu_customize_armor(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->armor];
        if (price > 0) {
            pilot->money += price * 0.85;
            pilot->armor--;
            mechlab_update(s);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->armor + 1];
        pilot->money -= price;
        pilot->armor++;
        mechlab_update(s);
    }
}

void lab_menu_customize_check_armor_price(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->armor];
        if (price < 1) {
            component_disable(c, 1);
        }
    } else {
        int32_t price = arm_leg_prices[pilot->har_id][pilot->armor + 1];
        if (price < 0 || price > pilot->money) {
            component_disable(c, 1);
        }
    }
}

void lab_menu_customize_stun_resistance(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = armor_prices[pilot->har_id][pilot->stun_resistance];
        if (price > 0) {
            pilot->money += price * 0.85;
            pilot->stun_resistance--;
            mechlab_update(s);
        }
    } else {
        int32_t price = armor_prices[pilot->har_id][pilot->stun_resistance + 1];
        pilot->money -= price;
        pilot->stun_resistance++;
        mechlab_update(s);
    }
}

void lab_menu_customize_check_stun_resistance_price(component *c, void *userdata) {
    scene *s = userdata;
    game_player *p1 = game_state_get_player(s->gs, 0);
    sd_pilot *pilot = game_player_get_pilot(p1);
    if (mechlab_get_selling(s)) {
        int32_t price = stun_resistance_prices[pilot->har_id][pilot->stun_resistance];
        if (price < 1) {
            component_disable(c, 1);
        }
    } else {
        int32_t price = stun_resistance_prices[pilot->har_id][pilot->stun_resistance + 1];
        if (price < 0 || price > pilot->money) {
            component_disable(c, 1);
        }
    }
}


static const button_details details_list[] = {
    {lab_menu_customize_color_main,                         NULL,          TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED}, // Blue
    {lab_menu_customize_color_third,                         NULL,          TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED}, // Yellow
    {lab_menu_customize_color_secondary,                         NULL,          TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED}, // Red
    {lab_menu_customize_arm_power, "ARM POWER",   TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {lab_menu_customize_leg_power, "LEG POWER",   TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {lab_menu_customize_arm_speed, "ARM SPEED",   TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {lab_menu_customize_leg_speed, "LEG SPEED",   TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {lab_menu_customize_armor,     "ARMOR",       TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {lab_menu_customize_stun_resistance,  "STUN RES.",   TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {NULL,                         "TRADE ROBOT", TEXT_HORIZONTAL, TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
    {lab_menu_customize_done,      "DONE",        TEXT_VERTICAL,   TEXT_CENTER, TEXT_MIDDLE, 0, 0, 0, 0, COM_ENABLED},
};

static const spritebutton_tick_cb tickers[] = {
    NULL,
    NULL,
    NULL,
    lab_menu_customize_check_arm_power_price,
    lab_menu_customize_check_leg_power_price,
    lab_menu_customize_check_arm_speed_price,
    lab_menu_customize_check_leg_speed_price,
    lab_menu_customize_check_armor_price,
    lab_menu_customize_check_stun_resistance_price,
    NULL,
    NULL
};

void lab_menu_focus_blue(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(547));
        } else {
            mechlab_set_hint(s, lang_get(548));
        }
    }
}

void lab_menu_focus_yellow(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(551));
        } else {
            mechlab_set_hint(s, lang_get(552));
        }
    }
}

void lab_menu_focus_red(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(549));
        } else {
            mechlab_set_hint(s, lang_get(550));
        }
    }
}

void lab_menu_focus_arm_power(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(553)); // TODO sprintf arm/leg
        } else {
            mechlab_set_hint(s, lang_get(554)); // TODO sprintf arm/leg
        }
    }
}

void lab_menu_focus_leg_power(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(555)); // TODO sprintf arm/leg
        } else {
            mechlab_set_hint(s, lang_get(556)); // TODO sprintf arm/leg
        }
    }
}

void lab_menu_focus_arm_speed(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(557)); // TODO sprintf arm/leg
        } else {
            mechlab_set_hint(s, lang_get(558)); // TODO sprintf arm/leg
        }
    }
}

void lab_menu_focus_leg_speed(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(559)); // TODO sprintf arm/leg
        } else {
            mechlab_set_hint(s, lang_get(560)); // TODO sprintf arm/leg
        }
    }
}

void lab_menu_focus_armor(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(561));
        } else {
            mechlab_set_hint(s, lang_get(562));
        }
    }
}

void lab_menu_focus_stun_resistance(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(563));
        } else {
            mechlab_set_hint(s, lang_get(564));
        }
    }
}

void lab_menu_focus_trade(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(565));
        } else {
            mechlab_set_hint(s, lang_get(566));
        }
    }
}

void lab_menu_focus_done(component *c, bool focused, void *userdata) {
    if (focused) {
        scene *s = userdata;
        if (mechlab_get_selling(s)) {
            mechlab_set_hint(s, lang_get(567));
        } else {
            mechlab_set_hint(s, lang_get(568));
        }
    }
}

static const spritebutton_focus_cb focus_cbs[] = {
    lab_menu_focus_blue,
    lab_menu_focus_yellow,
    lab_menu_focus_red,
    lab_menu_focus_arm_power,
    lab_menu_focus_leg_power,
    lab_menu_focus_arm_speed,
    lab_menu_focus_leg_speed,
    lab_menu_focus_armor,
    lab_menu_focus_stun_resistance,
    lab_menu_focus_trade,
    lab_menu_focus_done,
};

component *lab_menu_customize_create(scene *s) {
    animation *main_sheets = &bk_get_info(&s->bk_data, 1)->ani;
    animation *main_buttons = &bk_get_info(&s->bk_data, 3)->ani;
    animation *hand_of_doom = &bk_get_info(&s->bk_data, 29)->ani;

    // Initialize menu, and set button sheet
    sprite *msprite = animation_get_sprite(main_sheets, 0);
    component *menu = trnmenu_create(msprite->data, msprite->pos.x, msprite->pos.y);

    // Default text configuration
    text_settings tconf;
    text_defaults(&tconf);
    tconf.font = FONT_SMALL;
    tconf.cforeground = color_create(0, 0, 123, 255);

    // Init GUI buttons with locations from the "select" button sprites
    for(int i = 0; i < animation_get_sprite_count(main_buttons); i++) {
        tconf.valign = details_list[i].valign;
        tconf.halign = details_list[i].halign;
        tconf.padding.top = details_list[i].top;
        tconf.padding.bottom = details_list[i].bottom;
        tconf.padding.left = details_list[i].left;
        tconf.padding.right = details_list[i].right;
        tconf.direction = details_list[i].dir;

        sprite *bsprite = animation_get_sprite(main_buttons, i);
        component *button =
            spritebutton_create(&tconf, details_list[i].text, bsprite->data, COM_ENABLED, details_list[i].cb, s);
        component_set_size_hints(button, bsprite->data->w, bsprite->data->h);
        component_set_pos_hints(button, bsprite->pos.x, bsprite->pos.y);
        spritebutton_set_tick_cb(button, tickers[i]);

        spritebutton_set_focus_cb(button, focus_cbs[i]);

        component_tick(button);
        trnmenu_attach(menu, button);
    }

    // Bind hand animation
    trnmenu_bind_hand(menu, hand_of_doom, s->gs);

    return menu;
}
