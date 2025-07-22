const { tracker, filter } = require('./index');
const fs = require('fs');
const process = require('process');

// Read test data simulating multiple frames from file with one JSON object per line
//const testData = fs.readFileSync('./recamera_detections.log', 'utf8').split('\n').filter(Boolean).map(line => JSON.parse(line));
const testData = fs.readFileSync('./recamera_detections_3.log', 'utf8').split('\n').filter(Boolean).map(line => JSON.parse(line));

console.log('Running tracker tests...');

let currTimestamp = 0;
const FPS = 10;
const FRAME_INTERVAL = 1000 / FPS;
// Initialize variables to track processing time
let trackerTotalTime = 0;
let filterTotalTime = 0;
let frameCount = 0;

// Initialize memory tracking
let peakMemory = process.memoryUsage().heapUsed;
let memorySamples = [];
const MEMORY_SAMPLE_INTERVAL = 100; // Take memory sample every 100 frames
// Memory profiling configuration
const ENABLE_MEMORY_PROFILING = false;


// Function to track memory usage
function trackMemory(frameIndex, result) {
    if (!ENABLE_MEMORY_PROFILING) return;

    if (frameIndex % MEMORY_SAMPLE_INTERVAL === 0) {
        //gc();

        const memory = process.memoryUsage();
        peakMemory = Math.max(peakMemory, memory.heapUsed);

        memorySamples.push({
            frame: frameIndex,
            heapUsed: memory.heapUsed,
            heapTotal: memory.heapTotal,
            rss: memory.rss,
            active_objects: result.payload.active_objects.length,
            tracked_objects: Array.from(global['trackedObjects'] || []).length
        });
    }

}

const max_iter = ENABLE_MEMORY_PROFILING ? 100 : 1;

for (let iter = 0; iter < max_iter; iter++) {
    contextObj = {};

    // Mock Node-RED context
    const context = {
        get: (key) => {
            return contextObj[key] || null;
        },
        set: (key, value) => {
            contextObj[key] = value;
        }
    };
    testData.forEach((data, index) => {
        const msg = {
            payload: data
        };
        
        // Execute the tracker
        const startTime = Date.now();
        const result = tracker(context, msg, currTimestamp);
        const trackerTime = Date.now() - startTime;
        trackerTotalTime += trackerTime;
        frameCount++;
        
        // Execute the filter
        const startTime2 = Date.now();
        const filteredResult = filter(context, result);
        const filterTime = Date.now() - startTime2;
        filterTotalTime += filterTime;
        
        // Track memory usage
        trackMemory(index, result)
        
        if (filteredResult && max_iter === 1) {
            console.log(`Frame ${index + 1}:\n`, {
                timestamp: filteredResult.payload.timestamp,
                num_objects: filteredResult.payload.num_objects,
                class_counts: filteredResult.payload.class_counts,
                active_objects: filteredResult.payload.active_objects.map(obj => ({
                    classLabel: obj.classLabel,
                    meanConfidence: obj.meanConfidence,
                    numDetections: obj.numDetections,
                    missedDetections: obj.missedDetections
                }))
            });
        }
        currTimestamp += FRAME_INTERVAL;
    });
}


// Print the performance statistics
console.log('\nPerformance Statistics:');
console.log(`Total frames processed: ${frameCount}`);
console.log(`Tracker total time: ${trackerTotalTime} ms`);
console.log(`Filter total time: ${filterTotalTime} ms`);
console.log(`Average tracker time: ${trackerTotalTime / frameCount} ms`);
console.log(`Average filter time: ${filterTotalTime / frameCount} ms`);

if (ENABLE_MEMORY_PROFILING) {
    // Print memory statistics
    console.log('\nMemory Statistics:');
    console.log(`Peak heap usage: ${Math.round(peakMemory / 1024 / 1024)} MB`);
    console.log(`Final memory usage:`);
    console.log(process.memoryUsage());

    // Print memory usage over time
    console.log('\nMemory Usage Over Time:');
    console.log('Frame\tHeap Used (MB)\tHeap Total (MB)\tRSS (MB)\tActive Objects\tTracked Objects');
    memorySamples.forEach(sample => {
        console.log(
            `${sample.frame}\t${Math.round(sample.heapUsed / 1024 / 1024)}\t` +
            `${Math.round(sample.heapTotal / 1024 / 1024)}\t${Math.round(sample.rss / 1024 / 1024)}\t` +
            `${sample.active_objects}\t${sample.tracked_objects}`
        );
    });
}

/*
// Create a memory usage plot
const path = require('path');
const { spawn } = require('child_process');

const memoryData = memorySamples.map(sample => 
    `${sample.frame}\t${sample.heapUsed / 1024 / 1024}\n`.replace(/\./g, ',')
).join('');

fs.writeFileSync('memory_usage.dat', memoryData);

const gnuplot = spawn('gnuplot', []);

gnuplot.stdin.write(`
set terminal png size 1200,600
set output 'memory_usage.png'
set xlabel 'Frame'
set ylabel 'Memory Usage (MB)'
set title 'Memory Usage Over Time'
set grid
plot 'memory_usage.dat' with lines title 'Memory Usage'
`);

gnuplot.stdin.end();

gnuplot.on('close', () => {
    console.log('\nMemory usage plot created: memory_usage.png');
});*/
