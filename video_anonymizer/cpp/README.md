# Video Anonymizer C++ Projects

This directory contains multiple C++ implementations of the Video Anonymizer that detect people in images/videos using YOLOv11n segmentation model and anonymize them by replacing them with the background.


## Project Structure

The code has been organized into two main projects and a common directory:

- **default_project/** - Standard x86_64 implementation using [YOLOs-CPP](https://github.com/Geekgineer/YOLOs-CPP) and [ONNX Runtime](https://github.com/microsoft/onnxruntime)
- **recamera_project/** - RISC-V implementation for reCamera device
- **common/** - Shared processing code between both implementations
- **external/** - External dependencies


## Building and running the projects

Each project has its own CMakeLists.txt and build script, making them independently buildable.

### default_project (x86_64)

To build this project, you need to clone the [YOLOs-CPP](https://github.com/Geekgineer/YOLOs-CPP) repository in the `external/` directory. Follow the instructions in the YOLOs-CPP repository to build the project, including the installation of the dependencies (ONNX Runtime, OpenCV, etc.).

Additionally, this project does not include the YOLO models. You need to download and convert them using the YOLOs-CPP repository instructions and copy them to the top-level `models/` directory. In fact, you need to download the `yolo11n-seg.onnx` model to run the following example.


Build the project:

```bash
cd default_project
./build.sh
```

Run the application:

```bash
cd build
./video_anonymizer --gui
```

Check all available options with `--help`.

### recamera_project (RISC-V)

The pre-requisites to build this project are the same as the [sscma-example-sg200x](https://github.com/Seeed-Studio/sscma-example-sg200x) project. This implies building the [reCamera OS and tools repository](https://github.com/Seeed-Studio/reCamera-OS) following its instructions or downloading a pre-built image.

Additionally, you need to clone the [sscma-example-sg200x](https://github.com/Seeed-Studio/sscma-example-sg200x) repository in the `external/` directory.

Build the project:

```bash
cd recamera_project
export SG200X_SDK_PATH=/path/to/sg2002_recamera_emmc
./build.sh
```

Before running the application, you need to disable the Node-RED and SSCMA services in the reCamera device. These services will conflict with the application. To do this, you can use the following commands:

```bash
ssh recamera@{recamera_ip}
sudo su -
cd /etc/init.d
mv S91sscma-node _S91sscma-node.bak
mv S03node-red _S03node-red.bak
./_S91sscma-node.bak stop
./_S03node-red.bak stop
```

The file renaming is needed to avoid the services being restarted automatically.

The application needs to be copied to the reCamera device and run with root permissions. You can use the following commands:

```bash
scp build/anonymize_recamera recamera@{recamera_ip}
ssh recamera@{recamera_ip}
sudo ./anonymize_recamera --width=640 --height=480 --fps=2  /usr/share/supervisor/models/yolo11n_segment_cv181x_int8.cvimodel
```

This project uses the `yolo11n-seg.cvimodel` model. This model is already included in the reCamera device. There is no need to copy it to the local repository.


To simplify the process, you can use the included `test_on_recamera.sh` script. This script will copy the application to the reCamera device, run it and watch the video stream.

```bash
./test_on_recamera.sh anonymize_recamera --width=640 --height=480 --fps=2 --password=your_password /usr/share/supervisor/models/yolo11n_segment_cv181x_int8.cvimodel
```

To avoid typing the password every time, you can add your ssh key to the reCamera device:

```bash
ssh-copy-id recamera@{recamera_ip}
```

Also, the sudoers file can be modified inside the reCamera device to allow the application to run without password. Check the instructions inside the `test_on_recamera.sh` script.

Finally, to watch the video stream from the reCamera device, you can use the following command on your machine:

```bash
ffplay -fflags nobuffer -flags low_delay -rtsp_transport tcp -v verbose rtsp://admin:your_password@{recamera_ip}:8554/live
```

## Code Organization

### Common Code

The `common/` directory contains code shared between both implementations:

- **video_anonymizer**: Main implementation for anonymizing videos
- **idetector**: Interface for detector implementations 
- **detector_factory**: Factory for creating the proper detector implementation

### Detectors

- **default_detector** (in default_project): Uses YOLOs-CPP for ONNX model inference
- **recamera_detector** (in recamera_project): Uses SSCMA for ReCamera TPU inference


## License

The following files come from the [sscma-example-sg200x](https://github.com/Seeed-Studio/sscma-example-sg200x) repository and are licensed under its [Apache License 2.0](https://github.com/Seeed-Studio/sscma-example-sg200x/blob/main/LICENSE):
- `recamera_project/cvi_system.cpp`
- `recamera_project/cvi_system.h`
- `recamera_project/CMakeLists.txt`
- `cmake/build.cmake`
- `cmake/macro.cmake`
- `cmake/version.h.in`

The remaining files follow the same MIT license of the project. See the [LICENSE](../../LICENSE) file for details.

