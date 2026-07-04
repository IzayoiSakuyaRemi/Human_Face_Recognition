#include "frame_cap_pipeline.hpp"
#include "who_cam.hpp"

using namespace who::cam;
using namespace who::frame_cap;

// num of frames the model take to get result
#define MODEL_TIME 3

#if CONFIG_IDF_TARGET_ESP32S3

WhoFrameCap *get_dvp_frame_cap_pipeline()
{
    framesize_t frame_size = get_cam_frame_size_from_lcd_resolution();

#ifdef BSP_BOARD_ESP32_S3_KORVO_2
    auto cam = new WhoS3Cam(PIXFORMAT_RGB565, frame_size, MODEL_TIME + 3, true, true);
#else
    auto cam = new WhoS3Cam(PIXFORMAT_RGB565, frame_size, MODEL_TIME + 3);
#endif

    auto frame_cap = new WhoFrameCap();
    frame_cap->add_node<WhoFetchNode>("FrameCapFetch", cam);
    return frame_cap;
}

#elif CONFIG_IDF_TARGET_ESP32P4

WhoFrameCap *get_mipi_csi_frame_cap_pipeline()
{
    auto cam = new WhoP4Cam(
        V4L2_PIX_FMT_RGB565,
        MODEL_TIME + 1,  // reduced from +3: 6→4 buffers, saves ~2.5MB PSRAM
        V4L2_MEMORY_USERPTR,
        false,   // vertical_flip
        false    // horizontal_flip = OFF（配合 panel mirror_x=true 修正 WT99P4C5-S1 镜像）
    );

    auto frame_cap = new WhoFrameCap();
    frame_cap->add_node<WhoFetchNode>("FrameCapFetch", cam);
    return frame_cap;
}

// UVC pipeline 不动
WhoFrameCap *get_uvc_frame_cap_pipeline()
{
    auto cam = new WhoUVCCam(UVC_VS_FORMAT_MJPEG, 640, 480, 30, 4);

    auto frame_cap = new WhoFrameCap();
    frame_cap->add_node<WhoFetchNode>("FrameCapFetch", cam, false);

    frame_cap->add_node<WhoDecodeNode>(
        "FrameCapDecode",
        dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
        2,
        false
    );

    frame_cap->add_node<WhoPPAResizeNode>(
        "FrameCapPPAResize",
        800,
        600,
        dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
        MODEL_TIME + 1
    );

    return frame_cap;
}

#endif