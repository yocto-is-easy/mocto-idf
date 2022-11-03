#ifndef MIDF_HPP_
#define MIDF_HPP_

#include <functional>

#include <lrrp.h>

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

#define MIDF_DECL_PORT(port) const int midf_port = port;

#define LOCAL_HOST "127.0.0.1"

#define MIDF_DECL_FUNC(ret_t, name, ...) \
    template <typename ...Args> \
    ret_t name(Args... args) { \
        lrrp::client cli(LOCAL_HOST, midf_port); \
        auto req = lrrp::request_builder(#name).set_json(parameter_pack_to_json<Args...>(args...)).build(); \
        auto res = cli.send(req); \
        ret_t ret; \
        res.get_payload().get_to(ret); \
        return ret; \
    } \
    class handler_base_##name : public midf_handler<ret_t, __VA_ARGS__> {};

#define MIDF_DECL_FUNC_NO_ARGS(ret_t, name) \
    template <typename ...Args> \
    ret_t name(Args... args) { \
        lrrp::client cli(LOCAL_HOST, midf_port); \
        auto req = lrrp::request_builder(#name).set_json(parameter_pack_to_json<Args...>(args...)).build(); \
        auto res = cli.send(req); \
        ret_t ret; \
        res.get_payload().get_to(ret); \
        return ret; \
    } \
    class handler_base_##name : public midf_handler<ret_t> {};

#define MIDF_IMPL_FUNC(ret_t, name, ...) \
    class handler_impl_##name : public handler_base_##name \
    { \
    private: \
        virtual ret_t process(__VA_ARGS__) override; \
    }; \
    int id_##name = midf_server.add_handler(#name, [](const lrrp::request& req) -> lrrp::response { \
        static handler_impl_##name name; \
        return name.handle(req); \
    }); \
    ret_t handler_impl_##name::process

#define INIT_MIDF_SERVER() lrrp::server midf_server(midf_port);
#define START_MIDF_SERVER() midf_server.run();

#endif
