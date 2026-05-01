from flask import Flask, render_template, redirect, url_for, request, render_template_string
import mysql.connector as connector
from datetime import datetime, timezone
import pandas as pd
import matplotlib.pyplot as plt
import os

app = Flask(__name__)
#this is the place i intend for the database stuff to worked on in
def get_connection():
    return connector.connect(
        host = "localhost",
        port = 3306,
        user = "root",
        password = "labadmin",
        database = "ga_db"
    )

def get_utc_iso_timestamp():
    return datetime.now(\
        timezone.utcoffset(+2)).\
        isoformat().\
        replace("+00:00", "Z")

#These routes will be for the html pages that will need to be served to the admin/user of the system
#to be crafted by the front end engineers, Fanelo and Bridgette
@app.route("/", methods=["GET", "POST"])
def home():
    return render_template("index.html")

@app.route("/signup", methods=["GET", "POST"])
def signup():
    if request.method == "POST":
        data = request.form
        acc_email = data.get("email")
        acc_password = data.get("password")

        with get_connection() as conn:
            cursor = conn.cursor()
            # 1. Check if user already exists
            query = "SELECT * FROM users WHERE email = %s"
            cursor.execute(query, (acc_email,))
            result = cursor.fetchone()

            if result:
                return render_template("signup.html", msg="User already exists")

            # 3. Insert new user
            insert_query = "INSERT INTO users (email, passwrd) VALUES (%s, %s)"
            cursor.execute(insert_query, (acc_email, acc_password))
            conn.commit()

            print("User created successfully!")
            return redirect("/login")
    return render_template("signup.html")

@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        data = request.form

        acc_email = data.get("email")
        acc_password = data.get("password")
        print(data)

        # now to do pass validation
        with get_connection() as conn:
            cursor = conn.cursor()
            query = "SELECT passwrd FROM users WHERE email = %s"
            cursor.execute(query, (acc_email,))

            result = cursor.fetchone()
            print(result)

            if not result:
                print("User not found")
                # output USER NOT FOUND ON THE LOGIN SCREEN
                return render_template("login.html", msg = "User not found")
            stored_password = result[0]
            
            if acc_password == stored_password:
                print("Login successful!")
                match  acc_email:
                    case "240254260@edu.vut.ac.za":
                        # pir
                        return redirect("/pir_sensor")
                    case "218541309@edu.vut.ac.za":
                        # ldr
                        return redirect("/ldr_sensor")
                    case "224303635@edu.vut.ac.za":
                        # ultra
                        return redirect("/ultson_sensor")
                    case "240716574@edu.vut.ac.za":
                        # cam
                        return redirect("/camera")
                    case "221569766@edu.vut.ac.za":
                        # dht22
                        return redirect("/dht22_sensor")
                    case "admin@edu.vut.ac.za":
                        # dht22
                        return redirect("/admin")
            else:
                print("Invalid password")
                return redirect(url_for("/"))

    return render_template("login.html")

@app.route("/admin", methods=["GET", "POST"])
def admin():
    # sensor_types = ["LDR", "PIR", "TEMP", "HUMIDITY", "ULTRASONIC"]

    # disply all the logs and graphs for the sensors
    with get_connection() as conn:
        cursor = conn.cursor()
        pass

    return render_template("admin.html", sensors = 0)

@app.route("/about", methods=["GET", "POST"])
def about():
    return render_template("about")

@app.route("/team", methods=["GET", "POST"])
def team():
    return render_template("team.html")

@app.route("/dht", methods=["GET", "POST"])
def dht():
    return render_template("dht.html")

#all the following app routes will be for the different sensors so that they can send all of their data to the webserver, and the back end infra
#will be the point that will make decision that involve multiple sensors triggering eachother
#Handled by Sobonga and Maambele

"""
Basic Data flow:
1. ESP/sensor sends data to the webserver
2. Server then formats the information to a clear measurement[cleaning]
3. Then the data is logged into the sql database for recording purposes
4. Then after decisions are made with the data that has been captured [like turning on another sensor or sending a certain signal to that esp or even something in the server]
5. the app route must end with an informative exit code
"""

STREAM_DIR = "stream"
PHOTO_DIR = "photos/"
os.makedirs(PHOTO_DIR, exist_ok=True)
os.makedirs(STREAM_DIR, exist_ok=True)



@app.route("/pir_sensor", methods=["POST", "GET"])
def pir_sensor():
    #turn the data into a json
    """
    Json example stucture:
    {
    "device_id": "<members_name>_esp", # example: maambele_esp, fanelo_esp, bridggete_esp, muzi_esp, sobonga_esp
    "readings":
        {
        "type": "motion", # motion, light, temperature, humidity, distance [cam not included]
        "value": 19.598,  # motion will be a boolean 1 and 0
        "event": <event name>
        }
    }
    """

    if request.method == "POST":
        data = request.json

        device_id = data.get("device_id")
        readings = data.get("readings", {})

        event_type = readings.get("type")
        value = readings.get("value")
        is_armed = readings.get("isArmed")

        print(f"{device_id} | {event_type}: {value} | armed: {is_armed}")

        with get_connection() as conn:
            cursor = conn.cursor
            cursor.execute("")

        #print(data)
        return {"status": "ok"}, 200
    else:
        return render_template("pir.html")

# LDR SENSOR 
# =====================================================

# stores latest values
latest_ldr = {
    "device_id": "",
    "value": 0,
    "event": "",
    "time": ""
}

@app.route("/ldr_sensor", methods=["GET", "POST"])
def ldr_sensor():

    global latest_ldr

    if request.method == "POST":

        data = request.json

        device_id = data.get("device_id")
        readings = data.get("readings", {})

        event_type = readings.get("type")
        value = readings.get("value")
        event = readings.get("event")

        current_time = datetime.now().strftime("%H:%M:%S")

        latest_ldr["device_id"] = device_id
        latest_ldr["value"] = value
        latest_ldr["event"] = event
        latest_ldr["time"] = current_time

        print("================================")
        print("LDR SENSOR DATA RECEIVED")
        print("Device ID :", device_id)
        print("Type      :", event_type)
        print("Value     :", value)
        print("Event     :", event)
        print("Time      :", current_time)
        print("================================")

        return {"status": "ok"}, 200



@app.route("/ultson_sensor", methods=["GET", "POST"])
def ultson_sensor():
    """
    JSON:
    device_id: <fanelo_esp>,
    readings:{
        type:"distance" (),
        value:30,
        event: "door_open" or "door_closed"
    }
    """
    data = request.json

    device_id = data.get("device_id")
    readings = data.get("readings", {})

    event_type = readings.get("type")
    value = readings.get("value")

    print(f"{device_id} | {event_type}: {value}")

    return {"status": "ok"}, 200

@app.route("/dht22_sensor", methods=["GET", "POST"])
def dht22_sensor():
    """
    JSON:
    {
    device_id: <muzi_esp>,
    readings:{[
        {type:temperature_reading,
        value:25
        event: <fan_on>, <fan_off>},

        {type:humidity_reading, 
        value:}
    ]}
    }
    """
    data = request.json

    device_id = data.get("device_id")
    readings = data.get("readings", [])

    #temperature reading
    event_type = readings[0].get("type")
    value = readings[0].get("value")

    #humidity reading
    event_type = readings[1].get("type")
    value = readings[1].get("value")

    print(f"{device_id} | {event_type}: {value}")

    return {"status": "ok"}, 200

if __name__ == "__main__":
   app.run(debug=True, host="10.10.10.1", port=5000)

# if __name__ == "__main__":
#    app.run(debug=True, host="0.0.0.0", port=5000)
