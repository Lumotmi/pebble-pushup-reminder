# Pebble Workouts ⌚💪
*A 100% Vibe Coding Project - built entirely with AI!*

Get stronger every day right from your wrist! **Pebble Workouts** is your ultimate personal trainer for the Pebble Smartwatch. Whether you are stuck at a desk or working from home, this app ensures you stay active by gently reminding you to knock out a set of **push-ups, pull-ups and squats** at custom intervals.

## 🚀 Features
- **Push-ups, Pull-ups & Squats**: Track three exercises side by side. Each one has its own minimum goal, adaptive daily target, streak and history. Exercises you don't want can be switched off on the phone, or on the watch by setting their goal to "Off".
- **Adaptive Daily Goals**: The app intelligently learns from your performance while always respecting your configured minimum target! Crush your goals to trigger an 'Overload' day with a higher target, or get a 'Deload/Recovery' day if you need a break. Pushups done on a rest day also count and can trigger an 'Overload'. You can always set a new minimum goal manually!
- **Strict Mode**: Prevent your daily goal from ever being automatically reduced. For those who want to ensure their progression only moves forward, you can toggle Strict Mode in the settings.
- **Smart Reminders**: Set your active time window and reminder intervals. A reminder shows today's progress for every exercise and keeps coming until all of them are done. Reminders can automatically dismiss themselves after a configurable duration if you can't respond immediately. You can instantly **Snooze** reminders for a set duration (e.g., 1h, 4h, 8h, or until tomorrow) directly from the watch, or pause them manually from the main menu at any time. The app uses the reliable Pebble Wakeup API so it doesn't drain your battery while waiting in the background. You can also log reps directly from the reminder: press Select and pick the exercise (skipped if only one is left). After logging you return to the reminder until every exercise is done.
- **Quick Log**: Pick an exercise from the main menu and log your reps. Supports **Hold-to-Repeat**: hold down the up/down buttons to continuously and quickly increase or decrease the count!
- **Smartphone Dashboard**: Dive into your workout history! Check out an interactive performance chart (powered by Chart.js) right inside the Pebble app settings on your phone. Filter by exercise or view all three stacked, and export your data as CSV.
- **Offline Sync**: Logged pushups while disconnected from your phone will automatically synchronize the next time you connect!
- **Fully Offline & Bilingual**: No cloud dependencies! The settings menu (`pebble-clay`) renders completely offline. Available in English and German.

## 🛠️ Build Instructions
Since the official Pebble SDK is no longer actively maintained natively for modern OS, we use a Docker container to build the app safely.

1. Ensure you have [Docker](https://www.docker.com/) installed on your machine.
2. Clone this repository.
3. Open your terminal/command prompt in the project root folder.
4. Run the Pebble SDK build command via Docker:
   ```bash
   docker run --rm -v "%cd%:/app" -w /app rebble/pebble-sdk pebble build
   ```
   *(On macOS/Linux, replace `%cd%` with `$(pwd)`)*
5. The compiled app will be generated at `build/app.pbw`.

## 📱 Installation (Sideloading)
1. Transfer the `build/app.pbw` file to your smartphone (e.g., via Google Drive, Dropbox, or Email).
2. Open the file on your phone using the official **Pebble App** (or the Rebble patched app).
3. Confirm the installation when prompted.

## 📝 Changelog
See [CHANGELOG.md](CHANGELOG.md) for the latest updates.

## 📜 License
This project is open-source. Feel free to fork and modify!
