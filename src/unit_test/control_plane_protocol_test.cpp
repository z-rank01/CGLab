// control_plane 协议层（JSON-RPC 2.0）单元测试
// 覆盖：编解码、请求解析、方法分派、参数校验、版本协商、未知方法。

#include <cstdlib>
#include <iostream>
#include <string_view>

#include "json_rpc.h"

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
        check(has_cap("camera.set_culling"), "capabilities include camera.set_culling");
    }
} // namespace

int main()
{
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

    if (failures != 0)
    {
        std::cerr << failures << " control plane protocol test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All control plane protocol tests passed\n";
    return EXIT_SUCCESS;
}
