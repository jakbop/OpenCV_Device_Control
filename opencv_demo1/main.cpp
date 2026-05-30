#include <opencv2/opencv.hpp>
#include <iostream>
#include <map>
#include <MQTTClient.h>
#include <unistd.h>

using namespace cv;

// 创建一个 FaceDetectorYN 对象，加载 yunet.onnx 模型，并设置输入图像的大小为 320x320
Ptr<FaceDetectorYN> fd = FaceDetectorYN::create("../models/yunet.onnx", "", Size(320, 320));

// 创建一个 FaceRecognizerSF 对象，加载 face_recognizer_fast.onnx 模型
Ptr<FaceRecognizerSF> fr = FaceRecognizerSF::create("../models/face_recognizer_fast.onnx", "");

VideoCapture camera(0); // 打开 0 号摄像头

void registerFace(void);
std::string recognize_face(Mat img, Mat face);
void mqtt_publish(std::string message);


bool junge_exists = false;
time_t junge_time = 0;

int main()
{

    // 注册三个用户
    for (int i = 0; i < 3; i++)
    {
        registerFace();
    }

  
    Mat img, faces;
    int keyCode, r;
    int face_x, face_y, face_w, face_h;
    std::string userId;

    // 获取摄像头的分辨率和帧率
    int width = camera.get(CAP_PROP_FRAME_WIDTH);
    int height = camera.get(CAP_PROP_FRAME_HEIGHT);
    int fps = camera.get(CAP_PROP_FPS);

    std::cout << "Camera resolution: " << width << "x" << height << std::endl;
    std::cout << "Camera fps: " << fps << std::endl;

    int junge_flag;

    while (1)
    {
        // 从打开的摄像头读取一帧图像数据，并存放在 img 矩阵对象中
        camera.read(img);

        fd->setInputSize(img.size()); // 设置输入图像的大小为摄像头捕获的图像大小
        fd->detect(img, faces); // 在 img 图像中检测人脸，并将检测结果存放在 faces 矩阵对象中

        junge_flag = 0;

        // faces 矩阵对象的每一行表示一个检测到的人脸信息，每一行从左到右分别表示人脸区域左上角坐标、宽度、高度、右眼坐标、左眼坐标、鼻子坐标、右嘴角坐标、左嘴角坐标、置信度
        for(r = 0; r < faces.rows; r++)
        {
            // 识别人脸
            userId = recognize_face(img, faces.row(r));

            // 获取人脸区域的坐标和大小
            face_x = faces.at<float>(r, 0);
            face_y = faces.at<float>(r, 1);
            face_w = faces.at<float>(r, 2);
            face_h = faces.at<float>(r, 3);

            // 在 img 图像上绘制一个矩形框，框住检测到的人脸区域
        
            if(userId == "unknow")
                rectangle(img, Point(face_x, face_y), Point(face_x + face_w, face_y + face_h), Scalar(0, 0, 255), 2);
            else
            {
                rectangle(img, Point(face_x, face_y), Point(face_x + face_w, face_y + face_h), Scalar(0, 255, 0), 2);
                putText(img, userId, Point(face_x, face_y - 10), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 0, 0), 2);
            }

            if(userId == "Ww")
            {
                    junge_flag = 1;
            }

            // // 打印置信度
            // std::cout << "Confidence: " << faces.at<float>(r, 14) << std::endl;

            // 眼镜特效
            // int left_eye_x = faces.at<float>(r, 4);
            // int left_eye_y = faces.at<float>(r, 5);
            // int right_eye_x = faces.at<float>(r, 6);
            // int right_eye_y = faces.at<float>(r, 7);

            // circle(img, Point(left_eye_x, left_eye_y), 30, Scalar(0, 0, 0), 3);
            // circle(img, Point(right_eye_x, right_eye_y), 30, Scalar(0, 0, 0), 3);
        }

        if(junge_flag == 1)
        {
            junge_time = time(NULL);

            if(!junge_exists)
            {
                mqtt_publish("a");
                junge_exists = true;
            }
        }
        else
        {
            if(time(NULL) - junge_time > 10)
            {
                mqtt_publish("b");
                junge_exists = false;
            }
        }

        // 将 img 显示在名字为 junge 的窗口上
        imshow("junge", img);

        keyCode = waitKey(1);

        // 按空格拍照（即将一帧图像保存到图片文件中）
        if (keyCode == ' ')
            imwrite("../photos/1.jpg", img);

        if (keyCode == 'q')
            break;
    }

    camera.release();
    destroyAllWindows();

    // Mat img = imread("../junge.jpg");

    // imshow("yyds", img);
    // imshow("abc", img);

    // waitKey(0);

    return 0;
}

// 使用 map 容器存储用户 ID 和对应的人脸特征向量
std::map<std::string, Mat> faceDatabase;


// 人脸注册
void registerFace(void)
{
    std::string userId;

    std::cout << "请输入用户 ID：";
    std::cin >> userId;

    // 这里省略其他注册流程，例如输入用户姓名、年龄等信息

    std::cout << "请正对摄像头，按空格拍照注册人脸..." << std::endl;
    // 从摄像头捕获用户的人脸图像
    Mat img, faces;

    while (true)
    {
        camera.read(img);

        fd->setInputSize(img.size());
        fd->detect(img, faces);

        for (int r = 0; r < faces.rows; r++)
        {
            int face_x = faces.at<float>(r, 0);
            int face_y = faces.at<float>(r, 1);
            int face_w = faces.at<float>(r, 2);
            int face_h = faces.at<float>(r, 3);
            // 在 img 图像上绘制一个矩形框，框住检测到的人脸区域
            rectangle(img, Point(face_x, face_y), Point(face_x + face_w, face_y + face_h), Scalar(0, 255, 0), 2);
        }
        // 将 img 显示在名字为 Register Face 的窗口上
        imshow("Register Face", img);
        int keyCode = waitKey(1);

        if (keyCode == ' ')
        {
            // 保存注册的人脸图像
            if (faces.rows != 1)
            {
                std::cout << "请确保只检测到一个人脸进行注册！" << std::endl;
                continue;
            }

            // 对齐和裁减人脸图像
            Mat alignedFace;
            // 通过调用 FaceRecognizerSF 对象的 alignCrop() 方法，对检测到的人脸进行对齐和裁剪操作，得到一个新的 Mat 对象 alignedFace，其中只包含对齐和裁剪后的人脸图像
            fr->alignCrop(img, faces.row(0), alignedFace);

            // 提取人脸特征向量(128 点)
            Mat featureVector;
            fr->feature(alignedFace, featureVector);

            // 将用户 ID 和对应的人脸特征向量存储到数据库中
            faceDatabase[userId] = featureVector.clone();

            std::cout << "用户注册成功！" << std::endl;
            break;
        }
    }

    //destroyWindow("Register Face");
    destroyAllWindows();
}


// 人脸识别
// 识别人脸
std::string recognize_face(Mat img, Mat face)
{
    Mat aligned_img;  // 用于存放对齐和裁剪之后的人脸图像
    fr->alignCrop(img, face, aligned_img);  // 进行对齐和裁剪操作

    Mat feature;
    fr->feature(aligned_img, feature);  // 提取人脸特征向量（128 点）

    for(auto f : faceDatabase)
    {
        // math() 函数用于比较两个特征向量，计算它们之间的相似度，比较方法有两种：欧氏距离和余弦距离。这里采用余弦距离，计算得到的相似度值在 0 到 1 之间，值越大表示两个特征向量越相似。
        double cosine_score = fr->match(feature, f.second);  // 比较两个特征向量（采用余弦距离），获得相似度（0 - 1），值越大相似度越高

        if(cosine_score > 0.363)
            return f.first;
    }

    return "unknow";
}


// 基于 paho-mqtt-c 库连接 mqtt 服务器，服务器地址为 itmojun.com，端口号为 1883，匿名登录，向主题 itmojun/cmd 发布指定消息
// 安装 paho-mqtt 方法：
// 1. 下载 paho-mqtt-c 库的源代码：
//    git clone https://github.com/eclipse/paho.mqtt.c.git
// 2. 进入 paho-mqtt-c 目录，创建 build 目录并进入：
//    cd paho.mqtt.c
//    mkdir build
//    cd build
// 3. 使用 cmake 构建并安装库：
//    cmake .. 
//    make
//    sudo make install

void mqtt_publish(std::string message)
{
    // MQTTClient_publishMessage() 函数用于向 MQTT 服务器发布消息。函数的参数包括 MQTTClient 对象、主题名称、消息内容和消息质量等级等。函数会将消息发送到指定的主题上，供订阅该主题的客户端接收。
    const char* ADDRESS = "tcp://39.104.71.92:1883";
    std::string clientId = "opencv_demo_client_" + std::to_string(getpid());
    const char* TOPIC = "wengyuanhang/cmd";
    // MQTT 协议中定义了三种消息质量等级（QoS，Quality of Service），分别是 QoS 0、QoS 1 和 QoS 2。QoS 0 表示消息最多发送一次，可能会丢失；QoS 1 表示消息至少发送一次，可能会重复；QoS 2 表示消息仅发送一次，确保消息不会丢失或重复。这里设置为 QoS 1，表示消息至少发送一次。
    int QOS = 1;

    MQTTClient client;
    // 创建 MQTTClient 对象
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    rc = MQTTClient_create(&client, ADDRESS, clientId.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "MQTTClient_create failed, rc=" << rc << std::endl;
        return;
    }

    // 设置连接选项
    conn_opts.keepAliveInterval = 20;
    // cleansession 参数用于指定客户端是否在连接断开后清除会话状态。当 cleansession 设置为 1 时，客户端在连接断开后会清除所有的会话状态，包括订阅信息、未发送的消息等；当 cleansession 设置为 0 时，客户端在连接断开后会保留会话状态，下次连接时可以继续使用之前的订阅信息和未发送的消息等。这里设置为 1，表示每次连接都会清除会话状态。
    conn_opts.cleansession = 1;

    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "MQTTClient_connect failed, rc=" << rc << std::endl;
        MQTTClient_destroy(&client);
        return;
    }

    pubmsg.payload = (void*)message.c_str();
    pubmsg.payloadlen = static_cast<int>(message.size());
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "MQTTClient_publishMessage failed, rc=" << rc << std::endl;
    } else {
        rc = MQTTClient_waitForCompletion(client, token, 10000L);
        if (rc != MQTTCLIENT_SUCCESS) {
            std::cerr << "MQTTClient_waitForCompletion failed, rc=" << rc << std::endl;
        }
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
}
