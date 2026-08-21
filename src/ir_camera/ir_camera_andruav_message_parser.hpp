#ifndef IR_CameraANDRUAV_MESSAGE_PARSER_H
#define IR_CameraANDRUAV_MESSAGE_PARSER_H

/**
 * @file ir_camera_andruav_message_parser.hpp
 * @author Mohammad S. Hefny (mohammad.hefny@gmail.com)
 * @brief
 * @version 0.1
 * @date 2022-03-24
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "../de_common/helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

#include "../de_common/de_databus/de_message_parser_base.hpp"

#include "ir_camera_main.hpp"
#include "ir_camera_facade.hpp"

namespace de
{
    namespace ir_camera
    {

        class CIRCameraAndruavMessageParser : public de::comm::CAndruavMessageParserBase
        {
        public:
            static CIRCameraAndruavMessageParser& getInstance()
            {
                static CIRCameraAndruavMessageParser instance;
                return instance;
            }

            CIRCameraAndruavMessageParser(CIRCameraAndruavMessageParser const &) = delete;
            void operator=(CIRCameraAndruavMessageParser const &) = delete;

        private:
            CIRCameraAndruavMessageParser() {}

        public:
            ~CIRCameraAndruavMessageParser() {}

        protected:
            void parseRemoteExecute(Json_de &andruav_message) override;
            void parseCommand(Json_de &andruav_message, const char *full_message, const int &full_message_length, int messageType, uint32_t permission) override;

    
            inline bool validateField(const Json_de &message, const char *field_name, const Json_de::value_t field_type)
            {
                if (
                    (message.contains(field_name) == false) || (message[field_name].type() != field_type))
                    return false;

                return true;
            }

        private:
            de::ir_camera::CIRCameraMain &m_ir_camera_main = de::ir_camera::CIRCameraMain::getInstance();
            de::ir_camera::CIRCamera_Facade &m_ir_camera_facade = de::ir_camera::CIRCamera_Facade::getInstance();
        };

    };
};

#endif