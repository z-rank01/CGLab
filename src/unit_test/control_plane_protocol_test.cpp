// control_plane 协议层（JSON-RPC 2.0）单元测试
// 覆盖：编解码、请求解析、方法分派、参数校验、版本协商、未知方法。

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

#include "json_rpc.h"
#include "web_static.h"

namespace
{
    int failures = 0;

    void check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    void test_make_result_error_notification()
    {
        const auto result = control_plane::make_result(7, {{"ok", true}});
        check(result["jsonrpc"] == "2.0", "make_result jsonrpc tag");
        check(result["id"] == 7, "make_result id");
        check(result["result"]["ok"] == true, "make_result payload");

        const auto error = control_plane::make_error(nullptr, -32601, "nope");
        check(error["id"].is_null(), "make_error null id");
        check(error["error"]["code"] == -32601, "make_error code");
        check(error["error"]["message"] == "nope", "make_error message");

        const auto note = control_plane::make_notification("telemetry.frame", {{"fps", 60}});
        check(!note.contains("id"), "notification has no id");
        check(note["method"] == "telemetry.frame", "notification method");
    }

    void test_parse_valid()
    {
        const auto parsed = control_plane::parse_request(R"({"jsonrpc":"2.0","id":1,"method":"frame.pause"})");
        check(parsed.status == control_plane::parse_status::ok, "parse valid status");
        check(parsed.request.method == "frame.pause", "parse valid method");
        check(parsed.request.params.is_object(), "parse default params object");
        check(!parsed.request.is_notification, "parse request with id");
    }

    void test_parse_notification()
    {
        const auto parsed = control_plane::parse_request(R"({"jsonrpc":"2.0","method":"session.init","params":{"protocol_version":1}})");
        check(parsed.status == control_plane::parse_status::ok, "parse notification status");
        check(parsed.request.is_notification, "parse notification flag");
    }

    void test_parse_errors()
    {
        const auto garbage = control_plane::parse_request("not json at all");
        check(garbage.status == control_plane::parse_status::parse_error, "parse garbage");
        check(garbage.error_response["error"]["code"] == control_plane::error_parse_error, "parse garbage code");

        const auto no_method = control_plane::parse_request(R"({"id":1})");
        check(no_method.status == control_plane::parse_status::invalid_request, "parse missing method");

        const auto bad_params = control_plane::parse_request(R"({"id":1,"method":"x","params":[1,2]})");
        check(bad_params.status == control_plane::parse_status::invalid_request, "parse non-object params");
    }

    void test_dispatch_session_init()
    {
        const auto parsed = control_plane::parse_request(
            R"({"id":2,"method":"session.init","params":{"protocol_version":1}})");
        const auto dispatched = control_plane::dispatch_request("client-a", parsed.request);
        check(dispatched.outcome == control_plane::dispatch_outcome::immediate_response, "session.init immediate");
        check(dispatched.response["result"]["protocol_version"] == control_plane::protocol_version,
              "session.init version echo");
        check(dispatched.response["result"]["capabilities"].is_array(), "session.init capabilities");

        const auto custom_name = control_plane::dispatch_request("client-a", parsed.request, "TriangleSample");
        check(custom_name.response["result"]["server"] == "TriangleSample", "session.init custom server name");

        const auto bad_version = control_plane::parse_request(
            R"({"id":3,"method":"session.init","params":{"protocol_version":999}})");
        const auto rejected = control_plane::dispatch_request("client-a", bad_version.request);
        check(rejected.response["error"]["code"] == control_plane::error_invalid_params, "session.init bad version");
    }

    void test_dispatch_echo()
    {
        const auto parsed = control_plane::parse_request(R"({"id":4,"method":"debug.echo","params":{"message":"hi"}})");
        const auto dispatched = control_plane::dispatch_request("client-a", parsed.request);
        check(dispatched.outcome == control_plane::dispatch_outcome::queue_command, "echo queued");
        check(dispatched.command.kind == control_plane::command_kind::echo, "echo kind");
        check(dispatched.command.params["message"] == "hi", "echo payload");
        check(dispatched.command.client_id == "client-a", "echo client id");

        const auto missing = control_plane::parse_request(R"({"id":5,"method":"debug.echo","params":{}})");
        const auto rejected = control_plane::dispatch_request("client-a", missing.request);
        check(rejected.outcome == control_plane::dispatch_outcome::immediate_response, "echo invalid immediate");
        check(rejected.response["error"]["code"] == control_plane::error_invalid_params, "echo invalid code");
    }

    void test_dispatch_frame_controls()
    {
        const auto pause = control_plane::parse_request(R"({"id":6,"method":"frame.pause","params":{}})");
        const auto pause_cmd = control_plane::dispatch_request("c", pause.request);
        check(pause_cmd.command.kind == control_plane::command_kind::frame_pause, "pause kind");

        const auto resume = control_plane::parse_request(R"({"id":7,"method":"frame.resume"})");
        const auto resume_cmd = control_plane::dispatch_request("c", resume.request);
        check(resume_cmd.command.kind == control_plane::command_kind::frame_resume, "resume kind");

        const auto step = control_plane::parse_request(R"({"id":8,"method":"frame.step","params":{"count":5}})");
        const auto step_cmd = control_plane::dispatch_request("c", step.request);
        check(step_cmd.command.kind == control_plane::command_kind::frame_step, "step kind");
        check(step_cmd.command.params["count"] == 5, "step count");

        const auto step_default = control_plane::parse_request(R"({"id":9,"method":"frame.step"})");
        const auto step_default_cmd = control_plane::dispatch_request("c", step_default.request);
        check(step_default_cmd.command.params["count"] == 1, "step default count");

        const auto step_zero = control_plane::parse_request(R"({"id":10,"method":"frame.step","params":{"count":0}})");
        check(control_plane::dispatch_request("c", step_zero.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "step count 0 rejected");

        const auto step_huge = control_plane::parse_request(R"({"id":11,"method":"frame.step","params":{"count":65}})");
        check(control_plane::dispatch_request("c", step_huge.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "step count 65 rejected");
    }

    void test_dispatch_unknown_method()
    {
        const auto parsed = control_plane::parse_request(R"({"id":12,"method":"scene.explode"})");
        const auto dispatched = control_plane::dispatch_request("c", parsed.request);
        check(dispatched.outcome == control_plane::dispatch_outcome::immediate_response, "unknown immediate");
        check(dispatched.response["error"]["code"] == control_plane::error_method_not_found, "unknown code");
    }

    void test_dispatch_camera_methods()
    {
        using control_plane::command_kind;
        using control_plane::dispatch_outcome;

        // set_mode 合法值
        const auto orbit = control_plane::parse_request(R"({"id":20,"method":"camera.set_mode","params":{"mode":"orbit"}})");
        const auto orbit_cmd = control_plane::dispatch_request("c", orbit.request);
        check(orbit_cmd.outcome == dispatch_outcome::queue_command, "set_mode queued");
        check(orbit_cmd.command.kind == command_kind::camera_set_mode, "set_mode kind");
        check(orbit_cmd.command.params["mode"] == "orbit", "set_mode value");

        // set_mode 非法值
        const auto bad_mode = control_plane::parse_request(R"({"id":21,"method":"camera.set_mode","params":{"mode":"spin"}})");
        check(control_plane::dispatch_request("c", bad_mode.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_mode invalid value rejected");

        // set_params 部分字段 + 值域
        const auto params = control_plane::parse_request(
            R"({"id":22,"method":"camera.set_params","params":{"fov":60.0,"movement_speed":25.0}})");
        const auto params_cmd = control_plane::dispatch_request("c", params.request);
        check(params_cmd.command.kind == command_kind::camera_set_params, "set_params kind");
        check(params_cmd.command.params["fov"] == 60.0, "set_params fov");
        check(!params_cmd.command.params.contains("zoom_speed"), "set_params omits absent fields");

        const auto bad_fov = control_plane::parse_request(R"({"id":23,"method":"camera.set_params","params":{"fov":300}})");
        check(control_plane::dispatch_request("c", bad_fov.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_params fov out of range rejected");

        const auto empty_params = control_plane::parse_request(R"({"id":24,"method":"camera.set_params","params":{}})");
        check(control_plane::dispatch_request("c", empty_params.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_params empty rejected");

        // get_state
        const auto state = control_plane::parse_request(R"({"id":25,"method":"camera.get_state"})");
        check(control_plane::dispatch_request("c", state.request).command.kind == command_kind::camera_get_state,
              "get_state kind");

        // set_culling：布尔入队；缺失/非布尔拒绝
        const auto culling = control_plane::parse_request(
            R"({"id":29,"method":"camera.set_culling","params":{"enabled":true}})");
        const auto culling_cmd = control_plane::dispatch_request("c", culling.request);
        check(culling_cmd.command.kind == command_kind::camera_set_culling, "set_culling kind");
        check(culling_cmd.command.params["enabled"] == true, "set_culling value");

        const auto culling_missing = control_plane::parse_request(R"({"id":30,"method":"camera.set_culling","params":{}})");
        check(control_plane::dispatch_request("c", culling_missing.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_culling missing enabled rejected");

        const auto culling_bad = control_plane::parse_request(
            R"({"id":31,"method":"camera.set_culling","params":{"enabled":"yes"}})");
        check(control_plane::dispatch_request("c", culling_bad.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_culling non-boolean rejected");

        // bookmark
        const auto save = control_plane::parse_request(R"({"id":26,"method":"camera.bookmark.save","params":{"slot":3}})");
        const auto save_cmd = control_plane::dispatch_request("c", save.request);
        check(save_cmd.command.kind == command_kind::camera_bookmark_save, "bookmark.save kind");
        check(save_cmd.command.params["slot"] == 3, "bookmark.save slot");

        const auto go = control_plane::parse_request(R"({"id":27,"method":"camera.bookmark.goto","params":{"slot":0}})");
        check(control_plane::dispatch_request("c", go.request).command.kind == command_kind::camera_bookmark_goto,
              "bookmark.goto kind");

        const auto bad_slot = control_plane::parse_request(R"({"id":28,"method":"camera.bookmark.save","params":{"slot":8}})");
        check(control_plane::dispatch_request("c", bad_slot.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "bookmark slot 8 rejected");
    }

    void test_hello_notification()
    {
        const auto hello = control_plane::make_hello_notification();
        check(hello["method"] == "session.hello", "hello method");
        check(hello["params"]["protocol_version"] == control_plane::protocol_version, "hello version");
        const auto named_hello = control_plane::make_hello_notification("GltfSponzaSample");
        check(named_hello["params"]["server"] == "GltfSponzaSample", "hello custom server name");
    }

    void test_dispatch_scene_methods()
    {
        using control_plane::command_kind;
        using control_plane::dispatch_outcome;

        // load_asset：合法 path 入队；空 path / 缺失 path 拒绝
        const auto load = control_plane::parse_request(R"({"id":30,"method":"scene.load_asset","params":{"path":"assets/a.gltf"}})");
        const auto load_cmd = control_plane::dispatch_request("c", load.request);
        check(load_cmd.outcome == dispatch_outcome::queue_command, "load_asset queued");
        check(load_cmd.command.kind == command_kind::scene_load_asset, "load_asset kind");
        check(load_cmd.command.params["path"] == "assets/a.gltf", "load_asset path");

        const auto load_empty = control_plane::parse_request(R"({"id":31,"method":"scene.load_asset","params":{"path":""}})");
        check(control_plane::dispatch_request("c", load_empty.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "load_asset empty path rejected");

        // unload：合法 id 入队；字符串 id 拒绝
        const auto unload = control_plane::parse_request(R"({"id":32,"method":"scene.unload","params":{"id":2}})");
        const auto unload_cmd = control_plane::dispatch_request("c", unload.request);
        check(unload_cmd.command.kind == command_kind::scene_unload, "unload kind");
        check(unload_cmd.command.params["id"] == 2, "unload id");

        const auto unload_bad = control_plane::parse_request(R"({"id":33,"method":"scene.unload","params":{"id":"2"}})");
        check(control_plane::dispatch_request("c", unload_bad.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "unload string id rejected");

        // set_visibility：缺 visible / 非布尔拒绝
        const auto vis = control_plane::parse_request(R"({"id":34,"method":"scene.set_visibility","params":{"id":1,"visible":false}})");
        const auto vis_cmd = control_plane::dispatch_request("c", vis.request);
        check(vis_cmd.command.kind == command_kind::scene_set_visibility, "set_visibility kind");
        check(vis_cmd.command.params["visible"] == false, "set_visibility value");

        const auto vis_bad = control_plane::parse_request(R"({"id":35,"method":"scene.set_visibility","params":{"id":1}})");
        check(control_plane::dispatch_request("c", vis_bad.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_visibility missing visible rejected");

        // set_transform：部分字段；空字段 / 非 3 元数组拒绝
        const auto tf = control_plane::parse_request(
            R"({"id":36,"method":"scene.set_transform","params":{"id":1,"position":[0,1,2],"scale":[2,2,2]}})");
        const auto tf_cmd = control_plane::dispatch_request("c", tf.request);
        check(tf_cmd.command.kind == command_kind::scene_set_transform, "set_transform kind");
        check(tf_cmd.command.params["position"][1] == 1.0, "set_transform position");
        check(!tf_cmd.command.params.contains("rotation"), "set_transform omits absent fields");

        const auto tf_empty = control_plane::parse_request(R"({"id":37,"method":"scene.set_transform","params":{"id":1}})");
        check(control_plane::dispatch_request("c", tf_empty.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_transform no fields rejected");

        const auto tf_bad = control_plane::parse_request(
            R"({"id":38,"method":"scene.set_transform","params":{"id":1,"position":[0,1]}})");
        check(control_plane::dispatch_request("c", tf_bad.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_transform short vec3 rejected");

        // select：id 或 null；缺 id / 字符串拒绝
        const auto sel = control_plane::parse_request(R"({"id":39,"method":"scene.select","params":{"id":3}})");
        const auto sel_cmd = control_plane::dispatch_request("c", sel.request);
        check(sel_cmd.command.kind == command_kind::scene_select, "select kind");
        check(sel_cmd.command.params["id"] == 3, "select id");

        const auto sel_null = control_plane::parse_request(R"({"id":40,"method":"scene.select","params":{"id":null}})");
        const auto sel_null_cmd = control_plane::dispatch_request("c", sel_null.request);
        check(sel_null_cmd.command.kind == command_kind::scene_select, "select null kind");
        check(sel_null_cmd.command.params["id"].is_null(), "select null clears");

        const auto sel_bad = control_plane::parse_request(R"({"id":41,"method":"scene.select","params":{}})");
        check(control_plane::dispatch_request("c", sel_bad.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "select missing id rejected");

        // list
        const auto list = control_plane::parse_request(R"({"id":42,"method":"scene.list"})");
        check(control_plane::dispatch_request("c", list.request).command.kind == command_kind::scene_list,
              "list kind");

        // capabilities 覆盖 scene.*
        const auto init = control_plane::parse_request(
            R"({"id":43,"method":"session.init","params":{"protocol_version":1}})");
        const auto init_resp = control_plane::dispatch_request("c", init.request);
        const auto& caps = init_resp.response["result"]["capabilities"];
        const auto has_cap = [&caps](const char* name)
        {
            for (const auto& cap : caps)
            {
                if (cap == name)
                {
                    return true;
                }
            }
            return false;
        };
        check(has_cap("scene.load_asset"), "capabilities include scene.load_asset");
        check(has_cap("scene.list"), "capabilities include scene.list");
        check(has_cap("telemetry.scene"), "capabilities include telemetry.scene");
        check(has_cap("telemetry.load"), "capabilities include telemetry.load");
        check(has_cap("telemetry.load_progress"), "capabilities include telemetry.load_progress");
        check(has_cap("camera.set_culling"), "capabilities include camera.set_culling");
    }

    // I1：schema 与实现同源——方法表/capabilities/通知表互相咬合
    void test_schema_consistency()
    {
        const auto schema = control_plane::build_protocol_schema();
        check(schema["protocol_version"] == control_plane::protocol_version, "schema protocol version");

        std::set<std::string> capabilities;
        for (const auto& cap : schema["capabilities"])
        {
            capabilities.insert(cap.get<std::string>());
        }

        std::set<std::string> notification_names;
        for (const auto& note : schema["notifications"])
        {
            notification_names.insert(note["name"].get<std::string>());
        }

        bool methods_ok = true;
        bool telemetry_ok = true;
        for (const auto& method : schema["methods"])
        {
            const std::string name = method["name"].get<std::string>();
            if (name == "session.init")
            {
                continue; // 握手方法不进 capabilities（与现行行为一致）
            }
            // 每个 command 方法必须出现在 capabilities，反之亦然
            if (capabilities.count(name) == 0)
            {
                methods_ok = false;
            }
            capabilities.erase(name);
        }
        // 剩余 capabilities 必须全部由通知表覆盖（telemetry.*）
        for (const std::string& rest : capabilities)
        {
            if (notification_names.count(rest) == 0)
            {
                telemetry_ok = false;
            }
        }
        check(methods_ok, "schema methods covered by capabilities");
        check(telemetry_ok, "schema notifications cover remaining capabilities");

        // session.init 响应的 capabilities 与 schema 一致
        const auto init = control_plane::parse_request(
            R"({"id":50,"method":"session.init","params":{"protocol_version":1}})");
        const auto resp = control_plane::dispatch_request("c", init.request);
        check(resp.response["result"]["capabilities"] == schema["capabilities"],
              "session.init capabilities match schema");
    }

    void test_web_static()
    {
        using control_plane::content_type_for;
        using control_plane::resolve_web_path;
        const std::filesystem::path root = std::filesystem::path("srv").lexically_normal();

        const auto index = resolve_web_path(root, "/");
        check(index.has_value(), "resolve root to index.html");
        check(index && *index == (root / "index.html").lexically_normal(), "resolve root path value");

        const auto nested = resolve_web_path(root, "/a/b.js?ver=2");
        check(nested && *nested == (root / "a" / "b.js").lexically_normal(), "resolve nested with query");

        check(!resolve_web_path(root, "/../secret").has_value(), "reject dot-dot");
        check(!resolve_web_path(root, "/a/../../secret").has_value(), "reject nested dot-dot");
        check(!resolve_web_path(root, "/%2e%2e/secret").has_value(), "reject percent encoding");
        check(!resolve_web_path(root, "/a\\..\\secret").has_value(), "reject backslash");
        check(!resolve_web_path(root, "no-leading-slash").has_value(), "reject relative url");
        check(!resolve_web_path(root, "/c:/windows").has_value(), "reject drive segment");

        check(content_type_for("index.html") == "text/html; charset=utf-8", "content type html");
        check(content_type_for("app.js") == "text/javascript; charset=utf-8", "content type js");
        check(content_type_for("logo.png") == "image/png", "content type png");
        check(content_type_for("data.bin") == "application/octet-stream", "content type fallback");
    }

    // I1 golden：docs/control_plane_protocol.schema.json 必须等于 build_protocol_schema() 输出。
    // 有意改协议时以 --write 重新生成：`cglab_control_plane_protocol_tests --write`。
    constexpr std::string_view schema_relative_path = "docs/control_plane_protocol.schema.json";

    void test_schema_golden()
    {
        const std::string expected = control_plane::build_protocol_schema().dump(2) + "\n";
        const std::filesystem::path path =
            std::filesystem::path(CGLAB_SOURCE_DIR) / std::string(schema_relative_path);
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            check(false, "schema golden file readable (run with --write to generate)");
            return;
        }
        std::stringstream buffer;
        buffer << stream.rdbuf();
        check(buffer.str() == expected,
              "schema golden up to date (run cglab_control_plane_protocol_tests --write after intended protocol changes)");
    }
    void test_dispatch_debug_set_view()
    {
        // M8/B2：枚举名映射为下标（对齐 apps::debug_view_mode），引擎只搬运数值
        const auto shadow = control_plane::parse_request(R"({"id":60,"method":"debug.set_view","params":{"view":"shadow"}})");
        const auto shadow_cmd = control_plane::dispatch_request("c", shadow.request);
        check(shadow_cmd.outcome == control_plane::dispatch_outcome::queue_command, "set_view queued");
        check(shadow_cmd.command.kind == control_plane::command_kind::debug_set_view, "set_view kind");
        check(shadow_cmd.command.params["view"] == 1, "set_view shadow maps to 1");

        const auto resolved = control_plane::parse_request(R"({"id":61,"method":"debug.set_view","params":{"view":"resolved"}})");
        check(control_plane::dispatch_request("c", resolved.request).command.params["view"] == 4,
              "set_view resolved maps to 4");

        const auto bad = control_plane::parse_request(R"({"id":62,"method":"debug.set_view","params":{"view":"wireframe"}})");
        check(control_plane::dispatch_request("c", bad.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_view unknown view rejected");

        const auto missing = control_plane::parse_request(R"({"id":63,"method":"debug.set_view","params":{}})");
        check(control_plane::dispatch_request("c", missing.request).response["error"]["code"] ==
                  control_plane::error_invalid_params,
              "set_view missing view rejected");

        const auto init = control_plane::parse_request(
            R"({"id":64,"method":"session.init","params":{"protocol_version":1}})");
        const auto resp = control_plane::dispatch_request("c", init.request);
        const auto& caps = resp.response["result"]["capabilities"];
        bool found = false;
        for (const auto& cap : caps)
        {
            if (cap == "debug.set_view")
            {
                found = true;
            }
        }
        check(found, "capabilities include debug.set_view");
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--write") == 0)
    {
        // 重新生成 golden（协议有意变更后执行一次并随提交入库）
        const std::filesystem::path path =
            std::filesystem::path(CGLAB_SOURCE_DIR) / std::string(schema_relative_path);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << control_plane::build_protocol_schema().dump(2) << "\n";
        std::cout << "Wrote " << path << '\n';
        return stream ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    test_make_result_error_notification();
    test_parse_valid();
    test_parse_notification();
    test_parse_errors();
    test_dispatch_session_init();
    test_dispatch_echo();
    test_dispatch_frame_controls();
    test_dispatch_unknown_method();
    test_dispatch_camera_methods();
    test_hello_notification();
    test_dispatch_scene_methods();
    test_schema_consistency();
    test_web_static();
    test_schema_golden();
    test_dispatch_debug_set_view();

    if (failures != 0)
    {
        std::cerr << failures << " control plane protocol test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All control plane protocol tests passed\n";
    return EXIT_SUCCESS;
}
