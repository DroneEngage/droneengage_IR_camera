#ifndef IR_TRACKER_H
#define IR_TRACKER_H

#include <opencv2/opencv.hpp>
#include <thread>
#include <cstdint>
#include <string>
#include <vector>

#include "../driver/serial_mi48.hpp"

#define DEF_TRACK_ORIENTATION_DEG_0 0
#define DEF_TRACK_ORIENTATION_DEG_90 1
#define DEF_TRACK_ORIENTATION_DEG_180 2
#define DEF_TRACK_ORIENTATION_DEG_270 3

namespace de
{
namespace ir_tracker
{
class CCallBack_IRTracker {
public:
    virtual void onHotColdPoints(const float& hot_x, const float& hot_y,
                                 const float& cold_x, const float& cold_y,
                                 const float& max_temp, const float& min_temp,
                                 const bool should_skip_message) = 0;
    
    virtual void onIRStatusChanged(const int& status) = 0;
};

class CIRTracker {
public:
    CIRTracker(CCallBack_IRTracker* callback_tracker)
        : m_callback_tracker(callback_tracker),
          m_output_video_path(""),
          m_output_video_active(false) {
    }

    ~CIRTracker() {
        uninit();
    }

    // Initialization
    bool init(const std::string& thermal_port, 
              const std::string& output_video_device,
              uint16_t frames_to_skip_between_messages,
              const std::string& source_video_device = "",
              bool dual_camera_enabled = false,
              int display_mode = 3,
              bool display_enabled = false);
    
    bool uninit();
    
    // Control
    void start();
    void stop();
    void pause();
    
    // Core processing loop (runs in thread)
    void processIRFrames();
    
    // Calibration parameters
    struct CalibrationParams {
        double scale_x = 1.0;
        double scale_y = 1.0;
        int offset_x = 0;
        int offset_y = 0;
        double rotation = 0.0;
        double alpha = 0.5;
    };

    void setCalibrationParams(const CalibrationParams& params) {
        m_calib_params = params;
    }

    CalibrationParams& getCalibrationParams() {
        return m_calib_params;
    }

    void saveCalibrationToConfig();
    
private:
    // MI48 camera setup
    bool initThermalCamera(const std::string& thermal_port);
    
    // V4L2 output device setup (similar to tracker)
    bool initTargetVirtualVideoDevice(const std::string& output_video_device);
    void destroyVirtualVideoDevice();
    
    // Thermal frame processing
    void onThermalFrame(const std::vector<float>& temperatures, 
                        const uint16_t rows, 
                        const uint16_t cols);
    
    // Find hot/cold points in thermal data
    void findHotColdPoints(const cv::Mat& thermal_frame,
                          cv::Point& hot_point,
                          cv::Point& cold_point,
                          float& max_temp,
                          float& min_temp);
    
    // Convert thermal data to displayable image
    cv::Mat thermalToColorMap(const std::vector<float>& temperatures,
                              uint16_t rows, uint16_t cols);
    
    // Coordinate conversion (pixel to normalized)
    float revScaleX(const float& x) const;
    float revScaleY(const float& y) const;

private:
    // Dual camera helper methods
    cv::Mat stretchImage(const cv::Mat& image, double scale_x, double scale_y, 
                         int offset_x, int offset_y, double rotation);
    cv::Mat overlayThermalOnRGB(const cv::Mat& rgb_image, const cv::Mat& thermal_image);
    cv::Mat sideBySide(const cv::Mat& rgb_image, const cv::Mat& thermal_image);
    cv::Mat pictureInPicture(const cv::Mat& rgb_image, const cv::Mat& thermal_image, double pip_scale = 0.3);
    bool initRGBCamera(const std::string& source_video_device);
    bool m_process = false;
    std::string m_thermal_port;
    
    SerialCommandSender m_sender;
    CCallBack_IRTracker* m_callback_tracker;
    
    std::string m_output_video_path;
    bool m_output_video_active = false;
    int m_video_fd = -1;
    int m_yuv_frame_size = 0;
    
    bool m_virtual_device_opened = false;
    
    int m_image_width = 640;
    int m_image_height = 480;
    
    uint32_t m_target_fps = 30;
    
    std::thread m_framesThread;
    std::thread m_thermal_thread;
    
    uint16_t m_frames_to_skip_between_messages = 3;
    
    // Dual camera support
    bool m_dual_camera_enabled = false;
    bool m_display_enabled = false;
    int m_display_mode = 3;  // 1=separate, 2=side-by-side, 3=overlay, 4=pip
    cv::VideoCapture m_rgb_capture;
    std::string m_source_video_device;
    CalibrationParams m_calib_params;
};

}
}

#endif // IR_TRACKER_HPP