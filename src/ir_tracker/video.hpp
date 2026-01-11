#ifndef VIDEO_H
#define VIDEO_H

// Headers for V4L2
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

namespace de
{
namespace ir_tracker
{

class CVideo
{

    public:
    
        /**
         * @brief Wrapper for the ioctl system call that handles interrupted system calls.
         *
         * This function repeatedly calls ioctl until it either succeeds or fails for a reason
         * other than being interrupted by a signal (EINTR). This is useful for ensuring that
         * transient interruptions do not cause the operation to fail.
         *
         * @param fh      File handle on which to perform the ioctl operation.
         * @param request Device-dependent request code.
         * @param arg     Pointer to memory containing arguments for the ioctl request.
         * @return        Result of the ioctl call. Returns -1 on error, otherwise returns the result of ioctl.
         */
        static inline int xioctl(int fh, unsigned long request, void *arg) {
            int r;
            do {
                r = ioctl(fh, request, arg);
            } while (-1 == r && EINTR == errno);
            return r;
        }

        static bool getVideoResolution(const std::string& video_device_path, unsigned int& width, unsigned int& height);
        static bool getMaxSupportedResolution(const std::string& video_device_path, unsigned int& max_width, unsigned int& max_height);

        static int findVideoDeviceIndex(const std::string& targetDeviceName);

    private:

        static std::string trim(const std::string& str);
};

}
}

#endif
