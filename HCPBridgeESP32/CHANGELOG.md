# 2.0.2

- Fix API prefix shadowing with explicit exact URI matching on all own routes.
- Add isolated WLED read request/results, busy retries, error messages and the recorded real-device XML fixture.
- Show current variable values on variable blocks.
- Move settings into modal overlays; add separately stored German/English JSON dictionaries and NVS language selection.
- Capture door transitions in a bounded queue independent of WLED HTTP requests.
- Improve position ingestion for both directions and log invalid raw values.
- Poll MQTT every 200 ms with revision tracking and consistent position snapshots; update the overview every second.
- Add RAM bus log with raw registers, interpreted state/position and MQTT queue results, paged API and JSON export.
- Add serial diagnostic web viewer, bounded RAM log and JSON export while serial debugging is enabled.
- Include a corrected copy of the user's restore flow, disabled for review.
- No ESP32 firmware compilation, flashing, garage-door commands or live configuration writes performed.

# 2.0.2

- Fix API prefix shadowing with explicit exact URI matching on all own routes.
- Add isolated WLED read request/results, busy retries, error messages and the recorded real-device XML fixture.
- Show current variable values on variable blocks.
- Move settings into modal overlays; add separately stored German/English JSON dictionaries and NVS language selection.
- Capture door transitions in a bounded queue independent of WLED HTTP requests.
- Improve position ingestion for both directions and log invalid raw values.
- Poll MQTT every 200 ms with revision tracking and consistent position snapshots; update the overview every second.
- Add RAM bus log with raw registers, interpreted state/position and MQTT queue results, paged API and JSON export.
- Add serial diagnostic web viewer, bounded RAM log and JSON export while serial debugging is enabled.
- Include a corrected copy of the user's restore flow, disabled for review.
- No ESP32 firmware compilation, flashing, garage-door commands or live configuration writes performed.

# 2.0.1

- Add string, signed 32-bit int and bool variable definitions, initial values and optional persistent values.
- Add variable read/write blocks and variable lookup in conditions.
- Add typed int input to WLED preset blocks, with range/type checks.
- Add bounded /win XML reading and atomic field-to-variable mappings, repeated RGB fields and XML entity decoding.
- Add live XML field discovery and runtime variable display behind existing authentication.
- Keep existing flow files compatible; add backup/restore example.
- Set firmware version to 2.0.1. No compilation or flashing performed for this revision.
