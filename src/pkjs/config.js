module.exports = [
  {
    "type": "heading",
    "defaultValue": "Workouts Configuration"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "General Settings"
      },
      {
        "type": "select",
        "messageKey": "LANGUAGE",
        "defaultValue": "0",
        "label": "Language / Sprache",
        "options": [
          { 
            "label": "Deutsch", 
            "value": "0" 
          },
          { 
            "label": "English", 
            "value": "1" 
          }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "STRICT_MODE",
        "defaultValue": false,
        "label": "Strict Mode (Prevent Goal Reduction)",
        "description": "If enabled, your daily goals will never decrease automatically."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Exercises"
      },
      {
        "type": "text",
        "defaultValue": "Each exercise has its own minimum daily goal and adaptive progression."
      },
      {
        "type": "toggle",
        "messageKey": "ENABLE_PUSHUPS",
        "defaultValue": true,
        "label": "Push-ups / Liegestütze"
      },
      {
        "type": "slider",
        "messageKey": "DAILY_GOAL",
        "defaultValue": 30,
        "label": "Push-ups: Minimum Goal",
        "min": 5,
        "max": 500,
        "step": 5
      },
      {
        "type": "toggle",
        "messageKey": "ENABLE_PULLUPS",
        "defaultValue": true,
        "label": "Pull-ups / Klimmzüge"
      },
      {
        "type": "slider",
        "messageKey": "DAILY_GOAL_PULLUPS",
        "defaultValue": 10,
        "label": "Pull-ups: Minimum Goal",
        "min": 1,
        "max": 200,
        "step": 1
      },
      {
        "type": "toggle",
        "messageKey": "ENABLE_SQUATS",
        "defaultValue": true,
        "label": "Squats / Kniebeugen"
      },
      {
        "type": "slider",
        "messageKey": "DAILY_GOAL_SQUATS",
        "defaultValue": 30,
        "label": "Squats: Minimum Goal",
        "min": 5,
        "max": 500,
        "step": 5
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Reminders & Active Hours"
      },
      {
        "type": "slider",
        "messageKey": "REMINDER_INTERVAL",
        "defaultValue": 60,
        "label": "Reminder Interval (Minutes)",
        "description": "How often you want to be reminded.",
        "min": 15,
        "max": 240,
        "step": 15
      },
      {
        "type": "slider",
        "messageKey": "ACTIVE_START_HOUR",
        "defaultValue": 8,
        "label": "Active Start Hour",
        "description": "When should the reminders start? (0-23)",
        "min": 0,
        "max": 23,
        "step": 1
      },
      {
        "type": "slider",
        "messageKey": "ACTIVE_END_HOUR",
        "defaultValue": 20,
        "label": "Active End Hour",
        "description": "When should the reminders stop? (0-23)",
        "min": 0,
        "max": 23,
        "step": 1
      },
      {
        "type": "slider",
        "messageKey": "AUTO_DISMISS_DURATION",
        "defaultValue": 20,
        "label": "Auto-Dismiss Duration (Seconds)",
        "description": "How long until the reminder closes automatically? (0 = Disabled)",
        "min": 0,
        "max": 120,
        "step": 5
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "History Dashboard"
      },
      {
        "type": "text",
        "defaultValue": "<div id='dashboard-chart' style='margin:10px 0;'>No history available yet.</div><button class='btn' id='btn-export-csv' style='width:100%;'>Export CSV</button>"
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
