#include "shadowdive/stringparser.h"
#include "shadowdive/error.h"

#define _BSD_SOURCE // for strdup
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdio.h>

// private structs
typedef struct tag_attribute_t {
    const sd_stringparser_tag_info *tag_info;
    sd_stringparser_cb_t callback;
    void *data;
} tag_attribute;

typedef struct tag_list_t {
    // traverse tag_chain to find whether a given tag is valid or not
    struct tag_list_t *tag_chain[256];
    tag_attribute attrib;
    sd_stringparser_cb_t default_cb;
    void *default_cb_data;
} tag_list;

typedef struct anim_frame_t {
    // is_done is set to 1 if this frame is completed also no further processing for this frame 
    int is_done;

    // the duration of this frame in "ticks"
    int duration;

    int start_tick;

    // frame letter is always uppercase
    char frame_letter;

    // a list of tags ready to be "interpreted"
    int num_tags;
    const char **tags;
    int *tag_params;
} anim_frame;

typedef struct frame_list_t {
    unsigned int num_frames;
    anim_frame *frames;

    int current_frame;
    unsigned int last_tick;

    sd_stringparser_cb_t frame_change_cb;
    void *frame_change_data;
} frame_list;

enum {
    TAG_TAG=0,
    TAG_FRAME,
    TAG_MARKER,
    TAG_END
};

// list of valid tags and whether it has param or not
const sd_stringparser_tag_info tags[] = {
    {"aa", 0, NULL},
    {"ab", 0, NULL},
    {"ac", 0, NULL},
    {"ad", 0, NULL},
    {"ae", 0, NULL}, 
    {"af", 0, NULL},
    {"ag", 0, NULL},
    {"ai", 0, NULL},
    {"am", 0, NULL},
    {"ao", 0, NULL},
    {"as", 0, NULL},
    {"at", 0, NULL},
    {"aw", 0, NULL},
    {"ax", 0, NULL},
    {"ar", 0, NULL},
    {"al", 0, NULL},

    {"b1", 0, NULL},
    {"b2", 0, NULL},
    {"bb", 1, NULL},
    {"be", 0, NULL},
    {"bf", 1, "Blend finish"},
    {"bh", 0, NULL},
    {"bl", 1, NULL},
    {"bm", 1, NULL},
    {"bj", 1, NULL},
    {"bs", 1, "Blend start"},
    {"bu", 0, NULL},
    {"bw", 0, NULL},
    {"bx", 0, NULL},

    {"bpd", 1, NULL},
    {"bps", 1, NULL},
    {"bpn", 1, NULL},
    {"bpf", 0, NULL},
    {"bpp", 1, NULL},
    {"bpb", 1, NULL},
    {"bpo", 0, NULL},
    {"bz",  0, NULL},

    {"ba", 1, NULL},
    {"bc", 1, NULL},
    {"bd", 0, NULL},
    {"bg", 0, NULL},
    {"bi", 1, NULL},
    {"bk", 1, NULL},
    {"bn", 0, NULL},
    {"bo", 1, NULL},
    {"br", 0, "Draw additively?"},
    {"bt", 0, NULL},
    {"by", 0, NULL},

    {"cf", 0, NULL},
    {"cg", 0, NULL},
    {"cl", 0, NULL},
    {"cp", 0, "Apply damage?"},
    {"cw", 0, NULL},
    {"cx", 1, NULL},
    {"cy", 1, NULL},

    {"d", 1, "Re-enter animation at N ticks"},
    {"e", 0, NULL},
    {"f", 0, "Flip sprite vertically?"},
    {"g", 0, NULL},
    {"h", 0, NULL},
    {"i", 0, NULL},

    {"jf2", 0, "Allow chaining to destruction?"},
    {"jf", 0, "Allow chaining to scrap?"},
    {"jg", 0, NULL},
    {"jh", 0, "Allow chaining to 'high' moves?"},
    {"jj", 0, NULL},
    {"jl", 0, "Allow chaining to 'low' moves?"},
    {"jm", 0, "Allow chaining to 'mid' moves?"},
    {"jp", 0, NULL},
    {"jz", 0, "Allow chaining to anything? (Katana head stomp)"},
    {"jn", 1, "Allow chaining to move N?"},

    {"k",   1, NULL},
    {"l",   1, NULL},
    {"ma",  1, NULL},
    {"mc",  0, NULL},
    {"md",  1, "Destroy animation N?"},
    {"mg",  1, NULL},
    {"mi",  1, NULL},
    {"mm",  1, NULL},
    {"mn",  1, NULL},
    {"mo",  0, NULL},
    {"mp",  1, NULL},
    {"mrx", 1, NULL},
    {"mry", 1, NULL},
    {"ms",  0, NULL},
    {"mu",  1, NULL},
    {"mx",  1, "X position of new animation?"},
    {"my",  1, "Y position of new animation?"},
    {"m",   1, "Create instance of animation N"},
    {"n",   0, NULL},
    {"ox",  1, NULL},
    {"oy",  1, NULL},
    {"pa",  0, NULL},
    {"pb",  1, NULL},
    {"pc",  1, NULL},
    {"pd",  1, NULL},
    {"pe",  0, NULL},
    {"ph",  0, NULL},
    {"pp",  1, NULL},
    {"ps",  0, NULL},
    {"ptd", 1, NULL},
    {"ptp", 1, NULL},
    {"ptr", 1, NULL},
    {"q",   0, NULL},
    {"r",   0, "Flip sprite horizontally?"},
    {"s",   1, "Play sound N from sound table footer"},
    {"sa",  0, NULL},
    {"sb",  1, NULL},
    {"sc",  1, NULL},
    {"sd",  0, NULL},
    {"se",  1, NULL},
    {"sf",  1, "Sound frequency?"},
    {"sl",  1, "Sound loudness?"},
    {"smf", 1, "Stop playing music track N"},
    {"smo", 1, "Play music track N"},

    {"sp",  1, "Sound panning?"},
    {"sw",  1, NULL},
    {"t",   0, NULL},
    {"ua",  0, NULL},
    {"ub",  0, "Motion blur effect?"},
    {"uc",  0, NULL},
    {"ud",  0, NULL},
    {"ue",  0, NULL},
    {"uf",  0, NULL},
    {"ug",  0, NULL},
    {"uh",  0, NULL},
    {"uj",  0, NULL},
    {"ul",  0, NULL},
    {"un",  0, NULL},
    {"ur",  0, NULL},
    {"us",  0, NULL},
    {"uz",  0, NULL},
    {"v",   0, NULL},
    {"vsx", 0, NULL},
    {"vsy", 0, NULL},
    {"w",   0, NULL},

    {"x-", 1, "Decrement X coordinate by N?"},
    {"x+", 1, "Increment X coordinate by N?"},
    {"x=", 1, "Interpolate X coordinate to N by next frame?"},
    {"x",  1, "Set X to N (N defaults to 100)?"}, // if unspecified a value of 100 is assumed

    {"y-", 1, "Decrement Y coordinate by N?"},
    {"y+", 1, "Increment Y coordinate by N?"},
    {"y=", 1, "Interpolate to Y coordinate to N by next frame"},
    {"y",  1, "Set Y coordinatr to N (N defaults to 100)?"}, // if unspecified a value of 100 is assumed

    {"zg", 0, "Never used?"},
    {"zh", 0, "Never used?"},
    {"zj", 0, "Invulnerable to jumping attacks?"},
    {"zl", 0, "Never used?"},
    {"zm", 0, "Never used?"},
    {"zp", 0, "Invulnerable to projectiles?"},
    {"zz", 0, "Invulnerable to any attacks?"}
};

static void sd_taglist_add_tag(tag_list *list, const sd_stringparser_tag_info *attrib) {
    tag_list **plist = &list;
    const char *ptag = attrib->tag;
    do {
        if(*plist == NULL) {
            *plist = malloc(sizeof(tag_list));
            memset(*plist, 0, sizeof(tag_list));
        }
        plist = &((*plist)->tag_chain[(unsigned char)*ptag]);
    } while(*(++ptag));
    if(*plist == NULL) {
        *plist = malloc(sizeof(tag_list));
        memset(*plist, 0, sizeof(tag_list));
    }
    (*plist)->attrib.tag_info = attrib;
}

static tag_attribute *sd_taglist_find_tag(tag_list *list, const char *tag) {
    tag_list **plist = &list;
    const char *ptag = tag;
    do {
        plist = &((*plist)->tag_chain[(unsigned char)*ptag]);
    } while(*(++ptag));

    if(*plist && strcmp((*plist)->attrib.tag_info->tag, tag) == 0) {
        return &(*plist)->attrib;
    }

    return NULL;
}

static void sd_taglist_init(tag_list *list) {
    unsigned int tag_count =  sizeof(tags)/sizeof(sd_stringparser_tag_info);
    for(int i = 0; i < tag_count; ++i) {
        sd_taglist_add_tag(list, tags + i);
    }
}

static void sd_taglist_clear(tag_list *list) {
    unsigned int len = sizeof(list->tag_chain)/sizeof(tag_list*);
    for(int i = 0;i < len;++i) {
        if(list->tag_chain[i]) {
            sd_taglist_clear(list->tag_chain[i]);
            list->tag_chain[i] = NULL;
        }
    }
}

static void sd_taglist_set_cb(tag_list *list, const char *tag, sd_stringparser_cb_t cb, void *data) {
    if(tag) {
        tag_attribute *tag_attrib = sd_taglist_find_tag(list, tag);

        if(tag_attrib) {
            tag_attrib->callback = cb;
            tag_attrib->data = data;
        }
    } else {
        list->default_cb = cb;
        list->default_cb_data = data;
    }
}

static void sd_framelist_clear(frame_list *list) {
    for (int i = 0;i < list->num_frames;++i)
    {
        anim_frame *f = &list->frames[i];
        free(f->tag_params);
        free(f->tags);
        memset(f, 0, sizeof(anim_frame));
    }
    free(list->frames);
    list->frames = NULL;
    list->current_frame = 0;
    list->num_frames = 0;
}

static void sd_framelist_set(frame_list *list, int cur_frame, char frame_letter, int duration) {
    anim_frame *frame = &list->frames[cur_frame];
    frame->frame_letter = frame_letter;
    frame->duration = duration;
    frame->num_tags = 0;
}

static void sd_framelist_add_tag(frame_list *list, int cur_frame, tag_attribute *tag_attrib, int param){
    anim_frame *cur = &list->frames[cur_frame];
    cur->num_tags++;
    int ntags = cur->num_tags;

    cur->tag_params = realloc(cur->tag_params, ntags*sizeof(int));
    cur->tags = realloc(cur->tags, ntags*sizeof(char*));

    cur->tag_params[ntags-1] = param;
    cur->tags[ntags-1] = tag_attrib->tag_info->tag;
}

static void sd_framelist_resize(frame_list *list, int frames) {
    int prev_num_frames = list->num_frames;
    list->num_frames = frames;
    if(frames > 0) {
        list->frames = realloc(list->frames, frames*sizeof(anim_frame));
        if(frames > prev_num_frames) memset(list->frames+prev_num_frames, 0, (frames-prev_num_frames)*sizeof(anim_frame));
    } else {
        free(list->frames);
        list->frames = NULL;
    }
    list->current_frame = 0;
}

static void sd_framelist_process(frame_list *frames, tag_list *tags, unsigned int ticks) {
    anim_frame *cur = frames->current_frame < frames->num_frames ? 
                      &frames->frames[frames->current_frame] : 
                      NULL;

    // only skip frame if the difference between the last tick and current tick is greater than 1
    if(abs((long long)ticks-(long long)frames->last_tick) > 1) {
        // skip to the next frame 

        cur = &frames->frames[0];
        unsigned int next_frame_time=0;
        int i;
        for(i = 0;i < frames->num_frames;++i) {
            next_frame_time += cur->duration;
            if(ticks < next_frame_time) {
                cur = &frames->frames[i];
                frames->current_frame = i;
                break;
            }
        }
        // reached the end, set to null
        if(i == frames->num_frames) {
            cur = NULL;
        }
    }

    // only handle frame once
    if(cur && ticks >= cur->start_tick) {

        if(frames->frame_change_cb) {        
            sd_stringparser_cb_param cb_param = { NULL, //tag info 
                                                  0, //tag value
                                                  ticks, 
                                                  cur->duration,
                                                  cur->frame_letter,
                                                  frames->frame_change_data, // userdata
                                                };
            frames->frame_change_cb(&cb_param);
        }

        for(int i = 0;i < cur->num_tags;++i) {
            tag_attribute *tag = sd_taglist_find_tag(tags, cur->tags[i]);
            if(tag) {
                sd_stringparser_cb_param cb_param = { tag->tag_info,
                                                      cur->tag_params[i],
                                                      ticks, 
                                                      cur->duration,
                                                      cur->frame_letter,
                                                      NULL // userdata
                                                    };
                if(tag->callback) {
                    cb_param.userdata = tag->data;
                    tag->callback(&cb_param);
                } else if(tags->default_cb) {
                    cb_param.userdata = tags->default_cb_data;
                    tags->default_cb(&cb_param);
                }
            }
        }
        frames->current_frame++;
    }

    frames->last_tick = ticks;
}

/* Reads next integer value from string (eg md15s5- -> reads "15" and leaves the position to point to 's'. */
int rn_int(int *pos, const char *str) {
    int opos = 0;
    char buf[20];
    memset(buf, 0, 20);
    if (str[*pos] == '-' && str[(*pos)+1] >= '0' && str[(*pos)+1] <= '9') {
        // begins with - and is followed by a digit, must be negative number
        buf[opos] = str[*pos];
        (*pos)++;
        opos++;
    }
    while(str[*pos] >= '0' && str[*pos] <= '9') {
        buf[opos] = str[*pos];
        (*pos)++;
        opos++;
    }

    if(opos == 0) return 0;
    return atoi(buf);
}

// Reads frame letter and duration
void rn_frame(const char **str, char *frame_letter, int *duration) {
    if(isupper(**str)) {
        *frame_letter = **str;
        (*str)++;
    } else { 
        frame_letter = 0;
    }

    int pos=0;
    *duration = rn_int(&pos, *str);
    *str += pos;
}

// returns 0 if found, otherwise return nonzero
int rn_tag_attribute(tag_list *list, const char **str, tag_attribute *attrib) {
    const int N_LOOK = 3;
    const tag_attribute *scanned[N_LOOK];
    int nscanned = 0;
    tag_list *cur=list->tag_chain[(unsigned char)**str];

    memset(scanned, 0, sizeof(scanned));

    const char *skipto = *str;

    do {
        if(cur == NULL) {
            break;
        } else {
            scanned[nscanned] = &cur->attrib;
            nscanned++;
            if(nscanned == N_LOOK) {
                break;
            }
        }
        ++(*str);
        cur = cur->tag_chain[(unsigned char)**str];
    } while(**str);

    memset(attrib, 0, sizeof(tag_attribute));
    for(nscanned--;nscanned >= 0;--nscanned, --(*str)) {
        if(scanned[nscanned]->tag_info) {
            *attrib = *scanned[nscanned];
            *str = skipto + strlen(attrib->tag_info->tag);
            return 0;
        }
    }
    // skip current tag if its not found
    *str = skipto + 1;

    return 1;
}
void rn_descriptor_marker(const char **str) {
    if(**str == '-') (*str)++;
}

// skip to the next tag or frame
int next_tag(const char **str) {
    do {
        if(islower(**str)) {
            return TAG_TAG;
        } else if(isupper(**str)) {
            return TAG_FRAME;
        } else if(**str == '-' && isupper(*((*str)+1))) {
            return TAG_MARKER;
        }
    } while(*((*str)++));

    (*str)--;
    return TAG_END;
}

void parse_string(sd_stringparser *parser, 
                  void(*frame_cb)(sd_stringparser*, void*, char,int), 
                  void(*tag_cb)(sd_stringparser*, void*, tag_attribute*, int param), 
                  void *data) {

    if(parser->string) {
        int pos = 0;
        while(1) {
            const char *start = parser->string + pos;
            const char *str = parser->string + pos;

            int type = next_tag(&str);
            pos += (str-start);
            start = parser->string + pos;

            if(type == TAG_TAG) {
                // a tag
                tag_attribute attrib;
                if(rn_tag_attribute(parser->tag_list, &str, &attrib) == 0) {
                    // read the numeric param and call the callback function
                    // if a param is not present, 0 is assumed
                    int pos = 0;
                    int param = rn_int(&pos, str);
                    str += pos;
                    if(tag_cb) tag_cb(parser, data, &attrib, param);
                }
            } else if(type == TAG_FRAME) {
                // a frame
                int duration=0;
                char frame_letter=0;
                rn_frame(&str, &frame_letter, &duration);
                if(frame_cb) frame_cb(parser, data, frame_letter, duration);
            } else if(type == TAG_MARKER) {
                // an end of frame descriptor marker (a dash, '-')
                rn_descriptor_marker(&str);
            } else {
                // the end of stream
                break;
            }

            pos += (str-start);
        }
    }
}

void cb_count_frame(sd_stringparser *parser, void *pframes, char frame_letter, int duration) {
    int *frames = pframes;
    (*frames)++;
}
void cb_set_frame(sd_stringparser *parser, void *pcur_frame, char frame_letter, int duration) {
    int *cur_frame = pcur_frame;
    sd_framelist_set(parser->frame_list, *cur_frame, frame_letter, duration);

    frame_list *flist = parser->frame_list;

    anim_frame *cur = &flist->frames[*cur_frame];
    if(*cur_frame > 0) {
        // calculate the current frame's starting tick 
        anim_frame *prev = &flist->frames[*cur_frame-1];
        cur->start_tick = prev->start_tick + prev->duration;
    } else {
        cur->start_tick = 0;
    }

    (*cur_frame)++;
}

void cb_store_tag(sd_stringparser *parser, void *pcur_frame, tag_attribute *tag_attrib, int param) {
    int *cur_frame = pcur_frame;
    sd_framelist_add_tag(parser->frame_list, *cur_frame, tag_attrib, param);
}

sd_stringparser* sd_stringparser_create() {
    sd_stringparser *parser = (sd_stringparser*)malloc(sizeof(sd_stringparser));
    parser->tag_list = malloc(sizeof(tag_list));
    memset(parser->tag_list, 0 , sizeof(tag_list));
    parser->frame_list = malloc(sizeof(frame_list));
    memset(parser->frame_list, 0 , sizeof(frame_list));
    parser->string = 0;
    sd_taglist_init(parser->tag_list);
    sd_stringparser_reset(parser);
    return parser;
}

void sd_stringparser_delete(sd_stringparser *parser) {
    if(parser) {
        if(parser->string) free(parser->string);
        sd_taglist_clear(parser->tag_list);
        sd_framelist_clear(parser->frame_list);
        free(parser->frame_list);
        free(parser->tag_list);
        free(parser);
    }
}

int sd_stringparser_set_string(sd_stringparser *parser, const char *string) {
    if(parser->string) free(parser->string);
    parser->string = strdup(string);

    int frames=0;
    parse_string(parser, cb_count_frame, NULL, &frames);
    sd_framelist_resize(parser->frame_list, frames);

    // ignore animation string that does not contain frame letters
    if(frames > 0) {
        frames = 0;
        parse_string(parser, cb_set_frame, NULL, &frames);

        // now store the tags and params into the frame struct
        frames = 0;
        parse_string(parser, cb_count_frame, cb_store_tag, &frames);
        return 0;
    }
    return SD_ANIM_INVALID_STRING;
}

void sd_stringparser_set_cb(sd_stringparser *parser, const char *tag, sd_stringparser_cb_t cb, void *data) {
    sd_taglist_set_cb(parser->tag_list, tag, cb, data);
}

void sd_stringparser_set_default_cb(sd_stringparser *parser, sd_stringparser_cb_t cb, void *data) {
    sd_taglist_set_cb(parser->tag_list, NULL, cb, data);
}

void sd_stringparser_set_frame_change_cb(sd_stringparser *parser, sd_stringparser_cb_t callback, void *userdata) {
    frame_list *flist = parser->frame_list;
    flist->frame_change_cb = callback;
    flist->frame_change_data = userdata;
}

void sd_stringparser_reset(sd_stringparser *parser) {
    ((frame_list*)parser->frame_list)->current_frame = 0;
}


int sd_stringparser_run(sd_stringparser *parser, unsigned int ticks) {
    // Right, so
    // 1. Jump to the part of the string that "ticks" variable points to
    // 2. If the part has already been handled, just return.
    //    If not, parse the string part, call callbacks, etc.
    // 3. Return 0 for success, != 0 for somekind of error (use sd_error for returning string representation)

    sd_framelist_process(parser->frame_list, parser->tag_list, ticks);

    return 0;
}

int sd_stringparser_prettyprint_frame(sd_stringparser *parser, unsigned int frame) {
    unsigned int frames = ((frame_list*)parser->frame_list)->num_frames;
    if (frame < frames) {
        anim_frame f = ((frame_list*)parser->frame_list)->frames[frame];
        printf("Sprite %c for %u ticks with %d tags\n", f.frame_letter, f.duration, f.num_tags);
        for (int i = 0; i < f.num_tags; i++) {
            tag_attribute *tag_attrib = sd_taglist_find_tag((tag_list*)parser->tag_list, f.tags[i]);
            if (tag_attrib) {
                const char * desc = tag_attrib->tag_info->description ? tag_attrib->tag_info->description : "Unknown";
                if (tag_attrib->tag_info->has_param) {
                    printf("\t Tag %s, value %d, description %s\n", f.tags[i], f.tag_params[i], desc);
                } else {
                    printf("\t Tag %s, description %s\n", f.tags[i], desc);
                }
            }
        }
        return 0;
    }
    return 1;
}

int sd_stringparser_prettyprint(sd_stringparser *parser) {
    unsigned int frames = ((frame_list*)parser->frame_list)->num_frames;
    printf("Animation string contains %d frames\n", frames);
    for (int i = 0; i < frames; i++) {
        sd_stringparser_prettyprint_frame(parser, i);
    }
    return 0;
}
