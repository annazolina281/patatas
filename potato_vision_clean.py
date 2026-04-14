"""
Potato Rot Detector - Computer Vision Module
Integrates with ESP32 biosensor system via Serial.

KEY RULE:
  Potato NOT detected  ->  all sensor values = NULL, verdict = INVALID
  Potato detected      ->  show live ESP32 sensor readings + verdict
"""

import cv2
import numpy as np
import argparse
import threading
import time
import re
import serial
import serial.tools.list_ports
from datetime import datetime
from flask import Flask, jsonify, Response

# ================================================================
#  ANSI COLOURS
# ================================================================
class C:
    RST  = "\033[0m"
    BOLD = "\033[1m"
    DIM  = "\033[2m"
    RED    = "\033[31m";  GREEN  = "\033[32m"
    YELLOW = "\033[33m";  WHITE  = "\033[37m"
    BRED   = "\033[91m";  BGRN   = "\033[92m"
    BYEL   = "\033[93m";  BWHT   = "\033[97m"
    BGRED  = "\033[41m";  BGGRN  = "\033[42m"
    BGYEL  = "\033[43m";  BGCYN  = "\033[46m"
    BGDARK = "\033[100m"

def _vis(s: str) -> int:
    """Visible (non-ANSI) length of string."""
    return len(re.sub(r'\033\[[0-9;]*m', '', s))

# ================================================================
#  TABLE HELPERS  (inner width = TW chars)
# ================================================================
TW = 64

def _fill(text: str, width: int) -> str:
    """Pad text to exactly `width` visible chars."""
    diff = width - _vis(text)
    return text + (" " * max(diff, 0))

def div(l="├", m="─", r="┤") -> str:
    return l + m * TW + r

def blank_row() -> str:
    return "│" + " " * TW + "│"

def label_val(label: str, value: str, lw: int = 22) -> str:
    inner = "  " + _fill(label, lw) + value
    return "│" + _fill(inner, TW) + "│"

def center_row(text: str) -> str:
    vis  = _vis(text)
    lpad = (TW - vis) // 2
    rpad = TW - vis - lpad
    return "│" + " " * lpad + text + " " * rpad + "│"

# ── value formatters ─────────────────────────────────────────────
def null_str() -> str:
    return f"{C.DIM}NULL{C.RST}"

def val_str(v, unit: str) -> str:
    if v is None:
        return null_str()
    return f"{C.BWHT}{v}{C.RST}{C.DIM} {unit}{C.RST}"

# ── verdict badge ────────────────────────────────────────────────
_VBG = {
    "FRESH":   f"{C.BGGRN}\033[30m",
    "MONITOR": f"{C.BGCYN}\033[30m",
    "SUSPECT": f"{C.BGYEL}\033[30m",
    "ROTTEN":  f"{C.BGRED}{C.BWHT}",
    "INVALID": f"{C.BGDARK}{C.WHITE}",
}
def verdict_badge(v: str) -> str:
    bg = _VBG.get(v, _VBG["INVALID"])
    return f"{C.BOLD}{bg} {v} {C.RST}"

# ================================================================
#  MAIN TABLE PRINTER
# ================================================================
def print_table(potato: bool, confidence: float, sensor,
                fps: float, frame_no: int,
                esp_ok: bool, esp_port):

    ts      = datetime.now().strftime("%Y-%m-%d  %H:%M:%S")
    verdict = _verdict_from_sensor(sensor) if (potato and sensor) else "INVALID"

    if potato:
        cam_txt = f"{C.BGRN}✔  POTATO DETECTED     conf: {confidence:.0%}{C.RST}"
    else:
        cam_txt = f"{C.DIM}✘  NO POTATO  —  sensors: NULL / INVALID{C.RST}"

    if esp_ok:
        esp_txt = f"{C.BGRN}● ONLINE   {esp_port or ''}{C.RST}"
    else:
        esp_txt = f"{C.BRED}○ OFFLINE{C.RST}"

    out = []
    out.append("\n" + div("┌","─","┐"))
    out.append(center_row(f"{C.BOLD}{C.BWHT}POTATO ROT DETECTOR  ─  {ts}{C.RST}"))
    out.append(div())

    # Camera / FPS
    out.append(label_val(f"{C.BOLD}CAMERA{C.RST}", cam_txt, lw=10))
    out.append(label_val(f"{C.DIM}FPS{C.RST}",
                         f"{C.BWHT}{fps:5.1f}{C.RST}  {C.DIM}frame #{frame_no}{C.RST}", lw=10))
    out.append(div())

    # ESP32
    out.append(label_val(f"{C.BOLD}ESP32{C.RST}", esp_txt, lw=10))
    out.append(div())

    # Biosensor readings
    out.append(label_val(f"{C.BOLD}BIOSENSOR READINGS{C.RST}", "", lw=30))
    out.append(blank_row())

    def srow(lbl, key, unit):
        v = sensor.get(key) if sensor else None
        return label_val(f"{C.DIM}{lbl}{C.RST}", val_str(v, unit), lw=20)

    out.append(srow("Temperature",   "temp",     "°C"))
    out.append(srow("Humidity",      "humidity", "%"))
    out.append(srow("MQ-3  (EtOH)",  "mq3",      "ppm"))
    out.append(srow("MQ-135  (AQ)",  "mq135",    "ppm"))
    out.append(blank_row())
    out.append(div())

    # Verdict
    out.append(label_val(f"{C.BOLD}VERDICT{C.RST}", verdict_badge(verdict), lw=10))
    out.append(div("└","─","┘"))

    print("\n".join(out), flush=True)

# ================================================================
#  CONFIG
# ================================================================
# HSV ranges cover tan / beige / golden-brown / dark-brown potatoes
POTATO_HSV_LOWER  = np.array([5,  20,  40])    # wider: low sat/val floor
POTATO_HSV_UPPER  = np.array([35, 255, 240])   # up to warm orange-brown
# Second range: reddish-brown / dirty potatoes
POTATO_HSV_LOWER2 = np.array([0,  20,  40])
POTATO_HSV_UPPER2 = np.array([8,  255, 220])

POTATO_MIN_AREA   = 1500   # smaller — catches potato at any distance
POTATO_CONF_MIN   = 0.20   # lower — easier to trigger
FLASK_PORT       = 5000
SERIAL_BAUD      = 115200

# ================================================================
#  STATE
# ================================================================
app = Flask(__name__)

state = {
    "potato_detected": False,
    "confidence":      0.0,
    "bbox":            None,
    "fps":             0.0,
    "frame_count":     0,
    "esp32_connected": False,
    "esp32_port":      None,
    "last_sensor":     None,   # always updated from serial; gated on read
}

frame_lock  = threading.Lock()
latest_jpeg = None

def active_sensor():
    """Only expose sensor data when a potato is actually detected."""
    if state["potato_detected"]:
        return state["last_sensor"]
    return None   # <-- hard NULL gate: no potato = no readings

# ================================================================
#  POTATO DETECTION (HSV + shape)
# ================================================================
def detect_potato(frame):
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # Combine two HSV ranges: golden-tan + reddish-brown potatoes
    mask1 = cv2.inRange(hsv, POTATO_HSV_LOWER,  POTATO_HSV_UPPER)
    mask2 = cv2.inRange(hsv, POTATO_HSV_LOWER2, POTATO_HSV_UPPER2)
    mask  = cv2.bitwise_or(mask1, mask2)

    # Clean up noise
    k    = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN,  k, iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k, iterations=2)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    best_area, best_bbox = 0, None

    for cnt in contours:
        area = cv2.contourArea(cnt)
        if area < POTATO_MIN_AREA:
            continue
        h_area = cv2.contourArea(cv2.convexHull(cnt))
        # Relaxed solidity: 0.45 instead of 0.60 (potatoes aren't perfect circles)
        if h_area == 0 or area / h_area < 0.45:
            continue
        if area > best_area:
            best_area, best_bbox = area, cv2.boundingRect(cnt)

    if best_bbox is None:
        return False, 0.0, None, frame.copy()

    farea      = frame.shape[0] * frame.shape[1]
    size_score = min(best_area / (farea * 0.25), 1.0)  # lower denominator = easier score
    col_score  = cv2.countNonZero(mask) / farea
    conf       = min(size_score * 0.65 + col_score * 0.35, 1.0)
    detected   = conf >= POTATO_CONF_MIN

    debug = frame.copy()
    if detected:
        x, y, w, h = best_bbox
        cv2.rectangle(debug, (x, y), (x+w, y+h), (0, 220, 80), 2)
        cv2.putText(debug, f"POTATO  {conf:.0%}",
                    (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 220, 80), 2)
    else:
        # Draw dim outline even when below threshold (debug aid)
        if best_bbox:
            x, y, w, h = best_bbox
            cv2.rectangle(debug, (x, y), (x+w, y+h), (80, 80, 80), 1)
            cv2.putText(debug, f"? {conf:.0%}",
                        (x, y - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.50, (80, 80, 80), 1)

    return detected, conf, best_bbox if detected else None, debug

# ================================================================
#  SERIAL / ESP32
# ================================================================
def find_esp32_port():
    for p in serial.tools.list_ports.comports():
        if any(k in (p.description or "").lower()
               for k in ("cp210","ch340","ftdi","uart","esp","usb serial")):
            return p.device
    return None

def parse_sensor_line(line: str):
    if "[Sensor]" not in line:
        return None
    r = {}
    try:
        for tok in line.split(","):
            t = tok.strip()
            if "Temp="     in t: r["temp"]     = float(t.split("=")[1].split()[0])
            if "Humidity=" in t: r["humidity"] = float(t.split("=")[1].split()[0])
            if "MQ3="      in t: r["mq3"]      = float(t.split("=")[1].split()[0])
            if "MQ135="    in t: r["mq135"]    = float(t.split("=")[1].split()[0])
    except Exception:
        return None
    return r if r else None

def serial_reader(port: str):
    while True:
        try:
            ser = serial.Serial(port, SERIAL_BAUD, timeout=2)
            state["esp32_connected"] = True
            state["esp32_port"]      = port
            print(f"\n{C.BGRN}{C.BOLD}[ESP32]{C.RST} {C.BGRN}Connected on {port}{C.RST}\n")
            while True:
                raw    = ser.readline().decode("utf-8", errors="replace").strip()
                parsed = parse_sensor_line(raw)
                if parsed:
                    state["last_sensor"] = parsed
        except serial.SerialException as e:
            state["esp32_connected"] = False
            state["last_sensor"]     = None
            print(f"\n{C.BRED}{C.BOLD}[ESP32]{C.RST} {C.BRED}Disconnected — {e}{C.RST}\n")
            time.sleep(3)

# ================================================================
#  VERDICT
# ================================================================
def _verdict_from_sensor(sensor) -> str:
    if sensor is None:
        return "INVALID"
    mq3 = sensor.get("mq3", 0)
    if   mq3 > 150: return "ROTTEN"
    elif mq3 >  80: return "SUSPECT"
    elif mq3 >  30: return "MONITOR"
    else:           return "FRESH"

# ================================================================
#  FLASK API
# ================================================================
@app.route("/detect")
def api_detect():
    potato  = state["potato_detected"]
    sensor  = active_sensor()
    verdict = _verdict_from_sensor(sensor) if (potato and sensor) else "INVALID"
    return jsonify({
        "timestamp":       datetime.now().isoformat(),
        "potato_detected": potato,
        "confidence":      round(state["confidence"], 3) if potato else "NULL",
        "fps":             round(state["fps"], 1),
        "esp32_connected": state["esp32_connected"],
        "sensors": {
            "temperature": sensor["temp"]     if sensor else "NULL",
            "humidity":    sensor["humidity"] if sensor else "NULL",
            "mq3_ppm":     sensor["mq3"]      if sensor else "NULL",
            "mq135_ppm":   sensor["mq135"]    if sensor else "NULL",
        },
        "rot_verdict": verdict,
    })

@app.route("/stream")
def api_stream():
    def gen():
        while True:
            with frame_lock:
                jpg = latest_jpeg
            if jpg:
                yield b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + jpg + b"\r\n"
            time.sleep(0.033)
    return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=frame")

@app.route("/health")
def api_health():
    return jsonify({"status": "ok", "esp32": state["esp32_connected"]})

# ================================================================
#  CAMERA WINDOW OVERLAY
# ================================================================
FT  = cv2.FONT_HERSHEY_SIMPLEX
FTB = cv2.FONT_HERSHEY_DUPLEX
_CV = {
    "white":   (255,255,255), "green":  (50,220,80),
    "red":     (60,60,220),   "yellow": (30,200,220),
    "gray":    (140,140,140), "bg":     (20,20,20),
    "panel":   (30,30,30),
}
_VCV = {
    "FRESH":(50,220,80), "MONITOR":(30,200,220),
    "SUSPECT":(30,165,255), "ROTTEN":(60,60,220), "INVALID":(120,120,120),
}

def draw_overlay(frame):
    h, w = frame.shape[:2]
    potato = state["potato_detected"]
    conf   = state["confidence"]
    fps    = state["fps"]
    sensor = active_sensor()            # NULL-gated
    esp_ok = state["esp32_connected"]

    # top bar
    cv2.rectangle(frame, (0,0), (w,46), _CV["bg"], -1)
    cv2.putText(frame, "POTATO ROT DETECTOR", (12,30), FTB, 0.70, _CV["white"], 1, cv2.LINE_AA)
    cv2.putText(frame, f"FPS:{fps:.1f}", (w-90,30), FT, 0.50, _CV["gray"], 1, cv2.LINE_AA)
    dot = _CV["green"] if esp_ok else _CV["red"]
    cv2.circle(frame, (w-120,23), 6, dot, -1)
    cv2.putText(frame,
                f"ESP32 {state['esp32_port'] or ''}" if esp_ok else "ESP32 OFFLINE",
                (w-110,27), FT, 0.36, dot, 1, cv2.LINE_AA)

    # bottom banner
    if potato:
        cv2.rectangle(frame, (0,h-50), (w,h), (0,85,20), -1)
        cv2.putText(frame, f"POTATO DETECTED   CONF: {conf:.0%}",
                    (12,h-16), FTB, 0.70, _CV["green"], 1, cv2.LINE_AA)
    else:
        cv2.rectangle(frame, (0,h-50), (w,h), (28,28,28), -1)
        cv2.putText(frame, "SCANNING ...   NO POTATO   SENSORS: NULL",
                    (12,h-16), FT, 0.57, _CV["gray"], 1, cv2.LINE_AA)

    # sensor panel
    if potato and sensor:
        verdict = _verdict_from_sensor(sensor)
        vc = _VCV.get(verdict, _VCV["INVALID"])
        px,py,pw,ph = w-215, 54, 208, 185
        ov = frame.copy()
        cv2.rectangle(ov, (px,py), (px+pw,py+ph), _CV["panel"], -1)
        cv2.addWeighted(ov, 0.82, frame, 0.18, 0, frame)
        cv2.rectangle(frame, (px,py), (px+pw,py+ph), vc, 1)
        cv2.putText(frame, "BIOSENSOR READINGS", (px+8,py+18), FT, 0.40, _CV["gray"], 1)
        for i,(lbl,key,unit) in enumerate([
            ("Temp",    "temp",     "C"),
            ("Humidity","humidity", "%"),
            ("MQ-3",    "mq3",      "ppm"),
            ("MQ-135",  "mq135",    "ppm"),
        ]):
            yr = py+46+i*30
            cv2.putText(frame, lbl, (px+10,yr), FT, 0.42, _CV["gray"], 1, cv2.LINE_AA)
            cv2.putText(frame, f"{sensor.get(key,'?')} {unit}",
                        (px+100,yr), FTB, 0.46, _CV["white"], 1, cv2.LINE_AA)
        cv2.rectangle(frame,(px+6,py+ph-36),(px+pw-6,py+ph-8),vc,-1)
        cv2.putText(frame, f"VERDICT: {verdict}",
                    (px+12,py+ph-14), FTB, 0.50, _CV["bg"], 1, cv2.LINE_AA)

    elif potato and not sensor:
        px,py = w-215,54
        cv2.rectangle(frame,(px,py),(px+208,py+64),_CV["panel"],-1)
        cv2.rectangle(frame,(px,py),(px+208,py+64),_CV["yellow"],1)
        cv2.putText(frame,"WAITING FOR ESP32...",(px+10,py+38),FT,0.44,_CV["yellow"],1)

    else:
        px,py = w-175,54
        cv2.rectangle(frame,(px,py),(px+168,py+82),_CV["panel"],-1)
        cv2.rectangle(frame,(px,py),(px+168,py+82),_CV["gray"],1)
        cv2.putText(frame,"SENSORS",(px+10,py+24),FT,0.40,_CV["gray"],1)
        cv2.putText(frame,"NULL",   (px+10,py+58),FTB,0.90,_CV["red"],1,cv2.LINE_AA)
        cv2.putText(frame,"no potato",(px+10,py+76),FT,0.35,_CV["gray"],1)

    return frame

# ================================================================
#  ENTRY POINT
# ================================================================
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cam",        default="0")
    parser.add_argument("--res",        default="480")
    parser.add_argument("--esp32-port", default=None)
    args = parser.parse_args()

    try:    cam_src = int(args.cam)
    except: cam_src = args.cam

    cap_h = int(args.res)
    cap_w = int(cap_h * 4 / 3)

    # Flask
    threading.Thread(
        target=lambda: app.run("0.0.0.0", FLASK_PORT, debug=False, use_reloader=False),
        daemon=True).start()

    # ESP32 serial
    esp_port = args.esp32_port or find_esp32_port()
    if esp_port:
        threading.Thread(target=serial_reader, args=(esp_port,), daemon=True).start()
    else:
        print(f"{C.BYEL}[ESP32]{C.RST} No port found — vision-only mode")

    # Camera
    cap = cv2.VideoCapture(cam_src)
    if not cap.isOpened():
        print(f"{C.BRED}[ERROR]{C.RST} Cannot open camera: {cam_src}"); return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  cap_w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, cap_h)

    print(f"\n{C.BOLD}{C.BWHT}"
          "╔══════════════════════════════════════════════════════════════╗\n"
          "║         POTATO ROT DETECTOR  ─  Vision Module               ║\n"
          f"╚══════════════════════════════════════════════════════════════╝{C.RST}")
    print(f"  {C.DIM}API {C.RST} http://localhost:{FLASK_PORT}/detect  |  /stream  |  /health")
    print(f"  {C.DIM}CAM {C.RST} source={cam_src}  res={cap_w}x{cap_h}")
    print(f"  {C.DIM}QUIT{C.RST} press Q in camera window\n")

    global latest_jpeg
    prev_t  = time.time()
    log_ctr = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            print(f"{C.BYEL}[WARN]{C.RST} Frame grab failed, retrying...")
            time.sleep(0.1); continue

        detected, conf, bbox, dbg = detect_potato(frame)
        state["potato_detected"] = detected
        state["confidence"]      = conf
        state["bbox"]            = bbox
        state["frame_count"]    += 1

        now           = time.time()
        fps           = 1.0 / max(now - prev_t, 1e-6)
        state["fps"]  = fps
        prev_t        = now

        output = draw_overlay(dbg)

        _, jpg = cv2.imencode(".jpg", output, [cv2.IMWRITE_JPEG_QUALITY, 85])
        with frame_lock:
            latest_jpeg = jpg.tobytes()

        log_ctr += 1
        if log_ctr >= 30:
            print_table(
                potato    = detected,
                confidence= conf,
                sensor    = active_sensor(),   # NULL-gated
                fps       = fps,
                frame_no  = state["frame_count"],
                esp_ok    = state["esp32_connected"],
                esp_port  = state["esp32_port"],
            )
            log_ctr = 0

        cv2.imshow("Potato Rot Detector", output)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()
    print(f"\n{C.DIM}[INFO]{C.RST} Detector stopped.")


if __name__ == "__main__":
    main()
