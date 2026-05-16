from flask import Flask, render_template, redirect, request, send_file, jsonify
import mysql.connector as connector
from datetime import datetime
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import os
import time

app = Flask(__name__)

# ======================================================
# DIRECTORIES
# ======================================================
STREAM_DIR = "stream"
os.makedirs(STREAM_DIR, exist_ok=True)
os.makedirs("static", exist_ok=True)

# ======================================================
# CAPTURE QUEUE — integer counter, thread-safe lock
# ======================================================
# FIX: was a simple boolean (pir_triggered = True/False).
# Problem: if camera ESP missed the poll window (busy with stream timeout),
# the flag was already reset to False and the capture was lost forever.
#
# Now we use a COUNTER:
#   - PIR fires → counter += 1
#   - Camera polls → counter > 0 → return capture:true, counter -= 1
#   - Each PIR HIGH event is guaranteed exactly one photo, even if polled late.


# ======================================================
# LATEST SENSOR VALUES
# ======================================================
latest_pir = {
    "device_id": "sobonga_esp",
    "value": 0,
    "event": "No Motion",
    "armed": False,
    "time": "--"
}

latest_ldr = {
    "device_id": "bridgete_esp",
    "value": 0,
    "event": "dark",
    "time": "--"
}

latest_ultrasonic = {
    "device_id": "fanelo_esp",
    "distance_cm": 0,
    "event": "",
    "time": "--"
}

# ======================================================
# DATABASE CONNECTION
# ======================================================
def get_connection():
    return connector.connect(
        host="localhost",
        port=3306,
        user="root",
        password="labadmin",
        database="ga_db"
    )


# ======================================================
# HELPER — fetch last N readings for a named device
# ======================================================
def get_readings(device_name, limit=20):
    try:
        with get_connection() as conn:
            cursor = conn.cursor(dictionary=True)
            cursor.execute("""
                SELECT s.record_id, s.timestamp, s.reading_value
                FROM sensor_readings_log s
                JOIN device_info d ON s.device_id = d.device_id
                WHERE d.device_name = %s
                ORDER BY s.record_id DESC
                LIMIT %s
            """, (device_name, limit))
            rows = cursor.fetchall()
        return list(reversed(rows))
    except Exception as e:
        print(f"DB read error [{device_name}]: {e}")
        return []


# ══════════════════════════════════════════════════════
# HOME / AUTH
# ══════════════════════════════════════════════════════

@app.route("/", methods=["GET", "POST"])
def home():
    return render_template("index.html")

@app.route("/about")
def about():
    return render_template("about.html")

@app.route("/team")
def team():
    return render_template("team.html")

@app.route("/signup", methods=["GET", "POST"])
def signup():
    if request.method == "POST":
        acc_email    = request.form.get("email")
        acc_password = request.form.get("password")
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM users WHERE email = %s", (acc_email,))
            if cursor.fetchone():
                return render_template("signup.html", msg="User already exists")
            cursor.execute("INSERT INTO users (email, passwrd) VALUES (%s, %s)",
                           (acc_email, acc_password))
            conn.commit()
        return redirect("/login")
    return render_template("signup.html")

@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        acc_email    = request.form.get("email")
        acc_password = request.form.get("password")
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT passwrd FROM users WHERE email = %s", (acc_email,))
            result = cursor.fetchone()
        if not result:
            return render_template("login.html", msg="User not found")
        if acc_password != result[0]:
            return render_template("login.html", msg="Invalid password")
        routes = {
            "240254260@edu.vut.ac.za": "/pir_sensor",
            "218541309@edu.vut.ac.za": "/ldr_sensor",
            "224303635@edu.vut.ac.za": "/ultson_sensor",
            "240716574@edu.vut.ac.za": "/camera",
            "221569766@edu.vut.ac.za": "/dht22_sensor",
            "admin@edu.vut.ac.za":     "/admin",
        }
        return redirect(routes.get(acc_email, "/"))
    return render_template("login.html")


# ══════════════════════════════════════════════════════
# ADMIN
# ══════════════════════════════════════════════════════

@app.route("/admin")
def admin():
    try:
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) FROM camera_events")
            photo_count = cursor.fetchone()[0]
            cursor.execute("SELECT COUNT(*) FROM sensor_readings_log")
            reading_count = cursor.fetchone()[0]
    except Exception as e:
        print(f"Admin DB error: {e}")
        photo_count = reading_count = 0

    sensors = {
        "LDR":        latest_ldr        if latest_ldr["time"] != "--"  else None,
        "PIR":        latest_pir        if latest_pir["time"] != "--"  else None,
        "TEMP":       latest_dht22      if latest_dht22["time"] != ""  else None,
        "ULTRASONIC": latest_ultrasonic if latest_ultrasonic["time"] != "--" else None,
    }

    return render_template("dashboard.html",
                           photo_count=photo_count,
                           reading_count=reading_count,
                           sensors=sensors)


# ══════════════════════════════════════════════════════
# PIR SENSOR
# ══════════════════════════════════════════════════════

@app.route("/pir_sensor", methods=["GET", "POST"])
def pir_sensor():

    global latest_pir

    if request.method == "POST":

        data = request.json

        if not data:
            return {"status": "error"}, 400

        device_id = data.get("device_id", "sobonga_esp")

        readings = data.get("readings", {})

        raw_value = readings.get("value", 0)

        is_armed = readings.get("isArmed", False)

        value = 1 if str(raw_value).lower() in ("true", "1") else 0

        event = "Motion Detected" if value == 1 else "No Motion"

        latest_pir.update({
            "device_id": device_id,
            "value": value,
            "event": event,
            "armed": is_armed,
            "time": datetime.now().strftime("%H:%M:%S")
        })

        print(f"PIR | {device_id} | {event}")

        # DATABASE LOG
        try:

            with get_connection() as conn:

                cursor = conn.cursor()

                cursor.execute("""
                    INSERT INTO sensor_readings_log
                    (timestamp, reading_value, device_id)
                    VALUES (
                        %s,
                        %s,
                        (
                            SELECT device_id
                            FROM device_info
                            WHERE device_name = %s
                            LIMIT 1
                        )
                    )
                """, (
                    datetime.now(),
                    str(value),
                    device_id
                ))

                conn.commit()

        except Exception as e:

            print("PIR DB error:", e)

        return {"status": "ok"}, 200

    rows = get_readings("maambele_esp", 20)

    chart_labels = [
        str(r["timestamp"])[-8:]
        for r in rows
    ]

    chart_values = [
        1 if str(r["reading_value"]).lower() in ("1", "true")
        else 0
        for r in rows
    ]

    return render_template(
        "pir.html",
        pir=latest_pir,
        history=rows,
        chart_labels=chart_labels,
        chart_values=chart_values
    )

# ══════════════════════════════════════════════════════
# CAMERA PAGE
# ══════════════════════════════════════════════════════

@app.route("/camera")
def camera():
    try:
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT id, event_time
                FROM camera_events
                ORDER BY id DESC
                LIMIT 10
            """)
            rows = cursor.fetchall()
    except Exception as e:
        print(f"Camera page DB error: {e}")
        rows = []

    graph_rows   = list(reversed(rows))
    graph_labels = []
    graph_values = []
    for i, row in enumerate(graph_rows, start=1):
        event_time = row[1]
        graph_labels.append(
            event_time.strftime("%H:%M:%S") if event_time else "Unknown"
        )
        graph_values.append(i)

    return render_template("camera.html",
                           photos=rows,
                           graph_labels=graph_labels,
                           graph_values=graph_values)


# ══════════════════════════════════════════════════════
# LIVE STREAM
# ══════════════════════════════════════════════════════

@app.route("/cam_stream", methods=["POST"])
def cam_stream():
    img = request.data
    if img:
        with open(os.path.join(STREAM_DIR, "latest.jpg"), "wb") as f:
            f.write(img)
    return "ok", 200

@app.route("/latest.jpg")
def latest():

    path = os.path.join(STREAM_DIR, "latest.jpg")

    # No image has ever been received
    if not os.path.exists(path):
        return "No stream available", 404

    try:

        # Check how old the image is
        image_age = time.time() - os.path.getmtime(path)

        # If image older than 5 seconds,
        # assume ESP32-CAM disconnected
        if image_age > 5:

            print("Camera offline — latest.jpg expired")

            return "Camera offline", 404

        # Send latest frame
        return send_file(
            path,
            mimetype="image/jpeg",
            max_age=0
        )

    except Exception as e:

        print("latest.jpg error:", e)

        return "Stream error", 500

# ══════════════════════════════════════════════════════
# MOTION CAPTURE — save PIR-triggered photo
# ══════════════════════════════════════════════════════

@app.route("/motion_capture", methods=["POST"])
def motion_capture():

    img = request.data

    if not img:
        return {"status": "error"}, 400

    try:
        with get_connection() as conn:
            cursor = conn.cursor()

            # 1. INSERT IMAGE
            cursor.execute("""
                INSERT INTO camera_events (image, event_time)
                VALUES (%s, %s)
            """, (img, datetime.now()))

            # 2. CLEANUP (KEEP ONLY 10)
            cursor.execute("""
                SELECT COUNT(*) FROM camera_events
            """)

            count = cursor.fetchone()[0]

            if count > 10:
                cursor.execute("""
                    DELETE FROM camera_events
                    ORDER BY id ASC
                    LIMIT %s
                """, (count - 10,))

            conn.commit()

    except Exception as e:
        print("motion_capture DB error:", e)
        return {"status": "error", "msg": str(e)}, 500

    print("Saved capture OK")
    return {"status": "ok"}, 200
# ══════════════════════════════════════════════════════
# GALLERY
# ══════════════════════════════════════════════════════

@app.route("/gallery")
def gallery():
    try:
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT id, event_time FROM camera_events ORDER BY id DESC")
            rows = cursor.fetchall()
    except Exception as e:
        print(f"Gallery error: {e}")
        rows = []
    return render_template("gallery.html", photos=rows)

@app.route("/photo/<int:photo_id>")
def photo(photo_id):
    try:
        with get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT image FROM camera_events WHERE id = %s", (photo_id,))
            row = cursor.fetchone()
        if row:
            return row[0], 200, {"Content-Type": "image/jpeg"}
    except Exception as e:
        print(f"Photo fetch error: {e}")
    return "Not Found", 404


# ══════════════════════════════════════════════════════
# LDR SENSOR
# ══════════════════════════════════════════════════════

@app.route("/ldr_sensor", methods=["GET", "POST"])
def ldr_sensor():
    global latest_ldr

    if request.method == "POST":
        data = request.json
        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_id = data.get("device_id", "fanelo_esp")
        readings  = data.get("readings", {})
        value     = readings.get("value", 0)
        event     = readings.get("event", "dark")

        latest_ldr.update({
            "device_id": device_id,
            "value":     value,
            "event":     event,
            "time":      datetime.now().strftime("%H:%M:%S")
        })

        try:
            with get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute("""
                    INSERT INTO sensor_readings_log
                        (timestamp, reading_value, device_id)
                    VALUES (%s, %s,
                        (SELECT device_id FROM device_info
                         WHERE device_name = %s LIMIT 1))
                """, (datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                      str(value), device_id))
                conn.commit()
        except Exception as e:
            print(f"LDR DB error: {e}")

        return {"status": "ok"}, 200

    rows         = get_readings("fanelo_esp", 20)
    chart_labels = [str(r["timestamp"])[-8:] for r in rows]
    chart_values = [int(r["reading_value"]) for r in rows]

    return render_template("ldr.html",
                           ldr=latest_ldr,
                           history=rows,
                           chart_labels=chart_labels,
                           chart_values=chart_values)


# ══════════════════════════════════════════════════════
# ULTRASONIC SENSOR
# ══════════════════════════════════════════════════════

@app.route("/ultson_sensor", methods=["GET", "POST"])
def ultson_sensor():
    global latest_ultrasonic
    device_name = "bridgette_esp"

    if request.method == "POST":
        data = request.json
        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_name = data.get("device_id", "bridgette_esp")
        readings    = data.get("readings", {})
        value       = readings.get("value", 0)
        event       = readings.get("event", "")

        latest_ultrasonic.update({
            "device_id":   device_name,
            "distance_cm": value,
            "event":       event,
            "time":        datetime.now().strftime("%H:%M:%S")
        })

        try:
            with get_connection() as conn:
                cursor = conn.cursor(dictionary=True)
                cursor.execute("""
                    SELECT device_id FROM device_info
                    WHERE device_name = %s LIMIT 1
                """, (device_name,))
                result = cursor.fetchone()
                if not result:
                    return {"status": "error", "message": "Device not found"}, 404
                cursor.execute("""
                    INSERT INTO sensor_readings_log
                        (timestamp, reading_value, device_id)
                    VALUES (%s, %s, %s)
                """, (datetime.now(), str(value), result["device_id"]))
                conn.commit()
        except Exception as e:
            print(f"Ultrasonic DB error: {e}")

        return {"status": "ok"}, 200

    rows   = get_readings(device_name, 20)
    latest = rows[-1] if rows else None
    chart_labels = [str(r["timestamp"])[-8:] for r in rows]
    chart_values = [float(r["reading_value"]) for r in rows]

    return render_template("ultson.html",
                           ultrasonic=latest_ultrasonic,
                           latest=latest,
                           history=list(reversed(rows)),
                           chart_labels=chart_labels,
                           chart_values=chart_values)


# ══════════════════════════════════════════════════════
# DHT22 SENSOR (FIXED VERSION)
# ══════════════════════════════════════════════════════

latest_dht22 = {
    "device_id": "",
    "temperature": 0,
    "humidity": 0,
    "fan_status": "",
    "time": ""
}

@app.route("/dht22_sensor", methods=["GET", "POST"])
def dht22_sensor():

    global latest_dht22
    device_name = "muzi_esp"

    # ==================================================
    # POST (ESP32 sends data)
    # ==================================================
    if request.method == "POST":

        data = request.json
        if not data:
            return {"status": "error", "message": "No JSON received"}, 400

        device_name = data.get("device_id", "muzi_esp")
        readings = data.get("readings", [])

        if len(readings) < 2:
            return {"status": "error", "message": "Expected 2 readings"}, 400

        temp_value = readings[0].get("value", 0)
        hum_value  = readings[1].get("value", 0)
        fan_status = readings[0].get("event", "fan_off")

        latest_dht22.update({
            "device_id": device_name,
            "temperature": temp_value,
            "humidity": hum_value,
            "fan_status": fan_status,
            "time": datetime.now().strftime("%H:%M:%S")
        })

        # SAVE TO DB
        try:
            with get_connection() as conn:
                cursor = conn.cursor(dictionary=True)

                cursor.execute("""
                    SELECT device_id FROM device_info
                    WHERE device_name = %s LIMIT 1
                """, (f"{device_name}_temp",))
                temp_dev = cursor.fetchone()

                cursor.execute("""
                    SELECT device_id FROM device_info
                    WHERE device_name = %s LIMIT 1
                """, (f"{device_name}_humidity",))
                hum_dev = cursor.fetchone()

                if temp_dev:
                    cursor.execute("""
                        INSERT INTO sensor_readings_log
                        (timestamp, reading_value, device_id)
                        VALUES (%s, %s, %s)
                    """, (datetime.now(), str(temp_value), temp_dev["device_id"]))

                if hum_dev:
                    cursor.execute("""
                        INSERT INTO sensor_readings_log
                        (timestamp, reading_value, device_id)
                        VALUES (%s, %s, %s)
                    """, (datetime.now(), str(hum_value), hum_dev["device_id"]))

                conn.commit()

        except Exception as e:
            print("DHT22 DB error:", e)

        return {"status": "ok"}, 200

    # ==================================================
    # GET (Dashboard)
    # ==================================================
    temp_rows = get_readings("muzi_esp_temp", 20)
    hum_rows  = get_readings("muzi_esp_humidity", 20)

    chart_labels = [str(r["timestamp"])[-8:] for r in temp_rows]
    chart_temp = [float(r["reading_value"]) for r in temp_rows]
    chart_humidity = [float(r["reading_value"]) for r in hum_rows]

    # IMPORTANT FIX: zip done in Python
    history = list(zip(temp_rows, hum_rows))
    history = list(reversed(history))

    return render_template(
        "dht.html",
        dht22=latest_dht22,
        latest_temp=temp_rows[-1] if temp_rows else None,
        latest_hum=hum_rows[-1] if hum_rows else None,
        history=history,
        chart_labels=chart_labels,
        chart_temp=chart_temp,
        chart_humidity=chart_humidity
    )

# ══════════════════════════════════════════════════════
# RUN
# ══════════════════════════════════════════════════════
if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5000)
