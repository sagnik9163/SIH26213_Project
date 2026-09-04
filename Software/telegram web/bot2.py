from dotenv import load_dotenv
load_dotenv()

import os
import requests
import time

# Read from environment variables (see .env.example for local use).
BOT2_TOKEN = os.environ.get("BOT2_TOKEN")

# Locally this points at your own laptop. Once deployed, set this
# environment variable to your real dashboard's URL instead, e.g.
# https://yourproject.onrender.com/api/telegram-relay
YOUR_WEB_ENDPOINT = os.environ.get(
    "WEB_ENDPOINT", "http://localhost:5000/api/telegram-relay"
)


def get_updates(offset=None):
    url = f"https://api.telegram.org/bot{BOT2_TOKEN}/getUpdates"
    params = {"timeout": 30}
    if offset is not None:
        params["offset"] = offset
    return requests.get(url, params=params).json()


def main():
    if not BOT2_TOKEN:
        print("ERROR: BOT2_TOKEN environment variable is not set. Stopping.")
        return

    print("Bot2 is running and watching the channel... (Ctrl+C to stop)")
    offset = None

    while True:
        try:
            updates = get_updates(offset)

            if not updates.get("ok"):
                print("Error from Telegram:", updates)
                time.sleep(3)
                continue

            for update in updates.get("result", []):
                offset = update["update_id"] + 1
                post = update.get("channel_post")

                if post and "text" in post:
                    text = post["text"]
                    print(f"New channel message caught: {text}")

                    r = requests.post(YOUR_WEB_ENDPOINT, json={"text": text})
                    if r.status_code == 200:
                        print("Forwarded to dashboard successfully.")
                    else:
                        print("Failed to forward:", r.status_code, r.text)

        except requests.exceptions.ConnectionError:
            print("Could not reach the dashboard. Is app.py still running?")
            time.sleep(3)
        except KeyboardInterrupt:
            print("Bot2 stopped.")
            break


if __name__ == "__main__":
    main()
