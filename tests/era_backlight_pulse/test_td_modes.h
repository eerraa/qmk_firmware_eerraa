/* The report assertions run the real TD owner and physical matrix path. */
TEST_F(EraBacklightTapHoldTiming, ImmediateBasePreservesPhysicalEdgesAndWaitsOnlyForSecondPress) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_TRNS));
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_TRNS));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, MO(1)));
    ASSERT_TRUE(era_tapdance_set_mode(0, 2));

    EXPECT_REPORT(driver, (KC_CAPS));
    key.press(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_EMPTY_REPORT(driver);
    key.release(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    key.press(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 2U);
    key.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_REPORT(driver, (KC_CAPS));
    key.press(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_NO_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_EMPTY_REPORT(driver);
    key.release(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, ImmediateDoubleTapKeepsFirstBaseAndCanSuppressOnlyTheSecond) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_TRNS));
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, MO(1)));
    ASSERT_TRUE(era_tapdance_set_mode(0, 2));
    for (uint16_t output : {KC_X, KC_NO}) {
        ASSERT_TRUE(era_tapdance_set_action(0, 2, output));
        EXPECT_REPORT(driver, (KC_CAPS));
        key.press(); run_one_scan_loop();
        EXPECT_EMPTY_REPORT(driver);
        key.release(); run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_NO_REPORT(driver);
        key.press(); run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
        if (output == KC_X) {EXPECT_REPORT(driver, (KC_X)); EXPECT_EMPTY_REPORT(driver);}
        else {EXPECT_NO_REPORT(driver);}
        key.release(); run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
}

TEST_F(EraBacklightTapHoldTiming, EveryActionCombinationKeepsHoldAndTapHoldFallbacksDistinct) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    for (uint8_t mask = 0; mask < 8; ++mask) {
        SCOPED_TRACE(mask);
        testing::InSequence sequence;
        ASSERT_TRUE(era_tapdance_set_mode(0, 0));
        ASSERT_TRUE(era_tapdance_set_action(0, 1, mask & 1 ? MO(1) : KC_TRNS));
        ASSERT_TRUE(era_tapdance_set_action(0, 2, mask & 2 ? KC_X : KC_TRNS));
        ASSERT_TRUE(era_tapdance_set_action(0, 3, mask & 4 ? MO(2) : KC_TRNS));
        ASSERT_TRUE(era_tapdance_set_mode(0, 1));

        if (mask & 1) {EXPECT_NO_REPORT(driver);} else {EXPECT_REPORT(driver, (KC_CAPS));}
        key.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        EXPECT_EQ(layer_state, mask & 1 ? 2U : 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);
        if (mask & 1) {EXPECT_NO_REPORT(driver);} else {EXPECT_EMPTY_REPORT(driver);}
        key.release(); run_one_scan_loop();
        EXPECT_EQ(layer_state, 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);

        if (!(mask & 4)) {EXPECT_REPORT(driver, (KC_CAPS)); EXPECT_EMPTY_REPORT(driver);}
        if (!(mask & 4) && !(mask & 1)) {EXPECT_REPORT(driver, (KC_CAPS));}
        if (mask & 4) {EXPECT_NO_REPORT(driver);}
        key.press(); run_one_scan_loop(); key.release(); run_one_scan_loop();
        key.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        EXPECT_EQ(layer_state, mask & 4 ? 4U : mask & 1 ? 2U : 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);
        if (!(mask & 4) && !(mask & 1)) {EXPECT_EMPTY_REPORT(driver);} else {EXPECT_NO_REPORT(driver);}
        key.release(); run_one_scan_loop();
        EXPECT_EQ(layer_state, 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
}

TEST_F(EraBacklightTapHoldTiming, ExplicitNoInputDoesNotFallBackAndActiveActionsAreCaptured) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, KC_NO));
    ASSERT_TRUE(era_tapdance_set_mode(0, 1));
    EXPECT_NO_REPORT(driver);
    key.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    key.release(); run_one_scan_loop();
    key.press(); run_one_scan_loop(); key.release(); run_one_scan_loop();
    key.press(); run_one_scan_loop(); key.release(); run_one_scan_loop();
    key.press(); run_one_scan_loop(); key.release(); run_one_scan_loop();
    key.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    key.release(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);

    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_TRNS));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, MO(1)));
    ASSERT_TRUE(era_tapdance_set_mode(0, 2));
    EXPECT_REPORT(driver, (KC_CAPS));
    key.press(); run_one_scan_loop();
    ASSERT_TRUE(era_tapdance_set_mode(0, 1));
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, MO(2)));
    EXPECT_EMPTY_REPORT(driver);
    key.release(); run_one_scan_loop();
    key.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 2U);
    key.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, IndependentHoldTimeAndInterruptResolveTheSecondPressWithoutAnotherBase) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    auto other = KeymapKey(0, 1, 1, KC_A);
    auto layered = KeymapKey(1, 1, 1, KC_B);
    set_keymap({key, other, layered});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_TRNS));
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_TRNS));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, MO(1)));
    ASSERT_TRUE(era_tapdance_set_mode(0, 2));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 2000));
    ASSERT_TRUE(era_tapdance_set_hold_term(0, 180));
    for (uint8_t early : {0, 1}) {
        ASSERT_TRUE(era_tapdance_set_hold_other(0, early));
        EXPECT_REPORT(driver, (KC_CAPS)); key.press(); run_one_scan_loop();
        EXPECT_EMPTY_REPORT(driver); key.release(); run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_NO_REPORT(driver); idle_for(800); key.press(); run_one_scan_loop();
        EXPECT_EQ(layer_state, 0U);
        if (!early) {
            idle_for(160); EXPECT_EQ(layer_state, 0U);
            idle_for(30); EXPECT_EQ(layer_state, 2U);
        }
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_REPORT(driver, (KC_B)); other.press(); run_one_scan_loop();
        EXPECT_EQ(layer_state, 2U);
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_NO_REPORT(driver); key.release(); run_one_scan_loop(); EXPECT_EQ(layer_state, 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_EMPTY_REPORT(driver); other.release(); run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
    /* First press is still a physical base key when another key interrupts. */
    EXPECT_REPORT(driver, (KC_CAPS)); key.press(); run_one_scan_loop();
    EXPECT_REPORT(driver, (KC_CAPS, KC_A)); other.press(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    EXPECT_REPORT(driver, (KC_CAPS)); other.release(); run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver); key.release(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);
    era_tapdance_set_hold_term(0, 0); era_tapdance_set_hold_other(0, 0);
}
