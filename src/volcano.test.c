#include <platform.h>
#include <tests.h>
#include "volcano.h"

#include <kernel/region.h>
#include <kernel/terrain.h>

#include <util/attrib.h>

#include <attributes/reduceproduction.h>

#include <CuTest.h>

static void test_volcano_update(CuTest *tc) {
    region *r;
    const struct terrain_type *t_volcano, *t_active;
    
    test_cleanup();
    t_volcano = test_create_terrain("volcano", LAND_REGION);
    t_active = test_create_terrain("activevolcano", LAND_REGION);
    r = test_create_region(0, 0, t_active);
    a_add(&r->attribs, make_reduceproduction(25, 10));

    volcano_update();
    CuAssertPtrNotNull(tc, test_find_messagetype(r->msgs, "volcanostopsmoke"));
    CuAssertPtrEquals(tc, (void *)t_volcano, (void *)r->terrain);
    
    test_cleanup();
}

static void test_volcano_outbreak(CuTest *tc) {
    region *r;
    const struct terrain_type *t_volcano, *t_active;
    
    test_cleanup();
    t_volcano = test_create_terrain("volcano", LAND_REGION);
    t_active = test_create_terrain("activevolcano", LAND_REGION);
    r = test_create_region(0, 0, t_active);

    volcano_outbreak(r);
    CuAssertPtrEquals(tc, (void *)t_volcano, (void *)r->terrain);
    CuAssertPtrNotNull(tc, test_find_messagetype(r->msgs, "volcanooutbreak"));
    CuAssertPtrNotNull(tc, a_find(r->attribs, &at_reduceproduction));

    test_cleanup();
}

CuSuite *get_volcano_suite(void)
{
    CuSuite *suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, test_volcano_update);
    SUITE_ADD_TEST(suite, test_volcano_outbreak);
    return suite;
}
