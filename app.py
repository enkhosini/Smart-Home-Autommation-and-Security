from flask import Flask, render_template, redirect, url_for, request
import json
from datetime import datetime, timezone
import pandas as pd
import matplotlib.pyplot as plt

app = Flask(__name__)



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

#all the following app routes will be for the different sensors so that they can send all of their data to the webserver, and the back end infra
#will be the point that will make decision that involve multiple sensors triggering eachother
#Handled by Sobonga and Maambele

"""
Basic Data flow:
1. ESP/sensor sends data to the webserver
2. Server then formats the information to a clear measurement[cleaning]
3. Then the dta is logged into the sql database for recording purposes
4. Then after decisions are made with the data that has been captured [like turning on another sensor or sending a certain signal to that esp or even something in the server]
5. the app route must end with an informative exit code

"""

@app.route("/pir_sensor", methods=["POST"])
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
        "alert_code": 0   # 0 = no issues, 1 = issues present
        },
    "timestamp": "2026-04-02T14:32:10Z"
    }
    """

    data = request.json

    device_id = data.get("device_id")
    readings = data.get("readings", {})

    event_type = readings.get("type")
    value = readings.get("value")

    print(f"{device_id} | {event_type}: {value}")
    #print(data)
    return {"status": "ok"}, 200

@app.route("/cam_module", methods=["GET", "POST"])
def cam_module():
    return 0

@app.route("/ldr_sensor", methods=["GET", "POST"])
def ldr_sensor():
    """
    JSON:
    device_id: <bridgette>,
    readings:{
        type:light_measure,
        value:200
    }
    """
    data = request.json

    device_id = data.get("device_id")
    readings = data.get("readings", {})

    event_type = readings.get("type")
    value = readings.get("value")

    print(f"{device_id} | {event_type}: {value}")

    return {"status": "ok"}, 200

@app.route("/ultson_sensor", methods=["GET", "POST"])
def ultson_sensor():
    return 0

@app.route("/dht22_sensor", methods=["GET", "POST"])
def dht22_sensor():
    return 0

if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5000)