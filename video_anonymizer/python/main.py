#!/usr/bin/env python3
"""
Video Anonymizer - Main application

This module provides a command-line interface for anonymizing humans in videos
using AI-based detection and background subtraction techniques.
"""

import cv2
import numpy as np
import argparse
import time
from anonymizer import VideoAnonymizer
from preview import Preview

def process_video(input_source, output_path=None, bg_method='CNT', anon_mode='blur',
                  blur_strength=21, show_preview=True, conf_threshold=0.5, device='cpu',
                  max_width=1024, debug_model=False, show_detections=False,
                  learning_rate=None, bg_threshold=None, model_name="yolo11n-seg.pt",
                  use_tracking=True, tracker_type='KCF', track_history=15):
    """Process a video source and anonymize humans.
    
    Args:
        input_source: Path to video file or camera index (int)
        output_path: Path to save output video (None for no output)
        bg_method: Background subtraction method ('CNT', 'MOG2', 'KNN', 'GMG', 'LSBP')
        anon_mode: Anonymization mode ('blur', 'background', 'solid')
        blur_strength: Strength of blur if using blur mode
        show_preview: Whether to show preview window
        conf_threshold: Confidence threshold for detections (0.0-1.0)
        device: Device to run model on ('cpu', 'cuda')
        max_width: Maximum width to resize input to (preserves aspect ratio)
        debug_model: Whether to show AI model detections with labels
        show_detections: Whether to show bounding boxes
        learning_rate: Learning rate for background subtraction
        bg_threshold: Threshold for background subtraction
        model_name: Name of the YOLOv11 model file to use
        use_tracking: Whether to use object tracking
        tracker_type: Type of tracker to use ('KCF', 'CSRT', etc.)
        track_history: Number of frames to keep in tracking history
    """
    # Initialize video source
    if isinstance(input_source, str):
        cap = cv2.VideoCapture(input_source)
        source_name = input_source.split('/')[-1]
    else:
        cap = cv2.VideoCapture(input_source)
        source_name = f"Camera {input_source}"
    
    if not cap.isOpened():
        print(f"Error: Could not open video source {input_source}")
        return
    
    # Get video properties
    orig_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    orig_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    if fps <= 0:
        fps = 30  # Default to 30 fps if not available
    
    # Calculate scaling if necessary
    scale_factor = 1.0
    if max_width > 0 and orig_width > max_width:
        scale_factor = max_width / orig_width
        width = max_width
        height = int(orig_height * scale_factor)
    else:
        width = orig_width
        height = orig_height
    
    # Initialize video writer if output path is specified
    video_writer = None
    if output_path is not None:
        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        video_writer = cv2.VideoWriter(
            output_path, fourcc, fps, (width, height)
        )
    
    # Initialize anonymizer
    bg_params = {}
    if learning_rate is not None:
        bg_params['learning_rate'] = learning_rate
    if bg_threshold is not None:
        bg_params['threshold'] = bg_threshold
        
    anonymizer = VideoAnonymizer(
        bg_subtractor_method=bg_method,
        conf_threshold=conf_threshold,
        device=device,
        bg_params=bg_params,
        model_name=model_name,
        use_tracking=use_tracking,
        tracker_type=tracker_type,
        track_history=track_history
    )
    
    # Process video frames
    frame_count = 0
    processing_times = []
    
    print(f"Processing video from {source_name}...")
    print(f"Background subtractor: {bg_method}")
    print(f"Anonymization mode: {anon_mode}")
    
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        
        # Measure processing time
        start_time = time.time()
        
        # Resize frame if needed
        if scale_factor < 1.0:
            frame = cv2.resize(frame, (width, height))
            
        # Process frame with additional return values for visualization
        anonymized_frame, results, masks, original_frame = anonymizer.process_frame(
            frame,
            anonymization_mode=anon_mode,
            blur_strength=blur_strength,
            return_details=True,
            return_original=True,
            debug_mode=debug_model
        )
        
        # Calculate performance metrics
        processing_time = time.time() - start_time
        processing_times.append(processing_time)
        avg_time = np.mean(processing_times[-30:])  # Average of last 30 frames
        fps_estimate = 1.0 / avg_time if avg_time > 0 else 0
        
        # Add performance info to frame
        cv2.putText(anonymized_frame, f"Frame: {frame_count}", (10, 20),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
        cv2.putText(anonymized_frame, f"FPS: {fps_estimate:.1f}", (10, 45),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
        cv2.putText(anonymized_frame, f"Method: {bg_method}", (10, 70),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
        cv2.putText(anonymized_frame, f"Mode: {anon_mode}", (10, 95),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
        
        # Write frame to output video
        if video_writer is not None:
            video_writer.write(anonymized_frame)
        
        # Display windows based on enabled options
        if show_preview:
            cv2.imshow('Anonymized Video', anonymized_frame)
            
            # Show background model with tracking visualization if available
            background = anonymizer.get_background(visualize_tracking=True)
            if background is not None:
                cv2.imshow('Background Model with Tracking', background)
        
        # Display detection boxes if enabled
        if show_detections and results is not None and hasattr(results, 'boxes'):
            show_detection_boxes(anonymized_frame, results)
        
        # Display AI model debug visualization if enabled
        if debug_model and results is not None:
            # Initialize preview object if needed
            if not hasattr(process_video, 'preview'):
                process_video.preview = Preview(window_name='AI Model Debug')
            
            # Get class names and create visualization
            class_names = results.names if hasattr(results, 'names') else None
            debug_frame = process_video.preview.visualize_detections(original_frame, results, class_names)
            cv2.imshow('AI Model Debug', debug_frame)
            
            # Check for quit request from preview window
            if not process_video.preview.check_for_quit():
                break
        
        # Break loop on 'q' press or ESC key
        key = cv2.waitKey(1) & 0xFF
        if key == 27 or key == ord('q'):  # ESC key or 'q'
            break
        
        frame_count += 1
    
    # Clean up
    cap.release()
    if video_writer is not None:
        video_writer.release()
        
    # Close the preview window if it was created
    if hasattr(process_video, 'preview'):
        process_video.preview.close()
        
    cv2.destroyAllWindows()
    
    # Print performance summary
    avg_processing_time = np.mean(processing_times)
    avg_fps = 1.0 / avg_processing_time if avg_processing_time > 0 else 0
    print(f"\nProcessed {frame_count} frames")
    print(f"Average processing time: {avg_processing_time*1000:.1f} ms per frame")
    print(f"Average FPS: {avg_fps:.1f}")


def show_detection_boxes(frame, results):
    """Show bounding boxes around detected objects.
    
    Args:
        frame: Input video frame
        results: Detection results from YOLO model
    """
    detection_vis = frame.copy()
    
    # Draw boxes around detections
    for box in results.boxes.xyxy.cpu().numpy():
        x1, y1, x2, y2 = map(int, box[:4])
        cv2.rectangle(detection_vis, (x1, y1), (x2, y2), (0, 255, 0), 2)
    
    cv2.imshow('Detections', detection_vis)


def main():
    """Main function to parse arguments and run video processing."""
    # Close any OpenCV windows that might be left open from previous runs
    cv2.destroyAllWindows()
    
    parser = argparse.ArgumentParser(description='Video Human Anonymization')
    
    # Source and output options
    parser.add_argument('--input', '-i', type=str, default='0',
                        help='Path to input video file or camera index (default: 0)')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Path to output video file (default: no output)')
    
    # Algorithm options
    parser.add_argument('--bg-method', '-b', type=str, default='CNT',
                        choices=['CNT', 'MOG2', 'KNN', 'GMG', 'LSBP'],
                        help='Background subtraction method (default: CNT)')
    parser.add_argument('--anon-mode', '-a', type=str, default='background',
                        choices=['blur', 'background', 'solid'],
                        help='Anonymization mode (default: background)')
    parser.add_argument('--blur', type=int, default=41,
                        help='Blur strength (default: 41)')
    
    # Model options
    parser.add_argument('--confidence', '-c', type=float, default=0.2,
                        help='Confidence threshold for detections (default: 0.2)')
    parser.add_argument('--device', '-d', type=str, default='cpu',
                        choices=['cpu', 'cuda'],
                        help='Device to run model on (default: cpu)')
    parser.add_argument('--model', '-m', type=str, default='yolo11n-seg.pt',
                        help='YOLO model to use (default: yolo11n-seg.pt)')
                        
    # Tracking options
    parser.add_argument('--use-tracking', action='store_true', default=True,
                        help='Enable object tracking for stable detection (default: True)')
    parser.add_argument('--no-tracking', action='store_false', dest='use_tracking',
                        help='Disable object tracking')
    parser.add_argument('--tracker-type', type=str, default='KCF',
                        choices=['KCF', 'CSRT', 'MedianFlow'],
                        help='Type of tracker to use (default: KCF)')
    parser.add_argument('--track-history', type=int, default=15,
                        help='Number of frames to maintain tracking without detection (default: 15)')
    
    # Background model configuration
    parser.add_argument('--learning-rate', type=float, default=None,
                        help='Learning rate for background model (algorithm specific)')
    parser.add_argument('--bg-threshold', type=float, default=None,
                        help='Threshold for background subtraction (algorithm specific)')
    parser.add_argument('--max-width', type=int, default=1024,
                        help='Maximum width for processing (default: 1024, set to 0 to disable)')
    
    # Display options
    parser.add_argument('--no-preview', action='store_true',
                        help='Disable preview window')
    parser.add_argument('--debug-model', action='store_true',
                        help='Show all AI model detections with class names and confidence scores')
    parser.add_argument('--show-detections', action='store_true',
                        help='Show detection bounding boxes')
    
    args = parser.parse_args()
    
    # Convert input to int if it's a digit (camera index)
    if args.input.isdigit():
        args.input = int(args.input)
    
    # Process video
    process_video(
        input_source=args.input,
        output_path=args.output,
        bg_method=args.bg_method,
        anon_mode=args.anon_mode,
        blur_strength=args.blur,
        show_preview=not args.no_preview,
        conf_threshold=args.confidence,
        device=args.device,
        max_width=args.max_width,
        debug_model=args.debug_model,
        show_detections=args.show_detections,
        learning_rate=args.learning_rate,
        bg_threshold=args.bg_threshold,
        model_name=args.model,
        use_tracking=args.use_tracking,
        tracker_type=args.tracker_type,
        track_history=args.track_history
    )


if __name__ == '__main__':
    main()
