/* Central hook registration and ordered patch installation. */

static void patch_menu_trace_hooks(void) {
#ifdef GUNBROS_ENABLE_PERF_TRACE
    g_perf_mission_bind = hook_symbol_store_arm_trampoline_checked(
        "Mission Bind/timing", "_ZN12CMenuMission4BindEv",
        (uintptr_t)perf_mission_bind, &h_perf_mission_bind, 0xe92d4ff0u, 0xe5901008u);
    g_perf_mission_info_bind = hook_symbol_store_arm_trampoline_checked(
        "MissionInfo Bind/timing", "_ZN16CMenuMissionInfo4BindEv",
        (uintptr_t)perf_mission_info_bind, &h_perf_mission_info_bind, 0xe92d40f0u, 0xe5903004u);
    g_perf_list_bind = hook_symbol_store_arm_trampoline_checked(
        "List Bind/timing", "_ZN9CMenuList4BindEv",
        (uintptr_t)perf_list_bind, &h_perf_list_bind, 0xe92d45f0u, 0xe5903004u);
#endif
    /* Callback's first two ARM instructions are relocatable. Do not install a
     * partial notification patch on a different executable revision. */
    g_planet_unflatten = (planet_unflatten_fn)so_symbol(&so_mod,
        "_ZNK8CGunBros20UnFlattenObjectIndexE14GameObjectTypetRtRh");
    g_planet_available = (planet_query_fn)so_symbol(&so_mod,
        "_ZNK15CContentTracker8UserData24HasObjectBecomeAvailableE14GameObjectTypeth");
    g_planet_is_seen = (planet_query_fn)so_symbol(&so_mod,
        "_ZNK15CContentTracker8UserData17HasObjectBeenSeenE14GameObjectTypeth");
    g_planet_set_seen = (planet_seen_fn)so_symbol(&so_mod,
        "_ZN15CContentTracker8UserData17ObjectHasBeenSeenE14GameObjectTypethh");
    uintptr_t planet_callback = so_symbol(&so_mod,
        "_ZN12CMenuMission14PlanetCallbackEPviPK4Rect");
    if (g_planet_unflatten && g_planet_available && g_planet_is_seen && g_planet_set_seen &&
        planet_callback && !(planet_callback & 1u) &&
        ((const uint32_t *)planet_callback)[0] == 0xe92d41f0u &&
        ((const uint32_t *)planet_callback)[1] == 0xe5d03024u) {
        g_original_planet_callback = hook_symbol_store_arm_trampoline_checked(
            "Planet notification persistence", "_ZN12CMenuMission14PlanetCallbackEPviPK4Rect",
            (uintptr_t)trace_planet_callback, &h_planet_callback, 0xe92d41f0u, 0xe5d03024u);
        if (h_planet_callback.addr)
            hook_symbol_store("Planet selection acknowledgement",
                "_ZN15CContentTracker8UserData17ObjectHasBeenSeenE14GameObjectTypethh",
                (uintptr_t)trace_planet_object_seen, &h_planet_seen);
    }
    g_game_reset_state_settings = so_symbol(&so_mod, "_ZN5CGame18ResetStateSettingsEv");
    if (g_game_reset_state_settings) {
        /* Cold constructors use the original-entry continuation because their
         * prologues include PC-relative loads. Do not copy those into a trampoline. */
        hook_symbol_store("CGame constructor/inactive render state",
            "_ZN5CGameC1EP8CGunBros", (uintptr_t)trace_game_construct_complete,
            &h_game_construct_complete);
        hook_symbol_store("CGame base constructor/inactive render state",
            "_ZN5CGameC2EP8CGunBros", (uintptr_t)trace_game_construct_base,
            &h_game_construct_base);
    }
    hook_symbol_store("Classic online branch guard", "_ZN11CMenuSystem9SetBranchE10MenuBranch10MenuScreen",
        (uintptr_t)classic_set_branch, &h_classic_branch);
    hook_symbol_store("Classic Options background", "_ZN9CMenuList6OnShowEv",
        (uintptr_t)classic_options_show, &h_classic_options_show);
    hook_symbol_store("Classic menu guard", "_ZN11CMenuSystem7SetMenuE10MenuScreent10MenuBranch",
        (uintptr_t)trace_menu_system_set_menu, &h_menu_system_set_menu);
    hook_symbol_store("Classic popup guard", "_ZN11CMenuSystem8PushMenuE10MenuScreent10MenuBranch",
        (uintptr_t)trace_menu_system_push_menu, &h_menu_system_push_menu);
    hook_symbol_store("Classic Bank", "_ZN10CMenuStore7RefreshE10MenuActioni",
        (uintptr_t)classic_store_refresh, &h_classic_store_refresh);
    g_original_classic_provider_state = hook_symbol_store_arm_literal_trampoline_checked(
        "Classic refinery state/hot provider", "_ZN17CMenuDataProvider20GetElementValueInt32E16MenuDataCategoryij",
        (uintptr_t)classic_provider_state, &h_classic_provider_state, 0xe92d4ff0u, 0xe59f4914u);
    hook_symbol_store("Classic refinery purchase", "_ZN18CRefinementManager10UnlockSlotEjP15CPlayerProgress",
        (uintptr_t)classic_unlock, &h_classic_unlock);
    patch_store_stat_format_conversion();
    install_platform_music_hooks();
    gunbros_audio_set_game_vorbis_decoder(so_symbol(
        &so_mod,
        "_ZN3com3glu8platform10components21DecodeVorbisBitstreamEPKhjRPhRjhS7_S7_S7_"));
    configure_media_player_sound_enable(so_symbol(
        &so_mod,
        "_ZN3com3glu8platform10components12CMediaPlayer15SetSoundEnabledEh"));
    hook_symbol_store("CResourceLoader/AUDIO CMediaPlayer::PlayInternal",
                      "_ZN3com3glu8platform10components12CMediaPlayer12PlayInternalEPNS2_7CBinaryEhhNS2_13ICMediaPlayer15SoundStreamTypeE",
                      (uintptr_t)redirect_media_player_play_internal,
                      &h_media_player_play_internal);
    hook_symbol_store("AUDIO COptionsMgr::Read/default music",
                      "_ZN11COptionsMgr4ReadEv",
                      (uintptr_t)redirect_options_read,
                      &h_options_read);
    hook_symbol_store("AUDIO CBGM::Play(track)",
                      "_ZN4CBGM4PlayE8BGMTrackh",
                      (uintptr_t)redirect_bgm_play_track,
                      &h_bgm_play_track);
    g_original_resource_load_next =
        hook_symbol_store_arm_trampoline_checked(
            "CResourceLoader::LoadNext",
            "_ZN15CResourceLoader8LoadNextEv",
            (uintptr_t)trace_resource_load_next,
            &h_resource_load_next,
            0xe92d4030u, 0xe5904008u);
    g_original_gunbros_update =
        hook_symbol_store_arm_trampoline_checked(
            "CGunBros::Update", "_ZN8CGunBros6UpdateEi",
            (uintptr_t)trace_gunbros_update, &h_gunbros_update,
            0xe92d4ff0u, 0xe5d073d4u);
    g_original_game_update =
        hook_symbol_store_arm_trampoline_checked(
            "CGame::Update/gameplay-speed", "_ZN5CGame6UpdateEi",
            (uintptr_t)trace_game_update_scaled, &h_game_update,
            0xe92d41f0u, 0xe590300cu);
    g_original_game_load =
        hook_symbol_store_arm_trampoline_checked(
            "CGame::Load/menu-residency", "_ZN5CGame4LoadEP15CResourceLoader",
            (uintptr_t)trace_game_load, &h_game_load,
            0xe92d4070u, 0xe1a04000u);
    hook_symbol_store("CGunBros::ReInit",
                      "_ZN8CGunBros6ReInitEv",
                      (uintptr_t)trace_gunbros_reinit,
                      &h_gunbros_reinit);
    hook_symbol_store("CGunBros::ReInitializeAll",
                      "_ZN8CGunBros15ReInitializeAllEv",
                      (uintptr_t)trace_gunbros_reinitialize_all,
                      &h_gunbros_reinitialize_all);
    hook_symbol_store("CGunBros::Bind",
                      "_ZN8CGunBros4BindEv",
                      (uintptr_t)trace_gunbros_bind,
                      &h_gunbros_bind);
    hook_symbol_store("CBrother::Bind/diagnostic",
                      "_ZN8CBrother4BindEP4CMapPKNS_8TemplateEP20CPlayerConfigurationRK15CPlayerProgress",
                      (uintptr_t)trace_brother_bind_trace,
                      &h_brother_bind_trace);
    hook_symbol_store("CArmor::Template::Validate",
                      "_ZNK6CArmor8Template8ValidateEv",
                      (uintptr_t)trace_armor_template_validate,
                      &h_armor_template_validate);
    hook_symbol_store("CPlayer::Bind/diagnostic",
                      "_ZN7CPlayer4BindEP4CMapPKN8CBrother8TemplateEP20CPlayerConfigurationRK15CPlayerProgress",
                      (uintptr_t)trace_player_bind_trace,
                      &h_player_bind_trace);
    g_original_player_update_trace =
        hook_symbol_store_arm_trampoline_checked(
            "CPlayer::Update/input-facing",
            "_ZN7CPlayer6UpdateEi",
            (uintptr_t)trace_player_update_trace,
            &h_player_update_trace,
            0xe92d4ff0u, 0xe5d03785u);
    hook_symbol_store("CLevel::OnStart",
                      "_ZN6CLevel7OnStartEv",
                      (uintptr_t)trace_level_on_start,
                      &h_level_on_start);
    g_original_mastery_upgrade = hook_symbol_store_arm_trampoline_checked(
        "CWeaponMastery::Upgrade/local save", "_ZN14CWeaponMastery7UpgradeEPN4CGun8TemplateEthhj",
        (uintptr_t)trace_mastery_upgrade, &h_mastery_upgrade, 0xe92d47f0u, 0xe24dd008u);
    g_original_mastery_get_xp = hook_symbol_store_arm_trampoline_checked(
        "CWeaponMastery::GetXP/local restore", "_ZNK14CWeaponMastery5GetXPEthh",
        (uintptr_t)trace_mastery_get_xp, &h_mastery_get_xp, 0xe92d4010u, 0xe590c000u);
    hook_symbol_store("CWeaponMastery::LoadFromDisk/local restore",
        "_ZN14CWeaponMastery12LoadFromDiskEP15CResourceLoader",
        (uintptr_t)trace_mastery_load_file, &h_mastery_load_file);
    hook_symbol_store("CWeaponMastery::LoadFromDisk(stream)/local restore",
        "_ZN14CWeaponMastery12LoadFromDiskERN3com3glu8platform10components12CInputStreamEi",
        (uintptr_t)trace_mastery_load_stream, &h_mastery_load_stream);
    g_original_profile_manager_save =
        hook_symbol_store_arm_trampoline_checked(
            "CProfileManager::Save/resident-transition",
            "_ZN15CProfileManager4SaveE21ProfileDataStoreTypesP15CResourceLoaderh",
            (uintptr_t)trace_profile_manager_save,
            &h_profile_manager_save,
            0xe92d4ff0u, 0xe5d0c0dau);
    hook_symbol_store("CGunBros::LoadMenus",
                      "_ZN8CGunBros9LoadMenusEv",
                      (uintptr_t)trace_gunbros_load_menus,
                      &h_gunbros_load_menus);
    hook_symbol_store("CGunBros::LoadMission",
                      "_ZN8CGunBros11LoadMissionEv",
                      (uintptr_t)trace_gunbros_load_mission,
                      &h_gunbros_load_mission);
    g_original_gunbros_show_main_menu =
        hook_symbol_store_arm_trampoline_checked(
            "CGunBros::ShowMainMenu/menu-residency",
            "_ZN8CGunBros12ShowMainMenuE10MenuScreen",
            (uintptr_t)trace_gunbros_show_main_menu,
            &h_gunbros_show_main_menu,
            0xe92d4070u, 0xe5905004u);
    /* CGunBros::SetMenu was a trace/perf-context wrapper only.  Leaving the
     * native entry untouched avoids a code rewrite plus I-cache flush on each
     * visible menu transition in release builds. */
    g_original_registry_flatten = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::FlattenObjectIndex const/hot continuation", "_ZNK8CGunBros18FlattenObjectIndexE14GameObjectTypethRt",
        (uintptr_t)trace_gunbros_flatten_object_index_const, &h_gunbros_flatten_object_index_const, 0xe92d00f0u, 0xe590c13cu);
    g_original_registry_count = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::GetObjectCount const/hot continuation", "_ZNK8CGunBros14GetObjectCountE14GameObjectType",
        (uintptr_t)trace_gunbros_get_object_count_const, &h_gunbros_get_object_count_const, 0xe92d0070u, 0xe590313cu);
    g_original_registry_validate = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::ValidateGameObject const/hot continuation", "_ZNK8CGunBros18ValidateGameObjectE14GameObjectTypeth",
        (uintptr_t)trace_gunbros_validate_game_object_3_const, &h_gunbros_validate_game_object_3_const, 0xe351001bu, 0xe52d4004u);
    g_original_registry_validate_ref = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::ValidateGameObject(ref) const/hot continuation", "_ZNK8CGunBros18ValidateGameObjectE14GameObjectTypeRKN11IGameObject13GameObjectRefE",
        (uintptr_t)trace_gunbros_validate_game_object_ref_const, &h_gunbros_validate_game_object_ref_const, 0xe5d23006u, 0xe1d220b4u);
    /* Do not wrap the flattened GetGameObject overloads or the nested
     * UnFlattenObjectIndex method. Their original implementations already
     * propagate a false lookup. The exact UnFlatten null dereference is
     * guarded inline so no C wrapper/trampoline participates in this path. */
    g_original_registry_get = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::GetGameObject(pack)/hot continuation", "_ZN8CGunBros13GetGameObjectE14GameObjectTypeth",
        (uintptr_t)trace_gunbros_get_game_object_pack, &h_gunbros_get_game_object_pack, 0xe52d4004u, 0xe590c13cu);
    g_original_registry_get_const = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::GetGameObject(pack) const/hot continuation", "_ZNK8CGunBros13GetGameObjectE14GameObjectTypeth",
        (uintptr_t)trace_gunbros_get_game_object_pack_const, &h_gunbros_get_game_object_pack_const, 0xe52d4004u, 0xe590c13cu);
    g_original_registry_load_req = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::LoadGameObjectReq/hot continuation", "_ZN8CGunBros17LoadGameObjectReqE14GameObjectTypeth",
        (uintptr_t)trace_gunbros_load_game_object_req, &h_gunbros_load_game_object_req, 0xe92d4070u, 0xe24dd010u);
    g_original_registry_free_req = hook_symbol_store_arm_trampoline_checked(
        "CGunBros::FreeGameObjectReq/hot continuation", "_ZN8CGunBros17FreeGameObjectReqE14GameObjectTypeth",
        (uintptr_t)trace_gunbros_free_game_object_req, &h_gunbros_free_game_object_req, 0xe92d4070u, 0xe24dd010u);
    hook_symbol_store("CBrother::Spawn (construct)",
                      "_ZN8CBrother5SpawnEPKNS_9SpawnDataE",
                      (uintptr_t)trace_brother_spawn_construct,
                      &h_brother_spawn_construct);
    hook_symbol_store("CBrother::Respawn/loadout-residency",
                      "_ZN8CBrother7RespawnEPKNS_11RespawnDataE",
                      (uintptr_t)trace_brother_respawn_trace,
                      &h_brother_respawn_trace);
    hook_symbol_store("CWeaponMastery::AddXP",
                      "_ZN14CWeaponMastery5AddXPEP4CGunthhjj",
                      (uintptr_t)trace_weapon_mastery_add_xp,
                      &h_weapon_mastery_add_xp);
    hook_symbol_store("CMenuUpgradePopup::ShowForGuns",
                      "_ZN17CMenuUpgradePopup11ShowForGunsEh",
                      (uintptr_t)trace_menu_upgrade_popup_show_for_guns,
                      &h_menu_upgrade_popup_show_for_guns);
    hook_symbol_store("CChallengeManager::UpdateChallengeStatusData",
                      "_ZN17CChallengeManager25UpdateChallengeStatusDataEh",
                      (uintptr_t)trace_challenge_update_status_data,
                      &h_challenge_update_status_data);
    hook_symbol_store("CChallengeManager::UpdateFromLevelSession",
                      "_ZN17CChallengeManager22UpdateFromLevelSessionEPK6CLevelRKN11IGameObject13GameObjectRefEh",
                      (uintptr_t)trace_challenge_update_from_level_session,
                      &h_challenge_update_from_level_session);
    hook_symbol_store("CInputPad::Bind",
                      "_ZN9CInputPad4BindE11MissionTypeP17CMenuDataProvider",
                      (uintptr_t)trace_input_pad_bind,
                      &h_input_pad_bind);
    hook_symbol("CInput::OnTouch/contact ID", "_ZN6CInput7OnTouchEiii",
                (uintptr_t)trace_input_touch_down);
    hook_symbol("CInput::OnTouchMove/contact ID", "_ZN6CInput11OnTouchMoveEiii",
                (uintptr_t)trace_input_touch_move);
    hook_symbol("CInput::OnTouchRelease/contact ID", "_ZN6CInput14OnTouchReleaseEiii",
                (uintptr_t)trace_input_touch_release);
    g_original_input_pad_update_input =
        hook_symbol_store_arm_literal_trampoline_checked(
            "CInputPad::UpdateInput/input-release",
            "_ZN9CInputPad11UpdateInputEi",
            (uintptr_t)trace_input_pad_update_input,
            &h_input_pad_update_input,
            0xe92d41f0u, 0xe59f51ecu);
    hook_symbol_store("CGun::Fire",
                      "_ZN4CGun4FireEv",
                      (uintptr_t)trace_gun_fire,
                      &h_gun_fire);
    /* LoadMenu was trace-only.  Leaving the native entry untouched avoids an
     * instruction-cache flush on every menu resource transaction. */
    /* MenuStack Set/Push/Pop were trace-only hooks. */
    hook_symbol_store("CMenuNavigationBar::HideButtons",
                      "_ZN18CMenuNavigationBar11HideButtonsEh",
                      (uintptr_t)trace_menu_navigation_bar_hide_buttons,
                      &h_menu_navigation_bar_hide_buttons);
    g_original_player_progress_get_experience_for_level =
        hook_symbol_store_arm_trampoline_checked(
            "CPlayerProgress::GetExperienceForLevel",
            "_ZNK15CPlayerProgress21GetExperienceForLevelEv",
            (uintptr_t)trace_player_progress_get_experience_for_level,
            &h_player_progress_get_experience_for_level,
            0xe92d0070u, 0xe1d045b0u);
    g_original_player_progress_get_experience_delta =
        hook_symbol_store_arm_trampoline_checked(
            "CPlayerProgress::GetExperienceDelta",
            "_ZNK15CPlayerProgress18GetExperienceDeltaEv",
            (uintptr_t)trace_player_progress_get_experience_delta,
            &h_player_progress_get_experience_delta,
            0xe1d035b0u, 0xe590200cu);
    g_original_player_progress_get_percent_to_next_level =
        hook_symbol_store_arm_trampoline_checked(
            "CPlayerProgress::GetPercentToNextLevel",
            "_ZNK15CPlayerProgress21GetPercentToNextLevelEv",
            (uintptr_t)trace_player_progress_get_percent_to_next_level,
            &h_player_progress_get_percent_to_next_level,
            0xe92d4070u, 0xe1a04000u);
    hook_symbol_store("CFriendDataManager::GetFriendAvatarProgress",
                      "_ZN18CFriendDataManager23GetFriendAvatarProgressEi",
                      (uintptr_t)trace_friend_data_get_avatar_progress,
                      &h_friend_data_get_avatar_progress);
    hook_symbol_store("CFriendDataManager::GetFriendAvatarConfig",
                      "_ZNK18CFriendDataManager21GetFriendAvatarConfigEi",
                      (uintptr_t)trace_friend_data_get_avatar_config,
                      &h_friend_data_get_avatar_config);
    hook_symbol_store("CGameFlow::GetMission",
                      "_ZNK9CGameFlow10GetMissionEv",
                      (uintptr_t)trace_game_flow_get_mission,
                      &h_game_flow_get_mission);
    hook_symbol_store("CGame::GetPlayerData",
                      "_ZNK5CGame13GetPlayerDataERP20CPlayerConfigurationRP15CPlayerProgress",
                      (uintptr_t)trace_game_get_player_data,
                      &h_game_get_player_data);
    hook_symbol_store("CGameFlow::ConfigureBrother",
                      "_ZN9CGameFlow16ConfigureBrotherEPK20CPlayerConfigurationPK15CPlayerProgress",
                      (uintptr_t)trace_game_flow_configure_brother,
                      &h_game_flow_configure_brother);
    hook_symbol_store("CGameObjectPack::GetGameObject",
                      "_ZN15CGameObjectPack13GetGameObjectE14GameObjectTypeh",
                      (uintptr_t)trace_game_object_pack_get_game_object,
                      &h_game_object_pack_get_game_object);
    hook_symbol_store("CGameObjectPack::GetGameObject const",
                      "_ZNK15CGameObjectPack13GetGameObjectE14GameObjectTypeh",
                      (uintptr_t)trace_game_object_pack_get_game_object,
                      &h_game_object_pack_get_game_object_const);
    g_original_game_object_pack_init_game_object =
        hook_symbol_store_arm_literal_trampoline_checked(
            "CGameObjectPack::InitGameObject",
            "_ZN15CGameObjectPack14InitGameObjectE14GameObjectTypeh",
            (uintptr_t)trace_game_object_pack_init_game_object,
            &h_game_object_pack_init_game_object,
            0xe92d4ff0u, 0xe59f4108u);
    hook_symbol_store("CStoreAggregator::Configure",
                      "_ZN16CStoreAggregator9ConfigureEP10CPurchasesP20CPlayerConfigurationP15CPlayerProgress",
                      (uintptr_t)trace_store_aggregator_configure,
                      &h_store_aggregator_configure);
    g_original_store_aggregator_clear_cached_content =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::ClearCachedContent/residency",
            "_ZN16CStoreAggregator18ClearCachedContentEv",
            (uintptr_t)trace_store_aggregator_clear_cached_content,
            &h_store_aggregator_clear_cached_content,
            0xe92d4010u, 0xe5902018u);
    g_original_store_aggregator_is_item_owned_or_equipped =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::IsItemOwnedOrEquipped",
            "_ZNK16CStoreAggregator21IsItemOwnedOrEquippedERKN11IGameObject17GameObjectTypeRefE",
            (uintptr_t)trace_store_aggregator_is_item_owned_or_equipped,
            &h_store_aggregator_is_item_owned_or_equipped,
            0xe92d4070u, 0xe5912008u);
    g_original_store_aggregator_init_filtered_list =
        hook_symbol_store_arm_literal_trampoline_checked(
            "CStoreAggregator::InitFilteredList/reuse",
            "_ZN16CStoreAggregator16InitFilteredListEh",
            (uintptr_t)trace_store_aggregator_init_filtered_list,
            &h_store_aggregator_init_filtered_list,
            0xe92d4ff0u, 0xe59f2594u);
    hook_symbol_store("CStoreAggregator::AcquireItem",
                      "_ZN16CStoreAggregator11AcquireItemEPK10CStoreItemh",
                      (uintptr_t)trace_store_aggregator_acquire_item,
                      &h_store_aggregator_acquire_item);
    hook_symbol_store("CStoreAggregator::EquipItem",
                      "_ZN16CStoreAggregator9EquipItemEtP20CPlayerConfiguration",
                      (uintptr_t)trace_store_aggregator_equip_item,
                      &h_store_aggregator_equip_item);
    hook_symbol_store("CMenuMeshPlayer::BindPlayer",
                      "_ZN15CMenuMeshPlayer10BindPlayerEv",
                      (uintptr_t)trace_menu_mesh_player_bind_player,
                      &h_menu_mesh_player_bind_player);
    g_original_store_aggregator_get_item_status =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::GetItemStatus",
            "_ZN16CStoreAggregator13GetItemStatusEPK10CStoreItemh",
            (uintptr_t)trace_store_aggregator_get_item_status,
            &h_store_aggregator_get_item_status,
            0xe92d41f0u, 0xe5903060u);
    g_original_store_aggregator_is_item_level_locked =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::IsItemLevelLocked",
            "_ZNK16CStoreAggregator17IsItemLevelLockedEPK10CStoreItem",
            (uintptr_t)trace_store_aggregator_is_item_level_locked,
            &h_store_aggregator_is_item_level_locked,
            0xe3510000u, 0x01a00001u);
    g_original_store_aggregator_can_item_be_acquired =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::CanItemBeAcquired",
            "_ZNK16CStoreAggregator17CanItemBeAcquiredEPK10CStoreItem",
            (uintptr_t)trace_store_aggregator_can_item_be_acquired,
            &h_store_aggregator_can_item_be_acquired,
            0xe92d41f0u, 0xe2515000u);
    g_original_store_aggregator_get_filtered_item_count =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::GetFilteredItemCount",
            "_ZNK16CStoreAggregator20GetFilteredItemCountE14GameObjectType",
            (uintptr_t)trace_store_aggregator_get_filtered_item_count,
            &h_store_aggregator_get_filtered_item_count,
            0xe92d41f0u, 0xe1d072b8u);
    g_original_store_aggregator_get_item_cost =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::GetItemCost",
            "_ZN16CStoreAggregator11GetItemCostEt19InGameCurrencyTypes",
            (uintptr_t)trace_store_aggregator_get_item_cost,
            &h_store_aggregator_get_item_cost,
            0xe92d4010u, 0xe1a04002u);
    hook_symbol_store("CStoreAggregator::IsInAppPurchase",
                      "_ZN16CStoreAggregator15IsInAppPurchaseEt",
                      (uintptr_t)trace_store_aggregator_is_in_app_purchase,
                      &h_store_aggregator_is_in_app_purchase);
    g_original_store_aggregator_create_item_cost_string =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::CreateItemCostString",
            "_ZN16CStoreAggregator20CreateItemCostStringEt",
            (uintptr_t)trace_store_aggregator_create_item_cost_string,
            &h_store_aggregator_create_item_cost_string,
            0xe92d41f0u, 0xe24dd090u);
    g_original_store_aggregator_create_sale_string =
        hook_symbol_store_arm_trampoline_checked(
            "CStoreAggregator::CreateSaleString",
            "_ZN16CStoreAggregator16CreateSaleStringEt",
            (uintptr_t)trace_store_aggregator_create_sale_string,
            &h_store_aggregator_create_sale_string,
            0xe92d4ff0u, 0xe24dd0d4u);
    g_original_menu_store_handle_touch_input =
        hook_symbol_store_arm_trampoline_checked(
            "CMenuStore::HandleTouchInput",
            "_ZN10CMenuStore16HandleTouchInputEv",
            (uintptr_t)trace_menu_store_handle_touch_input,
            &h_menu_store_handle_touch_input,
            0xe92d41f0u, 0xe1a04000u);
    g_original_menu_store_on_show =
        hook_symbol_store_arm_literal_trampoline_checked(
            "CMenuStore::OnShow/filter-reuse",
            "_ZN10CMenuStore6OnShowEv",
            (uintptr_t)trace_menu_store_on_show,
            &h_menu_store_on_show,
            0xe92d41f0u, 0xe59f319cu);
    g_original_sprite_player_draw_rect =
        hook_symbol_store_arm_trampoline_checked(
            "CSpritePlayer::Draw(rect)",
            "_ZN13CSpritePlayer4DrawEPK4Rectssh",
            (uintptr_t)trace_sprite_player_draw_rect,
            &h_sprite_player_draw_rect,
            0xe92d47f0u, 0xe1a04000u);
    g_original_menu_system_get_font =
        hook_symbol_store_arm_trampoline_checked(
            "CMenuSystem::GetFont",
            "_ZN11CMenuSystem7GetFontE8MenuFonth",
            (uintptr_t)trace_menu_system_get_font,
            &h_menu_system_get_font,
            0xe92d40f0u, 0xe2815f52u);
    hook_symbol_store("CRefinementManager::GetIntervalDurationMS",
                      "_ZN18CRefinementManager21GetIntervalDurationMSE18RefinementInterval",
                      (uintptr_t)trace_refinement_get_interval_duration_ms,
                      &h_refinement_get_interval_duration_ms);
    hook_symbol_store("CRefinementManager::GetIntervalEfficiency",
                      "_ZN18CRefinementManager21GetIntervalEfficiencyE18RefinementInterval",
                      (uintptr_t)trace_refinement_get_interval_efficiency,
                      &h_refinement_get_interval_efficiency);
    hook_symbol_store("CRefinementManager::GetIntervalPurchaseCost",
                      "_ZN18CRefinementManager23GetIntervalPurchaseCostE18RefinementInterval19InGameCurrencyTypes",
                      (uintptr_t)trace_refinement_get_interval_purchase_cost,
                      &h_refinement_get_interval_purchase_cost);
    hook_symbol("CRefinementManager::UpdateRefinement/local clock",
                "_ZN18CRefinementManager16UpdateRefinementEv",
                (uintptr_t)trace_refinement_update_local);
    hook_symbol_store("CRefinementManager::BeginRefinement/local save",
                      "_ZN18CRefinementManager15BeginRefinementEj18RefinementIntervalyP15CPlayerProgress",
                      (uintptr_t)trace_refinement_begin_local,
                      &h_refinement_begin);
    hook_symbol_store("CRefinementManager::CollectResources/local save",
                      "_ZN18CRefinementManager16CollectResourcesEjP15CPlayerProgress",
                      (uintptr_t)trace_refinement_collect_local,
                      &h_refinement_collect_resources);
    g_original_refinery_meter_refresh =
        hook_symbol_store_arm_trampoline_checked(
            "CMenuGameResources::CResourceMeter::Refresh/perf",
            "_ZN18CMenuGameResources14CResourceMeter7RefreshEP5CMenu",
            (uintptr_t)trace_refinery_meter_refresh,
            &h_refinery_meter_refresh,
            0xe92d4030u, 0xe5d03006u);
    g_original_refinery_meter_draw =
        hook_symbol_store_arm_trampoline_checked(
            "CMenuGameResources::CResourceMeter::Draw/per-slot",
            "_ZN18CMenuGameResources14CResourceMeter4DrawEtt",
            (uintptr_t)trace_refinery_meter_draw,
            &h_refinery_meter_draw,
            0xe92d47f0u, 0xe1a05001u);
    g_original_refinery_transfer_effect_draw =
        hook_symbol_store_arm_trampoline_checked(
            "CMenuGameResources::CTransferEffect::Draw",
            "_ZN18CMenuGameResources15CTransferEffect4DrawEv",
            (uintptr_t)trace_refinery_transfer_effect_draw,
            &h_refinery_transfer_effect_draw,
            0xe92d4070u, 0xe1a04000u);
    hook_symbol_store("CMenuGameResources::OnShow",
                      "_ZN18CMenuGameResources6OnShowEv",
                      (uintptr_t)trace_menu_game_resources_on_show,
                      &h_menu_game_resources_on_show);
    hook_symbol_store("CMenuGameResources::MetersEnabled",
                      "_ZN18CMenuGameResources13MetersEnabledEh",
                      (uintptr_t)trace_menu_game_resources_meters_enabled,
                      &h_menu_game_resources_meters_enabled);
    hook_symbol("CMenuSplash::IsBusy",
                "_ZNK11CMenuSplash6IsBusyEv",
                (uintptr_t)menu_splash_busy_stub);
    hook_symbol_store("CMenuSplash::IsLoaded",
                      "_ZNK11CMenuSplash8IsLoadedEv",
                      (uintptr_t)trace_menu_splash_is_loaded,
                      &h_menu_splash_is_loaded);
    hook_symbol_store("CMenuSplash::Load",
                      "_ZN11CMenuSplash4LoadEP15CResourceLoader",
                      (uintptr_t)trace_menu_splash_load,
                      &h_menu_splash_load);
    hook_symbol_store("CResTOCManager::Init",
                      "_ZN14CResTOCManager4InitEv",
                      (uintptr_t)trace_res_toc_manager_init,
                      &h_res_toc_manager_init);
    hook_symbol_store("CResTOCManager::Bind",
                      "_ZN14CResTOCManager4BindEv",
                      (uintptr_t)trace_res_toc_manager_bind,
                      &h_res_toc_manager_bind);
    /* The three SetTargetResPack wrappers were trace-only and can run once
     * per queued resource.  Use the native entries directly. */
    g_original_image_pool_load_image =
        hook_symbol_store_arm_trampoline_checked(
            "CImagePool::LoadImage",
            "_ZN10CImagePool9LoadImageE11ImageFormatithihh",
            (uintptr_t)trace_image_pool_load_image,
            &h_image_pool_load_image,
            0xe92d41f0u, 0xe3510005u);
    hook_symbol_store("InstrTexure",
                      "_ZN3com3glu8platform8graphics11InstrTexureEPvPh",
                      (uintptr_t)trace_graphics_instr_texture,
                      &h_graphics_instr_texture);
}

void so_patch(void) {
    // Sample hook
    //hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN6glitch2os7Printer5printEPKcz"), (uintptr_t)&hookedFunction);
    patch_gunbros_unflatten_null_self_inline();
    patch_graphics2d_draw_null_surface();
    patch_brother_bind_missing_progress_table();
    patch_brother_function_resolver_bad_vtable();
    patch_brother_spawn_bad_vtable();
    patch_brother_refresh_sequence_null_state();
    patch_level_add_object_bad_vtable();
    patch_level_onstart_bad_vtable_call();
    patch_render_queue_add_bad_vtable_call();
    patch_render_sort_compare_bad_vtable_call();
    patch_render_queue_draw_bad_vtable_call();
    patch_level_draw_enemy_health_bars_bad_vtable_call();
    patch_level_draw_brother_health_bar_bad_vtable_call();
    patch_input_pad_base_draw_bad_vtable_call();
    patch_eventlog_current_equipment_stubs();
    patch_brother_update_animation_missing_movesets();
    patch_brother_update_normal_missing_current_gun();
    patch_level_on_enemy_killed_missing_mission();
    patch_script_refresh_null_state();
    patch_script_export_missing_tables();
    patch_gti2_keepalive_offline_arm_stub();
    patch_gti2_keepalive_after_append_guard();
    patch_gti2_keepalive_return_guard();
    patch_cngs_online_stubs();
    patch_menu_trace_hooks();
}
