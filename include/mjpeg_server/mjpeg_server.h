#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <string_view>
#include <memory>

// MjpegServer class - handles HTTP server and streaming
class MjpegServer final {
public:
    // Constructor and destructor
    explicit MjpegServer(int port = 8080) noexcept;
    ~MjpegServer();
    
    // Delete copy constructor and assignment operator
    MjpegServer(const MjpegServer&) = delete;
    MjpegServer& operator=(const MjpegServer&) = delete;
    
    // Allow move operations
    MjpegServer(MjpegServer&& other) noexcept;
    MjpegServer& operator=(MjpegServer&& other) noexcept;
    
    // Start/stop server
    [[nodiscard]] bool start();
    void stop() noexcept;
    
    // Update the frame to be streamed (called from main thread)
    void updateFrame(const cv::Mat& newFrame);
    
    // Get stream URL
    [[nodiscard]] std::string getStreamUrl() const noexcept;

private:
    // Server properties
    int serverPort_;
    int serverSocket_;
    
    // Shared state
    std::mutex frameMutex_;
    std::optional<cv::Mat> currentFrame_;
    std::atomic<bool> isRunning_;
    
    // Thread
    std::unique_ptr<std::thread> serverThread_;
    
    // Private methods
    void runServer();
    void handleClient(int clientSocket);
    
    // Constants
    static constexpr std::string_view CONTENT_TYPE = "multipart/x-mixed-replace; boundary=frame";
    static constexpr std::string_view BOUNDARY = "--frame";
    static constexpr std::string_view MIME_TYPE = "image/jpeg";
    static constexpr int DEFAULT_JPEG_QUALITY = 80;
    static constexpr auto FRAME_INTERVAL = std::chrono::milliseconds(33); // ~30fps
};
