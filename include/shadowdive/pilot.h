/*! \file
 * \brief Pilot structure handling.
 * \details Functions and structs for reading, writing and modifying OMF:2097 pilot data structures.
 * \copyright MIT license.
 * \date 2013-2014
 * \author Andrew Thompson
 * \author Tuomas Virtanen
 */ 

#ifndef _SD_PILOT_H
#define _SD_PILOT_H

#include <stdint.h>
#ifdef SD_USE_INTERNAL
    #include "shadowdive/internal/reader.h"
    #include "shadowdive/internal/memreader.h"
    #include "shadowdive/internal/writer.h"
    #include "shadowdive/internal/memwriter.h"
#endif

#ifdef __cplusplus 
extern "C" {
#endif

/*! \brief PIC pilot information
 *
 * Contains a pilot information. Current upgrades, powers, tournament, etc.
 * \todo The attitude values need to be extracted from the attitude variable on load, and re-packed on save
 * \todo The combat requirement values need to be extracted too.
 */
typedef struct {
    uint32_t unknown_a;
    char name[18];           ///< Pilot name
    uint16_t wins;           ///< Matches won by this pilot
    uint16_t losses;         ///< Matches lost by this pilot
    uint8_t rank;            ///< Rank
    uint8_t har_id;          ///< Har Identifier (255 = random)
    uint8_t arm_power;       ///< HAR Arm power (0-9).
    uint8_t leg_power;       ///< HAR Leg power (0-9).
    uint8_t arm_speed;       ///< HAR Arm speed (0-9).
    uint8_t leg_speed;       ///< HAR Leg speed (0-9).
    uint8_t armor;           ///< Har armor (0-9).
    uint8_t stun_resistance; ///< Har stun resistance (0-9).
    uint8_t power;           ///< Pilot power (1-25).
    uint8_t agility;         ///< Pilot agility (1-25).
    uint8_t endurance;       ///< Pilot endurance (1-25).
    uint8_t unknown_stat;
    uint16_t offense;        ///< Offense preference value (100 high, should be under 200).
    uint16_t defense;        ///< Defense preference value (100 high, should be under 200).
    uint32_t money;          ///< Amount of money the pilot currently has
    uint8_t color_1;         ///< Color 1 field for the HAR (0-15).
    uint8_t color_2;         ///< Color 2 field for the HAR (0-15).
    uint8_t color_3;         ///< Color 3 field for the HAR (0-15).
    char trn_name[13];       ///< Tournament file
    char trn_desc[31];       ///< Tournament description
    char trn_image[13];      ///< Tournament image file
    char unk_block_a[48];
    uint8_t pilot_id;        ///< Pilot ID
    uint8_t unknown_k;
    uint16_t force_arena;    ///< Tells if this pilot needs to play on a certain arena
    uint8_t difficulty;      ///< Difficulty setting
    char unk_block_b[2];
    uint8_t movement;        ///< Pilot can move in rankings
    char unk_block_c[6];
    char enhancements[11];   ///< Har enchancements. A field for each HAR.

    uint8_t secret;          ///< This character is a secret character, and only comes out when requirements match
    uint8_t only_fight_once; ///< This character can only be fought once per tournament
    uint8_t req_enemy;       ///< Required defeated enemy for this character to appear (Value 0 if not set, otherwise character id + 1)
    uint8_t req_difficulty;  ///< Required difficulty level for this character to appear
    uint8_t req_rank;        ///< Required ranking for this character to appear
    uint8_t req_vitality;    ///< Required vitality for this character to appear
    uint8_t req_fighter;     ///< Required fighter for this character to appear
    uint8_t req_accuracy;    ///< Required accuracy for this character to appear
    uint8_t req_avg_dmg;     ///< Required average damage for this character to appear
    uint8_t req_max_rank;    ///< Required max ranking for this character to appear
    uint8_t req_scrap;       ///< Must have scrapped an enemy for this character to appear
    uint8_t req_destroy;     ///< Must have destroyed an enemy for this character to appear

    uint8_t att_normal;
    uint8_t att_hyper;
    uint8_t att_jump;
    uint8_t att_def;
    uint8_t att_sniper;

    char unk_block_d[6];
    int16_t ap_throw;        ///< AI Preference for throw moves. Accepted value range (-400, 400).
    int16_t ap_special;      ///< AI Preference for special moves. Accepted value range (-400, 400).
    int16_t ap_jump;         ///< AI Preference for jump moves. Accepted value range (-400, 400).
    int16_t ap_high;         ///< AI Preference for high moves. Accepted value range (-400, 400).
    int16_t ap_low;          ///< AI Preference for low moves. Accepted value range (-400, 400).
    int16_t ap_middle;       ///< AI Preference for middle moves. Accepted value range (-400, 400).
    int16_t pref_jump;       ///< AI Preference for jump movement. Accepted value range (-400, 400).
    int16_t pref_fwd;        ///< AI Preference for forwards movement. Accepted value range (-400, 400).
    int16_t pref_back;       ///< AI Preference for backwards movement. Accepted value range (-400, 400).
    uint32_t unknown_e;
    float learning;          ///< How actively this pilot learns your combat tactics. Accepted value range (0-15).
    float forget;            ///< How quickly this pilot forgets your combat tactics. Accepted value range (0-3).
    char unk_block_f[24];
    uint16_t enemies_inc_unranked; ///< Enemies in current tournament, including unranked opponents
    uint16_t enemies_ex_unranked;  ///< Same as above, excluding unranked opponents.
    char unk_block_g[6];
    uint32_t winnings;       ///< Money made by winning opponents
    uint32_t total_value;
    char unk_block_h[162];
    uint16_t photo_id;       ///< Which face photo this pilot uses
} sd_pilot;

/*! \brief Initialize pilot struct
 *
 * Initializes the pilot structure with empty values.
 *
 * \retval SD_INVALID_INPUT Pilot struct pointer was NULL
 * \retval SD_SUCCESS Success.
 *
 * \param pilot Allocated pilot struct pointer.
 */
int sd_pilot_create(sd_pilot *pilot);

/*! \brief Free pilot structure
 *
 * Frees up all memory reserved by the pilot structure.
 * All contents will be freed, all pointers to contents will be invalid.
 *
 * \param pilot Pilot struct pointer.
 */
void sd_pilot_free(sd_pilot *pilot);

#ifdef SD_USE_INTERNAL
void sd_pilot_load_player_from_mem(sd_mreader *mreader, sd_pilot *pilot);
void sd_pilot_load_from_mem(sd_mreader *mreader, sd_pilot *pilot);
int sd_pilot_load(sd_reader *reader, sd_pilot *pilot);
void sd_pilot_save_player_to_mem(sd_mwriter *mwriter, const sd_pilot *pilot);
void sd_pilot_save_to_mem(sd_mwriter *mwriter, const sd_pilot *pilot);
int sd_pilot_save(sd_writer *writer, const sd_pilot *pilot);
#endif

#ifdef __cplusplus
}
#endif

#endif // _SD_PILOT_H