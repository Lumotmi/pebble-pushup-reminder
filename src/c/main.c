#include <pebble.h>

// ============================================================================
// Constants & Persist Keys
// ============================================================================
// Per-exercise keys (push-ups use these directly for backwards compatibility,
// other exercises are offset by EXERCISE_KEY_STRIDE * exercise index)
#define PERSIST_KEY_DAILY_GOAL        1
#define PERSIST_KEY_DAILY_COUNT       6
#define PERSIST_KEY_HISTORY           9   // blob: DayRecord[14]
#define PERSIST_KEY_EFFECTIVE_GOAL    10
#define PERSIST_KEY_DAY_TYPE          11
#define PERSIST_KEY_CONSEC_TRAIN_DAYS 12
#define PERSIST_KEY_ENABLED           16

// Shared keys
#define PERSIST_KEY_REMINDER_INTERVAL 2
#define PERSIST_KEY_ACTIVE_START_HOUR 3
#define PERSIST_KEY_ACTIVE_END_HOUR   4
#define PERSIST_KEY_LANGUAGE          5
#define PERSIST_KEY_LAST_DATE_YDAY    7
#define PERSIST_KEY_WAKEUP_ID         8
#define PERSIST_KEY_AUTO_DISMISS_DURATION 13
#define PERSIST_KEY_STRICT_MODE       14
#define PERSIST_KEY_SNOOZE_UNTIL      15

#define EXERCISE_KEY_STRIDE           100

// Default values
#define DEFAULT_REMINDER_INTERVAL 60
#define DEFAULT_ACTIVE_START_HOUR 8
#define DEFAULT_ACTIVE_END_HOUR   20
#define DEFAULT_AUTO_DISMISS_DURATION 20

// Wakeup cookie
#define WAKEUP_COOKIE_REMINDER 0

// Adaptive goals constants
#define HISTORY_SIZE            14

#define MAX_CONSECUTIVE_TRAIN   3
#define OVERLOAD_INCREASE_PCT   8   // 8% increase on goal met
#define DELOAD_DECREASE_PCT     10  // 10% decrease on < 80% achieved
#define DELOAD_THRESHOLD_PCT    80  // below this % triggers deload
#define FATIGUE_EXCESS_PCT      150 // >150% of goal triggers rest day

// ============================================================================
// Exercises
// ============================================================================
typedef enum {
  EX_PUSHUPS = 0,
  EX_PULLUPS = 1,
  EX_SQUATS  = 2,
  EX_COUNT
} ExerciseType;

static const uint16_t EXERCISE_DEFAULT_GOAL[EX_COUNT] = { 30, 10, 30 };
static const uint8_t  EXERCISE_PICKER_STEP[EX_COUNT]  = { 5, 1, 5 };

// ============================================================================
// Adaptive Goals: Day Types & History
// ============================================================================
typedef enum {
  DAY_TYPE_TRAINING_NORMAL  = 0,
  DAY_TYPE_TRAINING_OVERLOAD = 1,
  DAY_TYPE_TRAINING_DELOAD   = 2,
  DAY_TYPE_REST              = 3
} DayType;

typedef struct __attribute__((packed)) {
  uint16_t target;     // goal for that day
  uint16_t achieved;   // actual reps done
  uint8_t  day_type;   // DayType enum
  uint16_t yday;       // tm_yday (0-365)
} DayRecord;

typedef struct {
  bool      enabled;
  uint16_t  daily_goal;          // user-set minimum
  uint16_t  daily_count;
  uint16_t  effective_goal;      // adaptive goal for today
  DayType   day_type;
  uint8_t   consecutive_train_days;
  DayRecord history[HISTORY_SIZE];
  uint8_t   history_count;       // number of valid entries (0..14)
} ExerciseState;

static ExerciseState s_ex[EX_COUNT];

static uint32_t ex_key(ExerciseType ex, uint32_t key) {
  return key + (uint32_t)ex * EXERCISE_KEY_STRIDE;
}

// ============================================================================
// Language Support
// ============================================================================
typedef enum {
  LANG_DE = 0,
  LANG_EN = 1
} AppLanguage;

static AppLanguage s_language = LANG_DE;

static const char* translate(const char* de, const char* en) {
  return (s_language == LANG_EN) ? en : de;
}

static const char* exercise_name(ExerciseType ex) {
  switch (ex) {
    case EX_PULLUPS: return translate("Klimmzüge", "Pull-ups");
    case EX_SQUATS:  return translate("Kniebeugen", "Squats");
    default:         return translate("Liegestütze", "Push-ups");
  }
}

// ============================================================================
// Settings State
// ============================================================================
static uint16_t s_reminder_interval = DEFAULT_REMINDER_INTERVAL;
static uint8_t  s_active_start_hour = DEFAULT_ACTIVE_START_HOUR;
static uint8_t  s_active_end_hour   = DEFAULT_ACTIVE_END_HOUR;
static int      s_auto_dismiss_duration = DEFAULT_AUTO_DISMISS_DURATION;
static bool     s_strict_mode       = false;
static AppTimer *s_auto_dismiss_timer = NULL;
static time_t   s_snooze_until      = 0;

// Daily tracking state
static int      s_last_date_yday = -1;
static WakeupId s_wakeup_id = -1;
static bool     s_launched_by_wakeup = false;

// ============================================================================
// Exercise Helpers
// ============================================================================
static bool exercise_pending(ExerciseType ex) {
  ExerciseState *e = &s_ex[ex];
  return e->enabled && e->day_type != DAY_TYPE_REST && e->daily_count < e->effective_goal;
}

// Fills `out` with enabled exercises, returns count
static int get_enabled_exercises(ExerciseType out[EX_COUNT]) {
  int n = 0;
  for (int i = 0; i < EX_COUNT; i++) {
    if (s_ex[i].enabled) out[n++] = (ExerciseType)i;
  }
  return n;
}

// Fills `out` with exercises that still need reps today, returns count
static int get_pending_exercises(ExerciseType out[EX_COUNT]) {
  int n = 0;
  for (int i = 0; i < EX_COUNT; i++) {
    if (exercise_pending((ExerciseType)i)) out[n++] = (ExerciseType)i;
  }
  return n;
}

static bool any_exercise_pending(void) {
  for (int i = 0; i < EX_COUNT; i++) {
    if (exercise_pending((ExerciseType)i)) return true;
  }
  return false;
}

// ============================================================================
// UI Windows & Layers
// ============================================================================
static Window    *s_main_menu_window     = NULL;
static MenuLayer *s_main_menu_layer      = NULL;

static Window    *s_settings_menu_window = NULL;
static MenuLayer *s_settings_menu_layer  = NULL;

static Window *s_picker_window = NULL;
static Layer  *s_picker_layer  = NULL;

static Window *s_reminder_window = NULL;
static Layer  *s_reminder_layer  = NULL;

static Window    *s_chooser_window = NULL;
static MenuLayer *s_chooser_menu_layer = NULL;

static Window *s_quicklog_window = NULL;
static Layer  *s_quicklog_layer  = NULL;

static Window    *s_history_window = NULL;
static MenuLayer *s_history_menu_layer = NULL;

// ============================================================================
// Number Picker State
// ============================================================================
typedef enum {
  PICKER_DAILY_GOAL = 0,
  PICKER_REMINDER_INTERVAL,
  PICKER_START_HOUR,
  PICKER_END_HOUR
} PickerType;

static PickerType   s_picker_type;
static ExerciseType s_picker_ex;
static int s_picker_value;
static int s_picker_min;
static int s_picker_max;
static int s_picker_step;

// ============================================================================
// Forward Declarations
// ============================================================================
static void settings_menu_window_load(Window *window);
static void settings_menu_window_unload(Window *window);
static void picker_window_load(Window *window);
static void picker_window_unload(Window *window);
static void reminder_window_load(Window *window);
static void reminder_window_unload(Window *window);
static void schedule_next_wakeup(void);
static void check_and_reset_daily_count(void);
static void run_adaptive_algorithm(ExerciseType ex);

static void open_quicklog(ExerciseType ex, bool from_reminder);
static void open_exercise_chooser(void);
static void open_history(void);
static void update_app_glance(void);


// ============================================================================
// Persistent Storage: Load & Save
// ============================================================================
static void load_exercise(ExerciseType ex) {
  ExerciseState *e = &s_ex[ex];

  e->enabled = true;
  e->daily_goal = EXERCISE_DEFAULT_GOAL[ex];
  e->daily_count = 0;
  e->day_type = DAY_TYPE_TRAINING_NORMAL;
  e->consecutive_train_days = 0;

  if (persist_exists(ex_key(ex, PERSIST_KEY_ENABLED))) {
    e->enabled = persist_read_bool(ex_key(ex, PERSIST_KEY_ENABLED));
  }
  if (persist_exists(ex_key(ex, PERSIST_KEY_DAILY_GOAL))) {
    e->daily_goal = (uint16_t)persist_read_int(ex_key(ex, PERSIST_KEY_DAILY_GOAL));
  }
  if (persist_exists(ex_key(ex, PERSIST_KEY_DAILY_COUNT))) {
    e->daily_count = (uint16_t)persist_read_int(ex_key(ex, PERSIST_KEY_DAILY_COUNT));
  }
  if (persist_exists(ex_key(ex, PERSIST_KEY_EFFECTIVE_GOAL))) {
    e->effective_goal = (uint16_t)persist_read_int(ex_key(ex, PERSIST_KEY_EFFECTIVE_GOAL));
  } else {
    e->effective_goal = e->daily_goal; // Initialize from user baseline
  }
  if (persist_exists(ex_key(ex, PERSIST_KEY_DAY_TYPE))) {
    e->day_type = (DayType)persist_read_int(ex_key(ex, PERSIST_KEY_DAY_TYPE));
  }
  if (persist_exists(ex_key(ex, PERSIST_KEY_CONSEC_TRAIN_DAYS))) {
    e->consecutive_train_days = (uint8_t)persist_read_int(ex_key(ex, PERSIST_KEY_CONSEC_TRAIN_DAYS));
  }
  // Load history blob
  if (persist_exists(ex_key(ex, PERSIST_KEY_HISTORY))) {
    int bytes_read = persist_read_data(ex_key(ex, PERSIST_KEY_HISTORY), e->history, sizeof(e->history));
    e->history_count = bytes_read > 0 ? bytes_read / sizeof(DayRecord) : 0;
    if (e->history_count > HISTORY_SIZE) e->history_count = HISTORY_SIZE;
  } else {
    e->history_count = 0;
    memset(e->history, 0, sizeof(e->history));
  }
}

static void save_exercise(ExerciseType ex) {
  ExerciseState *e = &s_ex[ex];
  persist_write_bool(ex_key(ex, PERSIST_KEY_ENABLED), e->enabled);
  persist_write_int(ex_key(ex, PERSIST_KEY_DAILY_GOAL), e->daily_goal);
  persist_write_int(ex_key(ex, PERSIST_KEY_DAILY_COUNT), e->daily_count);
  persist_write_int(ex_key(ex, PERSIST_KEY_EFFECTIVE_GOAL), e->effective_goal);
  persist_write_int(ex_key(ex, PERSIST_KEY_DAY_TYPE), e->day_type);
  persist_write_int(ex_key(ex, PERSIST_KEY_CONSEC_TRAIN_DAYS), e->consecutive_train_days);
  // Save history blob
  persist_write_data(ex_key(ex, PERSIST_KEY_HISTORY), e->history, e->history_count * sizeof(DayRecord));
}

static void load_settings(void) {
  if (persist_exists(PERSIST_KEY_REMINDER_INTERVAL)) {
    s_reminder_interval = (uint16_t)persist_read_int(PERSIST_KEY_REMINDER_INTERVAL);
  }
  if (persist_exists(PERSIST_KEY_ACTIVE_START_HOUR)) {
    s_active_start_hour = (uint8_t)persist_read_int(PERSIST_KEY_ACTIVE_START_HOUR);
  }
  if (persist_exists(PERSIST_KEY_ACTIVE_END_HOUR)) {
    s_active_end_hour = (uint8_t)persist_read_int(PERSIST_KEY_ACTIVE_END_HOUR);
  }
  if (persist_exists(PERSIST_KEY_AUTO_DISMISS_DURATION)) {
    s_auto_dismiss_duration = persist_read_int(PERSIST_KEY_AUTO_DISMISS_DURATION);
  }
  if (persist_exists(PERSIST_KEY_LANGUAGE)) {
    s_language = (AppLanguage)persist_read_int(PERSIST_KEY_LANGUAGE);
  }
  if (persist_exists(PERSIST_KEY_LAST_DATE_YDAY)) {
    s_last_date_yday = persist_read_int(PERSIST_KEY_LAST_DATE_YDAY);
  }
  if (persist_exists(PERSIST_KEY_WAKEUP_ID)) {
    s_wakeup_id = (WakeupId)persist_read_int(PERSIST_KEY_WAKEUP_ID);
  }
  if (persist_exists(PERSIST_KEY_STRICT_MODE)) {
    s_strict_mode = persist_read_bool(PERSIST_KEY_STRICT_MODE);
  }
  if (persist_exists(PERSIST_KEY_SNOOZE_UNTIL)) {
    s_snooze_until = (time_t)persist_read_int(PERSIST_KEY_SNOOZE_UNTIL);
  }
  for (int i = 0; i < EX_COUNT; i++) {
    load_exercise((ExerciseType)i);
  }
}

static void save_settings(void) {
  persist_write_int(PERSIST_KEY_REMINDER_INTERVAL, s_reminder_interval);
  persist_write_int(PERSIST_KEY_ACTIVE_START_HOUR, s_active_start_hour);
  persist_write_int(PERSIST_KEY_ACTIVE_END_HOUR, s_active_end_hour);
  persist_write_int(PERSIST_KEY_LANGUAGE, s_language);
  persist_write_int(PERSIST_KEY_LAST_DATE_YDAY, s_last_date_yday);
  persist_write_int(PERSIST_KEY_WAKEUP_ID, s_wakeup_id);
  persist_write_bool(PERSIST_KEY_STRICT_MODE, s_strict_mode);
  if (s_snooze_until > 0) {
    persist_write_int(PERSIST_KEY_SNOOZE_UNTIL, (int)s_snooze_until);
  } else {
    persist_delete(PERSIST_KEY_SNOOZE_UNTIL);
  }
  for (int i = 0; i < EX_COUNT; i++) {
    save_exercise((ExerciseType)i);
  }
}

// ============================================================================
// Daily Count Management & Adaptive Algorithm
// ============================================================================
static void archive_day_to_history(ExerciseState *e, uint16_t target, uint16_t achieved, DayType type, uint16_t yday) {
  // Shift history if full (FIFO: oldest entry at index 0)
  if (e->history_count >= HISTORY_SIZE) {
    memmove(&e->history[0], &e->history[1], (HISTORY_SIZE - 1) * sizeof(DayRecord));
    e->history_count = HISTORY_SIZE - 1;
  }
  // Append new record
  DayRecord *rec = &e->history[e->history_count];
  rec->target = target;
  rec->achieved = achieved;
  rec->day_type = (uint8_t)type;
  rec->yday = yday;
  e->history_count++;
}

static void run_adaptive_algorithm(ExerciseType ex) {
  // Determine today's goal and day type based on yesterday's performance.
  // Called for each exercise when a new day is detected.
  ExerciseState *e = &s_ex[ex];

  // If no history yet, use user-set baseline
  if (e->history_count == 0) {
    e->effective_goal = e->daily_goal;
    e->day_type = DAY_TYPE_TRAINING_NORMAL;
    e->consecutive_train_days = 0;
    return;
  }

  // Get yesterday's record (most recent in history)
  DayRecord *yesterday = &e->history[e->history_count - 1];

  // --- Rule B: Immediate rest day if yesterday had >150% of goal ---
  if (!s_strict_mode && yesterday->day_type != DAY_TYPE_REST && yesterday->target > 0) {
    uint32_t fatigue_threshold = (uint32_t)yesterday->target * FATIGUE_EXCESS_PCT / 100;
    if (yesterday->achieved > fatigue_threshold) {
      e->day_type = DAY_TYPE_REST;
      e->effective_goal = 0;
      e->consecutive_train_days = 0;
      APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: REST DAY (fatigue - yesterday %d/%d)",
              ex, yesterday->achieved, yesterday->target);
      return;
    }
  }

  // --- Rule A: Rest day after MAX_CONSECUTIVE_TRAIN consecutive training days ---
  if (!s_strict_mode && e->consecutive_train_days >= MAX_CONSECUTIVE_TRAIN) {
    e->day_type = DAY_TYPE_REST;
    e->effective_goal = 0;
    e->consecutive_train_days = 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: REST DAY (scheduled recovery after %d days)",
            ex, MAX_CONSECUTIVE_TRAIN);
    return;
  }

  // --- Training Day: Calculate new goal ---
  // If yesterday was a rest day, carry forward the last training target
  uint16_t base_goal = e->effective_goal;
  if (yesterday->day_type == DAY_TYPE_REST) {
    // Find last training day's target from history
    for (int i = e->history_count - 1; i >= 0; i--) {
      if (e->history[i].day_type != DAY_TYPE_REST && e->history[i].target > 0) {
        base_goal = e->history[i].target;
        break;
      }
    }
    if (base_goal < e->daily_goal) base_goal = e->daily_goal;

    if (yesterday->achieved > 0 && base_goal > 0) {
      uint32_t pct = (uint32_t)yesterday->achieved * 100 / base_goal;

      // Fatigue check for rest day
      if (!s_strict_mode && pct > FATIGUE_EXCESS_PCT) {
        e->day_type = DAY_TYPE_REST;
        e->effective_goal = 0;
        e->consecutive_train_days = 0;
        APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: REST DAY (fatigue from rest day - %d/%d)",
                ex, yesterday->achieved, base_goal);
        return;
      }

      if (pct >= 100) {
        // Goal met or exceeded on rest day: Progressive Overload
        uint16_t increase = (base_goal * OVERLOAD_INCREASE_PCT) / 100;
        if (increase < 1) increase = 1;
        e->effective_goal = base_goal + increase;
        e->day_type = DAY_TYPE_TRAINING_OVERLOAD;
        e->consecutive_train_days++;
        APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: OVERLOAD (from rest day) %d -> %d (%d%% achieved)",
                ex, base_goal, e->effective_goal, (int)pct);
        return;
      } else {
        // Did some reps, but < 100%. No deload penalty on rest days!
        e->day_type = DAY_TYPE_TRAINING_NORMAL;
        e->effective_goal = base_goal;
        e->consecutive_train_days++;
        APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: TRAINING NORMAL (post-rest, partial %d%%), goal=%d",
                ex, (int)pct, e->effective_goal);
        return;
      }
    }

    // 0 achieved on rest day (or no valid base goal found)
    e->day_type = DAY_TYPE_TRAINING_NORMAL;
    e->effective_goal = base_goal;
    e->consecutive_train_days++;
    APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: TRAINING NORMAL (post-rest), goal=%d",
            ex, e->effective_goal);
    return;
  }

  // Yesterday was a training day - evaluate performance
  if (yesterday->target > 0) {
    uint32_t pct = (uint32_t)yesterday->achieved * 100 / yesterday->target;

    if (pct >= 100) {
      // Goal met or exceeded: Progressive Overload
      uint16_t increase = (base_goal * OVERLOAD_INCREASE_PCT) / 100;
      if (increase < 1) increase = 1;
      e->effective_goal = base_goal + increase;
      e->day_type = DAY_TYPE_TRAINING_OVERLOAD;
      APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: OVERLOAD %d -> %d (%d%% achieved)",
              ex, base_goal, e->effective_goal, (int)pct);
    } else if (pct < DELOAD_THRESHOLD_PCT) {
      // Below 80%: Deload (unless strict mode is enabled)
      if (s_strict_mode) {
        e->effective_goal = base_goal;
        e->day_type = DAY_TYPE_TRAINING_NORMAL;
        APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: STRICT MODE DELOAD PREVENTED (keep %d, %d%% achieved)",
                ex, e->effective_goal, (int)pct);
      } else {
        uint16_t decrease = (base_goal * DELOAD_DECREASE_PCT) / 100;
        if (decrease < 1) decrease = 1;
        e->effective_goal = base_goal > decrease ? base_goal - decrease : e->daily_goal;
        if (e->effective_goal < e->daily_goal) e->effective_goal = e->daily_goal;
        e->day_type = DAY_TYPE_TRAINING_DELOAD;
        APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: DELOAD %d -> %d (%d%% achieved)",
                ex, base_goal, e->effective_goal, (int)pct);
      }
    } else {
      // Between 80-99%: Keep same goal
      e->effective_goal = base_goal;
      e->day_type = DAY_TYPE_TRAINING_NORMAL;
      APP_LOG(APP_LOG_LEVEL_INFO, "Adaptive[%d]: NORMAL (keep %d, %d%% achieved)",
              ex, e->effective_goal, (int)pct);
    }
  } else {
    e->effective_goal = e->daily_goal;
    e->day_type = DAY_TYPE_TRAINING_NORMAL;
  }

  // Enforce minimum
  if (e->effective_goal < e->daily_goal) {
    e->effective_goal = e->daily_goal;
  }

  e->consecutive_train_days++;
}

static void check_and_reset_daily_count(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int today_yday = t->tm_yday;

  if (s_last_date_yday != today_yday) {
    for (int i = 0; i < EX_COUNT; i++) {
      ExerciseState *e = &s_ex[i];
      // Archive yesterday's data to history (if there was a previous day).
      // Days on which an exercise was disabled are recorded as rest days so
      // they neither break the streak nor trigger a deload.
      if (s_last_date_yday >= 0) {
        if (e->enabled) {
          archive_day_to_history(e, e->effective_goal, e->daily_count, e->day_type,
                                 (uint16_t)s_last_date_yday);
        } else {
          archive_day_to_history(e, 0, e->daily_count, DAY_TYPE_REST,
                                 (uint16_t)s_last_date_yday);
        }
      }

      // Reset daily count and run adaptive algorithm
      e->daily_count = 0;
      run_adaptive_algorithm((ExerciseType)i);
      APP_LOG(APP_LOG_LEVEL_INFO, "New day [%d]: effective goal=%d, type=%d, streak=%d",
              i, e->effective_goal, e->day_type, e->consecutive_train_days);
    }

    s_last_date_yday = today_yday;
    save_settings();
    update_app_glance();
  }
}

// ============================================================================
// Streak & History Utilities
// ============================================================================
static uint16_t calculate_streak(ExerciseType ex) {
  ExerciseState *e = &s_ex[ex];
  uint16_t streak = 0;
  // Iterate backwards from the most recent day (index: history_count - 1)
  for (int i = e->history_count - 1; i >= 0; i--) {
    // If achieved meets target (note: Rest Days have target 0, so 0 >= 0 is true)
    if (e->history[i].achieved >= e->history[i].target) {
      streak++;
    } else {
      break; // Streak broken
    }
  }

  // Add today if already achieved
  if (e->daily_count >= e->effective_goal) {
    streak++;
  }
  return streak;
}

// ============================================================================
// App Glance Integration
// ============================================================================
#if !PBL_PLATFORM_APLITE
static void glance_reload_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
  if (limit < 1) return;

  static char glance_text[48];

  ExerciseType pending[EX_COUNT];
  int num_pending = get_pending_exercises(pending);

  if (num_pending == 0) {
    snprintf(glance_text, sizeof(glance_text), "%s", translate("Alles erledigt!", "All done!"));
  } else {
    ExerciseState *e = &s_ex[pending[0]];
    if (num_pending > 1) {
      snprintf(glance_text, sizeof(glance_text), "%s %d/%d (+%d)",
               exercise_name(pending[0]), e->daily_count, e->effective_goal, num_pending - 1);
    } else {
      snprintf(glance_text, sizeof(glance_text), "%s %d/%d",
               exercise_name(pending[0]), e->daily_count, e->effective_goal);
    }
  }

  AppGlanceSlice slice = {
    .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
    .layout = {
      .icon = APP_GLANCE_SLICE_DEFAULT_ICON,
      .subtitle_template_string = glance_text,
    }
  };

  AppGlanceResult result = app_glance_add_slice(session, slice);
  if (result != APP_GLANCE_RESULT_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Glance Error: %d", result);
  }
}
#endif

static void update_app_glance(void) {
#if !PBL_PLATFORM_APLITE
  app_glance_reload(glance_reload_callback, NULL);
#endif
}

// ============================================================================
// Sync to Phone
// ============================================================================
static void send_rep_log(ExerciseType ex, uint16_t count) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int(iter, MESSAGE_KEY_LOG_PUSHUPS, &count, sizeof(uint16_t), true);
    uint32_t timestamp = time(NULL);
    dict_write_int(iter, MESSAGE_KEY_LOG_TIMESTAMP, &timestamp, sizeof(uint32_t), true);
    uint8_t ex_id = (uint8_t)ex;
    dict_write_int(iter, MESSAGE_KEY_LOG_EXERCISE, &ex_id, sizeof(uint8_t), false);
    app_message_outbox_send();
  }
}

// Payload: [num_exercises] then per exercise:
//   [exercise_id][num_records] + num_records * [achieved_lo, achieved_hi, yday_lo, yday_hi]
#define SYNC_DATA_MAX_SIZE (1 + EX_COUNT * (2 + (HISTORY_SIZE + 1) * 4))

static void send_offline_sync_data(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    uint8_t data[SYNC_DATA_MAX_SIZE];
    int offset = 0;
    data[offset++] = EX_COUNT;

    for (int ex = 0; ex < EX_COUNT; ex++) {
      ExerciseState *e = &s_ex[ex];
      data[offset++] = (uint8_t)ex;
      data[offset++] = e->history_count + 1;

      for (int i = 0; i < e->history_count; i++) {
        data[offset++] = e->history[i].achieved & 0xFF;
        data[offset++] = (e->history[i].achieved >> 8) & 0xFF;
        data[offset++] = e->history[i].yday & 0xFF;
        data[offset++] = (e->history[i].yday >> 8) & 0xFF;
      }
      // Add today
      data[offset++] = e->daily_count & 0xFF;
      data[offset++] = (e->daily_count >> 8) & 0xFF;
      data[offset++] = s_last_date_yday & 0xFF;
      data[offset++] = (s_last_date_yday >> 8) & 0xFF;
    }

    dict_write_data(iter, MESSAGE_KEY_SYNC_OFFLINE_DATA, data, offset);
    app_message_outbox_send();
  }
}

// ============================================================================
// Wakeup Scheduling
// ============================================================================
static void schedule_next_wakeup(void) {
  // Cancel any existing wakeup
  if (s_wakeup_id >= 0) {
    wakeup_cancel(s_wakeup_id);
    s_wakeup_id = -1;
  }

  // Check day state and decide if we need to schedule for tomorrow
  check_and_reset_daily_count();

  // Nothing left to do today (all goals met, rest days or disabled)
  bool schedule_for_tomorrow = !any_exercise_pending();
  if (schedule_for_tomorrow) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Workout: Nothing pending today, scheduling wakeup for tomorrow.");
  }

  time_t now = time(NULL);
  time_t wakeup_time;

  if (s_snooze_until > now) {
    wakeup_time = s_snooze_until;
    APP_LOG(APP_LOG_LEVEL_INFO, "Workout: Snooze active, scheduling wakeup at snooze time.");
  } else {
    if (s_snooze_until != 0) {
      s_snooze_until = 0;
      persist_delete(PERSIST_KEY_SNOOZE_UNTIL);
    }

    struct tm *t = localtime(&now);
    int current_hour = t->tm_hour;

    if (schedule_for_tomorrow) {
      struct tm tomorrow = *t;
      tomorrow.tm_mday += 1;
      tomorrow.tm_hour = s_active_start_hour;
      tomorrow.tm_min = 0;
      tomorrow.tm_sec = 0;
      wakeup_time = mktime(&tomorrow);
    } else {
      // Calculate next wakeup time
      wakeup_time = now + (s_reminder_interval * 60);
      struct tm *wt = localtime(&wakeup_time);
      int wakeup_hour = wt->tm_hour;

      // If the wakeup would be outside the active window, schedule for the start of the next active window
      if (wakeup_hour < s_active_start_hour || wakeup_hour >= s_active_end_hour) {
        // If we're before the start hour today, schedule for today's start
        if (current_hour < s_active_start_hour) {
          struct tm tomorrow = *t;
          tomorrow.tm_hour = s_active_start_hour;
          tomorrow.tm_min = 0;
          tomorrow.tm_sec = 0;
          wakeup_time = mktime(&tomorrow);
        } else {
          // Schedule for tomorrow's start hour
          struct tm tomorrow = *t;
          tomorrow.tm_mday += 1;
          tomorrow.tm_hour = s_active_start_hour;
          tomorrow.tm_min = 0;
          tomorrow.tm_sec = 0;
          wakeup_time = mktime(&tomorrow);
        }
      }
    }
  }

  s_wakeup_id = wakeup_schedule(wakeup_time, WAKEUP_COOKIE_REMINDER, true);

  if (s_wakeup_id >= 0) {
    struct tm *st = localtime(&wakeup_time);
    APP_LOG(APP_LOG_LEVEL_INFO, "Workout: Wakeup scheduled at %02d:%02d (id=%d)",
            st->tm_hour, st->tm_min, (int)s_wakeup_id);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Workout: Failed to schedule wakeup: %d", (int)s_wakeup_id);
  }

  save_settings();
}

// ============================================================================
// Reminder Window (shown when wakeup fires)
// ============================================================================
static void reminder_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int inset = PBL_IF_ROUND_ELSE(24, 8);

  // Background
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Accent header bar
  int header_h = PBL_IF_ROUND_ELSE(44, 36);
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, header_h), 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, translate("TRAININGSZEIT!", "WORKOUT TIME!"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(4, header_h - 32, bounds.size.w - 8, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Progress per exercise (enabled, non-rest); finished ones are greyed out
  static char progress_bufs[EX_COUNT][16];
  int y = header_h + 10;
  for (int i = 0; i < EX_COUNT; i++) {
    ExerciseState *e = &s_ex[i];
    if (!e->enabled || e->day_type == DAY_TYPE_REST) continue;

    bool done = e->daily_count >= e->effective_goal;
    graphics_context_set_text_color(ctx, done ? GColorLightGray : GColorBlack);

    graphics_draw_text(ctx, exercise_name((ExerciseType)i),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(inset, y + 5, bounds.size.w - 2 * inset, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    snprintf(progress_bufs[i], sizeof(progress_bufs[i]), "%d/%d", e->daily_count, e->effective_goal);
    graphics_draw_text(ctx, progress_bufs[i],
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(inset, y, bounds.size.w - 2 * inset, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    y += 28;
  }

  // Dismiss hint (two lines on rectangular screens, shortened to fit the round one)
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, PBL_IF_ROUND_ELSE("Sel: Log / Up: Snooze",
                                            translate("Sel: Log / Up: Snooze / Zurück: Weg",
                                                      "Sel: Log / Up: Snooze / Back: Dismiss")),
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(4, bounds.size.h - PBL_IF_ROUND_ELSE(30, 34), bounds.size.w - 8, 32),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void reminder_dismiss(ClickRecognizerRef recognizer, void *context);

static void auto_dismiss_timer_callback(void *data) {
  s_auto_dismiss_timer = NULL;
  reminder_dismiss(NULL, NULL);
}

static void cancel_auto_dismiss_timer(void) {
  if (s_auto_dismiss_timer) {
    app_timer_cancel(s_auto_dismiss_timer);
    s_auto_dismiss_timer = NULL;
  }
}

static void reminder_dismiss(ClickRecognizerRef recognizer, void *context) {
  cancel_auto_dismiss_timer();

  // Pop reminder window
  window_stack_pop(true);

  // Schedule next wakeup
  schedule_next_wakeup();

  // If launched by wakeup and no other window behind, close the app
  if (s_launched_by_wakeup) {
    window_stack_pop_all(true);
  }
}

static void reminder_select_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_auto_dismiss_timer();

  ExerciseType pending[EX_COUNT];
  int num_pending = get_pending_exercises(pending);

  if (num_pending == 1) {
    open_quicklog(pending[0], true);
  } else if (num_pending > 1) {
    open_exercise_chooser();
  } else {
    reminder_dismiss(recognizer, context);
  }
}

static void open_snooze_menu(void);

static void reminder_up_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_auto_dismiss_timer();
  open_snooze_menu();
}

static void reminder_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, reminder_up_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, reminder_dismiss);
  window_single_click_subscribe(BUTTON_ID_SELECT, reminder_select_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, reminder_dismiss);
}

static void reminder_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_reminder_layer = layer_create(bounds);
  layer_set_update_proc(s_reminder_layer, reminder_layer_update_proc);
  layer_add_child(window_layer, s_reminder_layer);

  window_set_click_config_provider(window, reminder_click_config_provider);
}

static void reminder_window_unload(Window *window) {
  layer_destroy(s_reminder_layer);
  s_reminder_layer = NULL;
}

static void show_reminder(void) {
  // Vibrate (respect Quiet Time)
  if (!quiet_time_is_active()) {
    // Distinct double-pulse pattern for workout reminders
    static const uint32_t segments[] = { 200, 100, 200, 100, 400 };
    VibePattern pat = { .durations = segments, .num_segments = 5 };
    vibes_enqueue_custom_pattern(pat);
  }

  // Show reminder window
  if (!s_reminder_window) {
    s_reminder_window = window_create();
    window_set_window_handlers(s_reminder_window, (WindowHandlers) {
      .load = reminder_window_load,
      .unload = reminder_window_unload
    });
  }
  window_stack_push(s_reminder_window, true);

  if (s_auto_dismiss_duration > 0) {
    cancel_auto_dismiss_timer();
    s_auto_dismiss_timer = app_timer_register(s_auto_dismiss_duration * 1000, auto_dismiss_timer_callback, NULL);
  }
}

// Wakeup handler (called if app is already running when wakeup fires)
static void wakeup_handler(WakeupId id, int32_t reason) {
  if (reason == WAKEUP_COOKIE_REMINDER) {
    check_and_reset_daily_count();
    // Don't remind when every exercise is done, resting or disabled
    if (any_exercise_pending()) {
      show_reminder();
    } else {
      schedule_next_wakeup();
    }
  }
}

// ============================================================================
// Exercise Chooser Window (reminder -> which exercise to log)
// ============================================================================
static ExerciseType s_chooser_items[EX_COUNT];
static int s_chooser_count = 0;

static uint16_t chooser_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_chooser_count;
}

static void chooser_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  static char subtitle_buf[16];
  if (cell_index->row >= s_chooser_count) return;

  ExerciseType ex = s_chooser_items[cell_index->row];
  snprintf(subtitle_buf, sizeof(subtitle_buf), "%d / %d", s_ex[ex].daily_count, s_ex[ex].effective_goal);
  menu_cell_basic_draw(ctx, cell_layer, exercise_name(ex), subtitle_buf, NULL);
}

static void chooser_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row >= s_chooser_count) return;
  open_quicklog(s_chooser_items[cell_index->row], true);
}

static void chooser_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_chooser_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_chooser_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = chooser_menu_get_num_rows,
    .draw_row = chooser_menu_draw_row,
    .select_click = chooser_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_chooser_menu_layer, window);

  #if defined(PBL_COLOR)
    menu_layer_set_normal_colors(s_chooser_menu_layer, GColorWhite, GColorBlack);
    menu_layer_set_highlight_colors(s_chooser_menu_layer, GColorRed, GColorWhite);
  #endif

  layer_add_child(window_layer, menu_layer_get_layer(s_chooser_menu_layer));
}

static void chooser_window_unload(Window *window) {
  menu_layer_destroy(s_chooser_menu_layer);
  s_chooser_menu_layer = NULL;
}

static void open_exercise_chooser(void) {
  s_chooser_count = get_pending_exercises(s_chooser_items);

  if (!s_chooser_window) {
    s_chooser_window = window_create();
    window_set_window_handlers(s_chooser_window, (WindowHandlers) {
      .load = chooser_window_load,
      .unload = chooser_window_unload
    });
  }
  window_stack_push(s_chooser_window, true);
}


// ============================================================================
// Number Picker Window
// ============================================================================
static void picker_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Background
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Title
  const char *title = "";
  switch (s_picker_type) {
    case PICKER_DAILY_GOAL:
      title = exercise_name(s_picker_ex);
      break;
    case PICKER_REMINDER_INTERVAL:
      title = translate("INTERVALL (MIN)", "INTERVAL (MIN)");
      break;
    case PICKER_START_HOUR:
      title = translate("STARTZEIT", "START TIME");
      break;
    case PICKER_END_HOUR:
      title = translate("ENDZEIT", "END TIME");
      break;
  }

  // Header bar
  int header_h = 30;
  graphics_context_set_fill_color(ctx, GColorCobaltBlue);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, header_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, title,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(4, 4, bounds.size.w - 8, header_h - 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Value display
  static char val_buf[16];
  if (s_picker_type == PICKER_START_HOUR || s_picker_type == PICKER_END_HOUR) {
    snprintf(val_buf, sizeof(val_buf), "%02d:00", s_picker_value);
  } else if (s_picker_type == PICKER_REMINDER_INTERVAL) {
    snprintf(val_buf, sizeof(val_buf), "%d min", s_picker_value);
  } else if (s_picker_type == PICKER_DAILY_GOAL && s_picker_value == 0) {
    snprintf(val_buf, sizeof(val_buf), "%s", translate("Aus", "Off"));
  } else {
    snprintf(val_buf, sizeof(val_buf), "%d", s_picker_value);
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, val_buf,
                     fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                     GRect(10, bounds.size.h / 2 - 30, bounds.size.w - 20, 50),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // UP/DOWN arrows
  int arrow_y_up = header_h + 8;
  int arrow_y_dn = bounds.size.h - 22;
  graphics_context_set_fill_color(ctx, GColorDarkGray);

  // Up arrow (triangle)
  GPoint up_pts[3] = {
    {bounds.size.w / 2, arrow_y_up},
    {bounds.size.w / 2 - 8, arrow_y_up + 10},
    {bounds.size.w / 2 + 8, arrow_y_up + 10}
  };
  GPathInfo up_info = { .num_points = 3, .points = up_pts };
  GPath *up_path = gpath_create(&up_info);
  gpath_draw_filled(ctx, up_path);
  gpath_destroy(up_path);

  // Down arrow (triangle)
  GPoint dn_pts[3] = {
    {bounds.size.w / 2, arrow_y_dn + 10},
    {bounds.size.w / 2 - 8, arrow_y_dn},
    {bounds.size.w / 2 + 8, arrow_y_dn}
  };
  GPathInfo dn_info = { .num_points = 3, .points = dn_pts };
  GPath *dn_path = gpath_create(&dn_info);
  gpath_draw_filled(ctx, dn_path);
  gpath_destroy(dn_path);

  // Hint footer
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, translate("SEL: Speichern | BACK: Abbruch",
                                     "SEL: Save | BACK: Cancel"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(4, bounds.size.h - 16, bounds.size.w - 8, 16),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void picker_up_handler(ClickRecognizerRef recognizer, void *context) {
  s_picker_value += s_picker_step;
  if (s_picker_value > s_picker_max) s_picker_value = s_picker_max;
  layer_mark_dirty(s_picker_layer);
}

static void picker_down_handler(ClickRecognizerRef recognizer, void *context) {
  s_picker_value -= s_picker_step;
  if (s_picker_value < s_picker_min) s_picker_value = s_picker_min;
  layer_mark_dirty(s_picker_layer);
}

static Window *s_confirm_window = NULL;
static TextLayer *s_confirm_text_layer = NULL;
static uint16_t s_pending_daily_goal = 0;

static void confirm_save_and_pop(void) {
  save_settings();
  update_app_glance();
  vibes_short_pulse();
  if (s_settings_menu_layer) {
    menu_layer_reload_data(s_settings_menu_layer);
  }
  window_stack_pop(true); // pop confirm
  window_stack_pop(true); // pop picker
}

static void confirm_up_handler(ClickRecognizerRef recognizer, void *context) {
  ExerciseState *e = &s_ex[s_picker_ex];
  e->daily_goal = s_pending_daily_goal;
  e->effective_goal = e->daily_goal;
  e->day_type = DAY_TYPE_TRAINING_NORMAL;
  e->consecutive_train_days = 0;
  confirm_save_and_pop();
}

static void confirm_down_handler(ClickRecognizerRef recognizer, void *context) {
  s_ex[s_picker_ex].daily_goal = s_pending_daily_goal;
  confirm_save_and_pop();
}

static void confirm_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, confirm_up_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, confirm_down_handler);
}

static void confirm_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_confirm_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 40, bounds.size.w, 100));
  text_layer_set_text_alignment(s_confirm_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_confirm_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  static char s_confirm_buf[128];
  snprintf(s_confirm_buf, sizeof(s_confirm_buf),
           translate("Ziel ist %d.\nAuf %d\nzuruecksetzen?\nUP: Ja DOWN: Nein",
                     "Goal is %d.\nReset to %d?\nUP: Yes DOWN: No"),
           s_ex[s_picker_ex].effective_goal, s_pending_daily_goal);
  text_layer_set_text(s_confirm_text_layer, s_confirm_buf);

  layer_add_child(window_layer, text_layer_get_layer(s_confirm_text_layer));
}

static void confirm_window_unload(Window *window) {
  text_layer_destroy(s_confirm_text_layer);
}

static void open_confirm_dialog(void) {
  if (!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_click_config_provider(s_confirm_window, confirm_click_config_provider);
    window_set_window_handlers(s_confirm_window, (WindowHandlers) {
      .load = confirm_window_load,
      .unload = confirm_window_unload,
    });
  }
  window_stack_push(s_confirm_window, true);
}

static void picker_select_handler(ClickRecognizerRef recognizer, void *context) {
  // Apply value
  switch (s_picker_type) {
    case PICKER_DAILY_GOAL: {
      ExerciseState *e = &s_ex[s_picker_ex];
      if (s_picker_value == 0) {
        // 0 = exercise disabled; keep the previous goal for re-enabling later
        e->enabled = false;
        break;
      }
      bool was_enabled = e->enabled;
      e->enabled = true;
      s_pending_daily_goal = (uint16_t)s_picker_value;
      if (was_enabled && s_pending_daily_goal < e->effective_goal) {
        open_confirm_dialog();
        return;
      }
      e->daily_goal = s_pending_daily_goal;
      if (!was_enabled || e->effective_goal < e->daily_goal) {
        e->effective_goal = e->daily_goal;
        if (e->day_type == DAY_TYPE_REST) e->day_type = DAY_TYPE_TRAINING_NORMAL;
      }
      break;
    }
    case PICKER_REMINDER_INTERVAL:
      s_reminder_interval = (uint16_t)s_picker_value;
      break;
    case PICKER_START_HOUR:
      s_active_start_hour = (uint8_t)s_picker_value;
      // Enforce: start < end
      if (s_active_start_hour >= s_active_end_hour) {
        s_active_end_hour = s_active_start_hour + 1;
        if (s_active_end_hour > 23) s_active_end_hour = 23;
      }
      break;
    case PICKER_END_HOUR:
      s_active_end_hour = (uint8_t)s_picker_value;
      // Enforce: end > start
      if (s_active_end_hour <= s_active_start_hour) {
        s_active_start_hour = s_active_end_hour - 1;
        if (s_active_start_hour > 23) s_active_start_hour = 0; // underflow wrap
      }
      break;
  }

  save_settings();
  update_app_glance();
  vibes_short_pulse();

  // Refresh menus to show updated values
  if (s_settings_menu_layer) {
    menu_layer_reload_data(s_settings_menu_layer);
  }
  if (s_main_menu_layer) {
    menu_layer_reload_data(s_main_menu_layer);
  }

  window_stack_pop(true);
}

static void picker_back_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void picker_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, picker_up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, picker_down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, picker_select_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, picker_back_handler);
}

static void picker_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_picker_layer = layer_create(bounds);
  layer_set_update_proc(s_picker_layer, picker_layer_update_proc);
  layer_add_child(window_layer, s_picker_layer);

  window_set_click_config_provider(window, picker_click_config_provider);
}

static void picker_window_unload(Window *window) {
  layer_destroy(s_picker_layer);
  s_picker_layer = NULL;
}

static void open_picker(PickerType type, int current, int min, int max, int step) {
  s_picker_type  = type;
  s_picker_value = current;
  s_picker_min   = min;
  s_picker_max   = max;
  s_picker_step  = step;

  if (!s_picker_window) {
    s_picker_window = window_create();
    window_set_window_handlers(s_picker_window, (WindowHandlers) {
      .load = picker_window_load,
      .unload = picker_window_unload
    });
  }
  window_stack_push(s_picker_window, true);
}

// ============================================================================
// Quick Log Window
// ============================================================================
static uint16_t s_quicklog_count = 0;
static ExerciseType s_quicklog_ex = EX_PUSHUPS;
static bool s_opened_from_reminder = false;

static void quicklog_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Background
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Header
  int header_h = 30;
  graphics_context_set_fill_color(ctx, GColorCobaltBlue);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, header_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, exercise_name(s_quicklog_ex),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(4, 4, bounds.size.w - 8, header_h - 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Count display
  static char ql_buf[8];
  snprintf(ql_buf, sizeof(ql_buf), "%d", s_quicklog_count);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, ql_buf,
                     fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                     GRect(10, bounds.size.h / 2 - 30, bounds.size.w - 20, 50),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // Arrows
  int arrow_y_up = header_h + 8;
  int arrow_y_dn = bounds.size.h - 22;
  graphics_context_set_fill_color(ctx, GColorDarkGray);

  GPoint up_pts[3] = {
    {bounds.size.w / 2, arrow_y_up},
    {bounds.size.w / 2 - 8, arrow_y_up + 10},
    {bounds.size.w / 2 + 8, arrow_y_up + 10}
  };
  GPathInfo up_info = { .num_points = 3, .points = up_pts };
  GPath *up_path = gpath_create(&up_info);
  gpath_draw_filled(ctx, up_path);
  gpath_destroy(up_path);

  GPoint dn_pts[3] = {
    {bounds.size.w / 2, arrow_y_dn + 10},
    {bounds.size.w / 2 - 8, arrow_y_dn},
    {bounds.size.w / 2 + 8, arrow_y_dn}
  };
  GPathInfo dn_info = { .num_points = 3, .points = dn_pts };
  GPath *dn_path = gpath_create(&dn_info);
  gpath_draw_filled(ctx, dn_path);
  gpath_destroy(dn_path);

  // Hint
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, translate("SEL: Speichern | BACK: Abbruch",
                                     "SEL: Save | BACK: Cancel"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(4, bounds.size.h - 16, bounds.size.w - 8, 16),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void quicklog_up_handler(ClickRecognizerRef recognizer, void *context) {
  s_quicklog_count++;
  if (s_quicklog_layer) layer_mark_dirty(s_quicklog_layer);
}

static void quicklog_down_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_quicklog_count > 0) s_quicklog_count--;
  if (s_quicklog_layer) layer_mark_dirty(s_quicklog_layer);
}

static void quicklog_select_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_quicklog_count > 0) {
    ExerciseState *e = &s_ex[s_quicklog_ex];
    e->daily_count += s_quicklog_count;
    save_settings();
    update_app_glance();
    send_rep_log(s_quicklog_ex, s_quicklog_count);
    schedule_next_wakeup();
    vibes_double_pulse();

    if (s_main_menu_layer) {
      menu_layer_reload_data(s_main_menu_layer);
    }

    APP_LOG(APP_LOG_LEVEL_INFO, "Workout: Quick Log [%d] added %d, daily total now %d",
            s_quicklog_ex, s_quicklog_count, e->daily_count);

    if (s_opened_from_reminder) {
      if (s_chooser_window && window_stack_contains_window(s_chooser_window)) {
        window_stack_remove(s_chooser_window, false);
      }
      if (any_exercise_pending()) {
        // Other exercises still open: return to the reminder with updated progress
        if (s_reminder_layer) layer_mark_dirty(s_reminder_layer);
      } else if (s_launched_by_wakeup) {
        window_stack_pop_all(true);
        return;
      } else if (s_reminder_window) {
        window_stack_remove(s_reminder_window, false);
      }
    }
  }
  window_stack_pop(true);
}

static void quicklog_back_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void quicklog_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, quicklog_up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, quicklog_down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, quicklog_select_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, quicklog_back_handler);
}

static void quicklog_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_quicklog_layer = layer_create(bounds);
  layer_set_update_proc(s_quicklog_layer, quicklog_layer_update_proc);
  layer_add_child(window_layer, s_quicklog_layer);

  window_set_click_config_provider(window, quicklog_click_config_provider);
}

static void quicklog_window_unload(Window *window) {
  layer_destroy(s_quicklog_layer);
  s_quicklog_layer = NULL;
}

static void open_quicklog(ExerciseType ex, bool from_reminder) {
  s_quicklog_count = 0;
  s_quicklog_ex = ex;
  s_opened_from_reminder = from_reminder;
  if (!s_quicklog_window) {
    s_quicklog_window = window_create();
    window_set_window_handlers(s_quicklog_window, (WindowHandlers) {
      .load = quicklog_window_load,
      .unload = quicklog_window_unload
    });
  }
  window_stack_push(s_quicklog_window, true);
}

// ============================================================================
// History Window (one section per enabled exercise)
// ============================================================================
static ExerciseType s_history_items[EX_COUNT];
static int s_history_sections = 0;

static uint16_t history_menu_get_num_sections(MenuLayer *menu_layer, void *data) {
  return s_history_sections;
}

static int16_t history_menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void history_menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  if (section_index >= s_history_sections) return;
  menu_cell_basic_header_draw(ctx, cell_layer, exercise_name(s_history_items[section_index]));
}

static uint16_t history_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  if (section_index >= s_history_sections) return 0;
  // Row 0 = Streak, Row 1..N = up to 7 history days
  ExerciseState *e = &s_ex[s_history_items[section_index]];
  return 1 + (e->history_count > 7 ? 7 : e->history_count);
}

static void history_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  static char title_buf[32];
  static char subtitle_buf[32];

  if (cell_index->section >= s_history_sections) return;
  ExerciseType ex = s_history_items[cell_index->section];
  ExerciseState *e = &s_ex[ex];

  if (cell_index->row == 0) {
    uint16_t streak = calculate_streak(ex);
    snprintf(title_buf, sizeof(title_buf), "%s: %d", translate("Aktuelle Streak", "Current Streak"), streak);
    snprintf(subtitle_buf, sizeof(subtitle_buf), "%s", translate("Tage in Folge", "Consecutive days"));
    menu_cell_basic_draw(ctx, cell_layer, title_buf, subtitle_buf, NULL);
  } else {
    // Offset by 1 for the streak row. Days are displayed from newest (index history_count - 1) backwards.
    int history_idx = e->history_count - cell_index->row;
    if (history_idx >= 0 && history_idx < e->history_count) {
      DayRecord *record = &e->history[history_idx];

      snprintf(title_buf, sizeof(title_buf), "%s %d", translate("Vor", "Days ago:"), cell_index->row);

      if (record->day_type == DAY_TYPE_REST) {
        snprintf(subtitle_buf, sizeof(subtitle_buf), "%s", translate("Ruhetag", "Rest Day"));
      } else {
        snprintf(subtitle_buf, sizeof(subtitle_buf), "%d / %d", record->achieved, record->target);
      }
      menu_cell_basic_draw(ctx, cell_layer, title_buf, subtitle_buf, NULL);
    }
  }
}

static void history_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_history_sections = get_enabled_exercises(s_history_items);

  s_history_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_history_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = history_menu_get_num_sections,
    .get_header_height = history_menu_get_header_height,
    .draw_header = history_menu_draw_header,
    .get_num_rows = history_menu_get_num_rows,
    .draw_row = history_menu_draw_row,
  });
  menu_layer_set_click_config_onto_window(s_history_menu_layer, window);

  #if defined(PBL_COLOR)
    menu_layer_set_normal_colors(s_history_menu_layer, GColorWhite, GColorBlack);
    menu_layer_set_highlight_colors(s_history_menu_layer, GColorCobaltBlue, GColorWhite);
  #endif

  layer_add_child(window_layer, menu_layer_get_layer(s_history_menu_layer));
}

static void history_window_unload(Window *window) {
  menu_layer_destroy(s_history_menu_layer);
  s_history_menu_layer = NULL;
}

static void open_history(void) {
  if (!s_history_window) {
    s_history_window = window_create();
    window_set_window_handlers(s_history_window, (WindowHandlers) {
      .load = history_window_load,
      .unload = history_window_unload
    });
  }
  window_stack_push(s_history_window, true);
}

// ============================================================================
// Settings Menu Window
// ============================================================================
// Rows 0..EX_COUNT-1: per-exercise minimum goal (0 = disabled),
// followed by interval, start time and end time
#define SETTINGS_ROW_INTERVAL   (EX_COUNT + 0)
#define SETTINGS_ROW_START      (EX_COUNT + 1)
#define SETTINGS_ROW_END        (EX_COUNT + 2)
#define SETTINGS_MENU_NUM_ROWS  (EX_COUNT + 3)

static uint16_t settings_menu_get_num_rows(MenuLayer *menu_layer,
                                            uint16_t section_index,
                                            void *data) {
  return SETTINGS_MENU_NUM_ROWS;
}

static void settings_menu_draw_row(GContext *ctx, const Layer *cell_layer,
                                    MenuIndex *cell_index, void *data) {
  static char subtitle_buf[32];

  if (cell_index->row < EX_COUNT) {
    ExerciseState *e = &s_ex[cell_index->row];
    if (e->enabled) {
      snprintf(subtitle_buf, sizeof(subtitle_buf),
               translate("Min %d (Adaptiv: %d)", "Min %d (Adaptive: %d)"),
               e->daily_goal, e->effective_goal);
    } else {
      snprintf(subtitle_buf, sizeof(subtitle_buf), "%s", translate("Deaktiviert", "Disabled"));
    }
    menu_cell_basic_draw(ctx, cell_layer, exercise_name((ExerciseType)cell_index->row),
                         subtitle_buf, NULL);
    return;
  }

  switch (cell_index->row) {
    case SETTINGS_ROW_INTERVAL:
      snprintf(subtitle_buf, sizeof(subtitle_buf), "%d min", s_reminder_interval);
      menu_cell_basic_draw(ctx, cell_layer,
                           translate("Erinnerungs-Intervall", "Reminder Interval"),
                           subtitle_buf, NULL);
      break;
    case SETTINGS_ROW_START:
      snprintf(subtitle_buf, sizeof(subtitle_buf), "%02d:00", s_active_start_hour);
      menu_cell_basic_draw(ctx, cell_layer,
                           translate("Startzeit", "Start Time"),
                           subtitle_buf, NULL);
      break;
    case SETTINGS_ROW_END:
      snprintf(subtitle_buf, sizeof(subtitle_buf), "%02d:00", s_active_end_hour);
      menu_cell_basic_draw(ctx, cell_layer,
                           translate("Endzeit", "End Time"),
                           subtitle_buf, NULL);
      break;
  }
}

static void settings_menu_select_callback(MenuLayer *menu_layer,
                                           MenuIndex *cell_index,
                                           void *data) {
  if (cell_index->row < EX_COUNT) {
    ExerciseType ex = (ExerciseType)cell_index->row;
    s_picker_ex = ex;
    open_picker(PICKER_DAILY_GOAL, s_ex[ex].enabled ? s_ex[ex].daily_goal : 0,
                0, 500, EXERCISE_PICKER_STEP[ex]);
    return;
  }

  switch (cell_index->row) {
    case SETTINGS_ROW_INTERVAL:
      open_picker(PICKER_REMINDER_INTERVAL, s_reminder_interval, 15, 240, 15);
      break;
    case SETTINGS_ROW_START:
      open_picker(PICKER_START_HOUR, s_active_start_hour, 0, 22, 1);
      break;
    case SETTINGS_ROW_END:
      open_picker(PICKER_END_HOUR, s_active_end_hour, 1, 23, 1);
      break;
  }
}

static void settings_menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_settings_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_settings_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = settings_menu_get_num_rows,
    .draw_row = settings_menu_draw_row,
    .select_click = settings_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_settings_menu_layer, window);

  #if defined(PBL_COLOR)
    menu_layer_set_normal_colors(s_settings_menu_layer, GColorWhite, GColorBlack);
    menu_layer_set_highlight_colors(s_settings_menu_layer, GColorCobaltBlue, GColorWhite);
  #endif

  layer_add_child(window_layer, menu_layer_get_layer(s_settings_menu_layer));
}

static void settings_menu_window_unload(Window *window) {
  menu_layer_destroy(s_settings_menu_layer);
  s_settings_menu_layer = NULL;
}

// ============================================================================
// Snooze Menu Window
// ============================================================================
static Window *s_snooze_menu_window = NULL;
static MenuLayer *s_snooze_menu_layer = NULL;

static uint16_t snooze_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return 4;
}

static void snooze_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      menu_cell_basic_draw(ctx, cell_layer, "1 Hour", NULL, NULL);
      break;
    case 1:
      menu_cell_basic_draw(ctx, cell_layer, "4 Hours", NULL, NULL);
      break;
    case 2:
      menu_cell_basic_draw(ctx, cell_layer, "8 Hours", NULL, NULL);
      break;
    case 3:
      menu_cell_basic_draw(ctx, cell_layer, "Until Tomorrow", NULL, NULL);
      break;
  }
}

static void snooze_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  time_t now = time(NULL);

  switch (cell_index->row) {
    case 0: s_snooze_until = now + 3600; break;
    case 1: s_snooze_until = now + 14400; break;
    case 2: s_snooze_until = now + 28800; break;
    case 3: {
      struct tm *t = localtime(&now);
      struct tm tomorrow = *t;
      tomorrow.tm_mday += 1;
      tomorrow.tm_hour = s_active_start_hour;
      tomorrow.tm_min = 0;
      tomorrow.tm_sec = 0;
      s_snooze_until = mktime(&tomorrow);
      break;
    }
  }

  save_settings();
  schedule_next_wakeup();

  // Close snooze menu
  window_stack_pop(true);

  // If reminder window is open, close it too
  if (s_reminder_window && window_stack_contains_window(s_reminder_window)) {
    window_stack_remove(s_reminder_window, false);
  }
}

static void snooze_menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_snooze_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_snooze_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = snooze_menu_get_num_rows,
    .draw_row = snooze_menu_draw_row,
    .select_click = snooze_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_snooze_menu_layer, window);

  #if defined(PBL_COLOR)
    menu_layer_set_normal_colors(s_snooze_menu_layer, GColorWhite, GColorBlack);
    menu_layer_set_highlight_colors(s_snooze_menu_layer, GColorCobaltBlue, GColorWhite);
  #endif

  layer_add_child(window_layer, menu_layer_get_layer(s_snooze_menu_layer));
}

static void snooze_menu_window_unload(Window *window) {
  menu_layer_destroy(s_snooze_menu_layer);
  s_snooze_menu_layer = NULL;
}

static void open_snooze_menu(void) {
  if (!s_snooze_menu_window) {
    s_snooze_menu_window = window_create();
    window_set_window_handlers(s_snooze_menu_window, (WindowHandlers) {
      .load = snooze_menu_window_load,
      .unload = snooze_menu_window_unload
    });
  }
  window_stack_push(s_snooze_menu_window, true);
}

// ============================================================================
// Main Menu Window
// ============================================================================
// One log row per enabled exercise, followed by History, Settings and Snooze
typedef enum {
  MAIN_ROW_HISTORY = 0,
  MAIN_ROW_SETTINGS,
  MAIN_ROW_SNOOZE,
  MAIN_ROW_FIXED_COUNT
} MainMenuFixedRow;

static uint16_t main_menu_get_num_rows(MenuLayer *menu_layer,
                                        uint16_t section_index,
                                        void *data) {
  ExerciseType enabled[EX_COUNT];
  return get_enabled_exercises(enabled) + MAIN_ROW_FIXED_COUNT;
}

static void main_menu_draw_exercise_row(GContext *ctx, const Layer *cell_layer, ExerciseType ex) {
  static char status_buf[48];
  ExerciseState *e = &s_ex[ex];

  const char *indicator = "";
  if (e->day_type == DAY_TYPE_TRAINING_OVERLOAD) indicator = "[+] ";
  else if (e->day_type == DAY_TYPE_TRAINING_DELOAD) indicator = "[-] ";
  else if (e->day_type == DAY_TYPE_REST) indicator = "[Zzz] ";

  const char *status_text = "";
  switch (e->day_type) {
    case DAY_TYPE_TRAINING_OVERLOAD: status_text = translate("Steigerung", "Overload"); break;
    case DAY_TYPE_TRAINING_DELOAD: status_text = translate("Erholung", "Deload"); break;
    case DAY_TYPE_TRAINING_NORMAL: status_text = translate("Normal", "Normal"); break;
    case DAY_TYPE_REST: status_text = translate("Regeneration", "Recovery"); break;
  }

  if (e->day_type == DAY_TYPE_REST) {
    if (e->daily_count > 0) {
      snprintf(status_buf, sizeof(status_buf), "%d %s%s", e->daily_count, indicator, status_text);
    } else {
      snprintf(status_buf, sizeof(status_buf), "%s%s", indicator, status_text);
    }
  } else {
    snprintf(status_buf, sizeof(status_buf), "%d/%d %s%s",
             e->daily_count, e->effective_goal, indicator, status_text);
  }
  menu_cell_basic_draw(ctx, cell_layer, exercise_name(ex), status_buf, NULL);
}

static void main_menu_draw_row(GContext *ctx, const Layer *cell_layer,
                                MenuIndex *cell_index, void *data) {
  ExerciseType enabled[EX_COUNT];
  int num_enabled = get_enabled_exercises(enabled);

  if (cell_index->row < num_enabled) {
    main_menu_draw_exercise_row(ctx, cell_layer, enabled[cell_index->row]);
    return;
  }

  switch (cell_index->row - num_enabled) {
    case MAIN_ROW_HISTORY:
      menu_cell_basic_draw(ctx, cell_layer,
                           translate("Verlauf", "History"),
                           translate("Letzte 7 Tage", "Last 7 days"), NULL);
      break;
    case MAIN_ROW_SETTINGS:
      menu_cell_basic_draw(ctx, cell_layer,
                           translate("Einstellungen", "Settings"),
                           translate("Ziele, Timer, Zeitfenster", "Goals, Timer, Window"), NULL);
      break;
    case MAIN_ROW_SNOOZE:
      if (s_snooze_until > time(NULL)) {
        menu_cell_basic_draw(ctx, cell_layer,
                             translate("Snooze beenden", "Cancel Snooze"),
                             translate("Erinnerungen fortsetzen", "Resume reminders"), NULL);
      } else {
        menu_cell_basic_draw(ctx, cell_layer,
                             "Snooze",
                             translate("Erinnerungen pausieren", "Pause reminders"), NULL);
      }
      break;
  }
}

static void main_menu_select_callback(MenuLayer *menu_layer,
                                       MenuIndex *cell_index,
                                       void *data) {
  ExerciseType enabled[EX_COUNT];
  int num_enabled = get_enabled_exercises(enabled);

  if (cell_index->row < num_enabled) {
    open_quicklog(enabled[cell_index->row], false);
    return;
  }

  switch (cell_index->row - num_enabled) {
    case MAIN_ROW_HISTORY:
      open_history();
      break;
    case MAIN_ROW_SETTINGS:
      // Open Settings Menu
      if (!s_settings_menu_window) {
        s_settings_menu_window = window_create();
        window_set_window_handlers(s_settings_menu_window, (WindowHandlers) {
          .load = settings_menu_window_load,
          .unload = settings_menu_window_unload
        });
      }
      window_stack_push(s_settings_menu_window, true);
      break;
    case MAIN_ROW_SNOOZE:
      if (s_snooze_until > time(NULL)) {
        s_snooze_until = 0;
        persist_delete(PERSIST_KEY_SNOOZE_UNTIL);
        schedule_next_wakeup();
        menu_layer_reload_data(menu_layer);
      } else {
        open_snooze_menu();
      }
      break;
  }
}

static void main_menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_main_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_main_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = main_menu_get_num_rows,
    .draw_row = main_menu_draw_row,
    .select_click = main_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_main_menu_layer, window);

  #if defined(PBL_COLOR)
    menu_layer_set_normal_colors(s_main_menu_layer, GColorWhite, GColorBlack);
    menu_layer_set_highlight_colors(s_main_menu_layer, GColorCobaltBlue, GColorWhite);
  #endif

  layer_add_child(window_layer, menu_layer_get_layer(s_main_menu_layer));
}

static void main_menu_window_unload(Window *window) {
  menu_layer_destroy(s_main_menu_layer);
  s_main_menu_layer = NULL;
}

// ============================================================================
// AppMessage Handler
// ============================================================================
static void apply_daily_goal(ExerciseType ex, uint16_t goal) {
  ExerciseState *e = &s_ex[ex];
  e->daily_goal = goal;
  if (e->effective_goal < e->daily_goal) {
    e->effective_goal = e->daily_goal;
  }
  save_exercise(ex);
}

static void apply_enabled(ExerciseType ex, bool enabled) {
  ExerciseState *e = &s_ex[ex];
  if (enabled && !e->enabled && e->effective_goal < e->daily_goal) {
    e->effective_goal = e->daily_goal;
  }
  e->enabled = enabled;
  save_exercise(ex);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  // Language
  Tuple *lang_t = dict_find(iter, MESSAGE_KEY_LANGUAGE);
  if (lang_t) {
    if (lang_t->type == TUPLE_CSTRING) {
      s_language = (AppLanguage)(lang_t->value->cstring[0] - '0');
    } else {
      s_language = (AppLanguage)lang_t->value->uint8;
    }
    persist_write_int(PERSIST_KEY_LANGUAGE, s_language);
  }

  // Daily Goals
  Tuple *goal_t = dict_find(iter, MESSAGE_KEY_DAILY_GOAL);
  if (goal_t) apply_daily_goal(EX_PUSHUPS, (uint16_t)goal_t->value->int32);
  Tuple *goal_pullups_t = dict_find(iter, MESSAGE_KEY_DAILY_GOAL_PULLUPS);
  if (goal_pullups_t) apply_daily_goal(EX_PULLUPS, (uint16_t)goal_pullups_t->value->int32);
  Tuple *goal_squats_t = dict_find(iter, MESSAGE_KEY_DAILY_GOAL_SQUATS);
  if (goal_squats_t) apply_daily_goal(EX_SQUATS, (uint16_t)goal_squats_t->value->int32);

  // Enabled Exercises
  Tuple *en_pushups_t = dict_find(iter, MESSAGE_KEY_ENABLE_PUSHUPS);
  if (en_pushups_t) apply_enabled(EX_PUSHUPS, en_pushups_t->value->int32 == 1);
  Tuple *en_pullups_t = dict_find(iter, MESSAGE_KEY_ENABLE_PULLUPS);
  if (en_pullups_t) apply_enabled(EX_PULLUPS, en_pullups_t->value->int32 == 1);
  Tuple *en_squats_t = dict_find(iter, MESSAGE_KEY_ENABLE_SQUATS);
  if (en_squats_t) apply_enabled(EX_SQUATS, en_squats_t->value->int32 == 1);

  // Reminder Interval
  Tuple *interval_t = dict_find(iter, MESSAGE_KEY_REMINDER_INTERVAL);
  if (interval_t) {
    s_reminder_interval = interval_t->value->uint16;
    persist_write_int(PERSIST_KEY_REMINDER_INTERVAL, s_reminder_interval);
  }

  // Active Start Hour
  Tuple *start_t = dict_find(iter, MESSAGE_KEY_ACTIVE_START_HOUR);
  if (start_t) {
    s_active_start_hour = start_t->value->uint8;
    persist_write_int(PERSIST_KEY_ACTIVE_START_HOUR, s_active_start_hour);
  }

  // Active End Hour
  Tuple *end_t = dict_find(iter, MESSAGE_KEY_ACTIVE_END_HOUR);
  if (end_t) {
    s_active_end_hour = end_t->value->uint8;
    persist_write_int(PERSIST_KEY_ACTIVE_END_HOUR, s_active_end_hour);
  }

  // Auto-Dismiss Duration
  Tuple *auto_dismiss_t = dict_find(iter, MESSAGE_KEY_AUTO_DISMISS_DURATION);
  if (auto_dismiss_t) {
    s_auto_dismiss_duration = auto_dismiss_t->value->int32;
    persist_write_int(PERSIST_KEY_AUTO_DISMISS_DURATION, s_auto_dismiss_duration);
  }

  // Strict Mode
  Tuple *strict_mode_t = dict_find(iter, MESSAGE_KEY_STRICT_MODE);
  if (strict_mode_t) {
    s_strict_mode = strict_mode_t->value->int32 == 1;
    persist_write_bool(PERSIST_KEY_STRICT_MODE, s_strict_mode);
  }

  // Refresh UI if any settings changed
  if (lang_t || goal_t || goal_pullups_t || goal_squats_t ||
      en_pushups_t || en_pullups_t || en_squats_t ||
      interval_t || start_t || end_t) {
    if (s_main_menu_layer) menu_layer_reload_data(s_main_menu_layer);
    if (s_settings_menu_layer) menu_layer_reload_data(s_settings_menu_layer);
    update_app_glance();
  }

  // Sync Request
  Tuple *sync_req_t = dict_find(iter, MESSAGE_KEY_REQUEST_SYNC);
  if (sync_req_t) {
    send_offline_sync_data();
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Workout: Inbox dropped: %d", reason);
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Workout: Outbox failed: %d", reason);
}

// ============================================================================
// App Init / Deinit
// ============================================================================
static void init(void) {
  // Load persistent settings
  load_settings();

  // Check for day rollover
  check_and_reset_daily_count();

  // Subscribe to wakeup service (handles wakeups while app is running)
  wakeup_service_subscribe(wakeup_handler);

  // Register AppMessage handlers
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(256, 256);

  // Check if app was launched by a wakeup
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    s_launched_by_wakeup = true;

    WakeupId id = 0;
    int32_t reason = 0;
    wakeup_get_launch_event(&id, &reason);

    if (reason == WAKEUP_COOKIE_REMINDER && any_exercise_pending()) {
      // Show reminder directly (main menu will be pushed below but reminder on top)
      s_main_menu_window = window_create();
      window_set_window_handlers(s_main_menu_window, (WindowHandlers) {
        .load = main_menu_window_load,
        .unload = main_menu_window_unload
      });
      window_stack_push(s_main_menu_window, false);
      show_reminder();
      return;
    }
  }

  // Normal launch: show main menu
  s_main_menu_window = window_create();
  window_set_window_handlers(s_main_menu_window, (WindowHandlers) {
    .load = main_menu_window_load,
    .unload = main_menu_window_unload
  });
  window_stack_push(s_main_menu_window, true);

  // Schedule next wakeup if none pending
  if (s_wakeup_id < 0 || !wakeup_query(s_wakeup_id, NULL)) {
    schedule_next_wakeup();
  }
}

static void deinit(void) {
  // Schedule next wakeup before exiting
  schedule_next_wakeup();

  // Save settings on exit
  save_settings();

  // Destroy windows
  if (s_history_window) {
    window_destroy(s_history_window);
  }
  if (s_quicklog_window) {
    window_destroy(s_quicklog_window);
  }
  if (s_chooser_window) {
    window_destroy(s_chooser_window);
  }
  if (s_reminder_window) {
    window_destroy(s_reminder_window);
  }
  if (s_picker_window) {
    window_destroy(s_picker_window);
  }
  if (s_settings_menu_window) {
    window_destroy(s_settings_menu_window);
  }
  if (s_confirm_window) {
    window_destroy(s_confirm_window);
  }
  if (s_snooze_menu_window) {
    window_destroy(s_snooze_menu_window);
  }
  window_destroy(s_main_menu_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
