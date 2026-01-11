#ifndef AI_CAMERA_FACADE_MODULE_H
#define AI_CAMERA_FACADE_MODULE_H


#include "../de_common/helpers/json_nlohmann.hpp"

using Json_de = nlohmann::json;

#include "../de_common/de_databus/de_facade_base.hpp"
#include "../de_common/de_databus/messages.hpp"


namespace de
{
namespace ir_tracker
{
    class CIRTracker_Facade : public de::comm::CFacade_Base
    {
        public:
            //https://stackoverflow.com/questions/1008019/c-singleton-design-pattern
            static CIRTracker_Facade& getInstance()
            {
                static CIRTracker_Facade instance;

                return instance;
            }

            CIRTracker_Facade(CIRTracker_Facade const&)         = delete;
            void operator=(CIRTracker_Facade const&)          = delete;

        
        private:

            CIRTracker_Facade()
            {
            };
        
         public:
            
            ~CIRTracker_Facade ()
            {
                
            };
                
        public:
            void sendTrackingTargetsLocation(const std::string& target_party_id, const Json_de targets_location) const;
            void sendTrackingTargetStatus(const std::string& target_party_id, const int status) const;
            
            
       
    };
}
}
#endif
