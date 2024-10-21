#include "json2pmgr.h"
#include "json.h"

int json2pmgr_values(std::string &values_json) {
    using namespace nlohmann;
    try {
        json jdefs = {
            /* increment this number each time you actualize this structure */
            {"PMGR_BINDING_VERSION", 1},

            /* defines related to object names */
            {"PMGR_MAX_TASK_NAME", PMGR_MAX_TASK_NAME},
            {"PMGR_MAX_TASK_PATH", PMGR_MAX_TASK_PATH},
            {"PMGR_MAX_TASK_USR", PMGR_MAX_TASK_USR},
            {"PMGR_MAX_TASK_GRP", PMGR_MAX_TASK_GRP},

            /* defines related to addreeses */
            {"PMGR_CHAN_TCP_PORT", PMGR_CHAN_TCP_PORT},
            {"PMGR_CHAN_UN_NAME", PMGR_CHAN_UN_NAME},
            {"PMGR_CHAN_UN_PERM", PMGR_CHAN_UN_PERM},

            {"pmgr_msg_type_e", {
                {"PMGR_MSG_START", PMGR_MSG_START},
                {"PMGR_MSG_WAITSTART", PMGR_MSG_WAITSTART},
                {"PMGR_MSG_STOP", PMGR_MSG_STOP},
                {"PMGR_MSG_WAITSTOP", PMGR_MSG_WAITSTOP},
                {"PMGR_MSG_ADD", PMGR_MSG_ADD},
                {"PMGR_MSG_RM", PMGR_MSG_RM},
                {"PMGR_MSG_WAITRM", PMGR_MSG_WAITRM},
                {"PMGR_MSG_LIST", PMGR_MSG_LIST},
                {"PMGR_MSG_LOAD_CFG", PMGR_MSG_LOAD_CFG},
                {"PMGR_MSG_CLEAR", PMGR_MSG_CLEAR},
                {"PMGR_MSG_GET_PID", PMGR_MSG_GET_PID},
                {"PMGR_MSG_GET_NAME", PMGR_MSG_GET_NAME},
                {"PMGR_MSG_EVENT_LOOP", PMGR_MSG_EVENT_LOOP},
                {"PMGR_MSG_REGISTER_EVENT", PMGR_MSG_REGISTER_EVENT},
                {"PMGR_MSG_UNREGISTER_EVENT", PMGR_MSG_UNREGISTER_EVENT},
                {"PMGR_MSG_REPLAY", PMGR_MSG_REPLAY},
                {"PMGR_MSG_RETVAL", PMGR_MSG_RETVAL},
                {"PMGR_CHAN_REGISTER", PMGR_CHAN_REGISTER},
                {"PMGR_CHAN_IDENTITY", PMGR_CHAN_IDENTITY},
                {"PMGR_CHAN_MESSAGE", PMGR_CHAN_MESSAGE},
                {"PMGR_CHAN_GET_IDENT", PMGR_CHAN_GET_IDENT},
                {"PMGR_CHAN_ON_DISCON", PMGR_CHAN_ON_DISCON},
                {"PMGR_CHAN_LIST", PMGR_CHAN_LIST},
                {"PMGR_CHAN_SELF", PMGR_CHAN_SELF},
            }},

            {"pmgr_task_state_e", {
                {"PMGR_TASK_STATE_INIT", PMGR_TASK_STATE_INIT},
                {"PMGR_TASK_STATE_STOPPED", PMGR_TASK_STATE_STOPPED},
                {"PMGR_TASK_STATE_STOPING", PMGR_TASK_STATE_STOPING},
                {"PMGR_TASK_STATE_RUNNING", PMGR_TASK_STATE_RUNNING},
            }},

            {"pmgr_task_flags_e", {
                {"PMGR_TASK_FLAG_PERSIST", PMGR_TASK_FLAG_PERSIST},
                {"PMGR_TASK_FLAG_NOSTDIO", PMGR_TASK_FLAG_NOSTDIO},
                {"PMGR_TASK_FLAG_PWDSELF", PMGR_TASK_FLAG_PWDSELF},
                {"PMGR_TASK_FLAG_AUTORUN", PMGR_TASK_FLAG_AUTORUN},
                {"PMGR_TASK_FLAG_MASK", PMGR_TASK_FLAG_MASK},
            }},

            {"pmgr_event_e", {
                {"PMGR_EVENT_TASK_START", PMGR_EVENT_TASK_START},
                {"PMGR_EVENT_TASK_STOP", PMGR_EVENT_TASK_STOP},
                {"PMGR_EVENT_TASK_ADD", PMGR_EVENT_TASK_ADD},
                {"PMGR_EVENT_TASK_RM", PMGR_EVENT_TASK_RM},
                {"PMGR_EVENT_CFG_RELOAD", PMGR_EVENT_CFG_RELOAD},
                {"PMGR_EVENT_CLEAR", PMGR_EVENT_CLEAR},
                {"PMGR_EVENT_MASK", PMGR_EVENT_MASK},
            }},

            {"pmgr_event_flags_e", {
                {"PMGR_EVENT_FLAG_PID_FILTER", PMGR_EVENT_FLAG_PID_FILTER},
                {"PMGR_EVENT_FLAG_NAME_FILTER", PMGR_EVENT_FLAG_NAME_FILTER},
                {"PMGR_EVENT_FLAGS_MASK", PMGR_EVENT_FLAGS_MASK},
            }},

            /* pmgr_chan_flags_e */
            {"pmgr_chan_flags_e", {
                {"PMGR_CHAN_CREAT", PMGR_CHAN_CREAT},
                {"PMGR_CHAN_WAITC", PMGR_CHAN_WAITC},
                {"PMGR_CHAN_BCAST", PMGR_CHAN_BCAST},
            }},
        };

        values_json = jdefs.dump(4, ' ');
    }
    catch (json::exception& e) {
        DBG("Config error: %s", e.what());
        return -1;
    }
    return 0;
}

#define TRANSFER_HELPER \
        ptr = (pmgr_hdr_t *)(_ptr);\
        using ptr_type_t = decltype(_ptr);\
        free_fn = [](pmgr_hdr_t *p) { delete (ptr_type_t)p; };

#define COPY_STRING(dst, src, maxlen) \
        if ((src).get<std::string>().size() + 1 > (maxlen)) { \
            DBG("String too large"); \
            return -1; \
        } \
        strcpy((dst), (src).get<std::string>().c_str());

int json2pmgr(std::string &src, std::shared_ptr<pmgr_hdr_t> &dst) {
    using namespace nlohmann;
    try {
        json jsrc = json::parse(src, nullptr, true, true);
        pmgr_hdr_t *ptr;
        std::function<void(pmgr_hdr_t *p)> free_fn;
        pmgr_msg_type_e msg_type = (pmgr_msg_type_e)jsrc["hdr"]["type"].get<int>();

        switch (msg_type) {
            case PMGR_MSG_START:
            case PMGR_MSG_WAITSTART:
            case PMGR_MSG_STOP:
            case PMGR_MSG_WAITSTOP:
            case PMGR_MSG_RM:
            case PMGR_MSG_WAITRM: {
                auto _ptr = new pmgr_task_name_t{
                    .hdr = { .size = sizeof(pmgr_task_name_t), .type = msg_type },
                };
                FnScope scope([&_ptr]{ delete _ptr; });
                COPY_STRING(_ptr->task_name, jsrc["task_name"], PMGR_MAX_TASK_NAME);
                scope.disable();
                TRANSFER_HELPER
            }
            break;

            case PMGR_MSG_ADD:
            case PMGR_MSG_REPLAY: {
                auto _ptr = new pmgr_task_t{
                    .hdr = { .size = sizeof(pmgr_task_t), .type = msg_type },
                    .p = jsrc["p"].get<uint64_t>(),
                    .pid = jsrc["pid"].get<uint64_t>(),
                    .state = (pmgr_task_state_e)jsrc["state"].get<int32_t>(),
                    .flags = (pmgr_task_flags_e)jsrc["flags"].get<int32_t>(),
                    .list_terminator = jsrc["list_terminator"].get<int32_t>(),
                };

                FnScope scope([&_ptr]{ delete _ptr; });
                COPY_STRING(_ptr->task_name, jsrc["task_name"], PMGR_MAX_TASK_NAME);
                COPY_STRING(_ptr->task_pwd, jsrc["task_pwd"], PMGR_MAX_TASK_PATH);
                COPY_STRING(_ptr->task_usr, jsrc["task_usr"], PMGR_MAX_TASK_USR);
                COPY_STRING(_ptr->task_grp, jsrc["task_grp"], PMGR_MAX_TASK_GRP);
                COPY_STRING(_ptr->task_path, jsrc["task_path"], PMGR_MAX_TASK_PATH);
                scope.disable();
                TRANSFER_HELPER;
            }
            break;

            case PMGR_MSG_LIST:
            case PMGR_MSG_CLEAR:
            case PMGR_MSG_LOAD_CFG: {
                auto _ptr = new pmgr_hdr_t{
                    .size = sizeof(pmgr_hdr_t),
                    .type = msg_type
                };

                TRANSFER_HELPER;
            }
            break;

            case PMGR_MSG_REGISTER_EVENT:
            case PMGR_MSG_UNREGISTER_EVENT:
            case PMGR_MSG_EVENT_LOOP: {
                auto _ptr = new pmgr_event_t{
                    .hdr = { .size = sizeof(pmgr_event_t), .type = msg_type },
                    .ev_type = (pmgr_event_e)jsrc["ev_type"].get<int32_t>(),
                    .ev_flags = (pmgr_event_flags_e)jsrc["ev_flags"].get<int32_t>(),
                    .task_pid = jsrc["task_pid"].get<int64_t>(),
                };
                FnScope scope([&_ptr]{ delete _ptr; });
                COPY_STRING(_ptr->task_name, jsrc["task_name"], PMGR_MAX_TASK_NAME);
                scope.disable();

                TRANSFER_HELPER;
            }
            break;

            case PMGR_CHAN_IDENTITY:
            case PMGR_MSG_GET_PID:
            case PMGR_MSG_GET_NAME: {
                auto _ptr = new pmgr_chann_identity_t{
                    .hdr = { .size = sizeof(pmgr_chann_identity_t), .type = msg_type },
                    .task_pid = jsrc["task_pid"].get<int64_t>(),
                };
                FnScope scope([&_ptr]{ delete _ptr; });
                COPY_STRING(_ptr->task_name, jsrc["task_name"], PMGR_MAX_TASK_NAME);
                scope.disable();

                TRANSFER_HELPER;
            }
            break;

            case PMGR_MSG_RETVAL: {
                auto _ptr = new pmgr_return_t{
                    .hdr = { .size = sizeof(pmgr_return_t), .type = msg_type },
                    .retval = jsrc["retval"].get<int32_t>(),
                };

                TRANSFER_HELPER;
            }
            break;

            case PMGR_CHAN_REGISTER: {
                auto _ptr = new pmgr_chann_t{
                    .hdr = { .size = sizeof(pmgr_chann_t), .type = msg_type },
                    .flags = (pmgr_chan_flags_e)jsrc["flags"].get<int32_t>(),
                };
                FnScope scope([&_ptr]{ delete _ptr; });
                COPY_STRING(_ptr->chan_name, jsrc["chan_name"], PMGR_MAX_TASK_NAME);
                scope.disable();

                TRANSFER_HELPER;
            }
            break;

            case PMGR_CHAN_MESSAGE: {
                std::string contents = jsrc["contents"].get<std::string>();
                if (contents.size() > 2000'000'000) {
                    DBG("Contents are too large...");
                    return -1;
                }
                int data_len = sizeof(pmgr_chann_msg_t) + contents.size() + 1;
                uint8_t *data = new uint8_t[data_len];
                auto msg = (pmgr_chann_msg_t *)data;
                *msg = pmgr_chann_msg_t {
                    .hdr = { .size = sizeof(pmgr_chann_msg_t), .type = msg_type },
                    .flags = (pmgr_chan_flags_e)jsrc["flags"].get<int32_t>(),
                    .src_id = jsrc["src_id"].get<int64_t>(),
                    .dst_id = jsrc["dst_id"].get<int64_t>(),
                };
                memcpy(msg + 1, contents.c_str(), contents.size() + 1);

                ptr = (pmgr_hdr_t *)data;
                free_fn = [](pmgr_hdr_t *p) { delete [] (uint8_t *)p; };
            }
            break;

            case PMGR_CHAN_GET_IDENT:
            case PMGR_CHAN_SELF:
            case PMGR_CHAN_LIST:
            case PMGR_CHAN_ON_DISCON: {
                auto _ptr = new pmgr_chann_msg_t{
                    .hdr = { .size = sizeof(pmgr_chann_msg_t), .type = msg_type },
                    .flags = (pmgr_chan_flags_e)jsrc["flags"].get<int32_t>(),
                    .src_id = jsrc["src_id"].get<int64_t>(),
                    .dst_id = jsrc["dst_id"].get<int64_t>(),
                };

                TRANSFER_HELPER;
            }
            break;
            default : {
                DBG("Unknown message type");
                return -1;
            }
        }
        dst = std::shared_ptr<pmgr_hdr_t>(ptr, free_fn);
    }
    catch (json::exception& e) {
        DBG("Failed to parse object: %s", e.what());
        return -1;
    }
    return 0;
}

#define VALIDATE_SIZE(hdr, type) { \
    if ((hdr)->size != sizeof(type)) { \
        DBG("Invalid size"); \
        return -1; \
    } \
} 
int json2pmgr(pmgr_hdr_t *src, std::string &dst) {
    using namespace nlohmann;
    switch (src->type) {
        case PMGR_MSG_START:
        case PMGR_MSG_WAITSTART:
        case PMGR_MSG_STOP:
        case PMGR_MSG_WAITSTOP:
        case PMGR_MSG_RM:
        case PMGR_MSG_WAITRM: {
            VALIDATE_SIZE(src, pmgr_task_name_t);
            auto msg = (pmgr_task_name_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"task_name", msg->task_name},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_MSG_ADD:
        case PMGR_MSG_REPLAY: {
            VALIDATE_SIZE(src, pmgr_task_t);
            auto msg = (pmgr_task_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"p", (int64_t)msg->p},
                {"pid", (int64_t)msg->pid},
                {"state", (int32_t)msg->state},
                {"flags", (int32_t)msg->flags},
                {"list_terminator", (int32_t)msg->list_terminator},
                {"task_name", msg->task_name},
                {"task_pwd", msg->task_pwd},
                {"task_usr", msg->task_usr},
                {"task_grp", msg->task_grp},
                {"task_path", msg->task_path},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_MSG_LIST:
        case PMGR_MSG_CLEAR:
        case PMGR_MSG_LOAD_CFG: {
            VALIDATE_SIZE(src, pmgr_hdr_t);
            json jdst = {
                "hdr", {
                    {"type", (int32_t)src->type},
                    {"size", (int32_t)src->size}
                }
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_MSG_REGISTER_EVENT:
        case PMGR_MSG_UNREGISTER_EVENT:
        case PMGR_MSG_EVENT_LOOP: {
            VALIDATE_SIZE(src, pmgr_event_t);
            auto msg = (pmgr_event_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"ev_type", (int32_t)msg->ev_type},
                {"ev_flags", (int32_t)msg->ev_flags},
                {"task_name", msg->task_name},
                {"task_pid", (int64_t)msg->task_pid},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_CHAN_IDENTITY:
        case PMGR_MSG_GET_PID:
        case PMGR_MSG_GET_NAME: {
            VALIDATE_SIZE(src, pmgr_chann_identity_t);
            auto msg = (pmgr_chann_identity_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"task_name", msg->task_name},
                {"task_pid", (int64_t)msg->task_pid},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_MSG_RETVAL: {
            VALIDATE_SIZE(src, pmgr_return_t);
            auto msg = (pmgr_return_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"retval", (int32_t)msg->retval},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_CHAN_REGISTER: {
            VALIDATE_SIZE(src, pmgr_chann_t);
            auto msg = (pmgr_chann_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"flags", (int32_t)msg->flags},
                {"chan_name", msg->chan_name},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_CHAN_MESSAGE: {
            if (src->size < sizeof(pmgr_chann_msg_t)) {
                DBG("Invalid size");
                return -1;
            }
            // we need to make sure that the content is a string, else just don't add it
            auto msg = (pmgr_chann_msg_t *)src;
            auto contents = (char *)msg + 1;
            bool has_null = false;
            for (int i = 0; i < src->size - sizeof(pmgr_chann_msg_t); i++)
                if (contents[i] == '\0')
                    has_null = true;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"flags", (int32_t)msg->flags},
                {"src_id", (int64_t)msg->src_id},
                {"dst_id", (int64_t)msg->dst_id},
                {"contents", has_null ? contents : ""},
            };
            dst = jdst.dump(4, ' ');
        }
        break;

        case PMGR_CHAN_GET_IDENT:
        case PMGR_CHAN_SELF:
        case PMGR_CHAN_LIST:
        case PMGR_CHAN_ON_DISCON: {
            VALIDATE_SIZE(src, pmgr_chann_msg_t);
            auto msg = (pmgr_chann_msg_t *)src;
            json jdst = {
                {"hdr", {{"type", (int32_t)src->type}, {"size", (int32_t)src->size}}},
                {"flags", (int32_t)msg->flags},
                {"src_id", (int64_t)msg->src_id},
                {"dst_id", (int64_t)msg->dst_id},
                {"contents", ""},
            };
            dst = jdst.dump(4, ' ');
        }
        break;
        default : {
            DBG("Unknown message type");
            return -1;
        }
    }
    return 0;
}
