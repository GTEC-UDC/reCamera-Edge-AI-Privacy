# Simple Object Tracker Testing Framework

Standalone implementation and testing framework for the reCamera object tracking algorithm. Test, benchmark, and modify the algorithm using real detection data without requiring hardware.

**Node-RED Integration:** The `tracker()` and `filter()` functions in `index.js` are directly compatible with Node-RED function nodes.

## Algorithm Overview

The tracker implements a multi-stage filtering system to provide stable object counts:

### Core Components

**1. Object Association**
- Matches new detections to existing tracked objects by class and spatial proximity
- Uses Euclidean distance between detection centers with configurable threshold (`MAX_DISTANCE`)
- Closest valid match wins when multiple objects compete

**2. State Management**
Each tracked object maintains:
- `meanConfidence`: Exponentially-smoothed confidence (α=1/8 smoothing factor)
- `consecutiveMatches`: Consecutive successful detections
- `consecutiveMisses`: Consecutive missed detections  
- `isActive`: Boolean determining if object is counted

**3. Activation Logic**
Objects become "active" when `activationMetric ≥ ACTIVATION_THRESHOLD`:
```
activationMetric = meanConfidence × (1 + consecutiveMatches × 0.1) × max(0, 1 - consecutiveMisses × 0.1)
```
Requires minimum `MIN_CONSECUTIVE_MATCHES` detections.

**4. Deactivation Logic**
Objects become "inactive" when `deactivationMetric < DEACTIVATION_THRESHOLD`:
- If `timeSinceLastSeen < MIN_DEACTIVATION_TIME`: metric = 100 (keep alive)
- Otherwise: metric includes time-based decay factor

**5. Temporal Persistence**
- Minimum lifetime: `MIN_DEACTIVATION_TIME` (default: 1000ms)
- Maximum lifetime: `MAX_DEACTIVATION_TIME` (default: 5000ms)
- Gradual confidence decay during missed detections

## Quick Start

```bash
npm install
node test.js
```

**Key Files:**
- `index.js` - Tracking algorithm (`tracker()` and `filter()` functions)
- `test.js` - Testing framework with performance benchmarking
- `recamera_detections_*.log` - Real detection data (3 datasets: 467, 783, 819 frames)

**Sample Output:**
```
Frame 15: { num_objects: 4, class_counts: { chair: 3, 'dining table': 1 } }
Performance: 467 frames, 0.19ms avg tracker time, 0.026ms avg filter time
```

## Configuration

**Detection Data Format:** JSON objects (one per line) with `data.boxes` (bounding boxes), `data.labels` (object classes), and `data.count` (frame number).

**Algorithm Parameters** (edit `index.js`):
```javascript
MIN_CONSECUTIVE_MATCHES = 5;  // Detections needed before activation
ACTIVATION_THRESHOLD = 50;    // Confidence threshold for activation  
DEACTIVATION_THRESHOLD = 20;  // Confidence threshold for deactivation
MIN_DEACTIVATION_TIME = 1000; // Minimum object lifetime (ms)
MAX_DEACTIVATION_TIME = 5000; // Maximum object lifetime (ms)
MAX_DISTANCE = 100;           // Max pixel distance for association
```

**Test Parameters** (edit `test.js`):
- Change dataset: `'./recamera_detections_3.log'` → `'./recamera_detections.log'`
- Enable memory profiling: `ENABLE_MEMORY_PROFILING = true`

## Dataset Collection

To capture new test data from a real reCamera device:

```bash
mosquitto_sub -h {mqtt_broker_ip} -t '#' -v -u {mqtt_user} -P {mqtt_password} -p 1883 | grep '/detection ' | cut -d ' ' -f 2- > recamera_detections_out.log
```

This command:
- Subscribes to all MQTT topics (`-t '#'`) on the broker
- Filters for detection messages (`grep '/detection '`)
- Extracts JSON payload (`cut -d ' ' -f 2-`)
- Saves to log file for testing

**Parameters:**
- `{mqtt_broker_ip}`: IP address of your MQTT broker
- `{mqtt_user}` / `{mqtt_password}`: MQTT authentication credentials



## License

MIT License (same as main reCamera project) 