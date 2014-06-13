#include <platform.h>
#include "types.h"
#include "jsonconf.h"
#include "race.h"
#include "building.h"
#include "ship.h"
#include <CuTest.h>
#include <cJSON.h>
#include <tests.h>
#include <stdio.h>

static void check_flag(CuTest *tc, const char *name, int flag) {
    char data[1024];
    const struct race *rc;
    cJSON *json;
    sprintf(data, "{\"races\" : { \"orc\": { \"%s\" : true }}}", name);

    json = cJSON_Parse(data);
    free_races();
    json_config(json);
    rc = rc_find("orc");
    CuAssertPtrNotNull(tc, rc);
    CuAssertIntEquals(tc, flag, rc->flags);
}

static void test_flags(CuTest *tc) {
    check_flag(tc, "playerrace", RCF_PLAYERRACE);
    check_flag(tc, "scarepeasants", RCF_SCAREPEASANTS);
    check_flag(tc, "cansteal", RCF_CANSTEAL);
    check_flag(tc, "noheal", RCF_NOHEAL);
    check_flag(tc, "undead", RCF_UNDEAD);
    check_flag(tc, "dragon", RCF_DRAGON);
    check_flag(tc, "fly", RCF_FLY);
    test_cleanup();
}

static void test_races(CuTest * tc)
{
    const char * data = "{\"races\": { \"orc\" : { "
        "\"damage\" : \"1d4\","
        "\"magres\" : 1.0,"
        "\"maxaura\" : 2.0,"
        "\"regaura\" : 3.0,"
        "\"speed\" : 4.0,"
        "\"recruitcost\" : 1,"
        "\"maintenance\" : 2,"
        "\"weight\" : 3,"
        "\"capacity\" : 4,"
        "\"hp\" : 5,"
        "\"ac\" : 6"
        "}}}";
    cJSON *json = cJSON_Parse(data);
    const struct race *rc;

    test_cleanup();

    CuAssertPtrNotNull(tc, json);
    CuAssertPtrEquals(tc, 0, races);
    json_config(json);

    CuAssertPtrNotNull(tc, races);
    rc = rc_find("orc");
    CuAssertPtrNotNull(tc, rc);
    CuAssertStrEquals(tc, "1d4", rc->def_damage);
    CuAssertDblEquals(tc, 1.0, rc->magres, 0.0);
    CuAssertDblEquals(tc, 2.0, rc->maxaura, 0.0);
    CuAssertDblEquals(tc, 3.0, rc->regaura, 0.0);
    CuAssertDblEquals(tc, 4.0, rc->speed, 0.0);
    CuAssertIntEquals(tc, 1, rc->recruitcost);
    CuAssertIntEquals(tc, 2, rc->maintenance);
    CuAssertIntEquals(tc, 3, rc->weight);
    CuAssertIntEquals(tc, 4, rc->capacity);
    CuAssertIntEquals(tc, 5, rc->hitpoints);
    CuAssertIntEquals(tc, 6, rc->armor);
    test_cleanup();
}

static void test_ships(CuTest * tc)
{
    const char * data = "{\"ships\": { \"boat\" : { "
        "\"construction\" : { \"maxsize\" : 20, \"reqsize\" : 10, \"minskill\" : 1 }"
        "}}}";

    cJSON *json = cJSON_Parse(data);
    const struct ship_type *st;

    test_cleanup();

    CuAssertPtrNotNull(tc, json);
    CuAssertPtrEquals(tc, 0, shiptypes);
    json_config(json);

    CuAssertPtrNotNull(tc, shiptypes);
    st = st_find("boat");
    CuAssertPtrNotNull(tc, st);
    CuAssertPtrNotNull(tc, st->construction);
    CuAssertIntEquals(tc, 10, st->construction->reqsize);
    CuAssertIntEquals(tc, 20, st->construction->maxsize);
    CuAssertIntEquals(tc, 1, st->construction->minskill);
    test_cleanup();
}

CuSuite *get_jsonconf_suite(void)
{
    CuSuite *suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, test_ships);
    SUITE_ADD_TEST(suite, test_races);
    SUITE_ADD_TEST(suite, test_flags);
    return suite;
}

