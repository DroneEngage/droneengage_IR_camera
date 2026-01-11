#ifndef IR_TrackerMAIN_MODULE_H
#define IR_TrackerMAIN_MODULE_H

#include <thread>
#include <memory>
#include "../de_common/de_databus/messages.hpp"
#include "../de_common/helpers/json_nlohmann.hpp"
#include "ir_tracker.hpp"
#include "ir_tracker_facade.hpp"

using Json_de = nlohmann::json;

namespace de
{
namespace ir_tracker
{
    class CIRTrackerMain: public CCallBack_IRTracker
    {
    public:
        static CIRTrackerMain& getInstance()
            {
                static CIRTrackerMain instance;

                return instance;
            }

            CIRTrackerMain(CIRTrackerMain const&)             = delete;
            void operator=(CIRTrackerMain const&)            = delete;

        
            // Note: Scott Meyers mentions in his Effective Modern
            //       C++ book, that deleted functions should generally
            //       be public as it results in better error messages
            //       due to the compilers behavior to check accessibility
            //       before deleted status

        private:

            CIRTrackerMain()
            {
            }
       
            ~CIRTrackerMain()
            {
                if (m_exit_thread == false)
                {
                    uninit();
                }
            };

        public:
            bool init();
            bool uninit();
            
            void loopScheduler();

        public:
            void enableTracking();
            void stopTracking();
            void pauseTracking();

        public:
            inline bool getCameraFlipped() const
            {
                return m_camera_flipped;
            }

            inline uint8_t getTrackingCameraDirection() const
            {
                return m_tracking_camera_direction;
            }

        public:
            //CCommon_Callback
            void OnConnectionStatusChangedWithAndruavServer (const int status) {};
        
            // Callback from CIRTracker
            void onHotColdPoints(const float& hot_x, const float& hot_y,
                        const float& cold_x, const float& cold_y,
                        const float& max_temp, const float& min_temp,
                        const bool should_skip_message) override;
    
            void onIRStatusChanged(const int& status) override;

        private:
            bool readConfigParameters();
            void reloadParametersIfConfigChanged();

        private:
            bool m_exit_thread = true;
            std::thread m_scheduler_thread;
            u_int64_t m_counter = 0;
            
            int m_ir_status = TrackingTarget_STATUS_TRACKING_STOPPED;
            
            // EMA smoothing for hot point
            bool m_ema_hot_init = false;
            double m_ema_hot_x = 0, m_ema_hot_y = 0;
            
            // EMA smoothing for cold point
            bool m_ema_cold_init = false;
            double m_ema_cold_x = 0, m_ema_cold_y = 0;
            
            double m_ema_alpha_base = 0.3;
            
            std::unique_ptr<de::ir_tracker::CIRTracker> m_tracker;
            de::ir_tracker::CIRTracker_Facade& m_ir_tracker_facade = de::ir_tracker::CIRTracker_Facade::getInstance();

            // Parsed configuration values
            uint16_t m_camera_orientation = DEF_TRACK_ORIENTATION_DEG_0;
            bool m_camera_flipped = false;
            uint8_t m_tracking_camera_direction = TRACKING_CAMERA_DIRECTION_NONE;
            
            std::string m_source_ir_port_device;
            std::string m_source_video_device;
            std::string m_output_video_device;
            
            uint16_t m_frames_to_skip_between_messages = 3;
            
            // Dual camera configuration
            bool m_dual_camera_enabled = false;
            bool m_display_enabled = false;
            int m_display_mode = 3;
            double m_calib_scale_x = 1.0;
            double m_calib_scale_y = 1.0;
            int m_calib_offset_x = 0;
            int m_calib_offset_y = 0;
            double m_calib_rotation = 0.0;
            double m_calib_alpha = 0.5;
    };
}
}

#endif
