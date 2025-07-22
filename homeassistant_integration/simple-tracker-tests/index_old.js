

// Export two functions that will be included in node-red.
module.exports = {
tracker: function(context, msg, simulated_timestamp) {
    // Check if the message is an invoke
    if (msg.payload.name !== "invoke" || !msg.payload.data) {
        return;
    }
    
    // Configuration parameters (adjustable via environment variables or settings)
    const ACTIVATION_THRESHOLD = 50; // Mean confidence to activate tracking
    const DEACTIVATION_THRESHOLD = 20; // Mean confidence to deactivate tracking
    const MIN_DEACTIVATION_TIME = 500; // Minimum time in milliseconds that the objects will live when they are not detected
    const MAX_DEACTIVATION_TIME = 2000; // Maximum time in milliseconds that the objects will live when they are not detected
    const MAX_DISTANCE = 100;         // Max pixel distance for association
    
    const TIME_WINDOW = MAX_DEACTIVATION_TIME; // Time window in milliseconds
    
    // Helper function to clamp value between min and max
    const clamp = (value, min, max) => Math.min(Math.max(value, min), max);
    
    class TrackedObject {
        constructor(classLabel, initialConfidence, position, timestamp) {
            this.classLabel = classLabel;
            this.confidences = [initialConfidence];
            this.lastPosition = position;
            this.timestamps = [timestamp];
            
            this.lastSeen = timestamp;
            this.totalConfidence = initialConfidence;
            this.maxConfidence = initialConfidence;
            this.detectionCount = 1;
            this.isActive = false;
            this.missedDetections = 0;
        }
    
        get meanConfidence() {
            return this.totalConfidence / (this.detectionCount + this.missedDetections);
        }
    
        update(confidence, position, timestamp) {
            this.confidences.push(confidence);
            this.totalConfidence += confidence;
            this.maxConfidence = Math.max(this.maxConfidence, confidence);
            this.detectionCount++;
            this.lastPosition = position;
            this.timestamps.push(timestamp);
            this.lastSeen = timestamp;
            this.missedDetections += confidence == 0 ? 1 : 0;
            this.pruneOldDetections(timestamp);
        }

        missedDetection(timestamp) {
            this.missedDetections++;
            this.timestamps.push(timestamp);
            this.confidences.push(0);
            this.pruneOldDetections(timestamp);
        }

        pruneOldDetections(timestamp) {
            const timeThreshold = timestamp - TIME_WINDOW;
            
            while (this.timestamps[0] < timeThreshold) {
                const confidence = this.confidences.shift();
                this.timestamps.shift();
                if (confidence == 0) {
                    this.missedDetections--;
                } else {
                    this.detectionCount--;
                }
                this.totalConfidence -= confidence;
            }
        }

        activationMetric() {
            const highConfidenceMetric = clamp((this.maxConfidence/100 + 0.5) ** 2 - 0.5, 0, 1.5);
            const effectiveDetections = Math.max(0, this.detectionCount - this.missedDetections);
            const detectionCountMetric = clamp(effectiveDetections / 10, 0, 1);
            const meanDetectionMetric = this.meanConfidence / 100;
            return (highConfidenceMetric*0.3 + detectionCountMetric*0.3 + meanDetectionMetric*0.4) * 100;
        }

        deactivationMetric(currentTimestamp) {
            const timeSinceLastDetection = (currentTimestamp - this.lastSeen);
            if (timeSinceLastDetection < MIN_DEACTIVATION_TIME) {
                return 100;
            } else {
                return this.meanConfidence * (1 - (timeSinceLastDetection - MIN_DEACTIVATION_TIME) / (MAX_DEACTIVATION_TIME - MIN_DEACTIVATION_TIME));
            }
        }
    }
    
    // Initialize context storage if not exists
    const trackedObjects = context.get('trackedObjects') || new Map();
    
    // Get current frame data
    const currentData = msg.payload.data;
    const currentTime = simulated_timestamp === undefined ? Date.now() : simulated_timestamp;
    
    // 1. Prune old entries
    for (const [key, obj] of trackedObjects) {
        if (currentTime - obj.lastSeen > TIME_WINDOW) {
            trackedObjects.delete(key);
        }
    }
    
    // copy tracked objects to avoid modifying the original
    let trackedObjectsCopy = new Map(trackedObjects);

    // 2. Process current detections
    currentData.labels.forEach((label, index) => {
        const [x, y, w, h, conf, classId] = currentData.boxes[index];
        const position = { x: x + w / 2, y: y + h / 2 }; // Center point
        const confidence = conf;

        // Find best match in tracked objects
        let bestMatch = null;
        let minDistance = Infinity;

        trackedObjectsCopy.forEach((obj, key) => {
            if (obj.classLabel === label) {
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
        });

        if (bestMatch) {
            const obj = trackedObjectsCopy.get(bestMatch);
            obj.update(confidence, position, currentTime);
            trackedObjectsCopy.delete(bestMatch);
            
            if (!obj.isActive && obj.activationMetric() >= ACTIVATION_THRESHOLD) {
                obj.isActive = true;
            }
        } else {
            const key = `${label}-${currentTime}-${index}`;
            trackedObjects.set(key, new TrackedObject(
                label,
                confidence,
                position,
                currentTime
            ));
        }
    });

    // Notify of missed detections for remaining objects
    trackedObjectsCopy.forEach(obj => {
        obj.missedDetection(currentTime);
    });
    
    // 3. Update activation states and calculate counts
    let numObjects = 0;
    const classCounts = {};
    let objectsToRemove = [];
    
    for (const [key, obj] of trackedObjects) {
        if (obj.isActive) {
            const deactMetric = obj.deactivationMetric(currentTime);
            if (deactMetric < DEACTIVATION_THRESHOLD) {
                obj.isActive = false;
                objectsToRemove.push(key);
            }
        }
        
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
        active_objects: Array.from(trackedObjects.values())
            .filter(obj => obj.isActive)
            .map(obj => ({
                classLabel: obj.classLabel,
                meanConfidence: obj.meanConfidence,
                lastPosition: [obj.lastPosition.x, obj.lastPosition.y],
                numDetections: obj.detectionCount,
                missedDetections: obj.missedDetections
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