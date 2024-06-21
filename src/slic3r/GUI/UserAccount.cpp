#include "UserAccount.hpp"

#include "format.hpp"
#include "GUI.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/log/trivial.hpp>

#include <wx/stdpaths.h>

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

UserAccount::UserAccount(wxEvtHandler* evt_handler, AppConfig* app_config, const std::string& instance_hash)
    : m_communication(std::make_unique<UserAccountCommunication>(evt_handler, app_config))
    , m_instance_hash(instance_hash)
{
}

UserAccount::~UserAccount()
{}

void UserAccount::set_username(const std::string& username)
{
    m_username = username;
    m_communication->set_username(username);
}

void UserAccount::clear()
{
    m_username = {};
    m_account_user_data.clear();
    m_printer_map.clear();
    m_communication->do_clear();
}

void UserAccount::set_remember_session(bool remember)
{
    m_communication->set_remember_session(remember);
}
void UserAccount::toggle_remember_session()
{
    m_communication->set_remember_session(!m_communication->get_remember_session());
}
bool UserAccount::get_remember_session()
{
    return m_communication->get_remember_session();
}

bool UserAccount::is_logged()
{
    return m_communication->is_logged();
}
void UserAccount::do_login()
{
    m_communication->do_login();
}
void UserAccount::do_logout()
{
    m_communication->do_logout();
}

std::string UserAccount::get_access_token()
{
    return m_communication->get_access_token();
}

std::string UserAccount::get_shared_session_key()
{
    return m_communication->get_shared_session_key();
}

boost::filesystem::path UserAccount::get_avatar_path(bool logged) const
{
    if (logged) {
        const std::string filename = "prusaslicer-avatar-" + m_instance_hash + m_avatar_extension;
        return into_path(wxStandardPaths::Get().GetTempDir()) / filename;
    } else {
        return  boost::filesystem::path(resources_dir()) / "icons" / "user.svg";
    }
}

void UserAccount::enqueue_connect_printer_models_action()
{
    m_communication->enqueue_connect_printer_models_action();
}
void UserAccount::enqueue_connect_status_action()
{
    m_communication->enqueue_connect_status_action();
}
void UserAccount::enqueue_avatar_action()
{
    m_communication->enqueue_avatar_action(m_account_user_data["avatar"]);
}
void UserAccount::enqueue_printer_data_action(const std::string& uuid)
{
    m_communication->enqueue_printer_data_action(uuid);
}

bool UserAccount::on_login_code_recieved(const std::string& url_message)
{
    m_communication->on_login_code_recieved(url_message);
    return true;
}

bool UserAccount::on_user_id_success(const std::string data, std::string& out_username)
{
    boost::property_tree::ptree ptree;
    try {
        std::stringstream ss(data);
        boost::property_tree::read_json(ss, ptree);
    }
    catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(error) << "UserIDUserAction Could not parse server response.";
        return false;
    }
    m_account_user_data.clear();
    for (const auto& section : ptree) {
        const auto opt = ptree.get_optional<std::string>(section.first);
        if (opt) {
            BOOST_LOG_TRIVIAL(debug) << static_cast<std::string>(section.first) << "    " << *opt;
            m_account_user_data[section.first] = *opt;
        }
       
    }
    if (m_account_user_data.find("public_username") == m_account_user_data.end()) {
        BOOST_LOG_TRIVIAL(error) << "User ID message from PrusaAuth did not contain public_username. Login failed. Message data: " << data;
        return false;
    }
    std::string public_username = m_account_user_data["public_username"];
    set_username(public_username);
    out_username = public_username;
    // enqueue GET with avatar url
    if (m_account_user_data.find("avatar") != m_account_user_data.end()) {
        const boost::filesystem::path server_file(m_account_user_data["avatar"]);
        m_avatar_extension = server_file.extension().string();
        enqueue_avatar_action();
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "User ID message from PrusaAuth did not contain avatar.";
    }
    // update printers list
    enqueue_connect_printer_models_action();
    return true;
}

void UserAccount::on_communication_fail()
{
    m_fail_counter++;
    if (m_fail_counter > 5) // there is no deeper reason why 5
    {
        m_communication->enqueue_test_connection();
        m_fail_counter = 0;
    }
}

namespace {
    std::string parse_tree_for_param(const pt::ptree& tree, const std::string& param)
    {
        for (const auto& section : tree) {
            if (section.first == param) {
                return section.second.data();
            }
            if (std::string res = parse_tree_for_param(section.second, param); !res.empty()) {
                return res;
            }
        }
        return {};
    }

    void parse_tree_for_param_vector(const pt::ptree& tree, const std::string& param, std::vector<std::string>& results)
    {
        for (const auto& section : tree) {
            if (section.first == param) {
                results.emplace_back(section.second.data());
            } else {
                parse_tree_for_param_vector(section.second, param, results);
            }
        }
    }

    pt::ptree parse_tree_for_subtree(const pt::ptree& tree, const std::string& param)
    {
        for (const auto& section : tree) {
            if (section.first == param) {
                return section.second;
            }
            else {
                if (pt::ptree res = parse_tree_for_subtree(section.second, param); !res.empty())
                    return res;
            }

        }
        return pt::ptree();
    }

    void fill_supported_printer_models_from_json_inner(const pt::ptree& ptree, std::vector<std::string>& result) {
        std::string printer_model = parse_tree_for_param(ptree, "printer_model");
        if (!printer_model.empty()) {
            result.emplace_back(printer_model);
        }
        pt::ptree out = parse_tree_for_subtree(ptree, "supported_printer_models");
        if (out.empty()) {
            BOOST_LOG_TRIVIAL(error) << "Failed to find supported_printer_models in printer detail.";
            return;
        }
        for (const auto& sub : out) {
            if (printer_model != sub.second.data()) {
                result.emplace_back(sub.second.data());
            }

        }
    }
}

bool UserAccount::on_connect_printers_success(const std::string& data, AppConfig* app_config, bool& out_printers_changed)
{
    BOOST_LOG_TRIVIAL(debug) << "Prusa Connect printers message: " << data;
    pt::ptree ptree;
    try {
        std::stringstream ss(data);
        pt::read_json(ss, ptree);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
        return false;
    }
    // tree format: 
    /*
    [{
        "printer_uuid": "972d2ce7-0967-4555-bff2-330c7fa0a4e1",
            "printer_state" : "IDLE"
    }, {
        "printer_uuid": "15d160fd-c7e4-4b5a-9748-f0e531c7e0f5",
        "printer_state" : "OFFLINE"
    }]
    */
    ConnectPrinterStateMap new_printer_map;
    for (const auto& printer_tree : ptree) {
        const auto printer_uuid = printer_tree.second.get_optional<std::string>("printer_uuid");
        if (!printer_uuid) {
            continue;
        }
        const auto printer_state = printer_tree.second.get_optional<std::string>("printer_state");
        if (!printer_state) {
            continue;
        }
        ConnectPrinterState state;
        if (auto pair = printer_state_table.find(*printer_state); pair != printer_state_table.end()) {
            state = pair->second;
        } else {
            assert(true); // On this assert, printer_state_table needs to be updated with *state_opt and correct ConnectPrinterState
            continue;
        }
        if (m_printer_uuid_map.find(*printer_uuid) == m_printer_uuid_map.end()) {
            BOOST_LOG_TRIVIAL(error) << "Missing printer model for printer uuid: " << *printer_uuid;
            continue;
        }
        std::pair<std::string, std::string> model_nozzle_pair = m_printer_uuid_map[*printer_uuid];

        if (new_printer_map.find(model_nozzle_pair) == new_printer_map.end()) {
            new_printer_map[model_nozzle_pair].reserve(static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT));
            for (size_t i = 0; i < static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT); i++) {
                new_printer_map[model_nozzle_pair].push_back(0);
            }
        }
        new_printer_map[model_nozzle_pair][static_cast<size_t>(state)] += 1;
    }

    // compare new and old printer map and update old map into new
    out_printers_changed = false;
    for (const auto& it : new_printer_map) {
        if (m_printer_map.find(it.first) == m_printer_map.end()) {
            // printer is not in old map, add it by copying data from new map
            out_printers_changed = true;
            m_printer_map[it.first].reserve(static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT));
            for (size_t i = 0; i < static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT); i++) {
                m_printer_map[it.first].push_back(new_printer_map[it.first][i]);
            }
        } else {
            // printer is in old map, check state by state
            for (size_t i = 0; i < static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT); i++) {
                if (m_printer_map[it.first][i] != new_printer_map[it.first][i])  {
                    out_printers_changed = true;
                    m_printer_map[it.first][i] = new_printer_map[it.first][i];
                }
            }
        }
    }
    return true;
}

bool UserAccount::on_connect_uiid_map_success(const std::string& data, AppConfig* app_config, bool& out_printers_changed)
{
    m_printer_uuid_map.clear();
    pt::ptree ptree;
    try {
        std::stringstream ss(data);
        pt::read_json(ss, ptree);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
        return false;
    }

    for (const auto& printer_tree : ptree) {
        const auto printer_uuid = printer_tree.second.get_optional<std::string>("printer_uuid");
        if (!printer_uuid) {
            continue;
        }
        const auto printer_model = printer_tree.second.get_optional<std::string>("printer_model");
        if (!printer_model) {
            continue;
        }
        const auto nozzle_diameter_opt = printer_tree.second.get_optional<std::string>("nozzle_diameter");
        const std::string nozzle_diameter = (nozzle_diameter_opt && *nozzle_diameter_opt != "0.0") ? *nozzle_diameter_opt : std::string();
        std::pair<std::string, std::string> model_nozzle_pair = { *printer_model, nozzle_diameter };
        m_printer_uuid_map[*printer_uuid] = model_nozzle_pair;
    }
    m_communication->on_uuid_map_success();
    return on_connect_printers_success(data, app_config, out_printers_changed);
}

std::string UserAccount::get_current_printer_uuid_from_connect(const std::string& selected_printer_id) const
{
    if (m_current_printer_data_json_from_connect.empty() || m_current_printer_uuid_from_connect.empty()) {
        return {};
    }

    pt::ptree ptree;
    try {
        std::stringstream ss(m_current_printer_data_json_from_connect);
        pt::read_json(ss, ptree);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse Printer data from Connect. " << e.what();
        return {};
    }

    std::string data_uuid = parse_tree_for_param(ptree, "uuid");
    assert(data_uuid == m_current_printer_uuid_from_connect);

    //std::string model_name = parse_tree_for_param(ptree, "printer_model");
    std::vector<std::string> compatible_printers;
    fill_supported_printer_models_from_json_inner(ptree, compatible_printers);
    if (compatible_printers.empty()) {
        return {};
    }
    
    return std::find(compatible_printers.begin(), compatible_printers.end(), selected_printer_id) == compatible_printers.end() ? "" : m_current_printer_uuid_from_connect;
}


std::string UserAccount::get_nozzle_from_json(const std::string& message) const
{
    std::string out;
    try {
        std::stringstream ss(message);
        pt::ptree ptree;
        pt::read_json(ss, ptree);

        out = parse_tree_for_param(ptree, "nozzle_diameter");
        //assert(!out.empty());
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
    }

    // Get rid of trailing zeros.
    // This is because somtimes we get "nozzle_diameter":0.40000000000000002
    // This will return wrong result for f.e. 0.05. But we dont have such profiles right now.
    if (size_t fist_dot = out.find('.'); fist_dot != std::string::npos) {
        if (size_t first_zero = out.find('0', fist_dot); first_zero != std::string::npos) {
            return out.substr(0, first_zero);
        }
    }
    return out;
}

std::string UserAccount::get_keyword_from_json(const std::string& json, const std::string& keyword) const
{
    std::string out;
    try {
        std::stringstream ss(json);
        pt::ptree ptree;
        pt::read_json(ss, ptree);

        out = parse_tree_for_param(ptree, keyword);
        //assert(!out.empty());
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
    }
    return out;
}

std::string UserAccount::get_print_data_from_json(const std::string &json, const std::string &keyword) const
{
    // copy subtree string f.e.
    // { "<keyword>": {"param1": "something", "filename":"abcd.gcode", "param3":true}, "something_else" : 0 }
    // into: {"param1": "something", "filename":"%1%", "param3":true, "size":%2%}
    // yes there will be 2 placeholders for later format

    // this will fail if not flat subtree
    size_t start_of_keyword = json.find("\""+keyword+"\"");
    if (start_of_keyword == std::string::npos)  
        return {};
    size_t start_of_sub = json.find('{', start_of_keyword);
    if (start_of_sub == std::string::npos)
        return {};
    size_t end_of_sub = json.find('}', start_of_sub);
    if (end_of_sub == std::string::npos)
        return {};
    size_t start_of_filename = json.find("\"filename\"", start_of_sub);
    if (start_of_filename == std::string::npos)
        return {};
    size_t filename_doubledot = json.find(':', start_of_filename);
    if (filename_doubledot == std::string::npos)
        return {};
    size_t start_of_filename_data = json.find('\"', filename_doubledot);
    if (start_of_filename_data == std::string::npos)
        return {};
    size_t end_of_filename_data = json.find('\"', start_of_filename_data + 1);
    if (end_of_filename_data == std::string::npos)
        return {};
    size_t size = json.size();
    std::string result = json.substr(start_of_sub, start_of_filename_data - start_of_sub + 1);
    result += "%1%";
    result += json.substr(end_of_filename_data, end_of_sub - end_of_filename_data);
    result += ",\"size\":%2%}";
    return result;
}

void UserAccount::fill_supported_printer_models_from_json(const std::string& json, std::vector<std::string>& result) const
{
    try {
        std::stringstream ss(json);
        pt::ptree ptree;
        pt::read_json(ss, ptree);

        fill_supported_printer_models_from_json_inner(ptree, result);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
    }
}

void UserAccount::fill_material_from_json(const std::string& json, std::vector<std::string>& result) const
{

    /* option 1: 
    "slot": {
			"active": 2,
			"slots": {
				"1": {
					"material": "PLA",
					"temp": 170,
					"fan_hotend": 7689,
					"fan_print": 0
				},
				"2": {
					"material": "PLA",
					"temp": 225,
					"fan_hotend": 7798,
					"fan_print": 6503
				},
				"3": {
					"material": "PLA",
					"temp": 36,
					"fan_hotend": 6636,
					"fan_print": 0
				},
				"4": {
					"material": "PLA",
					"temp": 35,
					"fan_hotend": 0,
					"fan_print": 0
				},
				"5": {
					"material": "PETG",
					"temp": 136,
					"fan_hotend": 8132,
					"fan_print": 0
				}
			}
		}
    */
    /* option 2
        "filament": {
			"material": "PLA",
			"bed_temperature": 60,
			"nozzle_temperature": 210
		}
    */
    // try finding "slot" subtree a use it to
    // if not found, find "filament" subtree
    try {
        std::stringstream ss(json);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        // find "slot" subtree
        pt::ptree slot_subtree = parse_tree_for_subtree(ptree, "slot");
        if (slot_subtree.empty()) {
            // if not found, find "filament" subtree
            pt::ptree filament_subtree = parse_tree_for_subtree(ptree, "filament");
            if (!filament_subtree.empty()) {
                std::string material = parse_tree_for_param(filament_subtree, "material");
                if (!material.empty()) {
                    result.emplace_back(std::move(material));
                }
            }
            return;
        }
        // search "slot" subtree for all "material"s
        parse_tree_for_param_vector(slot_subtree, "material", result);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
    }
}

}} // namespace slic3r::GUI