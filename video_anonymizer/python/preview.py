#!/usr/bin/env python3
"""
Preview class for AI detection visualization

This module provides a simple visualization for AI model detection results,
showing object classes, confidence scores, and segmentation masks.
"""

import cv2
import numpy as np

class Preview:
    """
    Display class for handling AI model detection visualizations.
    
    This class provides methods to show all detected objects with their
    class names, confidence scores, and segmentation masks in a window.
    """
    def __init__(self, window_name="AI Model Debug"):
        """
        Initialize the Preview display.
        
        Args:
            window_name: Name of the display window
        """
        self.window_name = window_name
        self.active = False
        self.font = cv2.FONT_HERSHEY_SIMPLEX
        self.font_scale = 0.5
        self.line_thickness = 2
        self.text_color = (255, 255, 255)  # White
        self.quit_requested = False  # Track if quit was requested

    def initialize(self):
        """Create the display window
        
        Uses default window creation settings to match other windows in the application.
        """
        cv2.namedWindow(self.window_name)
        self.active = True

    def close(self):
        """Close the display window"""
        if self.active:
            cv2.destroyWindow(self.window_name)
            self.active = False

    def check_for_quit(self):
        """Check if the user has requested to quit via keyboard
        
        Detects pressing 'q' or ESC key to exit the application.
        
        Returns:
            bool: True if the app should continue, False if quit was requested
        """
        if self.quit_requested:
            return False
            
        # Process window events and check for quit keys (q, ESC)
        key = cv2.waitKey(1) & 0xFF
        if key == 27 or key == ord('q'):  # ESC key or 'q'
            self.quit_requested = True
            return False
            
        return True
        
    def update(self, frame):
        """
        Update the display with a new frame.
        
        Args:
            frame: The image frame to display
            
        Returns:
            True if the app should continue, False if quit was requested
        """
        if frame is None or self.quit_requested:
            return False
            
        # Initialize window if needed
        if not self.active:
            self.initialize()
        
        try:
            # Add title with instructions
            cv2.putText(frame, "Press 'q' or 'ESC' to quit", 
                      (frame.shape[1] - 250, 30), self.font, self.font_scale, 
                      self.text_color, self.line_thickness)
            
            # Display the frame
            cv2.imshow(self.window_name, frame)
            
            # Check for quit events
            return self.check_for_quit()
        except Exception as e:
            print(f"Warning: Error updating preview window: {e}")
            return False

    def visualize_detections(self, original_frame, results, class_names=None):
        """Create visualization of objects detected in the frame
        
        Creates an overlay showing all detected objects with masks, class names,
        confidence scores, and borders.
        
        Args:
            original_frame: The original video frame
            results: Detection results from the AI model
            class_names: Dictionary mapping class ids to names
            
        Returns:
            ndarray: Visualization frame with detection overlays
        """
        # Use the original frame as the background
        vis_frame = original_frame.copy()
        
        # If no results or no masks, just return the original frame
        if results is None or not hasattr(results, 'masks') or results.masks is None:
            return vis_frame
            
        # Get all masks from results
        all_masks = results.masks.data
        
        # Process each mask
        for i, mask in enumerate(all_masks):
            if mask is None:
                continue
                
            # Convert mask to numpy array if it's a tensor
            mask_np = mask.cpu().numpy() if hasattr(mask, 'cpu') else mask
            
            # Resize mask if needed
            if mask_np.shape[:2] != original_frame.shape[:2]:
                mask_np = cv2.resize(mask_np, (original_frame.shape[1], original_frame.shape[0]))
            
            # Extract detection information
            class_id, class_name, confidence = self._get_detection_info(results, i, class_names)
            
            # Get color based on class ID
            color = self.get_color_for_class(class_id)
            
            # Apply colored mask overlay with transparency
            mask_8bit = (mask_np * 255).astype(np.uint8)
            overlay = np.zeros_like(original_frame)
            overlay[mask_np > 0] = color
            vis_frame = cv2.addWeighted(vis_frame, 1.0, overlay, 0.4, 0)  # 40% opacity
            
            # Draw white border around the mask
            contours, _ = cv2.findContours(mask_8bit, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            cv2.drawContours(vis_frame, contours, -1, (255, 255, 255), 2)
            
            # Add text label with class and confidence
            self._add_label(vis_frame, mask_8bit, results, i, class_name, confidence)
        
        return vis_frame
    
    def _get_detection_info(self, results, index, class_names):
        """Extract class ID, name and confidence for a detection
        
        Args:
            results: Detection results from the AI model
            index: Index of the detection
            class_names: Dictionary mapping class ids to names
            
        Returns:
            tuple: (class_id, class_name, confidence)
        """
        class_id = None
        class_name = f"Object {index+1}"
        confidence = None
        
        # Try to get class ID and confidence
        if hasattr(results, 'boxes') and results.boxes is not None and index < len(results.boxes):
            try:
                box = results.boxes[index]
                class_id = int(box.cls.item()) if hasattr(box.cls, 'item') else int(box.cls)
                
                # Get class name if available
                if class_names is not None and class_id in class_names:
                    class_name = class_names[class_id]
                
                # Get confidence
                confidence = float(box.conf.item()) if hasattr(box.conf, 'item') else float(box.conf)
            except:
                pass
                
        return class_id, class_name, confidence
    
    def _add_label(self, frame, mask, results, index, class_name, confidence):
        """Add a text label to the visualization
        
        Args:
            frame: The frame to add the label to
            mask: The object mask
            results: Detection results
            index: Detection index
            class_name: Name of the object class
            confidence: Detection confidence score
        """
        # Find position for the label
        label_pos = self._get_label_position(mask, results, index)
        if label_pos is None:
            return
            
        cx, cy = label_pos
        
        # Format label text
        confidence_text = f"{confidence:.2f}" if confidence is not None else ""
        label_text = f"{class_name} {confidence_text}"
        
        # Add dark background for text
        text_size, _ = cv2.getTextSize(label_text, self.font, self.font_scale, self.line_thickness)
        text_w, text_h = text_size
        cv2.rectangle(frame, 
                     (cx - text_w // 2 - 5, cy - text_h - 5), 
                     (cx + text_w // 2 + 5, cy + 5), 
                     (0, 0, 0), -1)  # Black background
        
        # Add text with class name and confidence
        cv2.putText(frame, label_text, 
                   (cx - text_w // 2, cy), 
                   self.font, self.font_scale, 
                   self.text_color, self.line_thickness)
    
    def _get_label_position(self, mask, results, index):
        """Calculate position for the label
        
        Args:
            mask: The object mask
            results: Detection results
            index: Detection index
            
        Returns:
            tuple or None: (x, y) coordinates for the label
        """
        # Try using mask centroid (most accurate)
        M = cv2.moments(mask)
        if M["m00"] > 0:
            cx = int(M["m10"] / M["m00"])
            cy = int(M["m01"] / M["m00"])
            return cx, cy
            
        # Fallback to bounding box center
        try:
            if hasattr(results, 'boxes') and index < len(results.boxes):
                box = results.boxes[index].xyxy.cpu().numpy()[0] if hasattr(results.boxes[index].xyxy, 'cpu') else results.boxes[index].xyxy
                cx = int((box[0] + box[2]) / 2)
                cy = int((box[1] + box[3]) / 2)
                return cx, cy
        except:
            pass
            
        return None
    def get_color_for_class(self, class_id):
        """Generate a color for a given class ID"""
        if class_id is None:
            return (200, 200, 200)  # Default gray
            
        # Use HSV color space for better visual distinction
        hue = (class_id * 30) % 180  # Rotate through hues, 30° steps
        color = cv2.cvtColor(np.uint8([[[hue, 200, 255]]]), cv2.COLOR_HSV2BGR)[0][0]
        return tuple(map(int, color))
