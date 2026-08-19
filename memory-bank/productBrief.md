# Product Brief

> This document captures the **product and project context** for development teams.
> It ensures all agents understand the product's purpose, users, constraints, **and the project's foundation**.

## Project Foundation

- **Project Name**: Hydroponic_Monitor (CMake/IDF project name: `HydroponicMonitor`)
- **Objectives**: This repository contains the firmware to operate an ESP32-S3-N16R8 development board.  This board will have several sensors attached to it.  The firmware needs to host a webpage that shows current stats.  Future capability includes the addition of a relay to control the pump of the hydroponic system itself.
- **Scope**: [To be defined]
- **Repository Structure**: **Poly-repo** — a single-deployable ESP-IDF firmware project.
  ```
  Hydroponic_Monitor/
  ├── CMakeLists.txt         # top-level IDF project definition (project: HydroponicMonitor)
  ├── platformio.ini         # PlatformIO env: esp32-s3-devkitm-1, framework = espidf
  ├── sdkconfig.esp32-s3-devkitm-1  # generated ESP-IDF Kconfig for the board
  ├── src/                   # application sources (app_main entrypoint) + component CMakeLists.txt
  ├── include/               # project header files shared across src/
  ├── lib/                   # project-private libraries (one directory per library)
  ├── test/                  # PlatformIO Test Runner unit tests
  ├── build/ , .pio/         # generated build output (see Open Questions — build/ is tracked)
  └── .vscode/               # PlatformIO IDE recommendations
  ```
- **Key Stakeholders**: Ryan Perkowski (sole contributor as of 2026-08-19)

## Git Configuration

- **Repository**: Yes
- **Provider**: GitHub
- **CLI Available**: gh (authenticated)
- **Remote URL**: https://github.com/MrMophandle/Hydroponic_Monitor.git
- **Default Branch**: main
- **Metadata Branch**: main
- **Routing Mode**: classic
- **Sync Automation**: none
- **Archive Strategy**: push-and-pr

## Product Overview

- **Name**: [Product name]
- **Value Proposition**: [What problem it solves and for whom]
- **Product Type**: Embedded firmware (ESP32-S3 device)
- **Stage**: Concept — scaffold only, no functionality implemented

## Key Functionality

Core capabilities this product provides:

- Reads sensors attached to the ESP32
- Creates and hosts (makes available on my LAN) a visually attractive webpage that displays the statistics collected from the metrics.
- Stores data in memory (FIFO) for historical data to be presented on the webpage.

## Markets Serviced

- **Primary Market**: [Industry/vertical]
- **Secondary Markets**: [Other industries if applicable]
- **Geographic Focus**: [Regions/countries, or "Global"]
- **Market Size**: [If known]

## Competitive Landscape

- **Direct Competitors**: [List main competitors]
- **Indirect Competitors**: [Alternative solutions users might choose]
- **Key Differentiators**: [What sets this product apart]
- **Competitive Advantages**: [Unique strengths]

## Key Personas

### Primary Users

| Persona | Role | Goals | Pain Points | Success Metrics |
|---------|------|-------|-------------|-----------------|
| [User (me)] | [me] | [Load a webpage that shows how the hydroponic system is performing] | [Current system is a bucket with a pump in it...no data collected] | [I can bookmark a page on my browser that shows me how the hydroponic system is doing] |

### Secondary Users

| Persona | Role | Goals |
|---------|------|-------|
| [Name] | [Job title/role] | [What they want to achieve] |

### Administrators/Operators

| Persona | Role | Responsibilities |
|---------|------|------------------|
| [Name] | [Job title/role] | [What they manage/configure] |

## User Flows

- **Primary Flow**: [No authentication, user loads the webpage and see's the statistics]
- **Onboarding**: [How new users get started — e.g. device provisioning / Wi-Fi setup]
- **Key Workflows**:
  - [Critical user journey 1]
  - [Critical user journey 2]

## Success Metrics & KPIs

### Business Metrics
- [To be defined]

### Product Metrics
- [To be defined]

### Technical Metrics
- [Sensor sampling interval / accuracy targets]
- [Uptime between resets]
- [Error/watchdog-reset rate thresholds]

## Non-Functional Requirements

### Performance

- **Response Time**: [Target sensor-read → publish latency]
- **Throughput**: [Sample rate per sensor]
- **Concurrent Users**: [N/A for firmware unless a local UI/API is exposed]
- **Boot Time**: [Target cold-boot to first reading]

### Scalability

- **Devices**: [Current and projected device count]
- **Data Volume**: [Telemetry points/day per device]
- **Growth Rate**: [Expected growth trajectory]
- **Peak Load**: [Expected peak vs average ratio]

### Security

- **Authentication**: [Device identity / broker credentials — to be defined]
- **Authorization**: [To be defined]
- **Compliance**: [Standards, if any]
- **Data Classification**: [Sensitivity levels handled]
- **Encryption**: [At rest (NVS), in transit (TLS) requirements]

### Availability & Reliability

- **Uptime Target**: [To be defined]
- **Recovery Time Objective (RTO)**: [To be defined]
- **Recovery Point Objective (RPO)**: [To be defined]
- **Failure Handling**: [Watchdog / brownout / reconnect strategy]
- **Backup Strategy**: [Local buffering of readings during connectivity loss?]

### Data & Privacy

- **Data Residency**: [Where telemetry is stored]
- **Data Retention**: [How long readings are kept]
- **Privacy Requirements**: [If applicable]
- **PII Handling**: [If applicable]

### Constrained-Environment Requirements

- **Flash budget**: [Target application image size vs partition table]
- **RAM budget**: [Target heap headroom]
- **Power budget**: [Mains powered / battery + deep-sleep duty cycle]
- **OTA**: [Whether over-the-air update is required]

### Browser/Platform Support

- **Hardware target**: ESP32-S3 (esp32-s3-devkitm-1)
- **Companion app/UI**: [None yet — define if a dashboard or mobile client is planned]

## Integration Points

### External Systems

| System | Purpose | Protocol | Direction |
|--------|---------|----------|-----------|
| [System name] | [Why integrated] | [MQTT/HTTP/etc.] | [Inbound/Outbound/Both] |

### APIs Consumed

| API | Provider | Purpose |
|-----|----------|---------|
| [API name] | [Provider] | [What it's used for] |

### APIs Provided

| API | Purpose | Consumers |
|-----|---------|-----------|
| [API name] | [What it does] | [Who uses it] |

### Data Sources

| Source | Type | Frequency |
|--------|------|-----------|
| [Sensor name] | [I2C/ADC/1-Wire/etc.] | [Sampling interval] |

## Constraints & Assumptions

### Business Constraints

- [Budget limitations]
- [Timeline requirements]
- [Resource constraints]

### Technical Constraints

- Target hardware is fixed to the ESP32-S3 DevKitM-1 (`platformio.ini`); changing boards requires
  regenerating `sdkconfig.esp32-s3-devkitm-1`.
- Framework is ESP-IDF (not Arduino) — Arduino-only libraries are not directly usable.
- `src/CMakeLists.txt` is PlatformIO-generated and glob-registers `src/*.*` as one IDF component.
- Building requires `IDF_PATH` to be set (see top-level `CMakeLists.txt`).

### Assumptions

- [Key assumption 1]
- [Key assumption 2]

## Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Generated build output (`build/`) is committed, so diffs are noisy and merge conflicts are likely | High | Medium | Add `build/` to `.gitignore` and `git rm -r --cached build/` (see Open Questions) |

## Open Questions

- [ ] What does the monitor actually measure (pH, EC/TDS, water + air temperature, humidity, water
      level, light) and which sensor parts are used?
      Answer: I have sensors available that measure bucket fullness (float sensor), light levels, and temperature.  I plan on adding a relay in a later build.
- [ ] How are readings delivered — Wi-Fi + MQTT, HTTP, BLE, local display, or SD logging?  
      Answer: HTTP through a locally hosted webpage.
- [ ] Is there a companion dashboard or mobile app?
      Answer: No
- [ ] Does the device actuate anything (dosing pumps, water pump, lights) or is it read-only?
      Answer: Today, no.  First iteration is strictly a monitoring platform.  V2 we will add the relay to control the pump (turn it on/off)
- [ ] Should the generated `build/` directory be untracked? It is currently committed (415 tracked
      files, nearly all build artifacts), while `.pio/` is already ignored.
      Answer: Open to your suggestion here.
- [ ] Is OTA update in scope?
      Answer: No, I will manually flash the ESP32 if/when we upgrade it.

## Document History

| Date | Author | Changes |
|------|--------|---------|
| 2026-08-19 | /bmb:init | Initial creation (greenfield scaffold) |

## Last Refreshed

2026-08-19
