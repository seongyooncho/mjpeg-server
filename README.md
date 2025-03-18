# MJPEG Server

A lightweight MJPEG streaming server library for C++ applications that need to stream video over HTTP.

## Features

- Simple API to start a streaming server
- Compatible with any browser that supports MJPEG streams
- Thread-safe frame updates
- Minimal dependencies (OpenCV and standard libraries)

## Requirements

- C++17 compatible compiler
- CMake 3.14 or newer
- OpenCV 4.x

## Usage

### Basic Example

```cpp
#include "mjpeg_server/mjpeg_server.h"
#include <opencv2/opencv.hpp>

int main() {
    // Create an MJPEG server on port 8080
    MjpegServer server(8080);
    
    // Start the server
    server.start();
    
    // Open a camera
    cv::VideoCapture cap(0);
    cv::Mat frame;
    
    while (true) {
        // Capture a frame
        cap >> frame;
        
        // Update the frame in the server
        server.updateFrame(frame);
        
        // Show the frame locally
        cv::imshow("Preview", frame);
        if (cv::waitKey(30) == 27) break; // ESC to exit
    }
    
    // Stop the server
    server.stop();
    
    return 0;
}
```

### Accessing the Stream

Once the server is running, you can access the MJPEG stream in a web browser at:
```
http://localhost:8080/
```

## Building

### As a standalone project

```bash
mkdir build && cd build
cmake ..
make
```

### As a dependency in another project

#### Using add_subdirectory

```cmake
add_subdirectory(path/to/mjpeg_server)
target_link_libraries(your_target PRIVATE mjpeg_server::mjpeg_server)
```

#### Using find_package

```cmake
find_package(mjpeg_server REQUIRED)
target_link_libraries(your_target PRIVATE mjpeg_server::mjpeg_server)
```

## License

[MIT License](LICENSE)
