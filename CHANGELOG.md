# Changelog

All notable changes to this project will be documented in this file.

## [3.0] - 2026-10-01

### Added
- **Pull-ups & Squats:** Alongside push-ups, the app now tracks pull-ups and squats. Each exercise has its own minimum goal (defaults: 30 / 10 / 30), adaptive progression (overload, deload, rest days), streak and 14-day history.
- **Per-exercise main menu:** The main menu shows one log entry per enabled exercise with today's progress.
- **Multi-exercise reminders:** The reminder lists today's progress for every exercise. Select opens an exercise chooser (or Quick Log directly if only one is left), and after logging you return to the reminder until all goals are met.
- **Enable/disable exercises:** Toggle exercises on the phone settings page, or set a goal to "Off" on the watch.
- **Dashboard exercise filter:** The phone dashboard can show a single exercise or all three as a stacked chart; table and CSV export include the exercise.

### Changed
- App renamed to **Workouts** with a new UUID, so it installs alongside the original Pushups app. Push-up data keeps its storage keys.
- The goal picker clamps at its minimum/maximum and supports hold-to-repeat.

### Fixed
- **Gadgetbridge key mapping:** The hardcoded message key IDs for `REQUEST_SYNC` and `SYNC_OFFLINE_DATA` were off by one. On Gadgetbridge this turned Strict Mode on at every app start and broke offline sync.
- **Offline sync buffer:** The AppMessage outbox (64 bytes) was too small for a full 14-day sync payload; it is now 256 bytes.
- The reminder's button hint no longer gets cut off at the bottom of the screen.

## [2.3] - 2026-09-27

### Added
- **Snooze Function (US-24):** You can now snooze reminder notifications for 1 hour, 4 hours, 8 hours, or until the next day by pressing the Up button directly on the reminder screen. The main menu also features a new Snooze option to let you manually pause or resume your reminders at any time.

## [2.2] - 2026-09-11

### Fixed
- **Settings Page CSV Export (US-23):** Fixed an issue where the "Export CSV" button produced no response. Due to file download restrictions in mobile WebViews, clicking the button now displays the CSV data directly within the page so it can be easily copied to the clipboard.

## [2.1.0] - 2026-09-09

### Fixed
- **History Chart Timeline (US-22):** Fixed a bug in the settings page dashboard where days without pushups were entirely skipped in the timeline. The chart now explicitly displays inactive days (0 pushups) and generates a correct, continuous calendar timeline for "Last 7 Days", "Last 30 Days", and "Last Year" views.

## [2.0.0] - 2026-09-09

### Added
- **Extended History Dashboard (US-21):** The history dashboard in the settings page now allows you to change the timeframe (7 Days, 30 Days, 1 Year, All Time) and toggle between the Bar Chart and a Data Table view. Your preferences are saved automatically.

## [1.9.0] - 2026-09-09

### Fixed
- **Settings Page History Dashboard:** Fixed an issue where the history dashboard on the settings page would display "No history available yet." despite existing records. This was caused by the Webview (Clay UI) not having access to the watchapp's internal local storage. Data is now injected into the Webview upon loading.

## [1.8.0] - 2026-09-08

### Added
- **Interactive History Chart (US-20):** Replaced the static HTML bar chart in the settings page with a fully interactive Chart.js diagram using an iframe wrapper. This allows for better visualization of your pushup progress over time without conflicting with the Clay UI.
- **Offline Sync (US-19):** Pushups logged on the watch while disconnected from the phone will now automatically sync to the phone's dashboard once the connection is restored (e.g. by opening the settings page).

### Fixed
- **Settings Page History (US-18):** Fixed a bug where the Settings Page history would show up as empty due to unsupported key formats from third-party connections like Gadgetbridge.

## [1.7.0] - 2026-09-07

### Added
- **Direct Reminder Logging (US-17):** You can now directly open the Quick Log screen straight from a reminder notification by pressing the Select button. This makes logging much faster and avoids having to open the main menu first. The notification hint text has been updated to reflect this change.

## [1.6.0] - 2026-08-23

### Added
- **Strict Mode (US-16):** Added a new setting "Strict Mode (Prevent Goal Reduction)" to the app's configuration on the phone. When enabled, your daily goal will never automatically decrease (no deloads and no scheduled rest days), ensuring your progression only moves forward.
- **Configurable Auto-Dismiss (US-15):** Reminder notifications on the watch will now automatically dismiss themselves after a configurable duration (default: 20 seconds). You can adjust this duration or disable the feature entirely in the app's settings on your phone.
- **User-defined Minimum Goal (US-14):** The adaptive algorithm now respects your configured minimum target. Your daily goal will never drop below this baseline during deloads.
- **Smartwatch Confirmation Dialog (US-14):** When you lower your minimum goal directly on the smartwatch, you will now be prompted whether you want to reset your current adaptive progress to this new minimum, or keep your progress.
- **Terminology Updates (US-14):** Renamed "Tagesziel (Manuell)" / "Manual Target" to "Minimalziel" / "Minimum Goal" across the UI to clarify its behavior.

## [1.5.0] - 2026-07-10

### Added
- **Rest Day Performance Tracking (US-15):** Pushups logged on a rest day now count! If you meet or exceed the target of your last training day on a rest day, it triggers progressive overload. If you do fewer pushups, there is no penalty (no deload), respecting your rest day.

## [1.4.0] - 2026-06-28

### Fixed
- **Reliable Daily Reminders (US-12):** Fixed a bug where the background wakeup timer would not schedule a wakeup for the next day once the daily goal was reached or on a rest day. Reminders now correctly start each morning without requiring a manual app launch.

## [1.3.0] - 2026-06-25

### Added
- **Hold-to-Repeat (US-04):** You can now hold down the Up or Down button in the pushup logging view to continuously increase or decrease the pushup count. This allows for much faster logging of large sets.

## [1.2.0] - 2026-06-21

### Fixed
- **Language Persistence**: Fixed an issue where changing the language in the Settings page was not correctly saved and applied due to incorrect string-to-integer conversion (US-11).

## [1.1.0] - 2026-06-17

### Added
- **Manual Override for Adaptive Goals**: Added the ability to instantly override the daily target. When you change your "Manual Target" in the settings, the app now adopts this immediately for the current day and resets the adaptive streak.
- **Main Screen Indicators**: The main menu now provides visual indicators (`[+]`, `[-]`, `[Zzz]`) next to your progress to immediately see if today's goal was scaled up, down, or if you're on a rest day.

### Changed
- **Streamlined Workflow**: Made "Quick Log" the primary action from the main menu, providing a one-click entry to log your pushups.
- **UI Rename**: Renamed "Baseline Goal" to "Manual Target" ("Tagesziel (Manuell)" in German) to clarify its purpose.

### Removed
- **Auto-Tracking Sensor Logic**: Removed the experimental accelerometer-based automatic pushup counting feature. This dramatically reduces the app size, saves battery life, and ensures users no longer encounter frustrating counting errors due to sensor inaccuracies. All tracking is now handled reliably through the Quick Log menu.

### Fixed
- **Missing Glyphs Bug**: Fixed an issue where "min" and other non-number characters were rendered as blocky placeholders across the app interface (Settings, Main Screen). All texts are now cleanly rendered using a bold system font (`BITHAM_42_BOLD`).

## [1.0.0] - Initial Release
- Basic goal setting, reminders, smartphone history export, and first stable build for all platforms.
