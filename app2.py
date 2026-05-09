from flask import Flask, render_template, redirect, url_for, request, send_file
import mysql.connector as connector
from datetime import datetime, timezone
import pandas as pd
import matplotlib
matplotlib.use("Agg")  # Non-interactive backend — must be set BEFORE importing pyplot
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import os
import io

app = Flask(__name__)

# ======================================================
# DIRECTORIES
# ======================================================
STREAM_DIR = "stream"
os.makedirs(STREAM_DIR, exist_ok=True)
os.makedirs("static", exist_ok=True)

# ======================================================
# DATABASE CONNECTION
# ======================================================
def get_connection():
    return connector.connect(
        host="localhost",
        port=3306,
        user="root",
        password="@Onepiece0907",
        database="ga_db"
    )


# ======================================================
# HOME
# ======================================================
@app.route("/", methods=["GET", "POST"])
def home():
    return render_template("index.html")


# ======================================================
# SIGNUP
# ======================================================
@app.route("/signup", methods=["GET", "POST"])
def signup():
    if request.method == "POST":
        data = request.form
        acc_email = data.get("email")
        acc_password = data.get("password")

        with get_connection() as conn:
            cursor = conn.cursor()

            cursor.execute("SELECT * FROM users WHERE email = %s", (acc_email,))
            result = cursor.fetchone()

            if result:
                return render_template("signup.html", msg="User already exists")

            cursor.execute(
                "INSERT INTO users (email, passwrd) VALUES (%s, %s)",
                (acc_email, acc_password)
            )
            conn.commit()

        return redirect("/login")

    return render_template("signup.html")


# ======================================================
# LOGIN
# ======================================================
@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        data = request.form
        acc_email = data.get("email")
        acc_password = data.get("password")

        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT passwrd FROM users WHERE email = %s", (acc_email,))
            result = cursor.fetchone()

            if not result:
                return render_template("login.html", msg="User not found")

            stored_password = result[0]

            if acc_password == stored_password:
                routes = {
                    "240254260@edu.vut.ac.za": "/pir_sensor",
                    "218541309@edu.vut.ac.za":  "/ldr_sensor",
                    "224303635@edu.vut.ac.za":  "/ultson_sensor",
                    "240716574@edu.vut.ac.za":  "/camera",
                    "221569766@edu.vut.ac.za":  "/dht22_sensor",
                    "admin@edu.vut.ac.za":       "/admin",
                }
                destination = routes.get(acc_email, "/")
                return redirect(destination)
            else:
                return render_template("login.html", msg="Invalid password")

    return render_template("login.html")


# ======================================================
# ADMIN
# ======================================================
@app.route("/admin", methods=["GET", "POST"])
def admin():
    with get_connection() as conn:
        cursor = conn.cursor()
        cursor.execute("SELECT COUNT(*) FROM camera_events")
        photo_count = cursor.fetchone()[0]

    return render_template("dashboard.html", photo_count=photo_count)


# ======================================================
# ABOUT / TEAM / DHT
# ======================================================
@app.route("/about")
def about():
    return render_template("about.html")

@app.route("/team")
def team():
    return render_template("team.html")

@app.route("/dht")
def dht():
    return render_template("dht.html")


# ======================================================
# CAMERA — MAIN PAGE (live stream + gallery + graph)
# ======================================================
@app.route("/camera")
def camera():
    """Landing page for the camera operator — shows live feed,
    gallery thumbnails and the motion-frequency graph."""
    with get_connection() as conn:
        cursor = conn.cursor()
        cursor.execute(
            "SELECT id, event_time FROM camera_events ORDER BY id DESC LIMIT 10"
        )
        rows = cursor.fetchall()

    return render_template("camera.html", photos=rows)


# ======================================================
# LIVE STREAM — receive frame from ESP32
# ======================================================
@app.route("/cam_stream", methods=["POST"])
def cam_stream():
    """ESP32 posts raw JPEG bytes here every ~1 s for the live view."""
    img = request.data
    with open(os.path.join(STREAM_DIR, "latest.jpg"), "wb") as f:
        f.write(img)
    return "ok", 200


# ======================================================
# SERVE LATEST FRAME (polled by the live-view page)
# ======================================================
@app.route("/latest.jpg")
def latest():
    path = os.path.join(STREAM_DIR, "latest.jpg")
    if not os.path.exists(path):
        return "No stream yet", 404
    with open(path, "rb") as f:
        data = f.read()
    return data, 200, {
        "Content-Type": "image/jpeg",
        "Cache-Control": "no-cache, no-store, must-revalidate",
        "Pragma": "no-cache",
        "Expires": "0"
    }


# ======================================================
# LIVE VIEW PAGE (standalone)
# ======================================================
@app.route("/view")
def view():
    return render_template("cam_livestream.html")


# ======================================================
# MOTION CAPTURE — receive motion photo from ESP32
# NOTE: ESP32 posts to /motion_capture (fixed from /upload_photo)
# ======================================================
@app.route("/motion_capture", methods=["POST"])
def motion_capture():
    """
    Receives a JPEG from the ESP32 when motion is detected.
    Saves the image as a BLOB in camera_events.
    Enforces a rolling window of 10 photos — oldest is deleted when
    the 11th arrives.
    """
    img = request.data

    if not img:
        return {"status": "error", "message": "No image data received"}, 400

    with get_connection() as conn:
        cursor = conn.cursor()

        # ── Insert the new photo ──────────────────────────────────────────
        cursor.execute(
            "INSERT INTO camera_events (image) VALUES (%s)",
            (img,)
        )
        conn.commit()

        # ── Enforce 10-photo cap ──────────────────────────────────────────
        # Count total rows
        cursor.execute("SELECT COUNT(*) FROM camera_events")
        total = cursor.fetchone()[0]

        if total > 10:
            # Find IDs of all rows beyond the newest 10 (i.e., the oldest ones)
            cursor.execute("""
                SELECT id FROM camera_events
                ORDER BY id ASC
                LIMIT %s
            """, (total - 10,))
            old_ids = [row[0] for row in cursor.fetchall()]

            if old_ids:
                format_strings = ",".join(["%s"] * len(old_ids))
                cursor.execute(
                    f"DELETE FROM camera_events WHERE id IN ({format_strings})",
                    old_ids
                )
                conn.commit()
                print(f"Auto-deleted {len(old_ids)} old photo(s). Keeping newest 10.")

    print("Motion photo saved to database.")
    return {"status": "saved"}, 200


# ======================================================
# GALLERY — show all stored photos (newest first)
# ======================================================
@app.route("/gallery")
def gallery():
    with get_connection() as conn:
        cursor = conn.cursor()
        cursor.execute(
            "SELECT id, event_time FROM camera_events ORDER BY id DESC"
        )
        rows = cursor.fetchall()

    return render_template("gallery.html", photos=rows)


# ======================================================
# SERVE A SINGLE PHOTO BY ID
# ======================================================
@app.route("/photo/<int:photo_id>")
def photo(photo_id):
    with get_connection() as conn:
        cursor = conn.cursor()
        cursor.execute(
            "SELECT image FROM camera_events WHERE id = %s",
            (photo_id,)
        )
        row = cursor.fetchone()

    if row:
        return row[0], 200, {"Content-Type": "image/jpeg"}

    return "Not Found", 404


# ======================================================
# CAMERA GRAPH — motion detections over time (last 10)
# ======================================================
@app.route("/camera_graph")
def camera_graph():
    """
    Generates a bar chart of motion-detection events using the
    timestamps of the (up to) 10 most recent captures.
    Returns the chart as a PNG image.
    """
    with get_connection() as conn:
        df = pd.read_sql(
            "SELECT event_time FROM camera_events ORDER BY id DESC LIMIT 10",
            conn
        )

    if df.empty:
        # Return a simple placeholder image rather than plain text so the
        # <img> tag in the template doesn't break.
        fig, ax = plt.subplots(figsize=(8, 4))
        ax.text(0.5, 0.5, "No camera data yet", ha="center", va="center",
                fontsize=14, color="grey")
        ax.axis("off")
    else:
        df["event_time"] = pd.to_datetime(df["event_time"])
        # Sort chronologically for the chart
        df = df.sort_values("event_time")

        # Label each capture with its local time string
        labels = df["event_time"].dt.strftime("%H:%M:%S")
        x = range(len(labels))

        fig, ax = plt.subplots(figsize=(10, 5))
        bars = ax.bar(x, [1] * len(labels), color="#e74c3c", edgecolor="black", width=0.6)

        ax.set_xticks(list(x))
        ax.set_xticklabels(labels, rotation=45, ha="right", fontsize=9)
        ax.set_yticks([])  # Height is always 1 — y-axis conveys no extra info
        ax.set_xlabel("Capture Time", fontsize=11)
        ax.set_title("Last 10 Motion Captures — Timeline", fontsize=13, fontweight="bold")

        # Annotate each bar with its sequence number
        for i, bar in enumerate(bars):
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 0.01,
                f"#{i+1}",
                ha="center", va="bottom", fontsize=8
            )

        plt.tight_layout()

    buf = io.BytesIO()
    plt.savefig(buf, format="png", dpi=100)
    plt.close(fig)
    buf.seek(0)

    return send_file(buf, mimetype="image/png")


# ======================================================
# PIR SENSOR
# ======================================================
@app.route("/pir_sensor", methods=["POST", "GET"])
def pir_sensor():
    if request.method == "POST":
        data = request.json
        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_id = data.get("device_id")
        readings  = data.get("readings", {})
        event_type = readings.get("type")
        value      = readings.get("value")
        is_armed   = readings.get("isArmed")

        print(f"PIR | {device_id} | {event_type}: {value} | armed: {is_armed}")

        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO sensor_readings_log (timestamp, reading_value, device_id)
                VALUES (%s, %s, (SELECT device_id FROM device_info WHERE device_name = %s LIMIT 1))
            """, (
                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                str(value),
                device_id
            ))
            conn.commit()

        return {"status": "ok"}, 200

    return render_template("pir.html")


# ======================================================
# LDR SENSOR
# ======================================================
latest_ldr = {
    "device_id": "",
    "value": 0,
    "event": "dark",
    "time": ""
}

@app.route("/ldr_sensor", methods=["GET", "POST"])
def ldr_sensor():
    global latest_ldr

    # ==================================================
    # POST REQUEST FROM ESP32
    # ==================================================
    if request.method == "POST":
        data = request.json

        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_id = data.get("device_id", "ESP32_LDR")
        readings = data.get("readings", {})

        event_type = readings.get("type", "ldr")
        value = readings.get("value", 0)
        event = readings.get("event", "dark")

        current_time = datetime.now().strftime("%H:%M:%S")

        # Save latest live values
        latest_ldr.update({
            "device_id": device_id,
            "value": value,
            "event": event,
            "time": current_time
        })

        print(f"LDR | {device_id} | {event_type}: {value} | event: {event}")

        # Save into database
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO sensor_readings_log
                (timestamp, reading_value, device_id)
                VALUES (%s, %s,
                    (SELECT device_id
                     FROM device_info
                     WHERE device_name = %s
                     LIMIT 1)
                )
            """, (
                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                str(value),
                device_id
            ))
            conn.commit()

        return {"status": "ok"}, 200

    # ==================================================
    # GET REQUEST FOR WEBSITE
    # ==================================================
    with get_connection() as conn:
        cursor = conn.cursor(dictionary=True)

        cursor.execute("""
            SELECT timestamp, reading_value
            FROM sensor_readings_log
            ORDER BY timestamp DESC
            LIMIT 20
        """)
        rows = cursor.fetchall()

    rows.reverse()

    chart_labels = []
    chart_values = []

    for row in rows:
        chart_labels.append(row["timestamp"].strftime("%H:%M:%S"))
        chart_values.append(float(row["reading_value"]))

    return render_template(
        "ldr.html",
        ldr=latest_ldr,
        history=rows,
        chart_labels=chart_labels,
        chart_values=chart_values
    )


# ======================================================
# ULTRASONIC SENSOR
# ======================================================
@app.route("/ultson_sensor", methods=["GET", "POST"])
def ultson_sensor():
    if request.method == "POST":
        data = request.json
        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_id  = data.get("device_id")
        readings   = data.get("readings", {})
        event_type = readings.get("type")
        value      = readings.get("value")
        event      = readings.get("event", "")

        print(f"ULTRASONIC | {device_id} | {event_type}: {value} | event: {event}")

        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO sensor_readings_log (timestamp, reading_value, device_id)
                VALUES (%s, %s, (SELECT device_id FROM device_info WHERE device_name = %s LIMIT 1))
            """, (
                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                str(value),
                device_id
            ))
            conn.commit()

        return {"status": "ok"}, 200

    return render_template("ultson.html")


# ======================================================
# DHT22 SENSOR
# ======================================================
@app.route("/dht22_sensor", methods=["GET", "POST"])
def dht22_sensor():
    if request.method == "POST":
        data = request.json
        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_id = data.get("device_id")
        readings  = data.get("readings", [])

        if len(readings) < 2:
            return {"status": "error", "message": "Expected 2 readings"}, 400

        temp_reading     = readings[0]
        humidity_reading = readings[1]

        temp_value     = temp_reading.get("value")
        humidity_value = humidity_reading.get("value")

        print(f"DHT22 | {device_id} | Temp: {temp_value}°C | Humidity: {humidity_value}%")

        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        with get_connection() as conn:
            cursor = conn.cursor()
            # Log temperature
            cursor.execute("""
                INSERT INTO sensor_readings_log (timestamp, reading_value, device_id)
                VALUES (%s, %s, (SELECT device_id FROM device_info WHERE device_name = %s LIMIT 1))
            """, (timestamp, str(temp_value), f"{device_id}_temp"))
            # Log humidity
            cursor.execute("""
                INSERT INTO sensor_readings_log (timestamp, reading_value, device_id)
                VALUES (%s, %s, (SELECT device_id FROM device_info WHERE device_name = %s LIMIT 1))
            """, (timestamp, str(humidity_value), f"{device_id}_humidity"))
            conn.commit()

        return {"status": "ok"}, 200

    return render_template("dht22.html")


# ======================================================
# RUN
# ======================================================
if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5000)
