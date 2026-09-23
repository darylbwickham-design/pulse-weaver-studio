#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-runtime-safety.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-scene-item-ref.hpp"
#include <QApplication>
#include <QDialog>
#include <QGraphicsRectItem>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>

static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static int teardown(bool protect, bool deferred)
{
    auto *dialog = new QDialog;
    auto *center = new QWidget(dialog);
    auto *scene = new QGraphicsScene(center);
    QGraphicsScene *editorScene = scene;
    if (protect)
        PulseRuntimeSafety::silenceSceneOnOwnerDestruction(scene, dialog);
    int lateCallbacks = 0, changes = 0;
    QObject::connect(scene, &QGraphicsScene::selectionChanged, dialog, [&] {
        ++changes;
        // Record the legacy invalid access rather than crashing the test process.
        if (!editorScene) ++lateCallbacks;
        else check(editorScene->selectedItems().size() <= 1, "valid live selection");
    });
    QObject::connect(dialog, &QObject::destroyed, [&] { editorScene = nullptr; });
    auto *item = scene->addRect(0, 0, 100, 100);
    item->setFlag(QGraphicsItem::ItemIsSelectable);
    item->setSelected(true);
    check(changes == 1, "live selection callback preserved");
    if (deferred) {
        dialog->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    } else delete dialog;
    check(!editorScene, "dialog destroyed");
    return lateCallbacks;
}

static void framing()
{
    using namespace PulseRuntimeSafety;
    const QByteArray body = "{\"visible\":false}";
    const QByteArray request = "POST /api/v1/source/visibility HTTP/1.1\r\nContent-Length: " +
        QByteArray::number(body.size()) + "\r\nAuthorization: Bearer fixture\r\n\r\n" + body;
    for (qsizetype i = 0; i < request.size(); ++i)
        check(httpFrame(request.left(i)) == HttpFrame::Incomplete, "never dispatch a fragmented body");
    check(httpFrame(request) == HttpFrame::Complete, "complete POST");
    check(httpFrame("GET / HTTP/1.1\r\n\r\n") == HttpFrame::Complete, "bodyless GET");
    for (const QByteArray &header : {QByteArray("Content-Length: -1"), QByteArray("Content-Length: +1"),
         QByteArray("Content-Length: 999999999999999999999999"), QByteArray("Content-Length: 65536"),
         QByteArray("Content-Length: 0\r\nContent-Length: 0"), QByteArray("Transfer-Encoding: chunked")})
        check(httpFrame("POST / HTTP/1.1\r\n" + header + "\r\n\r\n") == HttpFrame::Invalid, "reject ambiguous/oversized framing");
    check(httpFrame(request + "GET / HTTP/1.1\r\n\r\n") == HttpFrame::Invalid, "reject pipelining");
    check(httpFrame(QByteArray(65537, 'x')) == HttpFrame::Invalid, "bound unclosed headers");
    check(httpFrame("POST / HTTP/1.1\r\n\r\n{}") == HttpFrame::Invalid, "body requires length");
}

static std::atomic<int> destroyed{0};
static void sceneLifetime(const std::string &runtime)
{
    check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
    obs_audio_info audio{48000, SPEAKERS_STEREO};
    check(obs_reset_audio(&audio), "audio startup");
    obs_add_data_path((runtime + "/data/libobs/").c_str());
    const std::string graphics = runtime + "/bin/64bit/libobs-d3d11.dll";
    obs_video_info video{};
    video.graphics_module = graphics.c_str();
    video.fps_num = 30; video.fps_den = 1;
    video.base_width = video.output_width = 640;
    video.base_height = video.output_height = 480;
    video.output_format = VIDEO_FORMAT_RGBA;
    check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "graphics startup");
    obs_source_info type{};
    type.id = "pulse_safety_fixture";
    type.type = OBS_SOURCE_TYPE_INPUT;
    type.output_flags = OBS_SOURCE_VIDEO;
    type.get_name = [](void *) { return "Safety fixture"; };
    type.get_width = [](void *) -> uint32_t { return 640; };
    type.get_height = [](void *) -> uint32_t { return 480; };
    type.create = [](obs_data_t *, obs_source_t *) -> void * { return new int(1); };
    type.destroy = [](void *data) { delete static_cast<int *>(data); ++destroyed; };
    obs_register_source(&type);
    for (int i = 0; i < 100; ++i) {
        obs_scene_t *scene = obs_scene_create_private("scene");
        obs_sceneitem_t *group = obs_scene_add_group(scene, "group");
        check(group != nullptr, "create group");
        obs_source_t *camera = obs_source_create_private(type.id, "camera", nullptr);
        check(camera != nullptr, "create camera fixture");
        obs_sceneitem_t *item = obs_scene_add(obs_sceneitem_group_get_scene(group), camera);
        const auto id = obs_sceneitem_get_id(item);
        OBSSceneItem held = PulseRuntimeSafety::findSceneItem(scene, "camera");
        check(held == item, "group lookup returns referenced item");
        check(PulseRuntimeSafety::findSceneItemById(obs_sceneitem_group_get_scene(group), id) == item, "ID lookup");
        check(!PulseRuntimeSafety::findSceneItem(scene, "missing"), "missing item");
        obs_sceneitem_remove(item);
        obs_source_remove(obs_sceneitem_get_source(group));
        obs_source_release(camera);
        obs_scene_release(scene);
        obs_wait_for_destroy_queue();
        check(destroyed == i, "lookup retains source after scene deletion");
        check(obs_sceneitem_get_id(held) == id, "item remains readable after deletion");
        held = nullptr;
        obs_wait_for_destroy_queue();
        // Observe the fixture's own destruction callback as well as the
        // barrier, allowing for nested asynchronous destruction work.
        for (int wait = 0; wait < 1000 && destroyed.load() != i + 1; ++wait)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(destroyed == i + 1, "reference released without leak");
    }
    obs_shutdown();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    check(argc == 2, "pass the OBS runtime directory");
    check(teardown(false, false) > 0, "reproduce legacy callback after scene pointer reset");
    for (int i = 0; i < 100; ++i)
        check(teardown(true, i % 2) == 0, "no selection callbacks during owner teardown");
    framing();
    sceneLifetime(argv[1]);
    std::puts("PASS: legacy teardown reproduced; 100 safe teardowns; HTTP fragmentation/limits; 100 grouped item lifetimes.");
}
