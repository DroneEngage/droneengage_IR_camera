#include "ir_camera_facade.hpp"
#include "../de_common/helpers/colors.hpp"
#include "ir_camera_main.hpp"

using namespace de::ir_camera;

void CIRCamera_Facade::sendHotColdPointsLocation(
    const std::string &target_party_id, const Json_de targets_location) const {
  if (targets_location.empty()) {
    return;
  }

  Json_de message = {{"t", targets_location}};

#ifdef DDEBUG
  std::cout << "tracking:" << targets_location.dump() << std::endl;
#endif
#ifdef DDEBUG
  std::cout << _INFO_CONSOLE_BOLD_TEXT << "onTrack >> "
            << _LOG_CONSOLE_BOLD_TEXT << targets_location.dump()
            << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
  m_module.sendJMSG(target_party_id, message,
                    TYPE_AndruavMessage_TrackingTargetLocation, true);
}

void CIRCamera_Facade::sendIRCameraStatus(
    const std::string &target_party_id, const int status) const {
  de::ir_camera::CIRCameraMain &m_camera_main =
      de::ir_camera::CIRCameraMain::getInstance();

  const uint8_t tracking_camera_direction =
      m_camera_main.getCameraDirection();
  Json_de message = {
      {"a", status},
      {"b", tracking_camera_direction}
  };

  m_module.sendJMSG(target_party_id, message,
                    TYPE_AndruavMessage_IR_CAMERA_MI48_STATUS, true);

#ifdef DEBUG
  std::cout << "TrackingStatus:" << status << std::endl;
#endif
}

