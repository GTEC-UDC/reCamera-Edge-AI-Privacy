

// Export two functions that will be included in node-red.
module.exports = {
tracker: function(context, msg, simulated_timestamp) {
// Check if the message is an invoke
if (msg.payload.name !== "invoke" || !msg.payload.data) {
    return;
}

// Configuration parameters (adjustable via environment variables or settings)
const MIN_CONSECUTIVE_MATCHES = 5; // Minimum number of consecutive matches required to activate tracking of objects
const ACTIVATION_THRESHOLD = 50; // Threshold to activate tracking of objects
const DEACTIVATION_THRESHOLD = 20; // Threshold to deactivate tracking of objects
const MIN_DEACTIVATION_TIME = 1000; // Minimum time in milliseconds that the objects will live when they are not detected
const MAX_DEACTIVATION_TIME = 5000; // Maximum time in milliseconds that the objects will live when they are not detected
const MAX_DISTANCE = 100;         // Max pixel distance for association

// Helper function to clamp value between min and max
const clamp = (value, min, max) => Math.min(Math.max(value, min), max);

class TrackedObject {
    constructor(classLabel, initialConfidence, position, timestamp) {
        this.classLabel = classLabel;
        this.lastPosition = position;
        this.lastSeen = timestamp;
        
        this.meanConfidence = initialConfidence;
        this.consecutiveMisses = 0;
        this.consecutiveMatches = 0;
        
        this.isActive = false;
    }


    update(confidence, position, timestamp) {
        this.meanConfidence = (7*this.meanConfidence + confidence)/8;
        this.lastPosition = position;
        this.lastSeen = timestamp;
        this.consecutiveMisses = 0; // Reset miss counter on successful detection
        this.consecutiveMatches++;
    }

    missedDetection(timestamp) {
        this.consecutiveMisses++;
        this.meanConfidence = (7 * this.meanConfidence + 0) / 8;
        this.consecutiveMatches = 0;
    }

    activationMetric() {
        if (this.consecutiveMatches < MIN_CONSECUTIVE_MATCHES) {
            return 0.0;
        }
        // Consecutive matches
        const consecutiveMatchesFactor = 1 + this.consecutiveMatches * 0.1;
        // Penalize consecutive misses
        const missedPenalty = Math.max(0, 1 - this.consecutiveMisses * 0.1);

        return this.meanConfidence * consecutiveMatchesFactor * missedPenalty ;
    }

    deactivationMetric(currentTimestamp) {
        const timeSinceLastDetection = (currentTimestamp - this.lastSeen);
        if (timeSinceLastDetection < MIN_DEACTIVATION_TIME) {
            return 100.0;
        } else {
            const timeRatio = (timeSinceLastDetection - MIN_DEACTIVATION_TIME) / (MAX_DEACTIVATION_TIME - MIN_DEACTIVATION_TIME);
            // Consecutive matches
            const consecutiveMatchesFactor = 1 + this.consecutiveMatches * 0.1;
            // Penalize consecutive misses
            const missedPenalty = Math.max(0, 1 - this.consecutiveMisses * 0.1);
            return this.meanConfidence * (1 - timeRatio) * consecutiveMatchesFactor * missedPenalty;
        }
    }
}

// Initialize context storage if not exists
const trackedObjects = context.get('trackedObjects') || new Map();

// Get current frame data
const currentData = msg.payload.data;
//const currentTime = Date.now();
const currentTime = simulated_timestamp === undefined ? Date.now() : simulated_timestamp;

// 1. Simple cleanup - remove very old objects
for (const [key, obj] of trackedObjects) {
    if (currentTime - obj.lastSeen > MAX_DEACTIVATION_TIME) {
        trackedObjects.delete(key);
    }
}

// Copy tracked objects to avoid modifying during iteration
let unmatched = new Set(trackedObjects.keys());

// 2. Process current detections
currentData.labels.forEach((label, index) => {
    const [x, y, w, h, conf, classId] = currentData.boxes[index];
    const position = { x: x + w / 2, y: y + h / 2 }; // Center point
    const confidence = conf;

    // Find best match in tracked objects
    let bestMatch = null;
    let minDistance = Infinity;

    for (const [key, obj] of trackedObjects) {
        if (obj.classLabel === label && unmatched.has(key)) {
            // Calculate distance to last known position
            const lastPos = obj.lastPosition;
            const distance = Math.hypot(
                position.x - lastPos.x,
                position.y - lastPos.y
            );

            if (distance < MAX_DISTANCE && distance < minDistance) {
                minDistance = distance;
                bestMatch = key;
            }
        }
    }

    if (bestMatch && label === "person") {
        console.log("minDistance", minDistance);
    }

    if (bestMatch) {
        // Update existing tracked object
        const obj = trackedObjects.get(bestMatch);
        obj.update(confidence, position, currentTime);
        unmatched.delete(bestMatch);

        // Check activation
        if (!obj.isActive && obj.activationMetric() >= ACTIVATION_THRESHOLD) {
            obj.isActive = true;
        }
    } else {
        // Create new tracked object
        const key = `${label}-${currentTime}-${index}`;
        trackedObjects.set(key, new TrackedObject(
            label,
            confidence,
            position,
            currentTime
        ));
    }
});

// 3. Handle unmatched objects (missed detections)
unmatched.forEach(key => {
    const obj = trackedObjects.get(key);
    obj.missedDetection(currentTime);
});

// 4. Update activation states and calculate counts
let numObjects = 0;
const classCounts = {};
let objectsToRemove = [];

for (const [key, obj] of trackedObjects) {
    // Check deactivation for active objects
    if (obj.isActive) {
        const deactMetric = obj.deactivationMetric(currentTime);
        if (deactMetric < DEACTIVATION_THRESHOLD) {
            obj.isActive = false;
            objectsToRemove.push(key);
        }
    }

    // Update counts for active objects
    if (obj.isActive) {
        classCounts[obj.classLabel] = (classCounts[obj.classLabel] || 0) + 1;
        numObjects++;
    }
}

// Remove deactivated objects
objectsToRemove.forEach(key => trackedObjects.delete(key));

// Save updated tracking state
context.set('trackedObjects', trackedObjects);

// Prepare output message
msg.payload = {
    timestamp: currentTime,
    num_objects: numObjects,
    class_counts: classCounts,
    active_objects: Array.from(trackedObjects.values()).filter(obj => obj.isActive).map(obj => ({
        classLabel: obj.classLabel,
        meanConfidence: obj.meanConfidence,
        lastPosition: [obj.lastPosition.x, obj.lastPosition.y],
        numDetections: obj.totalUpdates,
        missedDetections: obj.consecutiveMisses
    }))
};

return msg;
},
filter: function(context, msg) {
    // This function only sends the messages where the count of objects has changed.
    const prevMsg = context.get('prevMsg');
    context.set('prevMsg', msg);

    if (!prevMsg) {
        return msg;
    }

    const prevNumObjects = prevMsg.payload.num_objects;
    const currentNumObjects = msg.payload.num_objects;
    
    if (prevNumObjects !== currentNumObjects) {
        return msg;
    }

    const prevClassCounts = prevMsg.payload.class_counts;
    const currentClassCounts = msg.payload.class_counts;
    
    for (const label in prevClassCounts) {
        if (prevClassCounts[label] !== currentClassCounts[label]) {
            return msg;
        }
    }
    
    for (const label in currentClassCounts) {
        if (!prevClassCounts.hasOwnProperty(label)) {
            return msg;
        }
    }

    return;
}
};