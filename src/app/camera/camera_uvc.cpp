#include "camera_uvc.h"

#include <cstdlib>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

CameraUvc::CameraUvc() {}

CameraUvc::~CameraUvc() {
  m_cap.release();
  cv::destroyAllWindows();
}

CameraUvc *CameraUvc::Instance() {

  static CameraUvc camera;

  return &camera;
}

void CameraUvc::initialize(std::string yaml_path) {
  // 解析配置
  YAML::Node camera = YAML::LoadFile(yaml_path)["camera"];
  m_camera.id = camera["id"].as<std::string>();
  m_camera.width = camera["width"].as<int>();
  m_camera.height = camera["height"].as<int>();
  m_camera.fps = camera["fps"].as<int>();

  cv::VideoCapture cap(m_camera.id, cv::CAP_V4L2);
  if (!cap.isOpened()) {
    spdlog::info("无法打开摄像头");
    return;
  }
  cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
  cap.set(cv::CAP_PROP_FRAME_WIDTH, m_camera.width);
  cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_camera.height);
  cap.set(cv::CAP_PROP_FPS, m_camera.fps);

  m_cap = cap;
  m_ffmpeg.initialize(m_camera.width, m_camera.height, m_camera.fps);
  ThreadPool::Instance()->enqueue(&CameraUvc::show_thread, this);
}

bool CameraUvc::frame_get(cv::Mat &frame) {
  m_cap >> frame;

  if (frame.empty()) {
    spdlog::info("无法读取帧");
    return false;
  }

  // 旋转图像（这会交换宽高）
  cv::rotate(frame, frame, cv::ROTATE_90_COUNTERCLOCKWISE);

  // 将旋转后的图像调整回原始尺寸以避免拉伸
  cv::resize(frame, frame, cv::Size(frame.rows, frame.rows));

  // 应用水平翻转
  cv::flip(frame, frame, 1);

  return true;
}

void CameraUvc::frame_show(cv::Mat frame) { m_frame.push(frame); }

void CameraUvc::show_thread() {

  spdlog::info("显示视频线程启动...");

  while (true) {
    cv::Mat frame = m_frame.pop();
    m_ffmpeg.encoder_push_stream(frame);
    // cv::imshow("USB Camera", frame);
    // cv::waitKey(1);
  }
}
