from dotenv import load_dotenv
load_dotenv()  # reads .env into environment variables, if the file exists

import os
from flask import Flask, request, render_template, jsonify, redirect, url_for, session
import sqlite3
from datetime import datetime
from werkzeug.security import generate_password_hash, check_password_hash
from bot1 import send_to_channel  # reuses the exact same sending logic as bot1.py

app = Flask(__name__)
DB = "messages.db"

# Used to sign the caregiver's login session cookie.
app.secret_key = os.environ.get("SECRET_KEY", os.urandom(24))

# --- THIS FIXES THE SERVER ERROR ---
# Defines the 'friendly_time' filter used in caregiver.html
@app.template_filter('friendly_time')
def friendly_time(date_str):
    try:
        # Converts "2023-10-25T14:32:00Z" to "02:32 PM"
        dt = datetime.strptime(date_str.replace("Z", ""), "%Y-%m-%dT%H:%M:%S")
        return dt.strftime("%I:%M %p")
    except Exception:
        return date_str

def init_db():
    """Creates all tables if they don't exist yet."""
    conn = sqlite3.connect(DB)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            text TEXT NOT NULL,
            received_at TEXT NOT NULL
        )
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS reminders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            reminder_time TEXT NOT NULL,
            message TEXT NOT NULL,
            created_at TEXT NOT NULL
        )
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS sos_alerts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            text TEXT NOT NULL,
            received_at TEXT NOT NULL,
            acknowledged INTEGER NOT NULL DEFAULT 0
        )
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    """)
    conn.commit()
    conn.close()

# Runs immediately when this file is imported
init_db()

def get_setting(key):
    conn = sqlite3.connect(DB)
    row = conn.execute("SELECT value FROM settings WHERE key = ?", (key,)).fetchone()
    conn.close()
    return row[0] if row else None

def set_setting(key, value):
    conn = sqlite3.connect(DB)
    conn.execute(
        "INSERT INTO settings (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
        (key, value)
    )
    conn.commit()
    conn.close()

def pin_is_set():
    return get_setting("caregiver_pin_hash") is not None

def is_authed():
    return session.get("caregiver_authed") is True


@app.route("/")
def index():
    """The raw message-relay dashboard."""
    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row
    rows = conn.execute("SELECT * FROM messages ORDER BY id DESC").fetchall()
    conn.close()
    return render_template("index.html", messages=rows)


@app.route("/caregiver")
def caregiver():
    """Entry point for the caregiver."""
    if not pin_is_set():
        return render_template("pin_setup.html", error=None)

    if not is_authed():
        return render_template("pin.html", error=None)

    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row
    reminders = conn.execute(
        "SELECT * FROM reminders ORDER BY id DESC"
    ).fetchall()
    sos_alerts = conn.execute(
        "SELECT * FROM sos_alerts WHERE acknowledged = 0 ORDER BY id DESC"
    ).fetchall()
    conn.close()
    
    # --- FETCH PROFILE IDENTITY DATA ---
    c_name = get_setting("caregiver_name") or ""
    p_name = get_setting("patient_name") or ""
    p_age = get_setting("patient_age") or ""

    return render_template("caregiver.html", 
                           reminders=reminders, 
                           sos_alerts=sos_alerts, 
                           result=None,
                           caregiver_name=c_name,
                           patient_name=p_name,
                           patient_age=p_age)


@app.route("/caregiver/setup", methods=["POST"])
def caregiver_setup():
    """First-time PIN and Profile creation."""
    if pin_is_set():
        return redirect(url_for("caregiver"))

    pin = request.form.get("pin", "").strip()
    confirm = request.form.get("confirm", "").strip()
    
    # --- GRAB INITIAL PROFILE DETAILS ---
    c_name = request.form.get("caregiver_name", "").strip()
    p_name = request.form.get("patient_name", "").strip()
    p_age = request.form.get("patient_age", "").strip()

    if not (pin.isdigit() and len(pin) == 4):
        return render_template("pin_setup.html", error="PIN must be exactly 4 digits.")
    if pin != confirm:
        return render_template("pin_setup.html", error="PINs don't match, try again.")

    # --- SAVE PROFILE DATA alongside PIN ---
    set_setting("caregiver_pin_hash", generate_password_hash(pin))
    set_setting("caregiver_name", c_name)
    set_setting("patient_name", p_name)
    set_setting("patient_age", p_age)
    
    session["caregiver_authed"] = True
    return redirect(url_for("caregiver"))


@app.route("/caregiver/update-profile", methods=["POST"])
def update_profile():
    """Allows the user to edit profile details from the dashboard."""
    if not is_authed():
        return redirect(url_for("caregiver"))
        
    set_setting("caregiver_name", request.form.get("caregiver_name", "").strip())
    set_setting("patient_name", request.form.get("patient_name", "").strip())
    set_setting("patient_age", request.form.get("patient_age", "").strip())
    
    return redirect(url_for("caregiver"))


@app.route("/caregiver/login", methods=["POST"])
def caregiver_login():
    pin = request.form.get("pin", "").strip()
    stored_hash = get_setting("caregiver_pin_hash")

    if stored_hash and check_password_hash(stored_hash, pin):
        session["caregiver_authed"] = True
        return redirect(url_for("caregiver"))

    return render_template("pin.html", error="Incorrect PIN, please try again.")


@app.route("/caregiver/logout", methods=["POST"])
def caregiver_logout():
    session.pop("caregiver_authed", None)
    return redirect(url_for("caregiver"))


@app.route("/caregiver/change-pin", methods=["POST"])
def caregiver_change_pin():
    """Lets a logged-in caregiver change the PIN from within the dashboard."""
    if not is_authed():
        return redirect(url_for("caregiver"))

    current = request.form.get("current_pin", "").strip()
    new = request.form.get("new_pin", "").strip()
    confirm = request.form.get("confirm_pin", "").strip()
    stored_hash = get_setting("caregiver_pin_hash")

    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row
    reminders = conn.execute("SELECT * FROM reminders ORDER BY id DESC").fetchall()
    sos_alerts = conn.execute(
        "SELECT * FROM sos_alerts WHERE acknowledged = 0 ORDER BY id DESC"
    ).fetchall()
    conn.close()
    
    c_name = get_setting("caregiver_name") or ""
    p_name = get_setting("patient_name") or ""
    p_age = get_setting("patient_age") or ""

    if not stored_hash or not check_password_hash(stored_hash, current):
        result = {"ok": False, "info": "Current PIN is incorrect."}
    elif not (new.isdigit() and len(new) == 4):
        result = {"ok": False, "info": "New PIN must be exactly 4 digits."}
    elif new != confirm:
        result = {"ok": False, "info": "New PINs don't match."}
    else:
        set_setting("caregiver_pin_hash", generate_password_hash(new))
        result = {"ok": True, "info": "PIN updated successfully."}

    return render_template("caregiver.html", reminders=reminders, sos_alerts=sos_alerts, result=None, pin_result=result, caregiver_name=c_name, patient_name=p_name, patient_age=p_age)


@app.route("/caregiver/add", methods=["POST"])
def caregiver_add():
    """Adds a reminder."""
    if not is_authed():
        return redirect(url_for("caregiver"))

    reminder_time = request.form.get("reminder_time", "").strip()
    reminder_message = request.form.get("reminder_message", "").strip()

    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row

    if not reminder_time or not reminder_message:
        result = {"ok": False, "info": "Both time and message are required"}
    else:
        formatted = f"/remind {reminder_time} {reminder_message}"
        ok, info = send_to_channel(formatted)
        if ok:
            conn.execute(
                "INSERT INTO reminders (reminder_time, message, created_at) VALUES (?, ?, ?)",
                (reminder_time, reminder_message, datetime.utcnow().isoformat(timespec="seconds") + "Z")
            )
            conn.commit()
            result = {"ok": True, "info": f"Reminder set for {reminder_time}"}
        else:
            result = {"ok": False, "info": info}

    reminders = conn.execute("SELECT * FROM reminders ORDER BY id DESC").fetchall()
    sos_alerts = conn.execute("SELECT * FROM sos_alerts WHERE acknowledged = 0 ORDER BY id DESC").fetchall()
    conn.close()
    
    c_name = get_setting("caregiver_name") or ""
    p_name = get_setting("patient_name") or ""
    p_age = get_setting("patient_age") or ""
    
    return render_template("caregiver.html", reminders=reminders, sos_alerts=sos_alerts, result=result, caregiver_name=c_name, patient_name=p_name, patient_age=p_age)


@app.route("/caregiver/delete/<int:reminder_id>", methods=["POST"])
def caregiver_delete(reminder_id):
    """Removes a reminder from THIS dashboard's list only."""
    if not is_authed():
        return redirect(url_for("caregiver"))

    conn = sqlite3.connect(DB)
    conn.execute("DELETE FROM reminders WHERE id = ?", (reminder_id,))
    conn.commit()
    conn.close()
    return redirect(url_for("caregiver"))


@app.route("/caregiver/acknowledge/<int:alert_id>", methods=["POST"])
def caregiver_acknowledge(alert_id):
    """Marks an SOS alert as acknowledged."""
    if not is_authed():
        return redirect(url_for("caregiver"))

    conn = sqlite3.connect(DB)
    conn.execute("UPDATE sos_alerts SET acknowledged = 1 WHERE id = ?", (alert_id,))
    conn.commit()
    conn.close()
    return redirect(url_for("caregiver"))


@app.route("/api/telegram-relay", methods=["POST"])
def receive_message():
    """Bot2 sends every message from the dedicated emergency channel here."""
    data = request.get_json(force=True, silent=True) or {}
    text = data.get("text", "").strip()

    if not text:
        return jsonify({"error": "no text field provided"}), 400

    conn = sqlite3.connect(DB)
    conn.execute(
        "INSERT INTO sos_alerts (text, received_at) VALUES (?, ?)",
        (text, datetime.utcnow().isoformat(timespec="seconds") + "Z")
    )
    conn.commit()
    conn.close()

    return jsonify({"status": "ok", "type": "sos_alert"}), 200


if __name__ == "__main__":
    app.run(debug=True, port=5000)