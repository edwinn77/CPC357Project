## BusWatch – IoT-Driven Real-Time Bus Occupancy & Route Management

BusWatch is an end-to-end IoT pipeline that:
- **Counts passengers in real time** using 2× IR sensors (bidirectional entry/exit).
- **Tracks route + current stop** using 2× push buttons (route switch + station switch).
- **Pushes live updates to Google Cloud** (Cloud Run → Pub/Sub → Dataflow → BigQuery).
- **Visualizes occupancy trends** in Looker Studio (dashboard).

### Live dashboard (Looker Studio)
- **Report link**: [Looker Studio dashboard](https://lookerstudio.google.com/reporting/9a2a1ad9-1292-4319-a0f5-ecb3c2560d57)

### Repository contents
- **`project.cpp`**: Arduino/ESP32 firmware (Maker Feather AIoT S3) — reads sensors/buttons and POSTs JSON to Cloud Run.
- **`main.py`**: Cloud Run HTTP service (Python `functions_framework`) — validates payload, enriches it, and publishes to Pub/Sub.

---

## System architecture (high level)

```mermaid
flowchart LR
  A[Maker Feather AIoT S3\n(IR sensors + buttons)] -->|HTTPS POST (JSON)| B[Cloud Run\nPython: functions_framework]
  B -->|Publish JSON| C[Pub/Sub Topic]
  C -->|Streaming pipeline| D[Dataflow]
  D --> E[BigQuery]
  E --> F[Looker Studio Dashboard]
  E --> G[AppSheet (optional)]
```

### What data flows through the system
- **Edge device → Cloud Run (HTTP)**: passenger count + event + route + stop
- **Cloud Run → Pub/Sub**: adds timestamp, capacity, and percentage before publishing
- **Pub/Sub → BigQuery (via Dataflow)**: stores events for real-time + historical analytics

---

## Hardware documentation

### Hardware required
- **Maker Feather AIoT S3 (ESP32-S3)** × 1
- **Infrared (IR) sensor module** × 2 (Entrance + Exit)
- **Push button** × 2 (Station + Route)
- **Resistors + breadboard + jumper wires**
- **USB power**

### Pin mapping (matches current `project.cpp`)
| Component | Purpose | Pin in code |
| --- | --- | --- |
| IR sensor (Entrance) | Passenger entered | `irEntrance = 42` |
| IR sensor (Exit) | Passenger exited | `irExit = 4` |
| Button 1 | Next station/stop | `btnStation = 39` |
| Button 2 | Next route (only when bus empty) | `btnRoute = 38` |

> If your wiring uses different pins (or you swapped Entrance/Exit sensors), update the pin constants in `project.cpp`.

### Route + stop list (current firmware)
- **Route A**: USM Gate → Subway → Queensbay → Sungai Nibong
- **Route B**: Penang Hill → Air Itam → Kek Lok Si
- **Route C**: Gurney Drive → Komtar → Jetty → Batu Feringghi

To customize routes/stops, edit these arrays in `project.cpp`:
- `routes[]`
- `stations[][]`
- `stationCounts[]`

---

## Firmware (Arduino / ESP32) setup

### 1) Edit the required constants (IMPORTANT)
Before uploading, you must update these in **`project.cpp`**:
- **WiFi SSID/password** (lines ~5–6)
- **Cloud endpoint URL** (line ~9)

Example (you must replace with your own values):

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* cloudEndpoint = "https://YOUR_CLOUD_RUN_URL";
```

### 2) Build + upload
- **Board support**: install the ESP32 board package in Arduino IDE (ESP32-S3).
- **Libraries**: `WiFi.h` and `HTTPClient.h` come with the ESP32 Arduino core.
- **Upload**: select the correct board/port and upload.

### 3) Runtime behavior (what you should expect)
- **IR entrance triggered** → increments `passengerCount` → sends `"Passenger Entered"`
- **IR exit triggered** → decrements `passengerCount` (min 0) → sends `"Passenger Exited"`
- **Station button** → cycles through stops for the current route
- **Route button** → switches route **only if** `passengerCount == 0` (safety rule)

---

## Cloud Run backend (`main.py`) setup

### What `main.py` does
- Exposes an HTTP endpoint: **`bus_logger(request)`**
- Validates input JSON includes **`passenger_count`**
- Looks up **authorized capacity** from BigQuery (per route)
- Adds Malaysia timestamp (UTC+8) unless `manual_timestamp` is provided
- Publishes the final payload to **Pub/Sub**

### 1) Edit the required constants (IMPORTANT)
Before deploying, you must update these in **`main.py`**:
- **`PROJECT_ID`** (line ~10)
- **`TOPIC_NAME`** (line ~11)

```python
PROJECT_ID = "your-gcp-project-id"
TOPIC_NAME = "your-pubsub-topic-name"
```

> If your BigQuery dataset/table names are different, also update the table path inside `get_authorized_limit()` (it currently queries `bus_data_dataset.busnumber`).

### 2) Create required GCP resources
- **APIs**: Cloud Run, Pub/Sub, BigQuery, Dataflow
- **Pub/Sub**:
  - Topic: `TOPIC_NAME` (example: `bus-occupancy-topic`)
- **BigQuery**:
  - Dataset: `bus_data_dataset` (or your own)
  - Lookup table: `busnumber` with at least:
    - `Route` (STRING) — e.g. `Route A`
    - `TotalBuses` (INT64) — used to compute `authorized_capacity = TotalBuses * 10`
  - Events table (target of Dataflow → BigQuery streaming) with columns matching the published payload:
    - `timestamp` (STRING or DATETIME)
    - `passenger_count` (INT64)
    - `event_type` (STRING)
    - `authorized_capacity` (INT64)
    - `percentage_capacity` (FLOAT64)
    - `route` (STRING)
    - `location` (STRING)

### 3) IAM permissions (common minimum)
For the **Cloud Run service account**:
- **Pub/Sub Publisher** on your topic
- **BigQuery Job User** (to run queries)
- **BigQuery Data Viewer** (on the dataset containing `busnumber`)

For the **Dataflow service account** (if using Dataflow):
- **Pub/Sub Subscriber**
- **BigQuery Data Editor** (on the target dataset/table)

### 4) Deploy to Cloud Run
How you deploy depends on your preferred workflow (Console UI vs `gcloud` vs Docker).

At minimum, ensure:
- Your service exposes the HTTP function `bus_logger`
- Your Cloud Run URL matches `cloudEndpoint` in `project.cpp`
- If you want the device to call it directly, configure the service to **allow unauthenticated access** (prototype) or implement a device auth mechanism (production).

### Local test (recommended before deploying)
Install dependencies and run the HTTP service locally:

```bash
pip install functions-framework google-cloud-pubsub google-cloud-bigquery
functions-framework --target=bus_logger --port=8080
```

Then send a test request:

```bash
curl -X POST http://localhost:8080 \
  -H "Content-Type: application/json" \
  -d "{\"passenger_count\":1,\"event\":\"Passenger Entered\",\"route\":\"Route A\",\"location\":\"USM Gate\"}"
```

### Example deploy with Docker (Cloud Run)
One common approach is to containerize this service with `functions-framework`, then deploy to Cloud Run.

High-level checklist:
- Build a container image that runs: `functions-framework --target=bus_logger --port=$PORT`
- Deploy to Cloud Run and set the service to accept the device's requests
- Update `cloudEndpoint` in `project.cpp` to the new Cloud Run URL

---

## API contract (device → Cloud Run)

### Request JSON (sent by `project.cpp`)
```json
{
  "passenger_count": 8,
  "event": "Passenger Entered",
  "route": "Route B",
  "location": "Air Itam"
}
```

### Published payload (Cloud Run → Pub/Sub)
```json
{
  "timestamp": "2026-01-11 14:05:12",
  "passenger_count": 8,
  "event_type": "Passenger Entered",
  "authorized_capacity": 40,
  "percentage_capacity": 20.0,
  "route": "Route B",
  "location": "Air Itam"
}
```

---

## Troubleshooting checklist

- **No data in BigQuery**:
  - Confirm Cloud Run is receiving requests (check Cloud Run logs).
  - Confirm Pub/Sub topic name matches `TOPIC_NAME`.
  - Confirm Dataflow job is running and writing into the correct BigQuery table.
- **Capacity always equals fallback (101)**:
  - Check `bus_data_dataset.busnumber` exists and has matching `Route` values (e.g. `Route A`).
  - Confirm Cloud Run service account has BigQuery permissions.
- **Arduino can’t reach Cloud Run**:
  - Confirm `cloudEndpoint` is the correct Cloud Run HTTPS URL.
  - Confirm Cloud Run allows unauthenticated access (or your device is authenticated).
  - Confirm WiFi SSID/password are correct.

---

## Links
- **Looker Studio dashboard**: [BusWatch report](https://lookerstudio.google.com/reporting/9a2a1ad9-1292-4319-a0f5-ecb3c2560d57)
- **Project repository**: [GitHub – edwinn77/CPC357Project](https://github.com/edwinn77/CPC357Project)


