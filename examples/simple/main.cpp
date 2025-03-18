#include "mjpeg_server/mjpeg_server.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>
#include <chrono>
#include <future>
#include <optional>

using namespace cv;
using namespace std;

// Global variables for signal handling
atomic<bool> isRunning{true};
unique_ptr<MjpegServer> server;

// Signal handler for clean shutdown
void signalHandler(int signal) {
    cout << "\nReceived signal " << signal << ", shutting down..." << endl;
    isRunning = false;
}

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        const auto cameraIndex = (argc > 1) ? stoi(argv[1]) : 0;
        const auto serverPort = (argc > 2) ? stoi(argv[2]) : 8080;
        
        // Register signal handlers
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // Initialize camera with timeout
        cout << "Initializing camera..." << endl;
        
        auto openCamera = [cameraIndex]() -> optional<VideoCapture> {
            // Try with AVFoundation first (macOS)
            VideoCapture cap(cameraIndex, CAP_AVFOUNDATION);
            
            if (!cap.isOpened()) {
                cout << "Failed with AVFoundation, trying default" << endl;
                cap.open(cameraIndex);
                
                if (!cap.isOpened()) {
                    return nullopt;
                }
            }
            
            // Set camera properties
            cap.set(CAP_PROP_FRAME_WIDTH, 640);
            cap.set(CAP_PROP_FRAME_HEIGHT, 480);
            
            return cap;
        };
        
        // Try to open the camera with timeout
        auto cameraFuture = async(launch::async, openCamera);
        if (cameraFuture.wait_for(5s) != future_status::ready) {
            cerr << "Camera initialization timed out" << endl;
            return 1;
        }
        
        auto camera = cameraFuture.get();
        if (!camera) {
            cerr << "Failed to open camera" << endl;
            return 1;
        }
        
        // Create and start MJPEG server
        server = make_unique<MjpegServer>(serverPort);
        
        if (!server->start()) {
            cerr << "Failed to start MJPEG server" << endl;
            return 1;
        }
        
        // Create preview window
        namedWindow("Camera Preview (ESC to exit)", WINDOW_AUTOSIZE);
        cout << "Press ESC in the preview window to exit" << endl;
        
        auto lastFrameTime = chrono::steady_clock::now();
        Mat frame;
        
        // Main loop - capture frames and update server
        while (isRunning) {
            // Regulate frame rate
            const auto now = chrono::steady_clock::now();
            const auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - lastFrameTime);
            
            if (elapsed < 33ms) { // Limit to ~30 FPS
                this_thread::sleep_for(33ms - elapsed);
            }
            
            lastFrameTime = chrono::steady_clock::now();
            
            // Capture frame
            *camera >> frame;
            
            if (frame.empty()) {
                cerr << "Empty frame captured" << endl;
                this_thread::sleep_for(100ms);
                continue;
            }
            
            // Update frame in the server
            server->updateFrame(frame);
            
            // Display frame locally
            imshow("Camera Preview (ESC to exit)", frame);
            
            // Check for ESC key
            if (waitKey(1) == 27) { // ESC key
                isRunning = false;
            }
        }
        
        // Clean up
        cout << "Shutting down..." << endl;
        
        // Stop server
        server.reset();
        
        // Release camera and close windows
        camera->release();
        destroyAllWindows();
        
        cout << "Application terminated" << endl;
        return 0;
    }
    catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }
}
