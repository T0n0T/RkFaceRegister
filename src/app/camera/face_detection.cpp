#include "face_detection.h"
#include "backend_sqlite.h"
#include "base64.h"
#include "camera_uvc.h"
#include "local_time.h"
#include "rknn_inference.h"
#include "socket.h"
#include "thread_pool.h"
#include "train_model.h"
#include "user_sqlite.h"
#include "web_connect.h"
#include <cstddef>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#define TASK_SWITCH(x) std::bind(&FaceDetection::x, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

FaceDetection::FaceDetection() {

    m_frame_interval_cnt = 0;
    m_task_state = 0;
    m_user_num = UserSQLite::Instance()->get_row_count();
}

FaceDetection::~FaceDetection() {}

FaceDetection *FaceDetection::Instance() {

    static FaceDetection face_detection;

    return &face_detection;
}

void FaceDetection::initialize(std::string yaml_path) {
    YAML::Node assets = YAML::LoadFile(yaml_path)["assets"];
    std::string font_path = assets[std::string("font_path")].as<std::string>();

    m_ft2 = cv::freetype::createFreeType2();
    m_ft2->loadFontData(font_path, 0);
    ThreadPool::Instance()->enqueue(&FaceDetection::dispose_thread, this);
    RnkkInference::Instance()->initialize(yaml_path);
}

int FaceDetection::detection_faces(cv::Mat image, std::vector<cv::Rect> &objects) {

    if (++m_frame_interval_cnt < 8)
        return 1;
    m_frame_interval_cnt = 0;
    objects.clear();
    RnkkInference::Instance()->detection_face(image, objects);
    if (objects.size() > 0) {
        // spdlog::info("face num: {}", objects.size());
    }

    return 0;
}

void FaceDetection::dispose_thread() {

    spdlog::info("dispose thread started");

    while (true) {
        cv::Mat frame = m_frame.pop();
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        // cv::cvtColor(frame, gray, cv::COLOR_RGBA2RGB);
        int frame_skipp_state = detection_faces(frame, m_faces);
        if (m_task_state == 0) {
            detection_face_task(frame, gray, m_faces, frame_skipp_state);
        } else if (m_task_state == 1) {
            enroll_face_task(frame, gray, m_faces);
        }
        CameraUvc::Instance()->frame_show(frame);
    }
}

void FaceDetection::enroll_face_task(cv::Mat &frame, cv::Mat &gray, std::vector<cv::Rect> faces) {
    size_t faces_size = faces.size();
    if (faces_size == 0 || faces_size > 1)
        return;

    cv::Rect face_rect = faces.front();
    cv::Mat face = gray(face_rect);
    TrainModel::Instance()->train_size(face);
    m_enroll_face_images.push_back(face);
    m_enroll_face_labels.push_back(m_user_num);
    size_t count = m_enroll_face_images.size();

    // 显示采集状态
    std::string label_text = "gather:" + std::to_string((int)((count * 100) / 50.0f)) + "%";
    cv::rectangle(frame, face_rect, cv::Scalar(0, 255, 0), 2);
    putText(frame, label_text, cv::Point(25, 25), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 0, 0), 2);

    if (count < 5)
        return;
    m_task_state = 0;
    spdlog::info("第 {} 张面部图像采集已完成", m_user_num);
    TrainModel::Instance()->train_data_add(m_enroll_face_images, m_enroll_face_labels);
    UserSQLite::Instance()->insert_data(m_user_num, m_user_name);
    m_enroll_face_images.clear();
    m_enroll_face_labels.clear();
}

void FaceDetection::detection_face_task(cv::Mat &frame, cv::Mat &gray, std::vector<cv::Rect> faces, int frame_skipp_state) {
    size_t faces_size = faces.size();
    if (faces_size == 0)
        return;

    if (frame_skipp_state) {
        size_t frame_skipp_size = m_frame_skipp.size();
        for (size_t i; i < frame_skipp_size; i++) {
            m_ft2->putText(frame, m_frame_skipp[i].label_text, m_frame_skipp[i].text_org, 30, m_frame_skipp[i].color, -1, cv::LINE_AA, false);
            cv::rectangle(frame, m_frame_skipp[i].face_rect, m_frame_skipp[i].scalar, 2);
        }
        return;
    }

    std::vector<int> detection_label;
    std::string label_text = "";
    FrameSkipp frame_skipp_tmp;
    m_frame_skipp.clear();

    for (size_t i = 0; i < faces_size; i++) {
        int predicted_label = -1;
        double confidence = 0.0;
        cv::Rect face_rect = faces[i];
        cv::Mat face = gray(face_rect);
        cv::Scalar scalar;
        cv::Scalar color;
        cv::Point text_org;

        bool state = TrainModel::Instance()->train_model_get(face, predicted_label, confidence);
        if (state && (predicted_label != -1) && (confidence < CONFIDENCE_THRESHOLD)) {
            detection_label.push_back(predicted_label);
            scalar = cv::Scalar(0, 255, 0);
            label_text = UserSQLite::Instance()->get_name_by_id(predicted_label);
            color = cv::Scalar(255, 0, 0);
            text_org = cv::Point(face_rect.x, face_rect.y - 35);
            m_ft2->putText(frame, label_text, text_org, 30, color, -1, cv::LINE_AA, false);
        } else {
            // 黄色
            scalar = cv::Scalar(0, 255, 255);
        }
        cv::rectangle(frame, face_rect, scalar, 2);
        frame_skipp_tmp.text_org = text_org;
        frame_skipp_tmp.color = color;
        frame_skipp_tmp.label_text = label_text;
        frame_skipp_tmp.face_rect = face_rect;
        frame_skipp_tmp.scalar = scalar;
        m_frame_skipp.push_back(frame_skipp_tmp);
    }
    if (detection_label.size() == 0)
        return;
    std::sort(detection_label.begin(), detection_label.end());
    if (m_last_detection_label == detection_label)
        return;
    m_last_detection_label = detection_label;
    std::string imgBase64 = util::encodeBase64(util::mat_to_buffer(frame));
    std::string current_time = util::get_cuurent_time();
    int imageId = BackendSQLite::Instance()->insert_data(current_time, imgBase64);
    WebConnect::Instance()->send_image(CLIENT_ALL, imageId, current_time, imgBase64);
    spdlog::info("{} was identified", label_text);
}

void FaceDetection::frame_data_add(cv::Mat frame) {

    m_frame.push(frame);
}

void FaceDetection::enroll_face(std::string name) {

    m_task_state = 1;
    m_user_name = name;
    m_user_num++;
}
