#include "mjpeg_server/mjpeg_server.h"

#include <iostream>
#include <array>
#include <sstream>

// Socket headers
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

using namespace cv;
using namespace std;

namespace {
    // Helper function to create HTTP headers
    string createMjpegHeaders() {
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
               "Cache-Control: no-cache\r\n"
               "Connection: close\r\n"
               "Access-Control-Allow-Origin: *\r\n"  // Enable CORS
               "\r\n";
    }
    
    // RAII wrapper for socket
    class SocketGuard {
    public:
        explicit SocketGuard(int fd) noexcept : fd_(fd) {}
        ~SocketGuard() { if (fd_ >= 0) close(fd_); }
        
        // Delete copy operations
        SocketGuard(const SocketGuard&) = delete;
        SocketGuard& operator=(const SocketGuard&) = delete;
        
        // Enable move operations
        SocketGuard(SocketGuard&& other) noexcept : fd_(exchange(other.fd_, -1)) {}
        SocketGuard& operator=(SocketGuard&& other) noexcept {
            if (this != &other) {
                if (fd_ >= 0) close(fd_);
                fd_ = exchange(other.fd_, -1);
            }
            return *this;
        }
        
        [[nodiscard]] int get() const noexcept { return fd_; }
        void release() noexcept { fd_ = -1; }
        
    private:
        int fd_;
    };
}

// Constructor implementation
MjpegServer::MjpegServer(int port) noexcept
    : serverPort_(port),
      serverSocket_(-1),
      isRunning_(false),
      serverThread_(nullptr) {
}

// Move constructor
MjpegServer::MjpegServer(MjpegServer&& other) noexcept
    : serverPort_(other.serverPort_),
      serverSocket_(exchange(other.serverSocket_, -1)),
      isRunning_(other.isRunning_.load()),
      serverThread_(std::move(other.serverThread_)) {  // Fixed: Added std:: qualifier
    
    // Move the frame if it exists
    lock_guard<mutex> lock(frameMutex_);
    lock_guard<mutex> otherLock(other.frameMutex_);
    currentFrame_ = std::move(other.currentFrame_);  // Fixed: Added std:: qualifier
}

// Move assignment
MjpegServer& MjpegServer::operator=(MjpegServer&& other) noexcept {
    if (this != &other) {
        stop();
        
        serverPort_ = other.serverPort_;
        serverSocket_ = exchange(other.serverSocket_, -1);
        isRunning_.store(other.isRunning_.load());
        serverThread_ = std::move(other.serverThread_);  // Fixed: Added std:: qualifier
        
        // Move the frame if it exists
        lock_guard<mutex> lock(frameMutex_);
        lock_guard<mutex> otherLock(other.frameMutex_);
        currentFrame_ = std::move(other.currentFrame_);  // Fixed: Added std:: qualifier
    }
    return *this;
}

// Destructor
MjpegServer::~MjpegServer() {
    stop();
}

bool MjpegServer::start() {
    // Make sure we're not already running
    if (isRunning_) {
        cerr << "MJPEG Server is already running" << endl;
        return false;
    }
    
    isRunning_ = true;
    
    // Start HTTP server thread
    serverThread_ = make_unique<thread>(&MjpegServer::runServer, this);
    
    cout << "MJPEG stream available at: " << getStreamUrl() << endl;
    
    return true;
}

void MjpegServer::stop() noexcept {
    if (!isRunning_)
        return;
        
    cout << "Shutting down MJPEG server..." << endl;
    isRunning_ = false;
    
    // Wait for server thread
    if (serverThread_ && serverThread_->joinable())
        serverThread_->join();
    serverThread_.reset();
    
    // Close socket
    if (serverSocket_ >= 0) {
        close(serverSocket_);
        serverSocket_ = -1;
    }
    
    cout << "MJPEG server stopped" << endl;
}

void MjpegServer::updateFrame(const cv::Mat& newFrame) {
    if (!newFrame.empty()) {
        lock_guard<mutex> lock(frameMutex_);
        currentFrame_ = newFrame.clone();
    }
}

string MjpegServer::getStreamUrl() const noexcept {
    return "http://localhost:" + to_string(serverPort_) + "/";
}

void MjpegServer::runServer() {
    try {
        // Create server socket
        serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket_ < 0) {
            throw runtime_error("Socket creation failed: " + string(strerror(errno)));
        }
        
        // Use RAII for the server socket
        SocketGuard serverSocketGuard(serverSocket_);
        
        // Set socket options
        int opt = 1;
        if (setsockopt(serverSocketGuard.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            throw runtime_error("setsockopt failed: " + string(strerror(errno)));
        }
        
        // Bind server socket - Using global namespace to avoid collision
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(serverPort_);
        
        if (::bind(serverSocketGuard.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            throw runtime_error("Bind failed: " + string(strerror(errno)));
        }
        
        // Start listening
        if (listen(serverSocketGuard.get(), 5) < 0) {
            throw runtime_error("Listen failed: " + string(strerror(errno)));
        }
        
        // Keep this socket alive (don't let RAII destroy it yet)
        serverSocket_ = serverSocketGuard.get();
        serverSocketGuard.release();
        
        // Accept and handle client connections
        while (isRunning_) {
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            
            // Accept client connection with timeout
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(serverSocket_, &readSet);
            
            // Set select timeout to allow checking isRunning_ periodically
            timeval timeout{0, 100000}; // 100ms
            
            int selectResult = select(serverSocket_ + 1, &readSet, nullptr, nullptr, &timeout);
            if (selectResult < 0) {
                cerr << "Select failed: " << strerror(errno) << endl;
                continue;
            }
            
            if (selectResult == 0 || !FD_ISSET(serverSocket_, &readSet)) {
                continue;  // Timeout or socket not ready
            }
            
            int clientSocket = accept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if (clientSocket < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    cerr << "Accept failed: " << strerror(errno) << endl;
                }
                continue;
            }
            
            // Handle client in a new thread using a lambda with better exception handling
            thread([this, clientSocket]() {
                try {
                    this->handleClient(clientSocket);
                }
                catch (const exception& e) {
                    cerr << "Uncaught exception in client thread: " << e.what() << endl;
                }
                catch (...) {
                    cerr << "Unknown exception in client thread" << endl;
                }
            }).detach();
        }
    }
    catch (const exception& e) {
        cerr << "Server error: " << e.what() << endl;
        isRunning_ = false;
    }
}

void MjpegServer::handleClient(int clientSocket) {
    try {
        // Use RAII for client socket
        SocketGuard socketGuard(clientSocket);
        
        // Read HTTP request
        array<char, 1024> buffer{};
        ssize_t bytesRead = recv(socketGuard.get(), buffer.data(), buffer.size() - 1, 0);
        
        if (bytesRead <= 0) {
            return;  // Connection closed or error
        }
        
        buffer[bytesRead] = '\0';  // Null-terminate for string handling
        string_view request(buffer.data(), bytesRead);  // Efficient non-owning view
        
        // Check if this is a GET request
        if (request.find("GET") != string_view::npos) {
            // Send MJPEG stream headers
            string headers = createMjpegHeaders();
            if (send(socketGuard.get(), headers.data(), headers.size(), 0) <= 0) {
                return;  // Client disconnected immediately
            }
            
            // Set socket to non-blocking for better handling
            int flags = fcntl(socketGuard.get(), F_GETFL, 0);
            fcntl(socketGuard.get(), F_SETFL, flags | O_NONBLOCK);
            
            // Track client state
            bool clientConnected = true;
            
            // Stream frames
            while (isRunning_ && clientConnected) {
                // Check if client is still connected with a non-blocking poll
                char testBuf[1];
                ssize_t testRead = recv(socketGuard.get(), testBuf, sizeof(testBuf), MSG_PEEK | MSG_DONTWAIT);
                if (testRead == 0) {
                    // Connection closed by client
                    clientConnected = false;
                    break;
                } else if (testRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    // Actual error
                    clientConnected = false;
                    break;
                }
                
                // Get current frame
                optional<Mat> frameCopy;
                {
                    lock_guard<mutex> lock(frameMutex_);
                    if (currentFrame_.has_value()) {
                        frameCopy = currentFrame_->clone();
                    }
                }
                
                if (!frameCopy) {
                    this_thread::sleep_for(chrono::milliseconds(10));
                    continue;
                }
                
                try {
                    // Encode frame to JPEG
                    vector<uchar> buf;
                    vector<int> params = {IMWRITE_JPEG_QUALITY, DEFAULT_JPEG_QUALITY};
                    imencode(".jpg", *frameCopy, buf, params);
                    
                    // Create frame header
                    stringstream frameHeaderStream;
                    frameHeaderStream << BOUNDARY << "\r\n"
                                     << "Content-Type: " << MIME_TYPE << "\r\n"
                                     << "Content-Length: " << buf.size() << "\r\n\r\n";
                    string frameHeader = frameHeaderStream.str();
                    
                    // Send frame header with error handling
                    ssize_t headerSent = send(socketGuard.get(), frameHeader.data(), frameHeader.size(), 0);
                    if (headerSent <= 0) {
                        clientConnected = false;
                        break;
                    }
                    
                    // Send frame data with error handling
                    ssize_t dataSent = send(socketGuard.get(), buf.data(), buf.size(), 0);
                    if (dataSent <= 0) {
                        clientConnected = false;
                        break;
                    }
                    
                    // Send frame boundary with error handling
                    string boundary = "\r\n";
                    ssize_t boundarySent = send(socketGuard.get(), boundary.data(), boundary.size(), 0);
                    if (boundarySent <= 0) {
                        clientConnected = false;
                        break;
                    }
                    
                    // Control frame rate
                    this_thread::sleep_for(FRAME_INTERVAL);
                }
                catch (const exception& e) {
                    // Log exception but don't crash the thread
                    cerr << "Error processing frame: " << e.what() << endl;
                    this_thread::sleep_for(chrono::milliseconds(100));
                }
            }
        }
    }
    catch (const exception& e) {
        // Log the exception but don't let it propagate and crash the program
        cerr << "Client handler exception: " << e.what() << endl;
    }
    catch (...) {
        // Catch any other exceptions
        cerr << "Unknown exception in client handler" << endl;
    }
    
    // Client disconnection logging (optional)
    cout << "Client disconnected" << endl;
}
