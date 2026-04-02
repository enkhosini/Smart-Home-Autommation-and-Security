from flask import Flask, render_template, redirect, request
import pandas as pd
import matplotlib.pyplot as plt

app = Flask(__name__)

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

@app.route("/pir_sensor", methods=["GET", "POST"])
def pir_sensor():
    return 0

@app.route("/cam_module", methods=["GET", "POST"])
def cam_module():
    return 0

@app.route("/ldr_sensor", methods=["GET", "POST"])
def ldr_sensor():
    return 0

@app.route("/ultson_sensor", methods=["GET", "POST"])
def ultson_sensor():
    return 0

@app.route("/dht22_sensor", methods=["GET", "POST"])
def dht22_sensor():
    return 0

if __name__ == "__main__":
    app.run(debug=True)