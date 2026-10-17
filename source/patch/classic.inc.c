static uintptr_t g_original_classic_provider_state;
static so_hook h_classic_provider_state, h_classic_unlock, h_classic_store_refresh;
static so_hook h_classic_branch, h_classic_options_show;

static void classic_set_branch(void *self, int branch, int screen) {
    if (branch == 3 || branch == 4) { classic_menu_block(5, 0); return; }
    if (classic_menu_block(screen, 0)) return;
    (void)SO_CONTINUE(int, h_classic_branch, self, branch, screen);
}

static void classic_options_show(void *self) {
    unsigned char *menu = self;
    if (trace_is_game_range(menu, 0xf0)) {
        const uint32_t *config = *(const uint32_t **)(void *)(menu+8);
        unsigned char *stack = *(unsigned char **)(void *)(menu+4);
        if (trace_is_game_range(config, 12) && config[0] == 0 && config[1] == 201 &&
            trace_is_game_range(stack, 0x74)) {
            void *system = *(void **)(void *)(stack+0x70);
            typedef void *(*get_movie_fn)(void *, int);
            get_movie_fn get_movie = (get_movie_fn)(uintptr_t)so_symbol(&so_mod, "_ZN11CMenuSystem8GetMovieEi");
            if (system && get_movie) {
                unsigned char *movie = get_movie(system, 9);
                if (trace_is_game_range(movie, 0xcc)) {
                    typedef int (*dimension_fn)(void);
                    dimension_fn width = (dimension_fn)(uintptr_t)so_symbol(&so_mod, "_ZN10MainScreen8GetWidthEv");
                    dimension_fn height = (dimension_fn)(uintptr_t)so_symbol(&so_mod, "_ZN10MainScreen9GetHeightEv");
                    if (width && height) {
                        *(int16_t *)(void *)movie = width()/2;
                        *(int16_t *)(void *)(movie+2) = height()/2;
                    }
                    *(void **)(void *)(menu+0xec) = movie;
                }
            }
        }
    }
    (void)SO_CONTINUE(int, h_classic_options_show, self);
}

static int classic_provider_state(void *self, int category, int element, unsigned index) {
    if (category == 0x41 && element == 0 && index < 12) {
        void *manager = vita_refinery_live_manager();
        if (manager) {
            trace_refinement_update_local(manager);
            return *(uint32_t *)(void *)vita_refinery_slot(manager, index);
        }
    }
    GUNBROS_PERF_BEGIN(provider_begin);
    int result;
    if (g_original_classic_provider_state)
        result = ((int (*)(void *, int, int, unsigned))g_original_classic_provider_state)(self, category, element, index);
    else
        result = SO_CONTINUE(int, h_classic_provider_state, self, category, element, index);
    GUNBROS_PERF_END(GUNBROS_STAGE_MENU_PROVIDER, provider_begin, 0);
    return result;
}

static int classic_unlock(void *manager, unsigned index, void *progress) {
    if (index >= 12 || !trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) return 0;
    vita_refinery_load_sidecar(manager);
    vita_refinery_restore_resident(manager, "UnlockSlot");
    uint32_t *state = (uint32_t *)(void *)vita_refinery_slot(manager, index);
    if (*state != 0) return 1; /* No repeated debit. */
    if (index > 6) return 0; /* Opened by collecting the preceding Standard tier. */
    if (!trace_is_game_range(progress, 0x44)) return 0;
    int result = SO_CONTINUE(int, h_classic_unlock, manager, index, progress);
    if (result) {
        vita_refinery_apply_progression(manager);
        vita_refinery_write_sidecar(manager);
        vita_offline_profile_request_save();
        vita_offline_profile_save(1);
        vita_refinery_request_meter_refresh();
    }
    return result;
}

static const char *classic_bank_convert(void) {
    /* Resolve again after confirmation; never keep a borrowed UI wallet. */
    void *progress = (void *)vita_offline_profile_progress_source();
    if (!trace_is_game_range(progress, 0x44)) return "Bank is not ready. Please reopen the Store.";
    uint64_t coins = store_progress_common_currency(progress);
    uint32_t glu = store_progress_rare_currency(progress);
    if (coins < 10000) return "Bank\nYou need 10,000 Coins for this exchange.";
    if (glu > UINT32_MAX - 40u) return "Bank\nYour Glu Coin balance is full.";
    store_progress_set_common_currency(progress, coins-10000);
    store_progress_set_rare_currency(progress, glu+40);
    /* Both balances are fields in ONE checksum-protected profile snapshot. */
    if (!vita_offline_profile_save(1)) {
        store_progress_set_common_currency(progress, coins);
        store_progress_set_rare_currency(progress, glu);
        vita_offline_profile_request_save();
        return "Bank\nThe exchange could not be saved. Your balance was restored.";
    }
    vita_offline_profile_request_save();
    return "Bank\nExchange saved: 10,000 Coins converted to 40 Glu Coins.";
}

static void classic_bank_open(void) {
    void *progress = (void *)vita_offline_profile_progress_source();
    char message[256];
    snprintf(message, sizeof(message), "Classic Bank\nCoins: %llu    Glu Coins: %u\n\nExchange 10,000 Coins for 40 Glu Coins?\nThis exchange works offline.",
             (unsigned long long)store_progress_common_currency(progress),
             store_progress_rare_currency(progress));
    gunbros_classic_dialog(message, classic_bank_convert);
}

static int classic_menu_block(int screen, unsigned short arg) {
    if (screen == 5 || screen == 6) {
        gunbros_classic_dialog("Online features are exclusive to Gun Bros Reloaded.\n\nPlease use the Gun Bros Reloaded version to access online features.", NULL);
        return 1;
    }
    if (screen == 4 && arg == 3) { classic_bank_open(); return 1; }
    return 0;
}

static void classic_store_refresh(void *self, int action, int argument) {
    if (action == 0x3e && argument == 3) { classic_bank_open(); return; }
    (void)SO_CONTINUE(int, h_classic_store_refresh, self, action, argument);
}
