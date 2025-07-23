# reCamera Edge AI Privacy Project

A comprehensive privacy-focused edge AI project for the [reCamera SG200X](https://github.com/Seeed-Studio/OSHW-reCamera-Series) device, featuring HomeAssistant integration and real-time video anonymization. This project demonstrates two complementary approaches to privacy-preserving computer vision on edge devices.

## Project Overview

The reCamera SG200X is an open-source AI camera powered by a RISC-V SoC, delivering on-device 1 TOPS AI performance with 5MP video processing capabilities. This project leverages the reCamera's edge computing capabilities to provide privacy-preserving computer vision solutions without requiring data transmission to external servers.

The project addresses privacy concerns in smart home and monitoring applications through two distinct approaches:

### HomeAssistant Integration
**Location**: [`homeassistant_integration/`](./homeassistant_integration/)

A Node-RED based solution that transforms reCamera into a smart home sensor for HomeAssistant, providing room occupancy and object detection information **without transmitting images**, ensuring complete visual privacy.

**Key Features:**
- Privacy-first: Only metadata transmitted, no images
- Real-time object detection using YOLOv11
- MQTT-based HomeAssistant device discovery
- Comprehensive sensor suite (FPS, object counts, processing delays)
- Object tracking with temporal persistence and confidence filtering
- Standalone testing framework for tracking algorithm validation and tuning

**Technical Implementation:**
- Node-RED flow with SSCMA nodes for AI processing
- Object tracking with confidence-based activation
- MQTT communication with HomeAssistant device discovery protocol


### Video Anonymizer
**Location**: [`video_anonymizer/`](./video_anonymizer/)

A multi-platform video anonymization system that detects and anonymizes people in video streams while preserving visual content for security, home automation or other monitoring purposes. The main idea is to allow the use of more powerful AI models for processing the video stream while keeping the visual content private. This way, users can leverage the full power of commercial cloud AI services without exposing sensitive visual content, as these external services typically require transmitting raw images or video data to their servers where it may be stored, analyzed, or used for model training, raising significant privacy and data sovereignty concerns.

**Key Features:**
- **Privacy-Preserving Monitoring**: Enables use of cloud AI services without exposing sensitive visual content
- **Multi-Platform Support**: Python (development), C++ (production), and reCamera embedded implementations
- **Real-Time Processing**: Live anonymization with RTSP streaming capability
- **Multiple Anonymization Methods**: Blur, background replacement, and solid fill options
- **Edge Deployment**: TPU-accelerated processing on reCamera devices

**Technical Implementation:**
- YOLOv11 segmentation for human detection across all platforms
- OpenCV background subtraction algorithms for background replacement
- H.264 RTSP streaming for reCamera deployment
- GPU/TPU acceleration support for performance optimization


## Implementation Overview and Comparison

This project provides two complementary approaches to privacy-preserving computer vision, each with distinct characteristics and use cases:

### HomeAssistant Integration
**Privacy Strategy**: Complete visual privacy through metadata-only transmission
- **Data Protection**: Zero image transmission - only object detection metadata (counts, labels, timing)
- **Processing**: Local YOLOv11n inference with advanced multi-stage object tracking
- **Communication**: MQTT with automatic HomeAssistant device discovery
- **Performance**: ~10 FPS on reCamera (optimized for embedded deployment)
- **AI Capabilities**: Limited to lightweight models (YOLOv11n) due to hardware constraints
- **Security**: MQTT authentication and encryption supported
- **Use Cases**: Smart home automation, room occupancy sensing, privacy-critical applications
- **Advantages**: Complete visual privacy, stable object counts, seamless smart home integration
- **Limitations**: Metadata-only output, constrained by reCamera performance and small model capabilities

### Video Anonymizer
**Privacy Strategy**: Visual anonymization while preserving video context
- **Data Protection**: Real-time human anonymization using background replacement, blur, or solid fill.
- **Processing**: YOLOv11 segmentation with OpenCV background modeling algorithms
- **Communication**: H.264 RTSP streaming (reCamera) or local display/file output
- **Performance**: 
  - Python/C++ (Desktop): High FPS with GPU acceleration
  - reCamera Embedded: ~1 FPS. reCamera performance is constrained by its single-core RISC-V CPU (Sophgo SG2002 SoC). The CPU is used for image processing and the OpenCV implementation is still not optimized for this architecture.
- **AI Capabilities**: Anonymized streams can be processed by state-of-the-art cloud AI models
- **Security**: RTSP password protection, local processing prevents cloud data exposure
- **Use Cases**: Security monitoring, cloud AI integration, visual content preservation
- **Advantages**: Preserves visual context, enables powerful cloud AI processing, multi-platform support
- **Limitations**: Higher computational overhead, requires visual processing pipeline. Anonymization quality is not always perfect, people may be partially visible in some frames.


## Project Structure

```
reCamera-Edge-AI-Privacy/
├── homeassistant_integration/          # Node-RED smart home integration
│   ├── flows.json                      # Main Node-RED flow configuration
│   ├── README.md                       # Integration setup and dashboard templates
│   └── simple-tracker-tests/          # Object tracking algorithm testing
├── video_anonymizer/                   # Multi-platform video anonymization
│   ├── python/                         # Python implementation
│   ├── cpp/                           # C++ implementations
│   │   ├── default_project/           # Standard C++ version
│   │   └── recamera_project/          # reCamera embedded version
│   └── models/                        # AI models and class labels
└── assets/                            # Documentation and example images
```

## Getting Started

For detailed setup instructions and implementation details, refer to the README files in each subdirectory:

- **HomeAssistant Integration**: See [`homeassistant_integration/README.md`](./homeassistant_integration/README.md)
- **Video Anonymizer**: See [`video_anonymizer/README.md`](./video_anonymizer/README.md)
- **C++ Implementations**: See [`video_anonymizer/cpp/README.md`](./video_anonymizer/cpp/README.md)

## Related Resources

- [reCamera Official Documentation](https://wiki.seeedstudio.com/reCamera_intro/)
- [SSCMA Framework](https://github.com/Seeed-Studio/sscma-example-sg200x)
- [HomeAssistant MQTT Discovery](https://www.home-assistant.io/docs/mqtt/discovery/)
- [YOLOv11 Documentation](https://docs.ultralytics.com/)
- [Sophgo](https://www.sophgo.com/) SG200X hardware and SDK support
- [HomeAssistant](https://www.home-assistant.io/)
- [Node-RED](https://nodered.org/)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Citation

If you use this project, please cite it using the following BibTeX entry:

```bibtex
@software{Carro_Lagoa_reCamera_Edge_AI,
author = {Carro Lagoa, Ángel and Domínguez Bolaño, Tomás and Barral Vales, Valentín and Escudero, Carlos J. and García-Naya, José A.},
doi = {10.5281/zenodo.16355557},
license = {MIT},
title = {{reCamera Edge AI Privacy Project}},
url = {https://github.com/GTEC-UDC/reCamera-Edge-AI-Privacy}
}
```


## Acknowledgements

This work has been supported by grant PID2022-137099NB-C42 (MADDIE) and by project TED2021-130240B-I00 (IVRY) funded by MCIN/AEI/10.13039/501100011033 and the European Union NextGenerationEU/PRTR.

![](./assets/acknowledgements_logos.png)

