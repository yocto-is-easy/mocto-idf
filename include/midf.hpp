#ifndef MIDF_HPP_
#define MIDF_HPP_

#include <functional>
#include <stdexcept>
#include <limits>
#include <chrono>

#include <lrrp.h>

using json = nlohmann::json;

template <typename ...Args>
json parameter_pack_to_json(Args... args) {
    json ret = {args...};
    return ret;
}

template <typename Ret, typename Tuple, typename Func, std::size_t... Is>
Ret tuple_pass_to_func(Tuple t, Func f, std::index_sequence<Is...>) {
    return f(std::get<Is>(t)...);
}

template <typename Tuple, std::size_t... Is>
void touple_to_json(Tuple& t, json j, std::index_sequence<Is...>) {
    ((std::get<Is>(t) = j.at(Is).get<std::tuple_element_t<Is, Tuple>>()), ...);
}

template <typename Ret, typename... Args>
Ret call_json(std::function<Ret(Args...)> func, json j) {
    std::tuple<Args...> args;
    touple_to_json(args, j, std::make_index_sequence<sizeof...(Args)>{});
    return tuple_pass_to_func<Ret>(args, func, std::make_index_sequence<sizeof...(Args)>{});
}

template <class Ret, class ...Args>
class midf_handler : public lrrp::handler_base
{
public:
    virtual lrrp::response handle(const lrrp::request& req) final {
        std::function<Ret(Args...)> f = [=](Args... args) -> Ret { return this->process(args...); };
        nlohmann::json payload = call_json(f, req.params());
        lrrp::response res = lrrp::response_builder()
            .set_payload(payload)
            .set_status(lrrp::status_type::ok)
            .build();
        return res;
    }

private:
    virtual Ret process(Args... args) = 0;
};


#define LOCAL_HOST "127.0.0.1"

namespace midf {
    class remote_call_error : public std::exception {
    private:
        char* m_msg;

    public:
        remote_call_error(char* msg) : m_msg(msg) {}

        char* what() {
            return m_msg;
        }
    };
}

#define MIDF_DECL_FUNC(ret_t, service_name, name, ...) \
    namespace service_name { \
        template <typename ...Args> \
        ret_t name(Args... args) { \
            lrrp::client cli(LOCAL_HOST, midf_port); \
            auto req = lrrp::request_builder(#name).set_json(parameter_pack_to_json<Args...>(args...)).build(); \
            auto res = cli.send(req); \
            ret_t ret; \
            try { \
                res.get_payload().get_to(ret); \
                return ret; \
            } catch(const std::exception&) { \
                throw midf::remote_call_error("could not get the result"); \
            } \
        } \
        class handler_base_##name : public midf_handler<ret_t, __VA_ARGS__> {}; \
    }

#define MIDF_DECL_FUNC_NO_ARGS(ret_t, service_name, name) \
    namespace service_name { \
        ret_t name() { \
            lrrp::client cli(LOCAL_HOST, midf_port); \
            auto req = lrrp::request_builder(#name).set_json(nlohmann::json::object({})).build(); \
            auto res = cli.send(req); \
            ret_t ret; \
            try { \
                res.get_payload().get_to(ret); \
                return ret; \
            } catch(const std::exception&) { \
                throw midf::remote_call_error("could not get the result"); \
            } \
        } \
        class handler_base_##name : public midf_handler<ret_t> {}; \
    }

#define MIDF_IMPL_FUNC(ret_t, service_name, name, ...) \
    class handler_impl_##service_name##_##name : public service_name::handler_base_##name \
    { \
    private: \
        virtual ret_t process(__VA_ARGS__) override; \
    }; \
    int id_##service_name##_##name = service_name##_midf_server.add_handler(#name, [](const lrrp::request& req) -> lrrp::response { \
        static handler_impl_##service_name##_##name name; \
        return name.handle(req); \
    }); \
    ret_t handler_impl_##service_name##_##name::process

#define MIDF_DECL_PORT(service_name, port) \
    namespace service_name { \
        const int midf_port = port; } \
    MIDF_DECL_FUNC_NO_ARGS(bool, service_name, ping); \
    namespace service_name { \
        using namespace std::chrono_literals; \
        bool wait_startup(uint64_t iterations = 1000/*as a result 10s by default*/, std::chrono::milliseconds delta = 10ms) { \
            for(uint64_t i = 0; i < iterations; ++i) { \
                try { \
                    if(service_name::ping()) { \
                        return true; \
                    } \
                } catch(const std::exception&) {} \
                std::this_thread::sleep_for(delta); \
            } \
            return false; \
        } \
    }

#define INIT_MIDF_SERVER(service_name) \
    lrrp::server service_name##_midf_server(service_name::midf_port); \
    MIDF_IMPL_FUNC(bool, service_name, ping) () { return true; }

#define START_MIDF_SERVER(service_name) \
    using namespace std::chrono_literals; \
    supervisor::wait_startup(10000, 10ms); /*it waits 100s*/ \
    supervisor::inform_about_me(AS_CALL_BACK(service_name, ping, bool)); \
    service_name##_midf_server.run();

#define START_MIDF_SERVER_WITHOUT_OBSERVER(service_name) \
    service_name##_midf_server.run();

namespace midf {

    template <typename Ret, typename... Args>
    class function {
    public:
        int m_port;
        std::string m_service_name;
        std::string m_func_name;

    public:
        function(int port, std::string service_name, std::string func_name)
            : m_port(port)
            , m_service_name(service_name)
            , m_func_name(func_name) {}

        function() = default;

        Ret operator()(Args... args) {
            lrrp::client cli(LOCAL_HOST, this->m_port);
            auto req = lrrp::request_builder(m_func_name).set_json(parameter_pack_to_json<Args...>(args...)).build();
            auto res = cli.send(req);
            Ret ret;
            res.get_payload().get_to(ret);
            return ret;
        }

        std::string of_service() {
            return m_service_name;
        }

    };

    template <typename Ret, typename ...Args>
    void to_json(nlohmann::json& j, const midf::function<Ret, Args...>& f) {
        j = json {
            {"port", f.m_port},
            {"func_name", f.m_func_name},
            {"service_name", f.m_service_name}
        };
    }

    template <typename Ret, typename ...Args>
    void from_json(const nlohmann::json& j, midf::function<Ret, Args...>& f) {
        j.at("port").get_to(f.m_port);
        j.at("func_name").get_to(f.m_func_name);
        j.at("service_name").get_to(f.m_service_name);
    }

} // namespace midf

#define AS_CALL_BACK(service_name, function_name, ...) \
    midf::function<__VA_ARGS__>(service_name::midf_port, #service_name, #function_name)

#endif
