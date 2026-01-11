#ifndef IR_TrackerANDRUAV_MESSAGE_PARSER_H
#define IR_TrackerANDRUAV_MESSAGE_PARSER_H

/**
 * @file tracker_andruav_message_parser.hpp
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

#include "ir_tracker_main.hpp"
#include "ir_tracker_facade.hpp"

namespace de
{
    namespace ir_tracker
    {

        class CIRTrackerAndruavMessageParser : public de::comm::CAndruavMessageParserBase
        {
        public:
            static CIRTrackerAndruavMessageParser& getInstance()
            {
                static CIRTrackerAndruavMessageParser instance;
                return instance;
            }

            CIRTrackerAndruavMessageParser(CIRTrackerAndruavMessageParser const &) = delete;
            void operator=(CIRTrackerAndruavMessageParser const &) = delete;

        private:
            CIRTrackerAndruavMessageParser() {}

        public:
            ~CIRTrackerAndruavMessageParser() {}

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
            de::ir_tracker::CIRTrackerMain &m_ir_tracker_main = de::ir_tracker::CIRTrackerMain::getInstance();
            de::ir_tracker::CIRTracker_Facade &m_ir_tracker_facade = de::ir_tracker::CIRTracker_Facade::getInstance();
        };

    };
};

#endif