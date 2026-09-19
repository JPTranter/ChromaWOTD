# Weather API Research & Comparison: Open Sources vs. BoM (Bureau of Meteorology)

This document evaluates weather data sources for ChromaWOTD's weather and alert rendering engine on the ESP32-S3.

## Comparison Matrix

| Feature / Criteria | Open-Meteo | BoM (Bureau of Meteorology) v1 API | BoM FTP / reg.bom.gov.au JSON |
| :--- | :--- | :--- | :--- |
| **Authentication / Key** | None (Free open API) | None (Public reverse-engineered endpoints) | None (Public anonymous HTTP) |
| **Payload Size** | ~350 - 500 bytes (tailored via query) | ~1.5 KB (observations), ~4 KB (daily) | ~120 KB (raw station observations) |
| **HTTPS Support** | Full HTTPS | Full HTTPS (AWS CloudFront / Akamai) | HTTP / HTTPS |
| **Location Querying** | Exact Lat/Long (`?latitude=..&longitude=..`) | Requires 6-char geohash (e.g. `r3gx2f` for Sydney) | Requires station ID (e.g. `IDN60901.94768`) |
| **Current Temperature** | Yes (`current=temperature_2m`) | Yes (`data.temp`) | Yes (`air_temp`) |
| **Conditions** | WMO weather codes (0-99 standard) | Descriptive text + icon descriptors | Raw text / manual observation codes |
| **Severe Weather Alerts** | Available via extra parameters | Dedicated `/warnings` endpoint | Separate warning feeds |
| **ESP32 Parsing Overhead** | Extremely low (small JSON filter) | Moderate (requires handling Geohash lookup) | Unusable on MCU (120 KB payload) |
| **Terms of Service & Stability** | Officially open for non-commercial & small devices | "Owned by BoM - do not copy/share" warning in header | Legacy infrastructure; subject to sudden block |

---

## Source Analysis

### 1. Open-Meteo (Recommended Global & Universal Engine)
* **Endpoint**: `https://api.open-meteo.com/v1/forecast?latitude=-33.8688&longitude=151.2093&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=2`
* **Pros**:
  * Clean, minimal payload (< 500 bytes) specifically designed for microcontrollers and embedded devices.
  * Standardized WMO weather codes (0=Clear, 1-3=Partly Cloudy, 51-67=Rain, etc.) which map directly to our condition TEXT (`cc_wmoCondition`). The display is text-only — the 4-icon vector engine was removed 2026-09-19 (LESSONS §61).
  * Worldwide coverage using national meteorological agency models (including Australian ACCESS models!).
* **Cons**:
  * Severe weather warning text requires an additional parameter or regional alert feed.

### 2. Australian Bureau of Meteorology (BoM) Official v1 API
* **Base URL**: `https://api.weather.bom.gov.au/v1/`
* **Key Endpoints**:
  * Observations: `GET /v1/locations/{geohash}/observations`
  * Daily Forecast: `GET /v1/locations/{geohash}/forecasts/daily`
  * Warnings: `GET /v1/locations/{geohash}/warnings`
* **Pros**:
  * Highly accurate, local Australian forecasts ("Morning smoke then sunny", exact rain chance percentages).
  * Native Australian severe weather warnings (`/warnings` returns localized gale, flood, fire, and storm warnings directly from the state forecast center).
* **Cons**:
  * Location addressing uses 6-character Geohashes (e.g., `r3gx2f` for Sydney Observatory Hill, `r1f985` for Melbourne).
  * Header contains strict BoM copyright notices.
  * Payloads are larger (~4-6 KB total across endpoints).

---

## Architectural Recommendation for ChromaWOTD

1. **Default / International / Fast Path**: **Open-Meteo**
   * Default engine for simplicity, universal GPS/lat-long coordinate lookup, and micro-payload size (~400 bytes).
2. **Australian Hyper-Local Mode (Optional toggle)**: **BoM v1 API**
   * If deployed in Australia and configured with a BoM geohash (or resolved from lat/long), query:
     1. `/locations/{geohash}/observations` for exact ambient temperature.
     2. `/locations/{geohash}/forecasts/daily` for condition text and rain likelihood.
     3. `/locations/{geohash}/warnings` to trigger ChromaWOTD's red alert line in the footer row.

---

## Local Configuration: Burwood East, VIC 3151 (Scoresby Observation Station)

### Geolocation & Geohash Mapping
* **Subrub / Postcode**: Burwood East, VIC 3151, Australia
* **Coordinates**: Latitude `-37.8528`, Longitude `145.1633`
* **Closest Observation Station**: Scoresby (BoM ID `086266` / `086068` nearby)
* **BoM Geohashes**:
  * **Scoresby**: `r1r293` (specifically `r1r293u` trimmed to 6 characters for the v1 API)
  * **Burwood East**: `r1r0wu` (6-char geohash)

### Tested API Calls for Scoresby / Burwood East

1. **Open-Meteo (Burwood East Coordinates)**:
   ```http
   GET https://api.open-meteo.com/v1/forecast?latitude=-37.8528&longitude=145.1633&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=Australia%2FMelbourne&forecast_days=2
   ```

2. **BoM v1 API (Scoresby `r1r293`)**:
   * Observations (Real-time ambient temp):
     ```http
     GET https://api.weather.bom.gov.au/v1/locations/r1r293/observations
     ```
   * Forecasts (Short text e.g. "Late showers.", daily max/min, rain %):
     ```http
     GET https://api.weather.bom.gov.au/v1/locations/r1r293/forecasts/daily
     ```
   * Warnings (Gale/Flood/Fire/Thunderstorm alerts):
     ```http
     GET https://api.weather.bom.gov.au/v1/locations/r1r293/warnings
     ```
