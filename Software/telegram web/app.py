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

# Used to sign the caregiver's login session cookie. Set this in your .env
# (and later in Render's Environment tab) to any random string — if it's
# not set, a temporary one is generated, which just means everyone gets
# logged out whenever the server restarts.
app.secret_key = os.environ.get("SECRET_KEY", os.urandom(24))


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
    # Small key/value table for settings — right now just the caregiver's
    # PIN, stored as a hash rather than as readable text.
    conn.execute("""
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    """)
    conn.commit()
    conn.close()


# Runs immediately when this file is imported — whether that's via
# "python app.py" (local) or "gunicorn app:app" (Render).
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
    """The raw message-relay dashboard (from earlier in the project).
    Kept for reference/debugging — the caregiver flow lives at /caregiver."""
    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row
    rows = conn.execute("SELECT * FROM messages ORDER BY id DESC").fetchall()
    conn.close()
    return render_template("index.html", messages=rows)


@app.route("/caregiver")
def caregiver():
    """Entry point for the caregiver.
    - No PIN set yet anywhere -> show the one-time setup screen.
    - PIN set but not logged in this session -> show the login screen.
    - Logged in -> show the reminders panel."""
    if not pin_is_set():
        return render_template("pin_setup.html", error=None)

    if not is_authed():
        return render_template("pin.html", error=None)

    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row
    reminders = conn.execute(
        "SELECT * FROM reminders ORDER BY id DESC"
    ).fetchall()
    conn.close()
    return render_template("caregiver.html", reminders=reminders, result=None)


@app.route("/caregiver/setup", methods=["POST"])
def caregiver_setup():
    """First-time PIN creation. Only reachable while no PIN has been set."""
    if pin_is_set():
        return redirect(url_for("caregiver"))

    pin = request.form.get("pin", "").strip()
    confirm = request.form.get("confirm", "").strip()

    if not (pin.isdigit() and len(pin) == 4):
        return render_template("pin_setup.html", error="PIN must be exactly 4 digits.")
    if pin != confirm:
        return render_template("pin_setup.html", error="PINs don't match, try again.")

    set_setting("caregiver_pin_hash", generate_password_hash(pin))
    session["caregiver_authed"] = True
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
    conn.close()

    if not stored_hash or not check_password_hash(stored_hash, current):
        result = {"ok": False, "info": "Current PIN is incorrect."}
    elif not (new.isdigit() and len(new) == 4):
        result = {"ok": False, "info": "New PIN must be exactly 4 digits."}
    elif new != confirm:
        result = {"ok": False, "info": "New PINs don't match."}
    else:
        set_setting("caregiver_pin_hash", generate_password_hash(new))
        result = {"ok": True, "info": "PIN updated successfully."}

    return render_template("caregiver.html", reminders=reminders, result=None, pin_result=result)


@app.route("/caregiver/add", methods=["POST"])
def caregiver_add():
    """Adds a reminder: builds the exact "/remind HH:MM message" string
    the ESP32 firmware expects, sends it via Bot1, and records it in our
    own local list so the caregiver can see what's been sent."""
    if not is_authed():
        return redirect(url_for("caregiver"))

    reminder_time = request.form.get("reminder_time", "").strip()
    reminder_message = request.form.get("reminder_message", "").strip()

    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row

    if not reminder_time or not reminder_message:
        result = {"ok": False, "info": "Both time and message are required"}
    else:
        # An <input type="time"> always outputs HH:MM (24-hour), which is
        # exactly what the ESP32's parser expects — no extra formatting needed.
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
    conn.close()
    return render_template("caregiver.html", reminders=reminders, result=result)


@app.route("/caregiver/delete/<int:reminder_id>", methods=["POST"])
def caregiver_delete(reminder_id):
    """Removes a reminder from THIS dashboard's list only. It does not
    reach out to the device — cancelling on the device itself still
    needs the /clear command sent directly in Telegram."""
    if not is_authed():
        return redirect(url_for("caregiver"))

    conn = sqlite3.connect(DB)
    conn.execute("DELETE FROM reminders WHERE id = ?", (reminder_id,))
    conn.commit()
    conn.close()
    return redirect(url_for("caregiver"))


@app.route("/api/telegram-relay", methods=["POST"])
def receive_message():
    """This is the endpoint Bot2 sends data to (if you're using the
    website-relay flow rather than the ESP32 polling Telegram directly).
    Expects JSON like: {"text": "some message"}"""
    data = request.get_json(force=True, silent=True) or {}
    text = data.get("text", "").strip()

    if not text:
        return jsonify({"error": "no text field provided"}), 400

    conn = sqlite3.connect(DB)
    conn.execute(
        "INSERT INTO messages (text, received_at) VALUES (?, ?)",
        (text, datetime.utcnow().isoformat(timespec="seconds") + "Z")
    )
    conn.commit()
    conn.close()

    return jsonify({"status": "ok"}), 200


if __name__ == "__main__":
    # debug=True auto-restarts the server when you edit the code — great for development.
    # Turn it off before putting this on a real server later.
    app.run(debug=True, port=5000)
