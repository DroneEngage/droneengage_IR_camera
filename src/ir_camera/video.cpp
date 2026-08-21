#include <stdio.h>
#include <string.h>
#include <iostream>
#include <fstream>       
#include <iostream>      // For input/output operations (e.g., std::cout, std::cerr)
#include <filesystem>    // For directory iteration and path manipulation (C++17+)
#include <algorithm>     // For std::remove_if (used for trimming whitespace)
#include <stdexcept>     // For std::runtime_error
#include <vector>
 #include <fcntl.h>
 #include <unistd.h>
 #include <sys/ioctl.h>
 #include <linux/videodev2.h>
 #include <errno.h>


#include "../de_common/helpers/colors.hpp"

#include "video.hpp"

using namespace de::ir_camera;

namespace fs = std::filesystem; // Use a shorter alias for std::filesystem



/**
 * @brief Queries the current resolution of a V4L2 video device.
 *
 * This function attempts to open the specified video device and use the
 * VIDIOC_G_FMT ioctl to retrieve its current width and height.
 *
 * @param video_device_path The path to the V4L2 video device (e.g., "/dev/video0").
 * @param width Reference to an unsigned int to store the retrieved width.
 * @param height Reference to an unsigned int to store the retrieved height.
 * @return true if the resolution was successfully retrieved, false otherwise.
 */
bool CVideo::getVideoResolution (const std::string& video_device_path, unsigned int& width, unsigned int& height)
{
    // Initialize output parameters
    width = 0;
    height = 0;

    // Check if the path looks like a V4L2 device
    if (video_device_path.rfind("/dev/video", 0) != 0) {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Error: " << _ERROR_CONSOLE_BOLD_TEXT_ << video_device_path << _NORMAL_CONSOLE_TEXT_ << " does not appear to be a V4L2 device path (does not start with /dev/video)." << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }

    int fd = open(video_device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cout << _ERROR_CONSOLE_TEXT_ << "Error: Failed to open V4L2 device " << _ERROR_CONSOLE_BOLD_TEXT_ << video_device_path << _NORMAL_CONSOLE_TEXT_ <<  ": " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // We are querying a capture device

    if (CVideo::xioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        width = fmt.fmt.pix.width;
        height = fmt.fmt.pix.height;
        std::cout << _SUCCESS_CONSOLE_TEXT_ << "Queried V4L2 device " << _LOG_CONSOLE_BOLD_TEXT << video_device_path
                  << _SUCCESS_CONSOLE_TEXT_ << " Resolution: " << _LOG_CONSOLE_BOLD_TEXT << width 
                  << _INFO_CONSOLE_TEXT << "x" << _LOG_CONSOLE_BOLD_TEXT << height << _NORMAL_CONSOLE_TEXT_ << std::endl;
        close(fd);
        return true;
    } else {
        std::cout << _ERROR_CONSOLE_TEXT_ << "Error: Failed to get format for V4L2 device " << video_device_path << ": " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        close(fd);
        return false;
    }   
}

bool CVideo::getMaxSupportedResolution(const std::string& video_device_path, unsigned int& max_width, unsigned int& max_height)
{
    max_width = 0;
    max_height = 0;

    if (video_device_path.rfind("/dev/video", 0) != 0) {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Error: " << _ERROR_CONSOLE_BOLD_TEXT_ << video_device_path << _NORMAL_CONSOLE_TEXT_ << " is not a V4L2 device path." << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }

    int fd = open(video_device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cout << _ERROR_CONSOLE_TEXT_ << "Error: Failed to open V4L2 device " << _ERROR_CONSOLE_BOLD_TEXT_ << video_device_path << _NORMAL_CONSOLE_TEXT_ <<  ": " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }

    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmtdesc.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; ++fmtdesc.index)
    {
        struct v4l2_frmsizeenum frmsize = {};
        frmsize.pixel_format = fmtdesc.pixelformat;
        for (frmsize.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0; ++frmsize.index)
        {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                max_width = std::max(max_width, frmsize.discrete.width);
                max_height = std::max(max_height, frmsize.discrete.height);
            }
            else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE || frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
            {
                max_width = std::max(max_width, frmsize.stepwise.max_width);
                max_height = std::max(max_height, frmsize.stepwise.max_height);
            }
        }
    }

    close(fd);

    if (max_width == 0 || max_height == 0)
    {
        std::cout << _ERROR_CONSOLE_TEXT_ << "Warning: Could not enumerate max resolution for " << video_device_path << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return false;
    }

    std::cout << _SUCCESS_CONSOLE_TEXT_ << "Max supported resolution for " << _LOG_CONSOLE_BOLD_TEXT << video_device_path
              << _SUCCESS_CONSOLE_TEXT_ << ": " << _LOG_CONSOLE_BOLD_TEXT << max_width << _INFO_CONSOLE_TEXT << "x" << _LOG_CONSOLE_BOLD_TEXT << max_height
              << _NORMAL_CONSOLE_TEXT_ << std::endl;
    return true;
}



// Function to trim leading and trailing whitespace from a string
std::string CVideo::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first) {
        return str; // No non-whitespace characters
    }
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
}

/**
 * @brief Finds the index of a video device given its name.
 *
 * This function iterates through /sys/devices/virtual/video4linux/ directories,
 * reads the 'name' or 'card' file within each, and compares it to the
 * provided target device name.
 *
 * In the Video4Linux API, when multiple nodes exist for a single camera, the primary capture device is almost always the one with the lowest device index (e.g., video0). The higher-numbered node (e.g., video6) is often for metadata or a secondary, non-video-capture function
 *
 * @param targetDeviceName The name of the video device to search for.
 * @return The integer index of the device (e.g., 0 for /dev/video0),
 * or -1 if the device is not found.
 */
int CVideo::findVideoDeviceIndex(const std::string& targetDeviceName) {
    std::cout << _LOG_CONSOLE_BOLD_TEXT << ".... Searching for video device: " << _INFO_CONSOLE_BOLD_TEXT << targetDeviceName << _NORMAL_CONSOLE_TEXT_ << std::endl;

    const std::string prefix = "video";
    std::vector<int> matchingDeviceNumbers;

    try {
        for (const auto& entry : fs::directory_iterator("/dev")) {
            const std::string filename = entry.path().filename().string();

            // Match /dev/videoX pattern
            if (filename.rfind(prefix, 0) == 0 && filename.size() > prefix.size()) {
                // ... (Device number extraction remains the same)
                std::string numStr = filename.substr(prefix.size());
                if (std::all_of(numStr.begin(), numStr.end(), [](unsigned char c){ return std::isdigit(c); })) {
                    int deviceNumber = std::stoi(numStr);
                    fs::path deviceDir = "/sys/class/video4linux/" + filename;

                    if (!fs::exists(deviceDir)) continue;

                    std::string currentCardLabel;
                    // ... (Logic to read 'name' or 'card' remains the same)
                    fs::path nameFilePath = deviceDir / "name";
                    std::ifstream nameFile(nameFilePath);
                    if (nameFile.is_open()) {
                        std::getline(nameFile, currentCardLabel);
                        nameFile.close();
                    } else {
                        fs::path cardFilePath = deviceDir / "card";
                        std::ifstream cardFile(cardFilePath);
                        if (cardFile.is_open()) {
                            std::getline(cardFile, currentCardLabel);
                            cardFile.close();
                        }
                    }
                    
                    if (trim(currentCardLabel) == trim(targetDeviceName)) {
                        matchingDeviceNumbers.push_back(deviceNumber);
                    }
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Filesystem error: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return -1;
    }

    // After iterating through all devices, select the lowest index
    if (!matchingDeviceNumbers.empty()) {
        int lowestIndex = *std::min_element(matchingDeviceNumbers.begin(), matchingDeviceNumbers.end());
        std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << ".... Found primary device: " << _INFO_CONSOLE_BOLD_TEXT << "/dev/video" << lowestIndex << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return lowestIndex;
    }

    std::cout << "Device '" << targetDeviceName << "' not found." << std::endl;
    return -1;
}