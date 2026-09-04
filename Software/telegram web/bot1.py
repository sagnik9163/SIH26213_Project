import os
import requests

# These are read from environment variables instead of being typed here.
# Locally: set them before running (see .env.example).
# On Render: set them in the dashboard under Environment.
BOT1_TOKEN = os.environ.get("BOT1_TOKEN")
CHANNEL_USERNAME = os.environ.get("CHANNEL_USERNAME")


def send_to_channel(text):
    """Sends `text` to the channel. Returns (success: bool, info: str)
    so callers (like the web dashboard) can react to the result."""
    if not BOT1_TOKEN or not CHANNEL_USERNAME:
        return False, "BOT1_TOKEN or CHANNEL_USERNAME environment variable is not set"

    url = f"https://api.telegram.org/bot{BOT1_TOKEN}/sendMessage"
    payload = {"chat_id": CHANNEL_USERNAME, "text": text}

    try:
        resp = requests.post(url, json=payload, timeout=10)
    except requests.exceptions.RequestException as e:
        return False, f"Network error reaching Telegram: {e}"

    if resp.status_code != 200:
        try:
            description = resp.json().get("description", "Unknown error")
        except ValueError:
            description = f"Telegram returned status {resp.status_code} (non-JSON response)"
        return False, description

    return True, "Message sent successfully"


if __name__ == "__main__":
    # Only runs when you execute "python bot1.py" directly.
    ok, info = send_to_channel("Hello from Bot1 - this is a test message")
    print(info)
