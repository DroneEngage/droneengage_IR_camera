#include <algorithm>
#include <chrono>
#include <errno.h>
#include <opencv2/core/ocl.hpp>
#include <opencv2/opencv.hpp>
#include <sys/mman.h>
#include <thread>
#include <mutex>
#include <atomic>

#include "../de_common/helpers/colors.hpp"
#include "../de_common/helpers/helpers.hpp"
#include "../de_common/de_databus/configFile.hpp"
#include "ir_tracker.hpp"
#include "video.hpp"

#include "../de_common/de_databus/messages.hpp"
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Include MI48 library
#include "../../../mi48_lib_c/serial_mi48.hpp"

using namespace de::ir_tracker;

// Global state for thermal frame handling
std::mutex g_frame_mutex;
cv::Mat g_thermal_frame;
uint16_t g_thermal_rows = 0;
uint16_t g_thermal_cols = 0;
std::atomic<bool> g_frame_ready{false};

bool CIRTracker::init(const std::string& thermal_port, 
                      const std::string& output_video_device,
                      uint16_t frames_to_skip_between_messages,
                      const std::string& source_video_device,
                      bool dual_camera_enabled,
                      int display_mode,
                      bool display_enabled) {
    
    // Enable OpenCV OpenCL acceleration if available
    cv::ocl::setUseOpenCL(true);

    m_frames_to_skip_between_messages = frames_to_skip_between_messages;
    m_process = false;
    m_thermal_port = thermal_port;
    m_dual_camera_enabled = dual_camera_enabled;
    m_display_mode = display_mode;
    m_display_enabled = display_enabled;
    m_source_video_device = source_video_device;

    // Initialize thermal camera
    if (!initThermalCamera(thermal_port)) {
        return false;
    }

    // Initialize RGB camera if dual camera mode is enabled
    if (m_dual_camera_enabled && !source_video_device.empty()) {
        if (!initRGBCamera(source_video_device)) {
            std::cout << _ERROR_CONSOLE_BOLD_TEXT_
                      << "WARNING: Failed to initialize RGB camera, disabling dual camera mode"
                      << _NORMAL_CONSOLE_TEXT_ << std::endl;
            m_dual_camera_enabled = false;
        }
    }

    // Initialize V4L2 output device
    // output_video_device is already translated to full path in ir_tracker_main.cpp
    if (!initTargetVirtualVideoDevice(output_video_device)) {
        m_sender.close_port();
        if (m_rgb_capture.isOpened()) {
            m_rgb_capture.release();
        }
        return false;
    }

    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << "IR Tracker has been initialized."
              << _NORMAL_CONSOLE_TEXT_ << std::endl;

    return true;
}

bool CIRTracker::initThermalCamera(const std::string& thermal_port) {
    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_
              << "Thermal Camera Port:" << _LOG_CONSOLE_BOLD_TEXT << thermal_port
              << _NORMAL_CONSOLE_TEXT_ << std::endl;

    if (!m_sender.open_port(thermal_port)) {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Failed to open thermal port: "
                  << _INFO_CONSOLE_BOLD_TEXT << thermal_port 
                  << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }

    m_sender.send_and_receive_serial_command();
    int camera_type;
    m_sender.get_senxor_type(camera_type);

    // Register frame callback using lambda to capture 'this'
    m_sender.register_frame_callback(
        [this](const std::vector<float>& temperatures, const uint16_t rows, const uint16_t cols) {
            this->onThermalFrame(temperatures, rows, cols);
        }
    );

    // Start thermal streaming in background thread
    m_thermal_thread = std::thread([this]() {
        m_sender.start_stream(true);
        m_sender.loop_on_read();
    });

    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << "Thermal camera initialized successfully"
              << _NORMAL_CONSOLE_TEXT_ << std::endl;

    return true;
}

bool CIRTracker::initTargetVirtualVideoDevice(const std::string& output_video_device) {
    m_virtual_device_opened = false;

    if (output_video_device.empty()) {
        m_output_video_active = false;
        return true;
    }

    m_output_video_path = output_video_device;
    m_output_video_active = true;

    // Open the virtual video device
    m_video_fd = open(m_output_video_path.c_str(), O_RDWR | O_NONBLOCK);

    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_
              << "Video Output:" << _LOG_CONSOLE_BOLD_TEXT << m_output_video_path
              << _NORMAL_CONSOLE_TEXT_ << std::endl;

    if (m_video_fd < 0) {
        std::cout << "Error: Could not open virtual video device "
                  << m_output_video_path << ": " << strerror(errno) << std::endl;
        return false;
    }

    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_
              << "Successfully opened virtual video device: "
              << _LOG_CONSOLE_BOLD_TEXT << m_output_video_path
              << _NORMAL_CONSOLE_TEXT_ << std::endl;

    // Wait for first frame to get dimensions
    int wait_count = 0;
    while (!g_frame_ready && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
    }

    if (!g_frame_ready) {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Timeout waiting for first thermal frame"
                  << _NORMAL_CONSOLE_TEXT_ << std::endl;
        close(m_video_fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        m_image_width = g_thermal_frame.cols;
        m_image_height = g_thermal_frame.rows;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = m_image_width;
    fmt.fmt.pix.height = m_image_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = m_image_width;
    fmt.fmt.pix.sizeimage = (m_image_width * m_image_height * 3) / 2;

    if (CVideo::xioctl(m_video_fd, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Failed to set video format on "
                  << _INFO_CONSOLE_BOLD_TEXT << m_output_video_path << " "
                  << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        close(m_video_fd);
        return false;
    }

    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << "Successfully set format for "
              << m_output_video_path << ":" << _INFO_CONSOLE_BOLD_TEXT
              << fmt.fmt.pix.width << _LOG_CONSOLE_BOLD_TEXT << "x"
              << _INFO_CONSOLE_BOLD_TEXT << fmt.fmt.pix.height
              << _LOG_CONSOLE_BOLD_TEXT << " pixformat YUV420"
              << _NORMAL_CONSOLE_TEXT_ << std::endl;

    m_yuv_frame_size = m_image_width * m_image_height * 3 / 2;
    m_virtual_device_opened = true;
    return true;
}

void CIRTracker::destroyVirtualVideoDevice() {
    if (m_video_fd != -1) {
        close(m_video_fd);
        m_video_fd = -1;
        std::cout << "Virtual video device closed." << std::endl;
    }
    m_virtual_device_opened = false;
}

bool CIRTracker::uninit() {
    stop();

    // Stop thermal camera streaming
    m_sender.stop_loop();
    m_sender.stop_stream();
    
    // Wait for thermal thread to finish
    if (m_thermal_thread.joinable()) {
        m_thermal_thread.join();
    }
    
    m_sender.close_port();

    // Release RGB camera if opened
    if (m_rgb_capture.isOpened()) {
        m_rgb_capture.release();
        std::cout << _LOG_CONSOLE_BOLD_TEXT << "RGB camera released"
                  << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }

    // Close and cleanup virtual output device
    if (m_virtual_device_opened || m_video_fd != -1) {
        destroyVirtualVideoDevice();
    }

    return true;
}

void CIRTracker::pause() {
    if (m_callback_tracker != nullptr) {
        m_callback_tracker->onIRStatusChanged(TrackingTarget_STATUS_TRACKING_STOPPED);
    }
}

void CIRTracker::stop() {
    if (!m_process)
        return;
    
    m_process = false;
    
    if (m_callback_tracker != nullptr) {
        m_callback_tracker->onIRStatusChanged(TrackingTarget_STATUS_TRACKING_STOPPED);
    }

    if (m_framesThread.joinable())
        m_framesThread.join();
}

void CIRTracker::start() {
    if (m_thermal_port.empty()) {
        return;
    }

    m_framesThread = std::thread([this]() { this->processIRFrames(); });
}

void CIRTracker::onThermalFrame(const std::vector<float>& temperatures, 
                                const uint16_t rows, 
                                const uint16_t cols) {
    if (temperatures.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_frame_mutex);
    g_thermal_rows = rows;
    g_thermal_cols = cols;

    // Create a matrix from temperature data
    cv::Mat frame(rows, cols, CV_32F);
    for (uint16_t c = 0; c < cols; ++c) {
        for (uint16_t r = 0; r < rows; ++r) {
            frame.at<float>(r, c) = temperatures[c * rows + r];
        }
    }

    // Normalize temperatures to 0-255 range for display
    double min_temp, max_temp;
    cv::minMaxLoc(frame, &min_temp, &max_temp);
    cv::Mat frame_normalized;
    frame.convertTo(frame_normalized, CV_8U, 255.0 / (max_temp - min_temp), 
                    -min_temp * 255.0 / (max_temp - min_temp));

    // Invert and apply colormap
    cv::Mat frame_inverted = 255 - frame_normalized;
    cv::applyColorMap(frame_inverted, g_thermal_frame, cv::COLORMAP_JET);

    // Rotate 90 degrees clockwise
    cv::rotate(g_thermal_frame, g_thermal_frame, cv::ROTATE_90_CLOCKWISE);

    g_frame_ready = true;
}

void CIRTracker::findHotColdPoints(const cv::Mat& thermal_frame,
                                   cv::Point& hot_point,
                                   cv::Point& cold_point,
                                   float& max_temp,
                                   float& min_temp) {
    if (thermal_frame.empty()) {
        return;
    }

    // Convert back to grayscale for min/max detection
    cv::Mat gray;
    cv::cvtColor(thermal_frame, gray, cv::COLOR_BGR2GRAY);

    double min_val, max_val;
    cv::minMaxLoc(gray, &min_val, &max_val, &cold_point, &hot_point);

    min_temp = static_cast<float>(min_val);
    max_temp = static_cast<float>(max_val);
}

cv::Mat CIRTracker::thermalToColorMap(const std::vector<float>& temperatures,
                                      uint16_t rows, uint16_t cols) {
    cv::Mat frame(rows, cols, CV_32F);
    for (uint16_t c = 0; c < cols; ++c) {
        for (uint16_t r = 0; r < rows; ++r) {
            frame.at<float>(r, c) = temperatures[c * rows + r];
        }
    }

    double min_temp, max_temp;
    cv::minMaxLoc(frame, &min_temp, &max_temp);
    cv::Mat frame_normalized;
    frame.convertTo(frame_normalized, CV_8U, 255.0 / (max_temp - min_temp),
                    -min_temp * 255.0 / (max_temp - min_temp));

    cv::Mat frame_inverted = 255 - frame_normalized;
    cv::Mat colored;
    cv::applyColorMap(frame_inverted, colored, cv::COLORMAP_JET);
    cv::rotate(colored, colored, cv::ROTATE_90_CLOCKWISE);

    return colored;
}

void CIRTracker::processIRFrames() {
    cv::Mat yuv_frame;

    const std::chrono::milliseconds target_frame_time_ms(1000 / m_target_fps);

    m_process = true;
    uint64_t frame_counter = 0;

    if (m_callback_tracker) {
        m_callback_tracker->onIRStatusChanged(TrackingTarget_STATUS_TRACKING_DETECTED);
    }

    std::string window_name = "IR Tracker";
    if (m_display_enabled) {
        cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
    }

    while (m_process) {
        auto start_time = std::chrono::high_resolution_clock::now();

        cv::Mat thermal_frame;
        cv::Mat rgb_frame;
        cv::Mat output_frame;
        cv::Point hot_point, cold_point;
        float max_temp = 0.0f, min_temp = 0.0f;

        // Capture RGB frame if dual camera enabled
        if (m_dual_camera_enabled && m_rgb_capture.isOpened()) {
            m_rgb_capture >> rgb_frame;
        }

        // Get thermal frame
        {
            std::lock_guard<std::mutex> lock(g_frame_mutex);
            if (!g_thermal_frame.empty()) {
                thermal_frame = g_thermal_frame.clone();
            }
        }

        if (thermal_frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Find hot and cold points on thermal frame
        findHotColdPoints(thermal_frame, hot_point, cold_point, max_temp, min_temp);

        // Draw markers on thermal frame
        cv::circle(thermal_frame, hot_point, 5, cv::Scalar(0, 0, 255), 2);
        cv::circle(thermal_frame, cold_point, 5, cv::Scalar(255, 0, 0), 2);
        cv::putText(thermal_frame, "HOT", cv::Point(hot_point.x + 10, hot_point.y),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);
        cv::putText(thermal_frame, "COLD", cv::Point(cold_point.x + 10, cold_point.y),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 1);

        // Combine frames based on display mode
        if (m_dual_camera_enabled && !rgb_frame.empty()) {
            switch (m_display_mode) {
                case 2: // Side-by-side
                    output_frame = sideBySide(rgb_frame, thermal_frame);
                    break;
                case 3: // Overlay
                    output_frame = overlayThermalOnRGB(rgb_frame, thermal_frame);
                    break;
                case 4: // Picture-in-Picture
                    output_frame = pictureInPicture(rgb_frame, thermal_frame);
                    break;
                default: // Thermal only
                    output_frame = thermal_frame;
                    break;
            }
        } else {
            output_frame = thermal_frame;
        }

        // Send callback with hot/cold point locations
        const bool should_skip_message = (frame_counter % m_frames_to_skip_between_messages) != 0;
        
        if (m_callback_tracker && !should_skip_message) {
            m_callback_tracker->onHotColdPoints(
                revScaleX(hot_point.x), revScaleY(hot_point.y),
                revScaleX(cold_point.x), revScaleY(cold_point.y),
                max_temp, min_temp,
                should_skip_message
            );
        }

        // Stream to V4L2 device (skip if display is enabled)
        if (!m_display_enabled && m_virtual_device_opened && !output_frame.empty()) {
            cv::cvtColor(output_frame, yuv_frame, cv::COLOR_BGR2YUV_I420);
            
            if (yuv_frame.isContinuous() &&
                yuv_frame.total() * yuv_frame.elemSize() == m_yuv_frame_size) {
                ssize_t bytes_written = write(m_video_fd, yuv_frame.data, m_yuv_frame_size);
                if (bytes_written < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::cerr << "Warning: Virtual device " << m_output_video_path
                                  << " buffer full? Try reading from it." << std::endl;
                    }
                }
            } else {
                std::cerr << "Fatal Error: YUV frame is not continuous or has unexpected size ("
                          << yuv_frame.total() * yuv_frame.elemSize() << " vs "
                          << m_yuv_frame_size
                          << "). Cannot write to V4L2 device." << std::endl;
            }
        }

        // Display window if enabled
        if (m_display_enabled && !output_frame.empty()) {
            cv::imshow(window_name, output_frame);
            int key = cv::waitKey(1) & 0xFF;
            
            const double scale_step = 0.02;
            const int offset_step = 2;
            const double rotation_step = 0.5;
            
            if (key == 'q' || key == 27) {
                std::cout << "Quit requested from display window" << std::endl;
                m_process = false;
            }
            else if (key == '2') {
                m_display_mode = 2;
                std::cout << "Mode: Side-by-side" << std::endl;
            }
            else if (key == '3') {
                m_display_mode = 3;
                std::cout << "Mode: Overlay" << std::endl;
            }
            else if (key == '4') {
                m_display_mode = 4;
                std::cout << "Mode: Picture-in-Picture" << std::endl;
            }
            else if (key == '+' || key == '=') {
                m_calib_params.alpha = std::min(1.0, m_calib_params.alpha + 0.1);
                std::cout << "Alpha: " << m_calib_params.alpha << std::endl;
            }
            else if (key == '-') {
                m_calib_params.alpha = std::max(0.0, m_calib_params.alpha - 0.1);
                std::cout << "Alpha: " << m_calib_params.alpha << std::endl;
            }
            else if (key == 'w') {
                m_calib_params.scale_y += scale_step;
                std::cout << "Scale Y: " << m_calib_params.scale_y << std::endl;
            }
            else if (key == 's') {
                m_calib_params.scale_y = std::max(0.1, m_calib_params.scale_y - scale_step);
                std::cout << "Scale Y: " << m_calib_params.scale_y << std::endl;
            }
            else if (key == 'd') {
                m_calib_params.scale_x += scale_step;
                std::cout << "Scale X: " << m_calib_params.scale_x << std::endl;
            }
            else if (key == 'a') {
                m_calib_params.scale_x = std::max(0.1, m_calib_params.scale_x - scale_step);
                std::cout << "Scale X: " << m_calib_params.scale_x << std::endl;
            }
            else if (key == 82) { // Up arrow
                m_calib_params.offset_y -= offset_step;
                std::cout << "Offset Y: " << m_calib_params.offset_y << std::endl;
            }
            else if (key == 84) { // Down arrow
                m_calib_params.offset_y += offset_step;
                std::cout << "Offset Y: " << m_calib_params.offset_y << std::endl;
            }
            else if (key == 81) { // Left arrow
                m_calib_params.offset_x -= offset_step;
                std::cout << "Offset X: " << m_calib_params.offset_x << std::endl;
            }
            else if (key == 83) { // Right arrow
                m_calib_params.offset_x += offset_step;
                std::cout << "Offset X: " << m_calib_params.offset_x << std::endl;
            }
            else if (key == 'z') {
                m_calib_params.rotation -= rotation_step;
                std::cout << "Rotation: " << m_calib_params.rotation << " deg" << std::endl;
            }
            else if (key == 'x') {
                m_calib_params.rotation += rotation_step;
                std::cout << "Rotation: " << m_calib_params.rotation << " deg" << std::endl;
            }
            else if (key == 'p') {
                saveCalibrationToConfig();
                std::cout << "Calibration saved to config file" << std::endl;
            }
            else if (key == 'r') {
                m_calib_params = CalibrationParams();
                std::cout << "Reset transformation" << std::endl;
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        if (elapsed_time < target_frame_time_ms) {
            auto sleep_for = (target_frame_time_ms - elapsed_time);
            std::this_thread::sleep_for(sleep_for);
        }

        ++frame_counter;
    }

    std::cout << _LOG_CONSOLE_BOLD_TEXT << "IR tracking off" << _NORMAL_CONSOLE_TEXT_
              << std::endl;

    if (m_display_enabled) {
        cv::destroyAllWindows();
    }
}

float CIRTracker::revScaleX(const float& x) const {
    return (x / m_image_width);
}

float CIRTracker::revScaleY(const float& y) const {
    return (y / m_image_height);
}

// Dual camera helper methods implementation
bool CIRTracker::initRGBCamera(const std::string& source_video_device) {
    m_rgb_capture.open(source_video_device);
    if (!m_rgb_capture.isOpened()) {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_
                  << "Failed to open RGB camera: " << source_video_device
                  << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }
    
    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_
              << "RGB camera opened: " << _LOG_CONSOLE_BOLD_TEXT << source_video_device
              << _NORMAL_CONSOLE_TEXT_ << std::endl;
    return true;
}

cv::Mat CIRTracker::stretchImage(const cv::Mat& image, double scale_x, double scale_y, 
                                  int offset_x, int offset_y, double rotation) {
    int h = image.rows;
    int w = image.cols;
    cv::Point2f center(w / 2.0f, h / 2.0f);
    
    cv::Mat rot_matrix = cv::getRotationMatrix2D(center, rotation, 1.0);
    rot_matrix.at<double>(0, 0) *= scale_x;
    rot_matrix.at<double>(0, 1) *= scale_x;
    rot_matrix.at<double>(1, 0) *= scale_y;
    rot_matrix.at<double>(1, 1) *= scale_y;
    rot_matrix.at<double>(0, 2) += offset_x;
    rot_matrix.at<double>(1, 2) += offset_y;
    
    cv::Mat result;
    cv::warpAffine(image, result, rot_matrix, cv::Size(w, h),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return result;
}

cv::Mat CIRTracker::overlayThermalOnRGB(const cv::Mat& rgb_image, const cv::Mat& thermal_image) {
    cv::Mat thermal_resized;
    cv::resize(thermal_image, thermal_resized, rgb_image.size(), 0, 0, cv::INTER_LINEAR);
    
    cv::Mat thermal_stretched = stretchImage(thermal_resized, 
        m_calib_params.scale_x, m_calib_params.scale_y,
        m_calib_params.offset_x, m_calib_params.offset_y, 
        m_calib_params.rotation);
    
    cv::Mat result;
    cv::addWeighted(rgb_image, 1.0 - m_calib_params.alpha, 
                    thermal_stretched, m_calib_params.alpha, 0, result);
    return result;
}

cv::Mat CIRTracker::sideBySide(const cv::Mat& rgb_image, const cv::Mat& thermal_image) {
    cv::Mat thermal_resized;
    double scale = static_cast<double>(rgb_image.rows) / thermal_image.rows;
    cv::resize(thermal_image, thermal_resized, cv::Size(), scale, scale, cv::INTER_LINEAR);
    
    cv::Mat result;
    cv::hconcat(rgb_image, thermal_resized, result);
    return result;
}

cv::Mat CIRTracker::pictureInPicture(const cv::Mat& rgb_image, const cv::Mat& thermal_image, double pip_scale) {
    cv::Mat result = rgb_image.clone();
    
    int pip_width = static_cast<int>(rgb_image.cols * pip_scale);
    int pip_height = static_cast<int>(rgb_image.rows * pip_scale);
    cv::Mat thermal_pip;
    cv::resize(thermal_image, thermal_pip, cv::Size(pip_width, pip_height), 0, 0, cv::INTER_LINEAR);
    
    int margin = 10;
    int x = rgb_image.cols - pip_width - margin;
    int y = rgb_image.rows - pip_height - margin;
    
    thermal_pip.copyTo(result(cv::Rect(x, y, pip_width, pip_height)));
    cv::rectangle(result, cv::Point(x - 2, y - 2), 
                  cv::Point(x + pip_width + 2, y + pip_height + 2),
                  cv::Scalar(255, 255, 255), 2);
    
    return result;
}

void CIRTracker::saveCalibrationToConfig() {
    de::CConfigFile& config = de::CConfigFile::getInstance();
    Json_de jsonConfig = config.GetConfigJSON();
    
    if (!jsonConfig.contains("dual_camera")) {
        jsonConfig["dual_camera"] = Json_de::object();
    }
    
    if (!jsonConfig["dual_camera"].contains("calibration")) {
        jsonConfig["dual_camera"]["calibration"] = Json_de::object();
    }
    
    jsonConfig["dual_camera"]["calibration"]["scale_x"] = m_calib_params.scale_x;
    jsonConfig["dual_camera"]["calibration"]["scale_y"] = m_calib_params.scale_y;
    jsonConfig["dual_camera"]["calibration"]["offset_x"] = m_calib_params.offset_x;
    jsonConfig["dual_camera"]["calibration"]["offset_y"] = m_calib_params.offset_y;
    jsonConfig["dual_camera"]["calibration"]["rotation"] = m_calib_params.rotation;
    jsonConfig["dual_camera"]["calibration"]["alpha"] = m_calib_params.alpha;
    
    config.updateJSON(jsonConfig.dump(4));
    config.saveConfigFile();
}
