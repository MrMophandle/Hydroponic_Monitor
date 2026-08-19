---
version: next
status: planned
priority: high
complexity: 4
linked_tasks: [sensor-monitoring-dashboard]
created: 2026-08-19
---

# Sensor Monitoring Dashboard

Firmware for an ESP32-S3-N16R8 that reads three sensors attached to a hydroponic
reservoir — water temperature (DS18B20, 1-Wire), ambient light (BH1750, I2C), and water
level (two float switches giving a three-band FULL/MID/LOW reading plus a FAULT state) —
retains 24 hours of samples in an in-RAM ring buffer, and serves a LAN-hosted web page
showing current values and a 24-hour chart.

Delivery is HTTP over the local network only: no MQTT, no cloud service, no companion
app, no authentication, and no OTA. The device is reachable at a stable mDNS hostname so
the page can be bookmarked, which is the product's stated success metric.

Version 1 is **monitoring only** — the device reads and reports, and actuates nothing.
Relay control of the reservoir pump is explicitly deferred to a later version; this
feature only leaves the seams for it (a `status_set()` reporting hook and a fail-safe
`FAULT` level state that a future pump interlock can consume).

**Complexity rationale**: Level 4 by the decision tree — Q5 fires on two counts. The work
requires phased implementation (six phases, each with its own observable deliverable), and
it carries multiple design decisions settled during brainstorming: the concurrent task
architecture and shared-state model, the driver abstraction boundary and its link-time
test seam, the data model plus downsampled/chunk-streamed API, and the failure-handling
and fail-safe strategy. It introduces roughly ten new modules into a repository that
currently contains an empty `app_main()`, so it is effectively the whole firmware rather
than a change to it. The single-deployable target and small absolute code size argue for
Level 3, but the phasing requirement is decisive under Q5.
