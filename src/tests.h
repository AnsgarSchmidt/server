#ifndef ERESSEA_TESTS_H
#define ERESSEA_TESTS_H

#define ASSERT_DBL_DELTA 0.001

enum param_t;

struct region;
struct unit;
struct faction;
struct message;
struct message_list;
struct race;
struct item_type;
struct building_type;
struct ship_type;
struct terrain_type;
struct castorder;
struct spellparameter;
struct locale;
struct strlist;
struct log_t;

struct CuTest;

void test_setup_test(struct CuTest *tc, const char *file, int line);
#define test_setup() test_setup_test(NULL, __FILE__, __LINE__)
#define test_setup_ex(tc) test_setup_test(tc, __FILE__, __LINE__)

void test_teardown(void);
void test_reset(void);
void test_log_stderr(int on);
struct log_t * test_log_start(int flags, struct strlist **slist);
void test_log_stop(struct log_t *log, struct strlist *slist);

void test_create_calendar(void);
void test_use_astral(void);
struct locale * test_create_locale(void);
struct terrain_type * test_create_terrain(const char * name, int flags);
struct race *test_create_race(const char *name);
struct region *test_create_region(int x, int y, const struct terrain_type *terrain);
struct region *test_create_ocean(int x, int y);
struct region *test_create_plain(int x, int y);
struct faction* test_create_faction(void);
struct faction *test_create_faction_ex(const struct race *rc, const struct locale *lang);
struct unit *test_create_unit(struct faction *f, struct region *r);
void test_create_world(void);
struct item_type * test_create_horse(void);
struct item_type * test_create_cart(void);
struct item_type * test_create_silver(void);
struct building * test_create_building(struct region * r, const struct building_type * btype);
struct ship * test_create_ship(struct region * r, const struct ship_type * stype);
struct item_type * test_create_itemtype(const char * name);
struct ship_type *test_create_shiptype(const char * name);
struct building_type *test_create_buildingtype(const char *name);
struct building_type* test_create_castle(void);
void test_create_castorder(struct castorder *co, struct unit *u, int level, float force, int range, struct spellparameter *par);
struct spell * test_create_spell(void);
int test_set_item(struct unit * u, const struct item_type *itype, int value);

void test_translate_param(const struct locale *lang, enum param_t param, const char *text);
const char * test_get_messagetype(const struct message *msg);
int test_count_messagetype(struct message_list *msgs, const char *name);
struct message *test_find_messagetype_ex(struct message_list *msgs, const char *name, struct message *prev);
struct message *test_find_messagetype(struct message_list *msgs, const char *name);
struct message *test_find_region_message(const struct region *r, const char *name, const struct faction *f);
struct message *test_find_faction_message(const struct faction *f, const char *name);
struct message *test_get_last_message(struct message_list *mlist);
void test_clear_messages(struct faction *f);
void test_clear_region_messages(struct region *r);
void test_clear_messagelist(struct message_list **msgs);
void assert_message(struct CuTest * tc, struct message *msg, char *name, int numpar);

void assert_pointer_parameter(struct CuTest * tc, struct message *msg, int index, void *arg);
void assert_int_parameter(struct CuTest * tc, struct message *msg, int index, int arg);
void assert_string_parameter(struct CuTest * tc, struct message *msg, int index, const char *arg);

void disabled_test(void *suite, void (*)(struct CuTest *), const char *name);

#define DISABLE_TEST(SUITE, TEST) disabled_test(SUITE, TEST, #TEST)

#endif
