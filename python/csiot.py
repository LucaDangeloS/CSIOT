#!/bin/python3
import time
import json
import paho.mqtt.client as mqtt
import board
import neopixel

LEDS_COUNT = 40
LED_PIN = board.D21

pixels = neopixel.NeoPixel(LED_PIN, LEDS_COUNT, auto_write=False)

# MQTT Configuration
MQTT_BROKER = "..."
MQTT_PORT = 8884
MQTT_USER = "..."
MQTT_PASSWORD = "..."
MQTT_TOPIC = "..."
CA_CERT_PATH = "ca.crt"

# LED Operation Modes
NOP, BLINK, TRAILING, OFF, ON, COLOR, ARRAY = range(7)

# Global Variables
COLORS = []
currentColorIdx = 0
MODE = OFF
PREVMODE = OFF
DELAY = 500
UPDATINGOP = False

# Convert HEX to RGB
def hex_to_color(hex_str):
    hex_str = hex_str.lstrip("#")
    rgb = tuple(int(hex_str[i:i+2], 16) for i in (0, 2, 4))
    return rgb

# Set LED color
def color_flash(color):
    pixels.fill(color)
    pixels.show()

# Wipe effect
def color_wipe(color, delay_time):
    for i in range(LEDS_COUNT):
        pixels[i] = color
        pixels.show()
        time.sleep(delay_time / 1000)

# Set modes
def set_blinking(delay_time):
    global MODE, DELAY, UPDATINGOP
    UPDATINGOP = True
    print("Setting blinking")
    MODE = BLINK
    DELAY = delay_time
    UPDATINGOP = False

def set_trailing(delay_time):
    global MODE, DELAY, UPDATINGOP
    UPDATINGOP = True
    print("Setting trailing")
    MODE = TRAILING
    DELAY = delay_time
    UPDATINGOP = False

def set_off():
    global MODE, PREVMODE, UPDATINGOP
    UPDATINGOP = True
    print("Setting Off")
    PREVMODE = MODE
    MODE = OFF
    color_flash((0, 0, 0))  # Turn off LEDs
    UPDATINGOP = False

# Process LED animations
def process_strip():
    global currentColorIdx, MODE

    if UPDATINGOP or not COLORS:
        return

    if MODE == BLINK:
        color_flash(COLORS[currentColorIdx])
        time.sleep(DELAY / 1000)

    elif MODE == TRAILING:
        color_wipe(COLORS[currentColorIdx], DELAY)

    if len(COLORS) > 1:
        currentColorIdx = (currentColorIdx + 1) % len(COLORS)

# Parse operation
def parse_op(op):
    return {
        "blink": BLINK,
        "trailing": TRAILING,
        "off": OFF,
        "color": COLOR,
        "array": ARRAY,
        "on": ON
    }.get(op, NOP)

# MQTT
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT Broker!")
        client.subscribe(MQTT_TOPIC)
    else:
        print(f"Failed to connect, return code {rc}")

def on_message(client, userdata, msg):
    global COLORS, MODE, PREVMODE, UPDATINGOP, DELAY

    print(f"Message received [{msg.topic}]: {msg.payload.decode()}")
    
    try:
        data = json.loads(msg.payload.decode())
    except json.JSONDecodeError:
        print("Invalid JSON format")
        return

    op = parse_op(data.get("op", ""))
    print(f"Operation: {op}")

    if op == BLINK:
        set_blinking(data["data"])

    elif op == TRAILING:
        set_trailing(data["data"])

    elif op == OFF:
        set_off()

    elif op == ON:
        if MODE == OFF:
            MODE = PREVMODE
        else:
            return
        if MODE == BLINK:
            set_blinking(DELAY)
        elif MODE == TRAILING:
            set_trailing(DELAY)

    elif op == COLOR:
        UPDATINGOP = True
        color = hex_to_color(data["data"])
        COLORS = [color, (0, 0, 0)]
        UPDATINGOP = False

    elif op == ARRAY:
        UPDATINGOP = True
        COLORS = [hex_to_color(c) for c in data["data"]]
        UPDATINGOP = False

client = mqtt.Client()
client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
client.tls_set(ca_certs=CA_CERT_PATH)
client.tls_insecure_set(True)
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER, MQTT_PORT, 60)

# Main Loop
client.loop_start()
while True:
    process_strip()
    time.sleep(0.1)
