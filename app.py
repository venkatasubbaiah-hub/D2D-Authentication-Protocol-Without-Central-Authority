import streamlit as st
import serial
import serial.tools.list_ports
import threading
import queue
import time
from datetime import datetime
from streamlit_autorefresh import st_autorefresh

st.set_page_config(
    page_title="Secure D2D Authentication Dashboard",
    page_icon="🔐",
    layout="wide"
)

# ---------- SESSION STATE ----------
if "event_queue" not in st.session_state:
    st.session_state.event_queue = queue.Queue()
if "connections" not in st.session_state:
    st.session_state.connections = {}
if "stop_events" not in st.session_state:
    st.session_state.stop_events = {}
if "logs" not in st.session_state:
    st.session_state.logs = []
if "counts" not in st.session_state:
    st.session_state.counts = {
        "ESP32": {"RX": 0, "TX": 0},
        "STM32": {"RX": 0, "TX": 0}
    }
if "send_message" not in st.session_state:
    st.session_state.send_message = ""

def serial_reader(board, ser, stop_event, event_queue):
    while not stop_event.is_set():
        try:
            raw = ser.readline()
            if raw:
                message = raw.decode("utf-8", errors="replace").strip()
                if message:
                    event_queue.put((
                        datetime.now().strftime("%H:%M:%S"),
                        board, "RX", message
                    ))
        except Exception as exc:
            event_queue.put((
                datetime.now().strftime("%H:%M:%S"),
                board, "ERROR", str(exc)
            ))
            break

def connect_board(board, port, baud):
    if board in st.session_state.connections:
        return False, board + " is already connected."
    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.2, write_timeout=1)
        time.sleep(1.2)
        stop_event = threading.Event()
        thread = threading.Thread(
            target=serial_reader,
            args=(board, ser, stop_event, st.session_state.event_queue),
            daemon=True
        )
        st.session_state.connections[board] = ser
        st.session_state.stop_events[board] = stop_event
        thread.start()
        return True, board + " connected on " + port
    except Exception as exc:
        return False, "Could not open " + port + ": " + str(exc)

def disconnect_board(board):
    stop_event = st.session_state.stop_events.pop(board, None)
    ser = st.session_state.connections.pop(board, None)
    if stop_event:
        stop_event.set()
    if ser:
        try:
            ser.close()
        except Exception:
            pass

def send_to_board(board, message):
    ser = st.session_state.connections.get(board)
    if ser is None or not ser.is_open:
        return False, board + " is not connected."
    if not message.strip():
        return False, "Enter a message first."
    try:
        payload = (message.rstrip("\r\n") + "\n").encode("utf-8")
        ser.write(payload)
        ser.flush()
        st.session_state.counts[board]["TX"] += 1
        st.session_state.logs.append({
            "Time": datetime.now().strftime("%H:%M:%S"),
            "Board": board,
            "Direction": "TX",
            "Message": message.strip()
        })
        return True, "Message sent to " + board
    except Exception as exc:
        return False, str(exc)

# Pull received serial messages from the background reader threads.
while True:
    try:
        timestamp, board, direction, message = st.session_state.event_queue.get_nowait()
    except queue.Empty:
        break
    if direction == "RX":
        st.session_state.counts[board]["RX"] += 1
    st.session_state.logs.append({
        "Time": timestamp,
        "Board": board,
        "Direction": direction,
        "Message": message
    })

st.session_state.logs = st.session_state.logs[-1000:]

# ---------- HEADER ----------
st.title("🔐 Secure Device-to-Device Authentication")
st.caption("Live dual-COM serial dashboard | STM32 NUCLEO-F446RE ↔ ESP32")
st.info(
    "This application monitors and sends serial data on two separate USB COM ports. "
    "HMAC mutual authentication is performed by the firmware on the STM32 and ESP32, "
    "not by this dashboard."
)

# ---------- PORT SETTINGS ----------
detected_ports = sorted({p.device for p in serial.tools.list_ports.comports()})
port_options = sorted(set(detected_ports + ["COM7", "COM9"]))
baud = st.sidebar.selectbox(
    "Baud rate",
    [115200, 9600, 19200, 38400, 57600, 230400],
    index=0
)
st.sidebar.write("Detected COM ports:", ", ".join(detected_ports) if detected_ports else "None")
st.sidebar.warning(
    "Close Arduino Serial Monitor, PuTTY, and other serial terminals before connecting."
)

# ---------- CONNECTION PANEL ----------
st.subheader("Board connections")
left, right = st.columns(2)

with left:
    st.markdown("### 🟢 ESP32")
    esp_default = port_options.index("COM7") if "COM7" in port_options else 0
    esp_port = st.selectbox("ESP32 USB port", port_options, index=esp_default, key="esp32_port")
    if "ESP32" not in st.session_state.connections:
        if st.button("Connect ESP32", use_container_width=True):
            ok, msg = connect_board("ESP32", esp_port, baud)
            (st.success if ok else st.error)(msg)
    else:
        st.success("ESP32 connected")
        if st.button("Disconnect ESP32", use_container_width=True):
            disconnect_board("ESP32")
            st.rerun()

with right:
    st.markdown("### 🔵 STM32 NUCLEO-F446RE")
    stm_default = port_options.index("COM9") if "COM9" in port_options else 0
    stm_port = st.selectbox("STM32 ST-LINK VCP port", port_options, index=stm_default, key="stm32_port")
    if "STM32" not in st.session_state.connections:
        if st.button("Connect STM32", use_container_width=True):
            ok, msg = connect_board("STM32", stm_port, baud)
            (st.success if ok else st.error)(msg)
    else:
        st.success("STM32 connected")
        if st.button("Disconnect STM32", use_container_width=True):
            disconnect_board("STM32")
            st.rerun()

if st.button("🔗 Connect BOTH boards", type="primary", use_container_width=True):
    if esp_port == stm_port:
        st.error("Choose two different COM ports.")
    else:
        for board, port in (("ESP32", esp_port), ("STM32", stm_port)):
            if board not in st.session_state.connections:
                ok, msg = connect_board(board, port, baud)
                (st.success if ok else st.error)(msg)
            else:
                st.info(board + " is already connected.")

if st.button("Disconnect BOTH boards", use_container_width=True):
    disconnect_board("ESP32")
    disconnect_board("STM32")
    st.rerun()

# ---------- COUNTERS ----------
st.divider()
st.subheader("Communication statistics")
a, b, c, d = st.columns(4)
a.metric("ESP32 RX", st.session_state.counts["ESP32"]["RX"])
b.metric("ESP32 TX", st.session_state.counts["ESP32"]["TX"])
c.metric("STM32 RX", st.session_state.counts["STM32"]["RX"])
d.metric("STM32 TX", st.session_state.counts["STM32"]["TX"])

# ---------- LIVE CONSOLES ----------
st.divider()
st.subheader("Live serial monitors")
col1, col2 = st.columns(2)

for col, board in ((col1, "ESP32"), (col2, "STM32")):
    with col:
        st.markdown("#### " + board + " console")
        board_lines = [
            "[" + item["Time"] + "] " + item["Direction"] + ": " + item["Message"]
            for item in st.session_state.logs if item["Board"] == board
        ]
        st.code("\n".join(board_lines[-100:]) if board_lines else "Waiting for serial data...")

# ---------- TRANSMIT ----------
st.divider()
st.subheader("Send test message")
with st.form("send_form", clear_on_submit=True):
    destination = st.selectbox("Destination", ["ESP32", "STM32"])
    outgoing = st.text_input("Message", placeholder="HELLO_FROM_DASHBOARD")
    submitted = st.form_submit_button("Send message", use_container_width=True)
    if submitted:
        ok, msg = send_to_board(destination, outgoing)
        (st.success if ok else st.error)(msg)

# ---------- EVENT LOG ----------
st.divider()
st.subheader("Combined communication log")
if st.session_state.logs:
    st.dataframe(
        list(reversed(st.session_state.logs[-300:])),
        use_container_width=True,
        hide_index=True
    )
else:
    st.caption("No serial messages received yet.")

if st.button("Clear logs and counters"):
    st.session_state.logs.clear()
    st.session_state.counts = {
        "ESP32": {"RX": 0, "TX": 0},
        "STM32": {"RX": 0, "TX": 0}
    }
    st.rerun()

st.caption(
    "COM7: ESP32 USB Serial. COM9: normally NUCLEO ST-LINK VCP, connected to STM32 USART2 PA2/PA3. "
    "STM32 firmware must send debug text on USART2 for it to appear on COM9. "
    "Board-to-board UART1 PB6/PB7 ↔ ESP32 UART2 GPIO32/GPIO33 is separate."
)

# Automatic UI refresh to display incoming serial data.
st_autorefresh(interval=700, key="d2d_auto_refresh")
