import json
import time
import queue
import base64
import logging
import cv2
import threading
from flask import Flask , Response, request, render_template, jsonify
from flask_socketio import SocketIO, emit

RTSP_URL = 'rtsp://192.168.1.68:8554/live'

logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(levelname)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)

class ClientConnect:
    def __init__(self, app_, socketio_, socket_, sqlite_):
        self.m_image_deque = queue.Queue()
        self.m_app = app_
        self.m_socketio = socketio_
        self.m_sqlite = sqlite_
        self.m_socket = socket_
        
        self._register_recv()
        self._register_routes()
        self._register_socketio()
        self.m_socket.start()
        self.cap = cv2.VideoCapture(RTSP_URL)

    #录入人脸命令
    def send_type_in(self, name):
        send_json = {}
        send_json["Cmd"] = "TypeIn"
        send_json["Name"] = name
        self.m_socket.data_subpackage(send_json)

    #删除照片发送给服务端
    def send_delete_image(self, id):
        send_json = {}
        send_json["Cmd"] = "DeleteImage"
        send_json["Id"] = id
        self.m_socket.data_subpackage(send_json)
    
    #发送需要图片的id
    def send_need_image_id(self, id_list):
        send_json = {}
        send_json["Cmd"] = "NeedImageId"
        send_json["Id"] = id_list
        self.m_socket.data_subpackage(send_json)
    
    #接收服务端的图片
    def recv_Upload_func(self, json_data):
        image_id = json_data['Id']
        image_time = json_data['Time']
        image_base64 = json_data['ImgBase64']
        self.m_sqlite.insert(image_id, image_time, image_base64)
        self.m_image_deque.put({'id': image_id, 'time': image_time, 'image': image_base64})
    
    def _start_stream(self):
        logging.info('Start streaming')
        data = self.m_sqlite.get_all_data()
        while not self.m_image_deque.empty():
            self.m_image_deque.get()
        for row in data:
            self.m_image_deque.put({'id': row[0], 'time': row[1], 'image': row[2]})
        self.m_socketio.start_background_task(self._stream_images) 
        self.m_socketio.start_background_task(self._generate_and_emit_frames) 

    def _handle_connect(self):
        logging.info('Web connected')
    
    def _handle_disconnect(self):
        logging.info('Web disconnected')

        #注册接收socket接收命令函数
    def _register_recv(self):
        self.m_socket.recv_cmd_func_bind("Upload", self.recv_Upload_func)
        self.m_socket.recv_cmd_func_bind("UploadId", self._recv_UploadId_func)

    #接收需要同步的照片id
    def _recv_UploadId_func(self, json_data):
        id_list = json_data["Id"]
        local_id = self.m_sqlite.get_all_id()
        missing = list(set(id_list) - set(local_id))
        missing.sort()
        if len(missing) == 0:
            return
        self.send_need_image_id(missing)
        logging.info(f"need image id: {missing}")

    def _register_routes(self):
        self.m_app.add_url_rule('/submit_form', 'submit_form', self._submit_form, methods=['POST'])
        self.m_app.add_url_rule('/delete_image', 'delete_image', self._delete_image, methods=['POST'])
    
    def _register_socketio(self):
        self.m_socketio.on_event('start_stream', self._start_stream)
        self.m_socketio.on_event('connect', self._handle_connect)
        self.m_socketio.on_event('disconnect', self._handle_disconnect)

    #录入人脸
    def _submit_form(self):
        name = request.get_json().get('name')
        logging.info(f"test: {request.get_json()}")
        logging.info(f"_submit_form recv name: {name}")
        self.send_type_in(name)
        return jsonify({"message": "录入成功", "status": "ok"}), 200


    #删除显示图片
    def _delete_image(self):
        image_id = request.get_json().get('id')
        logging.info("_delete_image recv id: ", image_id)
        self.send_delete_image(image_id)
        self.m_sqlite.delete_by_id(image_id)
        return jsonify({"message": "删除成功", "status": "ok"}), 200

    #向客户端显示照片
    def _stream_images(self):
        while True:
            if not self.m_image_deque.empty():
                img_bytes = self.m_image_deque.get()
                self.m_socketio.emit('image', {'data': img_bytes})
            self.m_socketio.sleep(0.01)

    def _generate_and_emit_frames(self):
        while True:
            success, frame = self.cap.read()
            if not success:
                continue
            # 编码成 JPEG
            _, buffer = cv2.imencode('.jpg', frame)
            # base64 编码
            frame_b64 = base64.b64encode(buffer).decode('utf-8')
            # 发送给前端
            self.m_socketio.emit('rtsp_stream', {'data': frame_b64})
            self.m_socketio.sleep(0.01)
