#include "unity/unity.h"
#include "syntropic/util/syn_scurve.h"

static SYN_SCurve sc;

void test_scurve_init(void) {
    syn_scurve_init(&sc, 100);
    TEST_ASSERT_EQUAL(100, sc.p);
    TEST_ASSERT_EQUAL(0, sc.v);
    TEST_ASSERT_EQUAL(0, sc.a);
    TEST_ASSERT_TRUE(sc.done);
}

void test_scurve_motion(void) {
    syn_scurve_init(&sc, 0);
    syn_scurve_set_constraints(&sc, 100, 10, 2);
    syn_scurve_set_target(&sc, 1000);
    
    TEST_ASSERT_FALSE(sc.done);
    
    /* Simulate motion */
    int ticks = 0;
    while (!sc.done && ticks < 2000) {
        syn_scurve_update(&sc);
        ticks++;
    }
    
    if (!sc.done) {
        printf("Failed to finish! p=%d, v=%d, a=%d, target=%d\n",
               sc.p, sc.v, sc.a, sc.target_p);
    }
    TEST_ASSERT_TRUE(sc.done);
    TEST_ASSERT_EQUAL(1000, sc.p);
    TEST_ASSERT_EQUAL(0, sc.v);
    TEST_ASSERT_EQUAL(0, sc.a);
    TEST_ASSERT_TRUE(ticks > 0 && ticks < 2000);
}

void test_scurve_reverse(void) {
    syn_scurve_init(&sc, 500);
    syn_scurve_set_constraints(&sc, 50, 5, 1);
    syn_scurve_set_target(&sc, 0);
    
    int ticks = 0;
    while (!sc.done && ticks < 2000) {
        syn_scurve_update(&sc);
        ticks++;
    }
    
    TEST_ASSERT_TRUE(sc.done);
    TEST_ASSERT_EQUAL(0, sc.p);
    TEST_ASSERT_EQUAL(0, sc.v);
}

/** set_target with dist==0 — exercises early-return done=true path */
void test_scurve_target_same(void) {
    syn_scurve_init(&sc, 42);
    syn_scurve_set_constraints(&sc, 100, 10, 2);
    /* Set target to current position */
    syn_scurve_set_target(&sc, 42);
    TEST_ASSERT_TRUE(sc.done);
    TEST_ASSERT_EQUAL(7, sc.current_phase);

    /* update when done — returns current position */
    int32_t p = syn_scurve_update(&sc);
    TEST_ASSERT_EQUAL(42, p);
}

/** Long motion with cruise phase — exercises syn_isqrt, Tv>0, case 3 in switch */
void test_scurve_cruise_phase(void) {
    /* Use large target so D_ad < d → Tv > 0 (cruise phase executes) */
    syn_scurve_init(&sc, 0);
    syn_scurve_set_constraints(&sc, 100, 10, 2);
    /* Tj=5, vj=50, Ta=5, vmax=100, D_ad=100*(10+5)=1500; d=5000>1500 → Tv>0 */
    syn_scurve_set_target(&sc, 5000);
    TEST_ASSERT_FALSE(sc.done);
    TEST_ASSERT_TRUE(sc.phase_ticks[3] > 0); /* cruise ticks */

    int ticks = 0;
    bool cruise_seen = false;
    while (!sc.done && ticks < 10000) {
        if (sc.current_phase == 3) cruise_seen = true;
        syn_scurve_update(&sc);
        ticks++;
    }
    TEST_ASSERT_TRUE(sc.done);
    TEST_ASSERT_TRUE(cruise_seen); /* case 3 was executed */
    TEST_ASSERT_EQUAL(5000, sc.p);
}

/** Small v_max relative to j_max — forces syn_isqrt branch (v_max <= vj) */
void test_scurve_isqrt_branch(void) {
    /* j_max=10, a_max=100, v_max=5 → Tj=a_max/j_max=10, vj=j_max*10^2=1000 >> v_max=5
     * So v_max < vj → Tj = isqrt(v_max/j_max) = isqrt(0) → forced to 1.
     * BUT for isqrt to exercise the loop body (lines 33-35), we need n>0.
     * Use j_max=2, a_max=100, v_max=8 → Tj=50, vj=2*2500=5000 >> v_max=8
     * → isqrt(v_max/j_max) = isqrt(4) = 2 (exercises loop body) */
    syn_scurve_init(&sc, 0);
    syn_scurve_set_constraints(&sc, 8, 100, 2);
    syn_scurve_set_target(&sc, 200);
    TEST_ASSERT_FALSE(sc.done);

    int ticks = 0;
    while (!sc.done && ticks < 5000) {
        syn_scurve_update(&sc);
        ticks++;
    }
    TEST_ASSERT_TRUE(sc.done);
    TEST_ASSERT_EQUAL(200, sc.p);
}

/** Short motion that forces Tj-- reduction (Tj > 1, Ta == 0 path) */
void test_scurve_tj_reduction(void) {
    /* Small distance — D_ad > d, so reduction loop runs.
     * With v_max=100, a_max=10, j_max=2, d=10:
     *   Tj=5, Ta=5, D_ad = 100 * 15 = 1500 >> 10 → loops many times
     *   Eventually Ta=0, then Tj decreases (line 91) */
    syn_scurve_init(&sc, 0);
    syn_scurve_set_constraints(&sc, 100, 10, 2);
    syn_scurve_set_target(&sc, 10); /* very short — forces Tj reduction */
    TEST_ASSERT_FALSE(sc.done);

    int ticks = 0;
    while (!sc.done && ticks < 500) {
        syn_scurve_update(&sc);
        ticks++;
    }
    TEST_ASSERT_TRUE(sc.done);
    TEST_ASSERT_EQUAL(10, sc.p);
}

void run_scurve_tests(void) {
    RUN_TEST(test_scurve_init);
    RUN_TEST(test_scurve_motion);
    RUN_TEST(test_scurve_reverse);
    RUN_TEST(test_scurve_target_same);
    RUN_TEST(test_scurve_cruise_phase);
    RUN_TEST(test_scurve_isqrt_branch);
    RUN_TEST(test_scurve_tj_reduction);
}
