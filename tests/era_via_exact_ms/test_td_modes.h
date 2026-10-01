TEST(EraExactMs, TapDanceModeWirePersistenceAndReload) {
    era_tapdance_init();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        ASSERT_TRUE(era_tapdance_set_mode(slot, 0));
        ASSERT_TRUE(era_tapdance_set_action(slot, 0, KC_TAB));
        ASSERT_TRUE(era_tapdance_set_action(slot, 1, KC_TRNS));
        ASSERT_TRUE(era_tapdance_set_action(slot, 2, KC_TRNS));
        ASSERT_TRUE(era_tapdance_set_action(slot, 3, MO(1)));
    }
    era_tapdance_save_config();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        uint8_t report[32] = {id_custom_get_value, id_custom_channel, static_cast<uint8_t>(80 + slot)};
        ASSERT_TRUE(era_tapdance_is_value_id(report[2]));
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
        EXPECT_EQ(report[3], 0);
        EXPECT_EQ(report[4], 0xd2);
        const auto before = era_state_sync_config_revision();
        report[0] = id_custom_set_value; report[3] = 3;
        EXPECT_FALSE(era_tapdance_handle_via_command(report, sizeof(report)));
        EXPECT_EQ(era_state_sync_config_revision(), before);
        report[3] = slot % 2 ? 1 : 2;
        EXPECT_FALSE(era_tapdance_handle_via_command(report, 3));
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
        EXPECT_NE(era_state_sync_config_revision(), before);
        EXPECT_EQ(report[4], 0xd2);
        const auto after = era_state_sync_config_revision();
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
        EXPECT_EQ(era_state_sync_config_revision(), after);
        EXPECT_EQ(era_tapdance_get_mode(slot), slot % 2 ? 1 : 2);
    }
    EXPECT_FALSE(era_tapdance_is_value_id(104));
    era_tapdance_reload_from_eeprom();
    for (uint8_t slot = 0; slot < 8; ++slot) EXPECT_EQ(era_tapdance_get_mode(slot), 0);
    for (uint8_t slot = 0; slot < 8; ++slot) ASSERT_TRUE(era_tapdance_set_mode(slot, slot % 2 ? 1 : 2));
    uint8_t save[32] = {id_custom_save, id_custom_channel};
    ASSERT_TRUE(era_tapdance_handle_via_command(save, sizeof(save)));
    for (uint8_t slot = 0; slot < 8; ++slot) ASSERT_TRUE(era_tapdance_set_mode(slot, 0));
    /* This is also the split ERA_CONFIG-domain reload entry. */
    era_tapdance_reload_from_eeprom();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        EXPECT_EQ(era_tapdance_get_mode(slot), slot % 2 ? 1 : 2);
        EXPECT_EQ(era_tapdance_get_action(slot, 0), KC_TAB);
        EXPECT_EQ(era_tapdance_get_action(slot, 3), MO(1));
    }
    EXPECT_FALSE(era_tapdance_set_action(0, 1, KC_NO));
    EXPECT_FALSE(era_tapdance_set_action(0, 1, MO(2)));
    EXPECT_EQ(era_tapdance_get_action(0, 1), KC_TRNS);
    ASSERT_TRUE(era_tapdance_set_mode(0, 1));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_NO));
    EXPECT_FALSE(era_tapdance_set_mode(0, 2));
    EXPECT_EQ(era_tapdance_get_mode(1), 1);
    EXPECT_EQ(era_tapdance_get_mode(4), 2);
    for (uint8_t slot = 0; slot < 8; ++slot) ASSERT_TRUE(era_tapdance_set_mode(slot, 0));
}

TEST(EraExactMs, TapDanceAdvancedTimingWireAndReload) {
    era_tapdance_init();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        ASSERT_TRUE(era_tapdance_set_hold_term(slot, 0));
        ASSERT_TRUE(era_tapdance_set_hold_other(slot, 0));
    }
    era_tapdance_save_config();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        uint8_t report[32] = {id_custom_get_value, id_custom_channel, static_cast<uint8_t>(80 + slot)};
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
        EXPECT_EQ(report[5], 0xd3);
        report[0] = id_custom_set_value; report[2] = 88 + slot; report[3] = 0xff; report[4] = 0xff;
        EXPECT_FALSE(era_tapdance_handle_via_command(report, 4));
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
        report[0] = id_custom_get_value;
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
        EXPECT_EQ(report[3], 255); EXPECT_EQ(report[4], 255); EXPECT_EQ(report[5], 0xd3);
        report[0] = id_custom_set_value; report[2] = 96 + slot; report[3] = 2;
        EXPECT_FALSE(era_tapdance_handle_via_command(report, sizeof(report)));
        report[3] = 1;
        EXPECT_FALSE(era_tapdance_handle_via_command(report, 3));
        ASSERT_TRUE(era_tapdance_handle_via_command(report, sizeof(report)));
    }
    era_tapdance_reload_from_eeprom();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        EXPECT_EQ(era_tapdance_get_hold_term(slot), 0);
        EXPECT_FALSE(era_tapdance_get_hold_other(slot));
        era_tapdance_set_hold_term(slot, slot ? 65535 : 180);
        era_tapdance_set_hold_other(slot, 1);
    }
    era_tapdance_save_config();
    era_tapdance_set_hold_term(0, 0); era_tapdance_set_hold_other(0, 0);
    era_tapdance_reload_from_eeprom();
    EXPECT_EQ(era_tapdance_get_hold_term(0), 180); EXPECT_TRUE(era_tapdance_get_hold_other(0));
    EXPECT_EQ(era_tapdance_get_hold_term(7), 65535); EXPECT_TRUE(era_tapdance_get_hold_other(7));
    for (uint8_t slot = 0; slot < 8; ++slot) {
        era_tapdance_set_hold_term(slot, 0); era_tapdance_set_hold_other(slot, 0);
    }
    era_tapdance_save_config();
}

TEST(EraExactMs, TimingPersistenceInvalidatesConfigButNotProtectedLocalState) {
    const auto before = era_state_sync_config_revision();
    era_state_sync_note_eeprom_span(37 + 172, 24);
    EXPECT_NE(era_state_sync_config_revision(), before);
    era_state_sync_note_config_semantic_commit(172, 24);
    const auto published = era_state_sync_config_revision();
    era_state_sync_config_persist_begin(172, 24);
    era_state_sync_note_eeprom_span(37 + 172, 24);
    era_state_sync_config_persist_end();
    EXPECT_EQ(era_state_sync_config_revision(), published);
    era_state_sync_note_eeprom_span(37 + 200, 80);
    EXPECT_EQ(era_state_sync_config_revision(), published);
}
