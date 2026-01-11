#ifndef IR_TrackerMAIN_MODULE_H
#define IR_TrackerMAIN_MODULE_H


#include "../de_common/de_databus/messages.hpp"

#include "../de_common/helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

namespace de
{
namespace ir_tracker
{
    class CIRTrackerMain
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
       
        public:

        inline bool getCameraFlipped() const
        {
            return m_camera_flipped;
        }

        public:
            //CCommon_Callback
            void OnConnectionStatusChangedWithAndruavServer (const int status) {};
        
        
        private:
            bool readConfigParameters();
            void reloadParametersIfConfigChanged();

        private:
            // Parsed configuration values
            uint16_t m_camera_orientation = DEF_TRACK_ORIENTATION_DEG_0;
            // camera image is flipped
            bool m_camera_flipped = false;
            uint8_t m_tracking_camera_direction = TRACKING_CAMERA_DIRECTION_NONE;
            
    };
}
}

#endif
