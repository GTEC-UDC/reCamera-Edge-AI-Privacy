#!/usr/bin/env python3
"""
Video Anonymizer Core Implementation

This module provides the VideoAnonymizer class which combines background subtraction
with AI-based human detection to anonymize people in videos. It supports multiple
background subtraction algorithms and anonymization modes.

The main pipeline includes:
1. Human detection using YOLO segmentation models
2. Background modeling using various subtractor algorithms
3. Anonymization by replacing human pixels with background or applying blur
"""

import cv2
import numpy as np
import torch
from ultralytics import YOLO
import time
from dataclasses import dataclass
from typing import List, Tuple, Optional, Dict, Any


@dataclass
class TrackedObject:
    """Class to store tracked object information"""
    box: np.ndarray  # [x1, y1, x2, y2]
    mask: Optional[np.ndarray] = None  # Binary mask
    class_id: int = 0  # Class ID
    confidence: float = 0.0  # Detection confidence
    tracker: Optional[Any] = None  # OpenCV tracker object
    last_seen: int = 0  # Frame index when last detected
    track_id: int = -1  # Unique tracking ID


class VideoAnonymizer:
    def __init__(self, bg_subtractor_method='CNT', conf_threshold=0.5, device='cpu', bg_params=None, model_name="yolo11n-seg.pt",
                 dilation_factor=0.1, min_dilation=5, max_dilation=30, dilation_iterations=1, warmup_dilation_factor=0.15,
                 use_tracking=True, tracker_type='KCF', track_history=15):
        """
        Initialize the video anonymizer.
        
        Args:
            bg_subtractor_method (str): Background subtraction method.
                Options: 'CNT', 'MOG2', 'KNN', 'GMG', 'LSBP'
            conf_threshold (float): Confidence threshold for YOLO detection.
            device (str): Device to run YOLO model (cpu, cuda).
            bg_params (dict): Optional parameters for background subtraction algorithm.
                               Can include 'learning_rate' and 'threshold'.
            model_name (str): Name of the YOLO model to use (default: yolo11n-seg.pt).
            dilation_factor (float): Factor to control mask dilation based on mask size (default: 0.1).
            min_dilation (int): Minimum dilation kernel size (default: 5).
            max_dilation (int): Maximum dilation kernel size (default: 30).
            dilation_iterations (int): Number of dilation iterations to perform (default: 1).
            warmup_dilation_factor (float): Factor for additional dilation during warmup (default: 0.15).
            use_tracking (bool): Whether to use object tracking to handle missing detections (default: True).
            tracker_type (str): Type of tracker to use: 'KCF', 'CSRT', 'MedianFlow' (default: 'KCF').
            track_history (int): Number of frames to maintain tracking without new detections (default: 15).
        """
        self.conf_threshold = conf_threshold
        self.bg_params = bg_params or {}
        self.model_name = model_name
        
        # Dilation parameters
        self.dilation_factor = dilation_factor
        self.min_dilation = min_dilation
        self.max_dilation = max_dilation
        self.dilation_iterations = dilation_iterations
        self.warmup_dilation_factor = warmup_dilation_factor
        
        # Tracking parameters
        self.use_tracking = use_tracking
        self.tracker_type = tracker_type
        self.track_history = track_history
        self.trackers = []
        self.tracked_objects = []
        self.last_detection_frame = 0  # Frame counter for tracking
        self.current_frame_idx = 0
        self.next_track_id = 0
        
        # Initialize background subtractor
        self.bg_subtractor_method = bg_subtractor_method
        self.bg_subtractor = self._create_bg_subtractor(bg_subtractor_method)
        
        # Initialize object detector (YOLO)
        print(f"Loading YOLO model '{model_name}' on device: {device}")
        try:
            # Try loading the specified model first
            self.model = YOLO(model_name)
        except Exception as e:
            print(f"Warning: Error loading specified model '{model_name}': {e}")
            # Fallback to default model
            try:
                self.model = YOLO("yolo11n-seg.pt")  # Segmentation model for better masks
            except Exception as e:
                print(f"Warning: Error loading segmentation model, trying detection model: {e}")
                self.model = YOLO("yolo11n.pt")  # Fallback to detection model
            
        self.device = device
        self.classes_to_detect = [0]  # 0 is person in COCO dataset
        
        # State variables
        self.background = None
        self.frame_count = 0
        self.warmup_frames = 30  # Number of frames to learn background
        self.last_clean_frame = None  # Keep track of last successful clean frame
    
    def _create_bg_subtractor(self, method):
        """Create the selected background subtractor."""
        if method == 'CNT':
            # BackgroundSubtractorCNT parameters
            params = {
                'minPixelStability': 15,
                'useHistory': True,
                'maxPixelStability': 15*60,
                'isParallel': True
            }
            # Apply custom parameters if provided
            if 'threshold' in self.bg_params:
                # CNT doesn't have a direct threshold parameter
                # Use threshold as minPixelStability
                params['minPixelStability'] = int(self.bg_params['threshold'])
            
            # For CNT, we also need to set maxPixelStability based on minPixelStability
            params['maxPixelStability'] = max(params['minPixelStability'] * 60, 900)
                
            return cv2.bgsegm.createBackgroundSubtractorCNT(**params)
            
        elif method == 'MOG2':
            params = {
                'history': 500,
                'varThreshold': 16,
                'detectShadows': False
            }
            # Apply custom parameters if provided
            if 'threshold' in self.bg_params:
                params['varThreshold'] = self.bg_params['threshold']
                
            return cv2.createBackgroundSubtractorMOG2(**params)
            
        elif method == 'KNN':
            params = {
                'history': 500,
                'dist2Threshold': 400.0,
                'detectShadows': False
            }
            # Apply custom parameters if provided
            if 'threshold' in self.bg_params:
                params['dist2Threshold'] = self.bg_params['threshold']
                
            return cv2.createBackgroundSubtractorKNN(**params)
            
        elif method == 'GMG':
            params = {
                'initializationFrames': 120,
                'decisionThreshold': 0.8
            }
            # Apply custom parameters if provided
            if 'threshold' in self.bg_params:
                params['decisionThreshold'] = self.bg_params['threshold']
                
            return cv2.bgsegm.createBackgroundSubtractorGMG(**params)
            
        elif method == 'LSBP':
            # LSBP has limited direct parameters
            return cv2.bgsegm.createBackgroundSubtractorLSBP()
            
        else:
            raise ValueError(f"Invalid background subtractor method: {method}")
    
    def get_background(self, visualize_tracking=False):
        """Get the current background model with optional tracking visualization.
        
        Args:
            visualize_tracking: If True, draw tracking boxes and masks on the background
            
        Returns:
            Background image with optional tracking visualization
        """
        try:
            # First check if we have a valid custom background
            if self.background is not None:
                bg = self.background.copy()
            # Then try to get from background subtractor if available    
            elif hasattr(self.bg_subtractor, 'getBackgroundImage'):
                bg = self.bg_subtractor.getBackgroundImage()
                if bg is not None and bg.size > 0:
                    # Save a copy to our self.background as backup
                    self.background = bg.copy()
                else:
                    return None
            else:
                # If we get here, we don't have a valid background
                return None
                
            # Draw tracking boxes and masks if requested
            if visualize_tracking and self.use_tracking and hasattr(self, 'tracked_objects') and self.tracked_objects:
                for obj in self.tracked_objects:
                    # Get the box coordinates
                    x1, y1, x2, y2 = map(int, obj.box)
                    
                    # Generate a color based on the track_id for consistent visualization
                    track_color = self._get_tracking_color(obj.track_id)
                    
                    # Draw the bounding box
                    cv2.rectangle(bg, (x1, y1), (x2, y2), track_color, 2)
                    
                    # Draw the track ID and confidence
                    label = f"ID:{obj.track_id}"
                    if hasattr(obj, 'confidence') and obj.confidence is not None:
                        label += f" {obj.confidence:.2f}"
                        
                    # Add text label
                    cv2.putText(bg, label, (x1, y1 - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, track_color, 2)
                    
                    # Draw the mask if available
                    if obj.mask is not None:
                        try:
                            # Handle mask format based on its type
                            if hasattr(obj.mask, 'data') and hasattr(obj.mask.data, 'cpu'):
                                # PyTorch tensor case
                                mask = obj.mask.data.cpu().numpy()
                            elif isinstance(obj.mask, np.ndarray):
                                # Already a NumPy array
                                mask = obj.mask
                            else:
                                # Try to convert to numpy array or skip
                                try:
                                    mask = np.array(obj.mask)
                                except:
                                    continue
                                    
                            # Normalize mask values if needed
                            if mask.dtype == np.float32 and mask.max() <= 1.0:
                                mask = (mask * 255).astype(np.uint8)
                                
                            # Make sure mask has the right shape
                            if len(mask.shape) > 2:
                                mask = mask.squeeze()
                            if len(mask.shape) > 2:
                                mask = mask[:, :, 0]
                                
                            # Resize mask if needed
                            if mask.shape[0] != bg.shape[0] or mask.shape[1] != bg.shape[1]:
                                mask = cv2.resize(mask, (bg.shape[1], bg.shape[0]))
                                
                            # Create a color overlay
                            mask_color = np.zeros_like(bg)
                            mask_color[:, :] = track_color
                            
                            # Apply the mask with transparency
                            alpha = 0.3  # Transparency level
                            mask_bool = mask > 128
                            if np.any(mask_bool):  # Only apply if there are mask pixels
                                bg[mask_bool] = cv2.addWeighted(
                                    bg[mask_bool], 1 - alpha,
                                    mask_color[mask_bool], alpha, 0
                                )
                        except Exception as e:
                            print(f"Warning: Error drawing mask for track {obj.track_id}: {e}")
                
                # Add a label indicating tracking is active
                cv2.putText(bg, f"Tracking: {len(self.tracked_objects)} objects", 
                          (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                          
            return bg
            
        except Exception as e:
            print(f"Warning: Error getting background: {e}")
            return self.background
            
    def _get_tracking_color(self, track_id):
        """Generate a consistent color for a track ID."""
        # Use a color mapping that generates distinct colors
        colors = [
            (0, 0, 255),    # Red
            (0, 255, 0),    # Green
            (255, 0, 0),    # Blue
            (0, 255, 255),  # Yellow
            (255, 0, 255),  # Magenta
            (255, 255, 0),  # Cyan
            (128, 0, 0),    # Dark Blue
            (0, 128, 0),    # Dark Green
            (0, 0, 128),    # Dark Red
            (128, 128, 0),  # Dark Cyan
            (128, 0, 128),  # Dark Magenta
            (0, 128, 128),  # Dark Yellow
        ]
        return colors[track_id % len(colors)]
    
    def _create_tracker(self, tracker_type):
        """Create an OpenCV tracker of the specified type."""
        if tracker_type == 'KCF':
            return cv2.TrackerKCF_create()
        elif tracker_type == 'CSRT':
            return cv2.TrackerCSRT_create()
        elif tracker_type == 'MedianFlow':
            return cv2.legacy.TrackerMedianFlow_create()
        else:
            print(f"Warning: Unknown tracker type {tracker_type}, falling back to KCF")
            return cv2.TrackerKCF_create()
    
    def _update_trackers(self, frame):
        """Update all active trackers with the new frame."""
        # Increment current frame counter
        self.current_frame_idx += 1
        
        # If no existing trackers, nothing to update
        if not self.tracked_objects:
            return []
        
        updated_objects = []
        for obj in self.tracked_objects:
            # Skip if tracker not initialized
            if obj.tracker is None:
                continue
                
            # Update tracker with new frame
            success, box = obj.tracker.update(frame)
            
            # If tracking successful, update the object's position
            if success:
                # Convert to int and ensure it's a proper bbox format
                x1, y1, w, h = [int(v) for v in box]
                x2, y2 = x1 + w, y1 + h
                obj.box = np.array([x1, y1, x2, y2])
                updated_objects.append(obj)
            # If tracker lost object but it was recently seen, keep it for a while
            elif (self.current_frame_idx - obj.last_seen) < self.track_history:
                updated_objects.append(obj)
        
        # Update the tracked objects list
        self.tracked_objects = updated_objects
        return updated_objects
        
    def _initialize_new_trackers(self, frame, detection_results):
        """Initialize new trackers for newly detected objects."""
        # If no detection results, nothing to initialize
        if detection_results is None or not hasattr(detection_results, 'boxes') or len(detection_results.boxes) == 0:
            return
            
        # Get original frame shape
        h, w = frame.shape[:2]
        
        # Process each detected box
        for i, box in enumerate(detection_results.boxes.data):
            # Parse detection data
            x1, y1, x2, y2, conf, class_id = box.cpu().numpy()
            x1, y1, x2, y2 = map(int, [x1, y1, x2, y2])
            class_id = int(class_id)
            
            # For person class or all classes in debug mode
            if class_id in self.classes_to_detect:
                # Check if this object overlaps with any existing tracked object
                # Simple IoU check to avoid duplicate trackers
                new_box = np.array([x1, y1, x2, y2])
                is_new_object = True
                

                ious = []
                for tracked_obj in self.tracked_objects:
                    iou = self._calculate_iou(new_box, tracked_obj.box)
                    ious.append(iou)
                
                if len(ious) > 0:
                    max_iou = max(ious)
                    if max_iou > 0.3:
                        # Update existing tracker
                        max_iou_idx = ious.index(max_iou)
                        tracked_obj = self.tracked_objects[max_iou_idx]       
                        
                        tracked_obj.box = new_box
                        tracked_obj.confidence = float(conf)
                        tracked_obj.last_seen = self.current_frame_idx
                        
                        # Get mask if available
                        if hasattr(detection_results, 'masks') and detection_results.masks is not None:
                            try:
                                mask = detection_results.masks[i].data.cpu().numpy()
                                tracked_obj.mask = mask
                            except:
                                pass
                        
                        # Reinitialize tracker
                        tracker = self._create_tracker(self.tracker_type)
                        tracker.init(frame, (x1, y1, x2-x1, y2-y1))
                        tracked_obj.tracker = tracker
                        
                        is_new_object = False
                        break

                
                if is_new_object:
                    # Create a new tracker
                    tracker = self._create_tracker(self.tracker_type)
                    tracker.init(frame, (x1, y1, x2-x1, y2-y1))
                    
                    # Get mask if available
                    mask = None
                    if hasattr(detection_results, 'masks') and detection_results.masks is not None:
                        try:
                            mask = detection_results.masks[i].data.cpu().numpy()
                        except:
                            pass
                        
                    # Create new tracked object
                    new_object = TrackedObject(
                        box=new_box,
                        mask=mask,
                        class_id=class_id,
                        confidence=float(conf),
                        tracker=tracker,
                        last_seen=self.current_frame_idx,
                        track_id=self.next_track_id
                    )
                    self.next_track_id += 1
                
                # If it's a new object, create a new tracker
                if is_new_object:
                    # Create a new tracker
                    tracker = self._create_tracker(self.tracker_type)
                    tracker.init(frame, (x1, y1, x2-x1, y2-y1))
                    
                    # Get mask if available
                    mask = None
                    if hasattr(detection_results, 'masks') and detection_results.masks is not None:
                        try:
                            mask = detection_results.masks[i].data.cpu().numpy()
                        except:
                            pass
                    
                    # Create new tracked object
                    new_object = TrackedObject(
                        box=new_box,
                        mask=mask,
                        class_id=class_id,
                        confidence=float(conf),
                        tracker=tracker,
                        last_seen=self.current_frame_idx,
                        track_id=self.next_track_id
                    )
                    self.next_track_id += 1
                    self.tracked_objects.append(new_object)
    
    def _calculate_iou(self, box1, box2):
        """Calculate Intersection over Union between two boxes."""
        # Determine coordinates of intersection box
        x1 = max(box1[0], box2[0])
        y1 = max(box1[1], box2[1])
        x2 = min(box1[2], box2[2])
        y2 = min(box1[3], box2[3])
        
        # Check if boxes overlap
        if x2 < x1 or y2 < y1:
            return 0.0
        
        # Calculate intersection area
        intersection_area = (x2 - x1) * (y2 - y1)
        
        # Calculate area of each box
        box1_area = (box1[2] - box1[0]) * (box1[3] - box1[1])
        box2_area = (box2[2] - box2[0]) * (box2[3] - box2[1])
        
        # Calculate union area
        union_area = box1_area + box2_area - intersection_area
        
        # Return IoU
        return intersection_area / union_area
    
    def detect_humans(self, frame, detect_all_classes=False, debug_mode=False):
        """Detect objects in the frame using YOLO.
        
        Args:
            frame: Input frame
            detect_all_classes: If True, detect all classes; if False, only detect classes in self.classes_to_detect
            debug_mode: If True, show additional debug information
            
        Returns:
            Tuple of (masks, detection_results)
        """
        if frame is None or self.model is None:
            return None, None
        
        
        # Ensure device is valid (only 'cpu' or 'cuda')
        device = self.device if self.device in ['cpu', 'cuda'] else 'cpu'
        
        # Only filter for specific classes if detect_all_classes is False
        classes = None if detect_all_classes else self.classes_to_detect
        
        # Run detection
        results = self.model.predict(
            frame, 
            conf=self.conf_threshold, 
            classes=classes,
            device=device,
            verbose=False
        )
        detection_results = results[0]
        
        # Update tracking if enabled
        if self.use_tracking:
            self._initialize_new_trackers(frame, detection_results)
            self._update_trackers(frame)
            self.last_detection_frame = self.current_frame_idx
        
        # Filter detected objects
        masks = None
        
        # If tracking is enabled and no new detections in this frame,
        # but we have tracked objects, use those instead
        if self.use_tracking and (not hasattr(detection_results, 'boxes') or 
                                    detection_results.boxes is None or 
                                    len(detection_results.boxes) == 0) and self.tracked_objects:
            # Create proxy masks from tracked objects
            h, w = frame.shape[:2]
            person_boxes = []
            masks = []
            
            for obj in self.tracked_objects:
                # Only use person class if not in debug mode and not detect_all_classes
                if not detect_all_classes and not debug_mode and obj.class_id not in self.classes_to_detect:
                    continue
                    
                # If we have the mask from detection, use it
                if obj.mask is not None:
                    masks.append(obj.mask)
                # Otherwise create a box-based mask
                else:
                    x1, y1, x2, y2 = map(int, obj.box)
                    box_mask = np.zeros((h, w), dtype=np.float32)
                    box_mask[y1:y2, x1:x2] = 1.0
                    
                    # Wrap in a torch-like object for compatibility
                    class MaskWrapper:
                        def __init__(self, data):
                            self.data = data
                        def cpu(self):
                            return self
                        def numpy(self):
                            return self.data
                    
                    masks.append(MaskWrapper(box_mask))
                    
                # Add box for bounding box visualization
                person_boxes.append(obj.box)
            
            # If we have tracker masks, return those
            if masks and len(masks) > 0:
                return masks, detection_results
        
        # Handle segmentation and detection models differently
        if hasattr(detection_results, 'masks') and detection_results.masks is not None:
            # For segmentation models like YOLOv8-seg
            if not detect_all_classes and not debug_mode:
                # Only for person class
                person_class_id = -1
                for class_id, class_name in detection_results.names.items():
                    if class_name.lower() == 'person':
                        person_class_id = class_id
                        break
                        
                if person_class_id >= 0:
                    # Filter masks to only include person class
                    person_masks = []
                    for i, box in enumerate(detection_results.boxes):
                        class_id = int(box.cls.item()) if hasattr(box.cls, 'item') else int(box.cls)
                        if class_id == person_class_id:
                            person_masks.append(detection_results.masks[i])
                    
                    # Update masks with filtered person masks
                    masks = person_masks if person_masks else None
                else:
                    masks = None
            else:
                # In debug mode or detect_all_classes, return all masks
                masks = detection_results.masks.data
        else:
            # For detection models without segmentation, create masks from boxes
            # Only for person class in debug mode
            if debug_mode and detection_results.boxes is not None:
                # Create masks only for person boxes
                person_class_id = -1
                for class_id, class_name in detection_results.names.items():
                    if class_name.lower() == 'person':
                        person_class_id = class_id
                        break
                        
                if person_class_id >= 0:
                    # Filter boxes to only include person class
                    person_boxes = []
                    for box in detection_results.boxes:
                        class_id = int(box.cls.item()) if hasattr(box.cls, 'item') else int(box.cls)
                        if class_id == person_class_id:
                            person_boxes.append(box)
                    
                    # Create masks from person boxes if any
                    if person_boxes:
                        masks = self.create_anonymization_mask(person_boxes)
            elif not debug_mode and detection_results.boxes is not None and len(detection_results.boxes) > 0:
                # If no masks but boxes are available in non-debug mode, create masks from all boxes
                masks = self.create_anonymization_mask(detection_results)
            
        return masks, detection_results
    
    def apply_mask_to_frame(self, frame, masks, detection_boxes=None, background=None, blur_strength=21, mode='blur'):
        """
        Apply anonymization to detected humans.
        
        Args:
            frame: Input frame
            masks: Object detection masks
            detection_boxes: Bounding boxes of detections (for adaptive blur)
            background: Background image to use (if available)
            blur_strength: Base strength of blur if using blur mode
            mode: 'blur', 'background', or 'solid'
            
        Returns:
            Processed frame with anonymized humans
        """
        # Simple validity check
        if frame is None or masks is None or len(masks) == 0:
            return frame.copy() if frame is not None else None
            
        # Get frame dimensions
        frame_height, frame_width = frame.shape[:2]
        
        # Create a valid binary mask from the input masks
        try:
            # Initialize empty mask
            combined_mask = np.zeros((frame_height, frame_width), dtype=np.uint8)
            
            # Process each mask and combine them
            mask_processed = False
            for mask in masks:
                try:
                    # Convert tensor to numpy
                    mask_np = mask.cpu().numpy()
                    
                    # Normalize to 0-1 range if needed
                    if mask_np.max() <= 1.0 and mask_np.dtype == np.float32:
                        # Already in 0-1 range, convert to 0-255
                        binary_mask = (mask_np * 255).astype(np.uint8)
                    else:
                        binary_mask = mask_np.astype(np.uint8)
                    
                    # Ensure mask has two dimensions
                    if len(binary_mask.shape) > 2:
                        binary_mask = binary_mask.squeeze()
                        # If still has more than 2 dimensions, take first channel
                        if len(binary_mask.shape) > 2:
                            binary_mask = binary_mask[:, :, 0]
                    
                    # Resize mask to match frame dimensions
                    if binary_mask.shape[0] != frame_height or binary_mask.shape[1] != frame_width:
                        binary_mask = cv2.resize(binary_mask, (frame_width, frame_height))
                    
                    # Calculate the size of the mask (non-zero pixels)
                    mask_size = np.count_nonzero(binary_mask)
                    if mask_size > 0:
                        # Calculate bounding box of the mask
                        y_indices, x_indices = np.where(binary_mask > 0)
                        if len(y_indices) > 0 and len(x_indices) > 0:
                            x_min, y_min = np.min(x_indices), np.min(y_indices)
                            x_max, y_max = np.max(x_indices), np.max(y_indices)
                            
                            # Calculate the mask dimensions
                            width = x_max - x_min
                            height = y_max - y_min
                            
                            # Adaptive kernel size based on mask dimensions
                            mask_dimension = max(width, height)
                            kernel_size = int(mask_dimension * self.dilation_factor)
                            
                            # Apply limits
                            kernel_size = max(self.min_dilation, min(self.max_dilation, kernel_size))
                            
                            # Ensure odd size for kernel
                            if kernel_size % 2 == 0:
                                kernel_size += 1
                        else:
                            # Fallback if we can't determine dimensions
                            kernel_size = self.min_dilation
                    else:
                        # Empty mask, use minimum dilation
                        kernel_size = self.min_dilation
                        
                    # Apply dilation
                    kernel = np.ones((kernel_size, kernel_size), np.uint8)
                    binary_mask = cv2.dilate(binary_mask, kernel, iterations=self.dilation_iterations)
                    
                    # Add to combined mask
                    combined_mask = cv2.bitwise_or(combined_mask, binary_mask)
                    mask_processed = True
                except Exception as e:
                    print(f"Warning: Error processing individual mask: {e}")
                    continue
            
            # If no masks were successfully processed, return original frame
            if not mask_processed:
                return frame.copy()
                
        except Exception as e:
            print(f"Warning: Error creating combined mask: {e}")
            return frame.copy()
        
        # Now apply the anonymization method based on mode
        if mode == 'blur':
            try:
                # Apply adaptive blur based on detection size if boxes are provided
                if detection_boxes is not None and len(detection_boxes) > 0:
                    # Create a copy for the result
                    result = frame.copy()
                    
                    # Get connected components from the mask for separate processing
                    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(combined_mask, connectivity=8)
                    
                    # Process each connected component (skip the background which is label 0)
                    for i in range(1, num_labels):
                        # Get the area of this component
                        area = stats[i, cv2.CC_STAT_AREA]
                        # Make a mask for just this component
                        component_mask = (labels == i).astype(np.uint8) * 255
                        
                        # Determine blur size based on component size
                        # Minimum blur is blur_strength, scaling up for larger detections
                        # Calculate as percentage of frame area
                        relative_size = area / (frame_height * frame_width)
                        adaptive_blur = max(blur_strength, 
                                          int(blur_strength * (1 + 3 * relative_size)))
                        # Make sure blur kernel is odd
                        if adaptive_blur % 2 == 0:
                            adaptive_blur += 1
                            
                        # Apply blur to this region
                        component_mask_3ch = cv2.cvtColor(component_mask, cv2.COLOR_GRAY2BGR) / 255.0
                        blurred_region = cv2.GaussianBlur(frame, (adaptive_blur, adaptive_blur), 0)
                        # Blend based on this component mask
                        result = result * (1 - component_mask_3ch) + blurred_region * component_mask_3ch
                    
                    return result.astype(np.uint8)
                else:
                    # Fallback to standard blur if no boxes provided
                    blurred = cv2.GaussianBlur(frame, (blur_strength, blur_strength), 0)
                    # Create a floating point mask for alpha blending
                    mask_float = combined_mask.astype(np.float32) / 255.0
                    # Expand mask to 3 channels
                    mask_float = cv2.merge([mask_float, mask_float, mask_float])
                    
                    # Alpha blend the original and blurred frames
                    blurred_part = blurred * mask_float
                    orig_part = frame * (1 - mask_float)
                    result = blurred_part + orig_part
                    return result.astype(np.uint8)
            except Exception as e:
                print(f"Warning: Error applying adaptive blur mode: {e}")
                # Fall back to simple blur
                return cv2.GaussianBlur(frame, (blur_strength, blur_strength), 0)
                
        elif mode == 'background' and background is not None:
            try:
                # Debug background dimensions
                #print(f"Background shape: {background.shape}, Frame shape: {frame.shape}")
                
                # Ensure background is valid and properly sized
                if len(background.shape) != 3 or background.shape[2] != 3:
                    print(f"Warning: Invalid background format: {background.shape}")
                    return self.apply_mask_to_frame(frame, masks, None, blur_strength, 'blur')
                    
                if background.shape[:2] != (frame_height, frame_width):
                    print(f"Resizing background from {background.shape[:2]} to {(frame_width, frame_height)}")
                    background = cv2.resize(background, (frame_width, frame_height))
                
                # Create masks for foreground (humans) and background
                human_mask = combined_mask
                human_mask_inv = cv2.bitwise_not(human_mask)
                
                # Alpha blending approach (more robust than bitwise operations)
                # Convert masks to float32 and normalize to 0-1 range
                mask_float = human_mask.astype(np.float32) / 255.0
                mask_inv_float = human_mask_inv.astype(np.float32) / 255.0
                
                # Expand to 3 channels
                mask_float = np.repeat(mask_float[:, :, np.newaxis], 3, axis=2)
                mask_inv_float = np.repeat(mask_inv_float[:, :, np.newaxis], 3, axis=2)
                
                # Blend frame and background using masks
                result = (frame * mask_inv_float) + (background * mask_float)
                return result.astype(np.uint8)
            except Exception as e:
                print(f"Warning: Error applying background mode: {e}")
                # Fall back to blur mode
                return self.apply_mask_to_frame(frame, masks, None, blur_strength, 'blur')
                
        elif mode == 'solid':
            try:
                # Create a solid gray color image
                solid_color = np.ones_like(frame) * 127
                
                # Create masks for foreground (humans) and background
                human_mask = combined_mask
                human_mask_inv = cv2.bitwise_not(human_mask)
                
                # Expand masks to 3 channels for use with color images
                human_mask_3ch = cv2.cvtColor(human_mask, cv2.COLOR_GRAY2BGR)
                human_mask_inv_3ch = cv2.cvtColor(human_mask_inv, cv2.COLOR_GRAY2BGR)
                
                # Extract the humans from original frame and replace with solid color
                frame_bg = cv2.bitwise_and(frame, human_mask_inv_3ch)
                solid_fg = cv2.bitwise_and(solid_color, human_mask_3ch)
                
                # Combine both parts
                result = cv2.add(frame_bg, solid_fg)
                return result
            except Exception as e:
                print(f"Warning: Error applying solid mode: {e}")
                # Fall back to blur mode
                return self.apply_mask_to_frame(frame, masks, None, blur_strength, 'blur')
        
        # If we get here, something went wrong with the mode selection
        return frame.copy()
    
    def create_combined_mask(self, frame, masks):
        """
        Create a combined mask from multiple segmentation masks.
        
        Args:
            frame: Input frame
            masks: Individual segmentation masks from YOLO
            
        Returns:
            A combined binary mask
        """
        # Get frame dimensions
        frame_height, frame_width = frame.shape[:2]
        
        try:
            # Initialize empty mask
            combined_mask = np.zeros((frame_height, frame_width), dtype=np.uint8)
            
            # Process each mask and combine them
            mask_processed = False
            for mask in masks:
                try:
                    # Convert tensor to numpy
                    mask_np = mask.cpu().numpy()
                    
                    # Normalize to 0-1 range if needed
                    if mask_np.max() <= 1.0 and mask_np.dtype == np.float32:
                        # Already in 0-1 range, convert to 0-255
                        binary_mask = (mask_np * 255).astype(np.uint8)
                    else:
                        binary_mask = mask_np.astype(np.uint8)
                    
                    # Ensure mask has two dimensions
                    if len(binary_mask.shape) > 2:
                        binary_mask = binary_mask.squeeze()
                        # If still has more than 2 dimensions, take first channel
                        if len(binary_mask.shape) > 2:
                            binary_mask = binary_mask[:, :, 0]
                    
                    # Resize mask to match frame dimensions
                    if binary_mask.shape[0] != frame_height or binary_mask.shape[1] != frame_width:
                        binary_mask = cv2.resize(binary_mask, (frame_width, frame_height))
                    
                    # Calculate the size of the mask (non-zero pixels)
                    mask_size = np.count_nonzero(binary_mask)
                    if mask_size > 0:
                        # Calculate bounding box of the mask
                        y_indices, x_indices = np.where(binary_mask > 0)
                        if len(y_indices) > 0 and len(x_indices) > 0:
                            x_min, y_min = np.min(x_indices), np.min(y_indices)
                            x_max, y_max = np.max(x_indices), np.max(y_indices)
                            
                            # Calculate the mask dimensions
                            width = x_max - x_min
                            height = y_max - y_min
                            
                            # Adaptive kernel size based on mask dimensions
                            mask_dimension = max(width, height)
                            kernel_size = int(mask_dimension * self.dilation_factor)
                            
                            # Apply limits
                            kernel_size = max(self.min_dilation, min(self.max_dilation, kernel_size))
                            
                            # Ensure odd size for kernel
                            if kernel_size % 2 == 0:
                                kernel_size += 1
                        else:
                            # Fallback if we can't determine dimensions
                            kernel_size = self.min_dilation
                    else:
                        # Empty mask, use minimum dilation
                        kernel_size = self.min_dilation
                        
                    # Apply dilation
                    kernel = np.ones((kernel_size, kernel_size), np.uint8)
                    binary_mask = cv2.dilate(binary_mask, kernel, iterations=self.dilation_iterations)
                    
                    # Add to combined mask
                    combined_mask = cv2.bitwise_or(combined_mask, binary_mask)
                    mask_processed = True
                except Exception as e:
                    print(f"Warning: Error processing individual mask: {e}")
                    continue
            
            # If no masks were successfully processed, return empty mask
            if not mask_processed:
                return np.zeros((frame_height, frame_width), dtype=np.uint8)
                
            return combined_mask
                
        except Exception as e:
            print(f"Warning: Error creating combined mask: {e}")
            return np.zeros((frame_height, frame_width), dtype=np.uint8)
        
    def create_anonymization_mask(self, detection_results):
        """Convert YOLO detection results to masks."""
        if detection_results.masks is not None and len(detection_results.masks) > 0:
            return detection_results.masks.data
        
        # If using detection model without masks, create masks from boxes
        masks = []
        if detection_results.boxes is not None and len(detection_results.boxes) > 0:
            for box in detection_results.boxes.data:
                if int(box[5]) in self.classes_to_detect:  # Check if class is person
                    try:
                        # Convert coordinates to integers, ensuring they're within bounds
                        h, w = detection_results.orig_shape
                        x1, y1, x2, y2 = map(int, box[:4])
                        x1, y1 = max(0, x1), max(0, y1)
                        x2, y2 = min(w, x2), min(h, y2)
                        
                        # Create a tensor-like object similar to what YOLO segmentation would produce
                        mask = np.zeros((h, w), dtype=np.float32)
                        mask[y1:y2, x1:x2] = 1.0  # Set bounding box region to 1.0
                        
                        # Calculate mask dimensions from box
                        box_width = x2 - x1
                        box_height = y2 - y1
                        mask_dimension = max(box_width, box_height)
                        
                        # Calculate adaptive kernel size based on box dimensions
                        kernel_size = int(mask_dimension * self.dilation_factor)
                        
                        # Apply limits
                        kernel_size = max(self.min_dilation, min(self.max_dilation, kernel_size))
                        
                        # Ensure odd size for kernel
                        if kernel_size % 2 == 0:
                            kernel_size += 1
                        
                        # Apply dilation
                        kernel = np.ones((kernel_size, kernel_size), np.uint8)
                        mask = cv2.dilate(mask.astype(np.uint8), kernel, iterations=self.dilation_iterations).astype(np.float32)
                        
                        # Wrap in a torch-like object with cpu() method for compatibility
                        class TensorWrapper:
                            def __init__(self, data):
                                self.data = data
                            def cpu(self):
                                return self
                            def numpy(self):
                                return self.data
                        
                        masks.append(TensorWrapper(mask))
                    except Exception as e:
                        print(f"Warning: Error creating mask from box: {e}")
                        continue
        
        return masks
        
    def process_frame(self, frame, anonymization_mode='blur', blur_strength=21, return_details=False, return_original=False, debug_mode=False):
        """
        Process a frame to anonymize humans.
        
        Args:
            frame: Input video frame
            anonymization_mode: 'blur', 'background', or 'solid'
            blur_strength: Strength of the blur if using blur mode
            
        Returns:
            Processed frame with anonymized humans
        """
        if frame is None:
            return None
            
        # Create a copy of the original frame
        original = frame.copy()
        
        # Detect objects using YOLO and update trackers
        detection_boxes = None
        detection_results = None
        all_class_results = None
        
        # First get detection results
        masks, detection_results = self.detect_humans(frame, detect_all_classes=debug_mode)
        
        # Store the full results for debug mode if needed
        if debug_mode:
            all_class_results = detection_results
        
        # Update trackers with the new frame
        tracked_objects = self._update_trackers(frame)
        
        # Combine detection and tracking results for anonymization
        if detection_results is not None and hasattr(detection_results, 'boxes') and detection_results.boxes is not None:
            # Get the person class ID (usually 0 in COCO dataset used by YOLO)
            person_class_id = -1
            for class_id, class_name in detection_results.names.items():
                if class_name.lower() == 'person':
                    person_class_id = class_id
                    break
            
            # Initialize lists for combined results
            combined_boxes = []
            combined_masks = []
            
            # Add detection results
            if person_class_id >= 0 and hasattr(detection_results, 'masks') and detection_results.masks is not None:
                for i, box in enumerate(detection_results.boxes):
                    class_id = int(box.cls.item()) if hasattr(box.cls, 'item') else int(box.cls)
                    if class_id == person_class_id and i < len(detection_results.masks):
                        mask = detection_results.masks.data[i]
                        # Ensure mask is 2D
                        if len(mask.shape) > 2:
                            mask = mask.squeeze()
                        combined_boxes.append(box)
                        combined_masks.append(mask)
            
            # Add tracking results
            if tracked_objects:
                for obj in tracked_objects:
                    if obj.mask is not None:
                        # Convert tracking mask to torch tensor format if needed
                        if isinstance(obj.mask, np.ndarray):
                            mask = torch.from_numpy(obj.mask)
                        else:
                            mask = obj.mask
                        # Ensure mask is 2D
                        if len(mask.shape) > 2:
                            mask = mask.squeeze()
                        combined_masks.append(mask)
                    else:
                        # Create a mask from the tracking box if no mask is available
                        x1, y1, x2, y2 = map(int, obj.box)
                        h, w = frame.shape[:2]
                        mask = np.zeros((h, w), dtype=np.float32)
                        mask[y1:y2, x1:x2] = 1.0
                        combined_masks.append(torch.from_numpy(mask))
            
            # Use combined results for anonymization
            if combined_masks:
                # Ensure all masks are 2D and on the same device
                combined_masks = [mask.squeeze() if len(mask.shape) > 2 else mask for mask in combined_masks]
                device = 'cuda' if torch.cuda.is_available() else 'cpu'
                combined_masks = [mask.to(device) for mask in combined_masks]
                masks = torch.stack(combined_masks) if len(combined_masks) > 0 else None
            else:
                masks = None
        
        # If we haven't set masks yet or if masks is empty, try to create from detection results
        if masks is None or (isinstance(masks, list) and len(masks) == 0):
            if detection_results.masks is not None and not debug_mode:
                # In non-debug mode, use all masks (should only be persons)
                masks = detection_results.masks.data
            if masks is None or len(masks) == 0:
                # Fallback to bounding boxes if segmentation masks are empty
                # Only for person class in debug mode
                if debug_mode and detection_results.boxes is not None:
                    # Create masks only for person boxes
                    person_class_id = -1
                    for class_id, class_name in detection_results.names.items():
                        if class_name.lower() == 'person':
                            person_class_id = class_id
                            break
                            
                    if person_class_id >= 0:
                        # Filter boxes to only include person class
                        person_boxes = []
                        for box in detection_results.boxes:
                            class_id = int(box.cls.item()) if hasattr(box.cls, 'item') else int(box.cls)
                            if class_id == person_class_id:
                                person_boxes.append(box)
                        
                        # Create masks from person boxes if any
                        if person_boxes:
                            masks = self.create_anonymization_mask(person_boxes)
                elif not debug_mode and detection_results.boxes is not None and len(detection_results.boxes) > 0:
                    # If no masks but boxes are available in non-debug mode, create masks from all boxes
                    masks = self.create_anonymization_mask(detection_results)
        
        # Extract bounding boxes for adaptive blur
        if detection_results.boxes is not None and len(detection_results.boxes) > 0:
            detection_boxes = detection_results.boxes
        
        
        # Always maintain our background model regardless of warmup status
        if self.background is None:
            self.background = frame.copy()
            
        # Process based on warmup status
        if self.frame_count < self.warmup_frames:
            # During warmup - we need to ensure people don't contaminate the background model
            
            # First, create a clean frame with no people
            if masks is not None and len(masks) > 0:
                try:
                    # Create a combined mask of all people and add a generous border
                    combined_mask = self.create_combined_mask(frame, masks)
                    
                    # For warmup, calculate mask size statistics
                    mask_pixels = np.count_nonzero(combined_mask)
                    if mask_pixels > 0:
                        # Find mask boundaries
                        y_indices, x_indices = np.where(combined_mask > 0)
                        if len(y_indices) > 0 and len(x_indices) > 0:
                            x_min, y_min = np.min(x_indices), np.min(y_indices)
                            x_max, y_max = np.max(x_indices), np.max(y_indices)
                            
                            # Calculate mask dimensions
                            width = x_max - x_min
                            height = y_max - y_min
                            
                            # Use larger dilation factor during warmup
                            mask_dimension = max(width, height)
                            kernel_size = int(mask_dimension * self.warmup_dilation_factor)
                            
                            # Apply limits but allow larger maximum for warmup
                            warmup_max = self.max_dilation * 2
                            kernel_size = max(self.min_dilation, min(warmup_max, kernel_size))
                            
                            # Ensure odd size
                            if kernel_size % 2 == 0:
                                kernel_size += 1
                        else:
                            # Fallback
                            kernel_size = max(15, min(frame.shape[1], frame.shape[0]) // 50)
                    else:
                        # Default
                        kernel_size = max(15, min(frame.shape[1], frame.shape[0]) // 50)
                    
                    # Dilate mask with warmup settings
                    kernel = np.ones((kernel_size, kernel_size), np.uint8)
                    combined_mask = cv2.dilate(combined_mask, kernel, iterations=2)
                    
                    # Get statistics from non-masked regions (the surroundings)
                    inverted_mask = cv2.bitwise_not(combined_mask)
                    
                    # If most of the frame is masked (more than 80%), use a default color
                    masked_ratio = np.count_nonzero(combined_mask) / (combined_mask.shape[0] * combined_mask.shape[1])
                    
                    if masked_ratio > 0.8:  # Too much is masked, use a safe default
                        mean_color = (120, 120, 120)  # Mid-gray as safe default
                    else:
                        # Calculate mean color of non-masked areas (surroundings)
                        mean_color = cv2.mean(frame, mask=inverted_mask)[:3]  # Get BGR mean color
                    
                    # Create a solid color frame filled with the mean color
                    solid_color_frame = np.ones_like(frame) * mean_color
                    
                    # For warmup, we completely replace masked areas with solid color
                    clean_frame = frame.copy()
                    mask_3ch = cv2.cvtColor(combined_mask, cv2.COLOR_GRAY2BGR) / 255.0
                    clean_frame = clean_frame * (1 - mask_3ch) + solid_color_frame * mask_3ch
                    
                    # Define blur kernel sizes
                    def make_valid_kernel_size(size):
                        size = max(3, size)  # At least 3
                        return size if size % 2 == 1 else size + 1  # Ensure odd
                    
                    # Apply a light blur to smooth the transitions
                    blur_size = make_valid_kernel_size(21)  # Fixed medium blur size
                    clean_frame = cv2.GaussianBlur(clean_frame.astype(np.uint8), 
                                                (blur_size, blur_size), 0)
                    
                    # Use this clean frame to update the background model
                    try:
                        # Force a high learning rate for quick adaptation during warmup
                        forced_learning_rate = 0.5  # Higher rate for faster background init
                        self.bg_subtractor.apply(clean_frame, learningRate=forced_learning_rate)
                        
                        # Always update our manual background during warmup
                        self.background = clean_frame.copy()
                    except Exception as e:
                        print(f"Warning: Error updating background model: {e}")
                    
                    # For display, we want to show what's happening with the background model
                    # Show the clean frame that's being used for the background model
                    anonymized_frame = clean_frame.copy()
                    
                    # Draw a border on the anonymized_frame to indicate we're in warmup mode
                    border_thickness = 10
                    h, w = anonymized_frame.shape[:2]
                    # Draw a red border to indicate warmup
                    cv2.rectangle(anonymized_frame, (0, 0), (w, h), (0, 0, 255), border_thickness)
                except Exception as e:
                    print(f"Warning: Error during warmup processing: {e}")
                    anonymized_frame = cv2.GaussianBlur(frame, (blur_strength, blur_strength), 0)
            else:
                # No humans detected during warmup - safe to update background directly
                try:
                    # During warmup, use -1 learning rate for fastest adaptation
                    self.bg_subtractor.apply(frame, learningRate=-1)
                    if self.frame_count % 5 == 0:  # Update every 5 frames
                        self.background = frame.copy()
                except Exception as e:
                    print(f"Warning: Error updating background model: {e}")
                    
                # No anonymization needed
                anonymized_frame = original
        else:
            # After warmup period
            if masks is not None and len(masks) > 0:  # We detected humans
                try:
                    # Use our internal background model (more reliable than bg subtractor)
                    background = self.background
                    
                    # Create a clean frame where humans are replaced with background
                    clean_frame = self.apply_mask_to_frame(
                        frame, masks, detection_boxes, background, blur_strength, 'background'
                    )
                    
                    # Use the clean_frame (where people are completely replaced with background)
                    # to update the background model - this ensures no human elements contaminate the model
                    learning_rate = self.bg_params.get('learning_rate', 0.01)
                    
                    # Update background subtractor with the clean frame (people replaced with background)
                    try:
                        self.bg_subtractor.apply(clean_frame, learningRate=learning_rate)
                        
                        # Also update our manual background model periodically
                        if self.frame_count % 10 == 0:  # Every 10 frames
                            alpha = min(learning_rate * 2, 0.2)  # Control update speed
                            self.background = cv2.addWeighted(
                                self.background, 1-alpha,
                                clean_frame, alpha,
                                0
                            )
                    except Exception as e:
                        print(f"Warning: Error updating bg subtractor: {e}")
                    
                    # Anonymize original frame based on the selected mode
                    if anonymization_mode == 'background':
                        anonymized_frame = clean_frame  # Already anonymized
                    else:
                        anonymized_frame = self.apply_mask_to_frame(
                            frame, masks, detection_boxes, None, blur_strength, anonymization_mode
                        )
                        
                    # Save the last clean frame for future use
                    self.last_clean_frame = clean_frame
                except Exception as e:
                    print(f"Warning: Error in anonymization pipeline: {e}")
                    # Fallback to simple blur if advanced processing fails
                    try:
                        anonymized_frame = self.apply_mask_to_frame(
                            frame, masks, detection_boxes, None, blur_strength, 'blur'
                        )
                    except Exception:
                        anonymized_frame = original
            else:
                # No humans detected - update our background model and subtractor
                try:
                    # Get learning rate from bg_params if available
                    learning_rate = self.bg_params.get('learning_rate', 0.01)
                    self.bg_subtractor.apply(frame, learningRate=learning_rate)
                    
                    # Slowly update our background model (more stable)
                    if self.frame_count % 10 == 0:  # Update every 10 frames
                        # Blend current frame into background
                        alpha = min(learning_rate * 10, 0.1)  # Small alpha for smooth updates
                        self.background = cv2.addWeighted(
                            self.background, 1-alpha,
                            frame, alpha,
                            0
                        )
                except Exception as e:
                    print(f"Warning: Error updating background model: {e}")
                
                anonymized_frame = original
                
        # Save background for methods that don't support getBackgroundImage
        if not hasattr(self.bg_subtractor, 'getBackgroundImage'):
            try:
                fg_mask = self.bg_subtractor.apply(frame.copy(), learningRate=0)
                if self.background is None:
                    self.background = frame.copy()
                else:
                    # Update background only in areas without foreground
                    mask_inv = cv2.bitwise_not(fg_mask)
                    mask_inv_rgb = cv2.cvtColor(mask_inv, cv2.COLOR_GRAY2BGR)
                    
                    # Get learning rate from bg_params if available
                    learning_rate = self.bg_params.get('learning_rate', 0.01)
                    alpha = min(learning_rate * 5, 0.05)  # Limit max rate but use parameter
                    
                    # Slowly update background in non-foreground regions
                    bg_update = cv2.bitwise_and(frame, mask_inv_rgb)
                    self.background = cv2.addWeighted(
                        self.background, 1 - alpha,
                        bg_update, alpha,
                        0
                    )
            except Exception as e:
                print(f"Warning: Error maintaining custom background: {e}")
                if self.background is None:
                    self.background = frame.copy()
        
        self.frame_count += 1
        
        # Return additional details if requested
        if return_details:
            if return_original:
                # If in debug mode, return all class detections
                results_to_return = all_class_results if debug_mode else detection_results
                return anonymized_frame, results_to_return, masks, original
            else:
                results_to_return = all_class_results if debug_mode else detection_results
                return anonymized_frame, results_to_return, masks
        else:
            return anonymized_frame
    
    def draw_detection_overlay(self, frame, detection_results):
        """Draw detection boxes and labels for visualization."""
        result = frame.copy()
        
        if detection_results.boxes is not None:
            for box in detection_results.boxes.data:
                x1, y1, x2, y2, conf, cls = box
                if cls in self.classes_to_detect and conf >= self.conf_threshold:
                    # Convert coordinates to integers
                    x1, y1, x2, y2 = map(int, [x1, y1, x2, y2])
                    
                    # Draw bounding box
                    cv2.rectangle(result, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    
                    # Draw label
                    label = f"Person: {conf:.2f}"
                    cv2.putText(result, label, (x1, y1 - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
                                
        return result
    
    def reset(self):
        """Reset the background subtractor and counters."""
        self.bg_subtractor = self._create_bg_subtractor(self.bg_subtractor_method)
        self.background = None
        self.frame_count = 0
