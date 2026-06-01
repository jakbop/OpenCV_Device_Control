/**
 * ============================================================================
 * 项目名称：OpenMV_Control — 基于人脸识别的物联网控制系统
 * 功能描述：通过摄像头实时检测和识别人脸，根据识别结果通过 MQTT 协议发送控制指令，
 *          人脸特征数据持久化存储在阿里云 MySQL 数据库中。
 *
 * 核心流程：
 *   1. 启动时连接 MySQL，加载已注册用户的人脸特征向量到内存
 *   2. 主循环中逐帧进行人脸检测与识别
 *   3. 识别到特定用户（Ww）时，通过 MQTT 发送上线消息；离开超过10秒发送离线消息
 *   4. 按 R 键可随时注册新人脸，数据同步写入数据库
 *
 * 依赖库：
 *   - OpenCV 4.5+（含 DNN 模块）：人脸检测（YuNet）、人脸识别（FaceRecognizerSF）
 *   - paho-mqtt-c：MQTT 协议通信
 *   - MySQL C Client（libmysqlclient）：数据库操作
 *
 * 操作说明：
 *   - 按 R 键：注册新人脸
 *   - 按空格键：拍照保存到 photos/ 目录
 *   - 按 Q 键：退出程序
 * ============================================================================
 */

#include <opencv2/opencv.hpp>
#include <iostream>
#include <map>
#include <cstring>
#include <MQTTClient.h>
#include <unistd.h>
#include <mysql/mysql.h>

using namespace cv;

/* ========================== 全局对象 ========================== */

// 人脸检测器，使用 YuNet 模型（基于 OpenCV DNN）
// 参数1：模型文件路径  参数2：空字符串表示不使用额外配置  参数3：初始输入尺寸 320x320
Ptr<FaceDetectorYN> fd = FaceDetectorYN::create("../models/yunet.onnx", "", Size(320, 320));

// 人脸识别器，使用 FaceRecognizerSF 模型
// 用于对检测到的人脸进行对齐裁剪、提取128维特征向量、计算特征相似度
Ptr<FaceRecognizerSF> fr = FaceRecognizerSF::create("../models/face_recognizer_fast.onnx", "");

// 打开 0 号摄像头（默认摄像头设备）
VideoCapture camera(0);

// 人脸数据库（内存），key=用户ID，value=128维特征向量（Mat 1x128 CV_32F）
// 程序启动时从 MySQL 加载，注册新人脸时同步更新
std::map<std::string, Mat> faceDatabase;

/* ========================== MySQL 配置 ========================== */

// MySQL 连接句柄
MYSQL *mysql_conn = NULL;

// 阿里云 MySQL 数据库连接参数
const char* DB_HOST = "39.104.71.92";   // 数据库主机地址
const char* DB_USER = "Ww";             // 数据库用户名
const char* DB_PASS = "123456";         // 数据库密码
const char* DB_NAME = "OpenMVdb";       // 数据库名称
int DB_PORT = 3306;                     // 数据库端口号

/* ========================== 业务状态变量 ========================== */

// junge_exists：标记目标用户（Ww）是否当前在场
// junge_time：记录目标用户最后一次被检测到的时间戳
// 用于实现"离开超过10秒才发送离线消息"的逻辑，避免短暂遮挡误判
bool junge_exists = false;
time_t junge_time = 0;

/* ========================== 函数声明 ========================== */

void registerFace(void);                                          // 人脸注册
std::string recognize_face(Mat img, Mat face);                    // 人脸识别
void mqtt_publish(std::string message);                           // MQTT 消息发布
bool mysql_init_connection(void);                                 // MySQL 连接初始化
void mysql_close_connection(void);                                // MySQL 连接关闭
bool mysql_save_face(const std::string& userId, const Mat& featureVector);  // 保存人脸特征到数据库
bool mysql_load_faces(void);                                      // 从数据库加载所有人脸特征

/* ========================== MySQL 操作函数实现 ========================== */

/**
 * mysql_init_connection - 初始化 MySQL 连接
 *
 * 执行流程：
 *   1. mysql_library_init() — 初始化 MySQL 客户端库
 *   2. mysql_init() — 创建连接句柄
 *   3. mysql_options() — 设置自动重连和字符集
 *   4. mysql_real_connect() — 建立与远程数据库的 TCP 连接
 *
 * 返回值：连接成功返回 true，失败返回 false
 */
bool mysql_init_connection(void)
{
    // 初始化 MySQL 客户端库，必须在其他 MySQL 函数之前调用
    if (mysql_library_init(0, NULL, NULL)) {
        std::cerr << "mysql_library_init() failed" << std::endl;
        return false;
    }

    // 创建 MySQL 连接句柄
    mysql_conn = mysql_init(NULL);
    if (!mysql_conn) {
        std::cerr << "mysql_init() failed" << std::endl;
        mysql_library_end();
        return false;
    }

    // 设置自动重连：当 MySQL 连接意外断开时，下次查询会自动尝试重新连接
    bool reconnect = true;
    mysql_options(mysql_conn, MYSQL_OPT_RECONNECT, &reconnect);

    // 设置字符集为 utf8mb4，支持中文用户名和 emoji
    mysql_options(mysql_conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    // 建立与阿里云 MySQL 服务器的连接
    // 参数依次为：句柄、主机、用户名、密码、数据库名、端口、unix_socket、客户端标志
    if (!mysql_real_connect(mysql_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)) {
        std::cerr << "MySQL 连接失败: " << mysql_error(mysql_conn) << std::endl;
        mysql_close(mysql_conn);
        mysql_conn = NULL;
        mysql_library_end();
        return false;
    }

    std::cout << "MySQL 连接成功" << std::endl;
    return true;
}

/**
 * mysql_close_connection - 关闭 MySQL 连接并释放资源
 *
 * 必须在程序退出前调用，释放连接句柄和客户端库资源
 */
void mysql_close_connection(void)
{
    if (mysql_conn) {
        mysql_close(mysql_conn);
        mysql_conn = NULL;
    }
    // 释放 MySQL 客户端库资源，与 mysql_library_init() 配对
    mysql_library_end();
}

/**
 * mysql_save_face - 将用户人脸特征向量保存到 MySQL 数据库
 *
 * @param userId         用户ID（如 "Ww"）
 * @param featureVector  128维人脸特征向量（Mat 1x128 CV_32F，共512字节）
 *
 * 实现细节：
 *   - 使用 mysql_real_escape_string() 对用户ID和二进制特征数据进行转义，
 *     防止特殊字符导致 SQL 语法错误或 SQL 注入
 *   - 使用 INSERT ... ON DUPLICATE KEY UPDATE 语法：
 *     如果 user_id 已存在则更新特征向量，否则插入新记录
 *   - 特征向量以 BLOB 类型存储在数据库中
 *
 * 返回值：保存成功返回 true，失败返回 false
 */
bool mysql_save_face(const std::string& userId, const Mat& featureVector)
{
    if (!mysql_conn) return false;

    // clone() 确保特征数据在内存中是连续存储的
    Mat continuousFeature = featureVector.clone();
    // 计算特征数据的字节大小：128个float × 4字节 = 512字节
    unsigned long dataSize = continuousFeature.total() * continuousFeature.elemSize();

    // 对用户ID进行 SQL 转义，防止特殊字符破坏 SQL 语句
    char escapedUserId[256];
    mysql_real_escape_string(mysql_conn, escapedUserId, userId.c_str(), userId.length());

    // 对二进制特征数据进行 SQL 转义
    // 转义后最大长度为原数据的 2倍+1，+1 是为了存放结尾的 '\0'
    char* escapedData = new char[dataSize * 2 + 1];
    mysql_real_escape_string(mysql_conn, escapedData, reinterpret_cast<const char*>(continuousFeature.data), dataSize);

    // 构建 SQL 语句：INSERT 新记录，若 user_id 已存在则 UPDATE 特征向量
    std::string query = "INSERT INTO face_users (user_id, feature_vector) VALUES ('";
    query += escapedUserId;
    query += "', '";
    query += escapedData;
    query += "') ON DUPLICATE KEY UPDATE feature_vector = '";
    query += escapedData;
    query += "'";

    // 执行 SQL 语句
    int res = mysql_query(mysql_conn, query.c_str());
    delete[] escapedData;

    if (res != 0) {
        std::cerr << "MySQL 插入失败: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }

    return true;
}

/**
 * mysql_load_faces - 从 MySQL 数据库加载所有已注册用户的人脸特征向量
 *
 * 执行流程：
 *   1. 执行 SELECT 查询获取所有记录
 *   2. mysql_store_result() 获取完整结果集
 *   3. 逐行读取，将 BLOB 数据还原为 Mat(1, 128, CV_32F) 特征向量
 *   4. 校验特征向量大小是否为 512字节（128 × sizeof(float)）
 *   5. 存入内存 faceDatabase
 *
 * 返回值：加载成功返回 true，失败返回 false
 */
bool mysql_load_faces(void)
{
    if (!mysql_conn) return false;

    // 查询所有已注册用户的 ID 和特征向量
    if (mysql_query(mysql_conn, "SELECT user_id, feature_vector FROM face_users") != 0) {
        std::cerr << "MySQL 查询失败: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }

    // 获取完整结果集到客户端内存
    MYSQL_RES *result = mysql_store_result(mysql_conn);
    if (!result) {
        std::cerr << "MySQL 获取结果集失败: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }

    // 逐行遍历结果集
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        // mysql_fetch_lengths() 返回每列数据的实际字节长度
        unsigned long *lengths = mysql_fetch_lengths(result);
        if (!lengths || !row[0] || !row[1]) continue;

        std::string userId = row[0];           // 第0列：用户ID
        unsigned long featureSize = lengths[1]; // 第1列：特征向量的实际字节长度

        // 创建 1x128 的 float 矩阵用于存放特征向量
        Mat feature(1, 128, CV_32F);
        // 校验特征向量大小：128个float × 4字节 = 512字节
        if (featureSize == 128 * sizeof(float)) {
            // 将数据库中的二进制数据拷贝到 Mat 的数据区
            memcpy(feature.data, row[1], featureSize);
            // 存入内存数据库，clone() 确保数据独立
            faceDatabase[userId] = feature.clone();
            std::cout << "加载用户: " << userId << std::endl;
        } else {
            std::cerr << "用户 " << userId << " 的特征向量大小异常: " << featureSize << std::endl;
        }
    }

    // 释放结果集内存
    mysql_free_result(result);
    return true;
}

/* ========================== 人脸注册函数 ========================== */

/**
 * registerFace - 人脸注册流程
 *
 * 流程：
 *   1. 关闭主窗口，避免出现两个摄像头窗口导致卡死
 *   2. 终端提示输入用户 ID
 *   3. 打开注册窗口，实时显示摄像头画面并标注检测到的人脸
 *   4. 用户按空格拍照：
 *      - 检查画面中是否只有1个人脸（防止多人误注册）
 *      - alignCrop() 对齐裁剪人脸
 *      - feature() 提取128维特征向量
 *      - 存入内存 faceDatabase + 写入 MySQL 数据库
 *   5. 按 ESC 取消注册
 *   6. 注册结束后关闭所有窗口，返回主循环
 */
void registerFace(void)
{
    // 先关闭主窗口"junge"，避免注册时出现两个摄像头窗口
    // destroyAllWindows() 关闭所有 OpenCV 窗口
    // waitKey(1) 让 OpenCV 事件循环执行窗口销毁操作，否则 std::cin 会阻塞导致窗口卡死
    destroyAllWindows();
    waitKey(1);

    std::string userId;

    // 在终端提示用户输入要注册的用户ID
    std::cout << "请输入用户 ID：";
    std::cin >> userId;

    std::cout << "请正对摄像头，按空格拍照注册人脸，按 ESC 取消..." << std::endl;
    Mat img, faces;

    // 注册循环：持续显示摄像头画面，等待用户操作
    while (true)
    {
        // 从摄像头读取一帧图像
        camera.read(img);

        // 设置检测器输入尺寸为当前帧大小，并执行人脸检测
        fd->setInputSize(img.size());
        fd->detect(img, faces);

        // 在画面上绘制检测到的人脸矩形框（绿色），方便用户确认位置
        for (int r = 0; r < faces.rows; r++)
        {
            int face_x = faces.at<float>(r, 0);  // 人脸区域左上角 x 坐标
            int face_y = faces.at<float>(r, 1);  // 人脸区域左上角 y 坐标
            int face_w = faces.at<float>(r, 2);  // 人脸区域宽度
            int face_h = faces.at<float>(r, 3);  // 人脸区域高度
            rectangle(img, Point(face_x, face_y), Point(face_x + face_w, face_y + face_h), Scalar(0, 255, 0), 2);
        }

        // 在 "Register Face" 窗口中显示画面
        imshow("Register Face", img);
        int keyCode = waitKey(1);

        // 按空格键拍照注册
        if (keyCode == ' ')
        {
            // 必须确保画面中只有1个人脸，否则无法确定注册的是谁
            if (faces.rows != 1)
            {
                std::cout << "请确保只检测到一个人脸进行注册！" << std::endl;
                continue;
            }

            // alignCrop()：根据检测到的关键点（眼、鼻、嘴）对人脸进行仿射变换对齐，
            // 并裁剪出标准化的人脸图像，消除角度和尺度差异
            Mat alignedFace;
            fr->alignCrop(img, faces.row(0), alignedFace);

            // feature()：从对齐后的人脸图像中提取128维特征向量
            // 该特征向量是人脸的数学表示，同一个人的不同照片特征相似度高
            Mat featureVector;
            fr->feature(alignedFace, featureVector);

            // 将特征向量存入内存数据库
            faceDatabase[userId] = featureVector.clone();

            // 将特征向量同步写入 MySQL 数据库（持久化存储）
            if (mysql_save_face(userId, featureVector)) {
                std::cout << "用户 " << userId << " 注册成功！已保存到数据库" << std::endl;
            } else {
                std::cerr << "用户 " << userId << " 注册成功！但保存到数据库失败" << std::endl;
            }
            break;
        }

        // 按 ESC 键取消注册
        if (keyCode == 27)
        {
            std::cout << "取消注册" << std::endl;
            break;
        }
    }

    // 关闭注册窗口，主循环中会自动重建 "junge" 主窗口
    destroyAllWindows();
}

/* ========================== 人脸识别函数 ========================== */

/**
 * recognize_face - 识别单个人脸
 *
 * @param img   原始图像
 * @param face  检测到的单个人脸信息（faces矩阵的一行）
 *
 * 流程：
 *   1. alignCrop() — 对齐裁剪人脸
 *   2. feature() — 提取128维特征向量
 *   3. 遍历 faceDatabase，用 match() 计算余弦相似度
 *   4. 相似度 > 0.363 则认为是同一人，返回用户ID
 *   5. 所有已注册用户都不匹配则返回 "unknow"
 *
 * 返回值：识别到的用户ID，或 "unknow"
 */
std::string recognize_face(Mat img, Mat face)
{
    // 对齐裁剪：将检测到的人脸旋转缩放到标准姿态
    Mat aligned_img;
    fr->alignCrop(img, face, aligned_img);

    // 提取特征向量：将人脸图像转换为128维浮点向量
    Mat feature;
    fr->feature(aligned_img, feature);

    // 遍历内存中所有已注册用户，逐一比对特征向量
    for(auto f : faceDatabase)
    {
        // match() 计算两个特征向量的余弦相似度，值域 [0, 1]
        // 值越大表示越相似，0.363 是经验阈值
        double cosine_score = fr->match(feature, f.second);

        if(cosine_score > 0.363)
            return f.first;  // 匹配成功，返回用户ID
    }

    return "unknow";  // 所有已注册用户都不匹配
}

/* ========================== MQTT 通信函数 ========================== */

/**
 * mqtt_publish - 通过 MQTT 协议发布消息
 *
 * @param message 要发布的消息内容（"a" 表示用户上线，"b" 表示用户离线）
 *
 * 连接信息：
 *   - 服务器：39.104.71.92:1883
 *   - 主题：wengyuanhang/cmd
 *   - QoS：1（至少一次送达）
 *   - 客户端ID：opencv_demo_client_<进程PID>
 *
 * 注意：当前实现为同步阻塞方式，每次发布都会新建连接，
 *       可能导致摄像头画面卡顿，后续可优化为异步或长连接
 */
void mqtt_publish(std::string message)
{
    // MQTT Broker 地址
    const char* ADDRESS = "tcp://39.104.71.92:1883";
    // 客户端ID，附加进程PID确保唯一性
    std::string clientId = "opencv_demo_client_" + std::to_string(getpid());
    // 发布主题
    const char* TOPIC = "wengyuanhang/cmd";
    // QoS 1：消息至少送达一次，可能重复
    int QOS = 1;

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // 创建 MQTT 客户端实例
    rc = MQTTClient_create(&client, ADDRESS, clientId.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "MQTTClient_create failed, rc=" << rc << std::endl;
        return;
    }

    // 设置连接选项
    conn_opts.keepAliveInterval = 20;   // 心跳间隔20秒，防止连接被断开
    conn_opts.cleansession = 1;         // 清除会话：连接断开后不保留订阅和未发送消息

    // 连接到 MQTT Broker
    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "MQTTClient_connect failed, rc=" << rc << std::endl;
        MQTTClient_destroy(&client);
        return;
    }

    // 设置消息内容
    pubmsg.payload = (void*)message.c_str();                    // 消息体
    pubmsg.payloadlen = static_cast<int>(message.size());       // 消息长度
    pubmsg.qos = QOS;                                           // 消息质量等级
    pubmsg.retained = 0;                                        // 不保留消息（Broker不存储最后一条）

    // 发布消息并等待完成
    rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "MQTTClient_publishMessage failed, rc=" << rc << std::endl;
    } else {
        // waitForCompletion() 阻塞等待消息送达确认，超时10秒
        rc = MQTTClient_waitForCompletion(client, token, 10000L);
        if (rc != MQTTCLIENT_SUCCESS) {
            std::cerr << "MQTTClient_waitForCompletion failed, rc=" << rc << std::endl;
        }
    }

    // 断开连接并销毁客户端，超时10秒等待断开完成
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
}

/* ========================== 主函数 ========================== */

/**
 * main - 程序入口
 *
 * 整体流程：
 *   1. 检查摄像头是否可用
 *   2. 连接 MySQL 数据库，加载已注册用户的人脸特征
 *   3. 进入主循环：
 *      a. 读取摄像头帧
 *      b. YuNet 人脸检测
 *      c. FaceRecognizerSF 人脸识别
 *      d. 根据识别结果执行 MQTT 业务逻辑
 *      e. 显示画面，处理按键
 *   4. 退出时释放摄像头、关闭窗口、断开数据库连接
 */
int main()
{
    // 检查摄像头是否成功打开
    if (!camera.isOpened()) {
        std::cerr << "摄像头打开失败" << std::endl;
        return -1;
    }

    // 连接 MySQL 数据库
    if (!mysql_init_connection()) {
        std::cerr << "MySQL 连接失败，程序退出" << std::endl;
        return -1;
    }

    // 从数据库加载所有已注册用户的人脸特征向量到内存
    if (mysql_load_faces()) {
        std::cout << "从数据库加载了 " << faceDatabase.size() << " 个已注册用户" << std::endl;
    } else {
        std::cerr << "从数据库加载人脸数据失败" << std::endl;
    }

    // 声明主循环中使用的变量
    Mat img, faces;               // img: 当前帧图像, faces: 检测到的人脸信息矩阵
    int keyCode, r;               // keyCode: 按键值, r: 循环计数器
    int face_x, face_y, face_w, face_h;  // 人脸区域坐标和尺寸
    std::string userId;           // 识别到的用户ID

    // 获取摄像头参数并打印
    int width = camera.get(CAP_PROP_FRAME_WIDTH);   // 帧宽度
    int height = camera.get(CAP_PROP_FRAME_HEIGHT); // 帧高度
    int fps = camera.get(CAP_PROP_FPS);             // 帧率

    std::cout << "Camera resolution: " << width << "x" << height << std::endl;
    std::cout << "Camera fps: " << fps << std::endl;
    std::cout << "按 R 键注册新人脸，按空格拍照，按 Q 键退出" << std::endl;

    // junge_flag：当前帧是否检测到目标用户 "Ww"
    int junge_flag;

    // ==================== 主循环 ====================
    while (1)
    {
        // 从摄像头读取一帧图像
        camera.read(img);

        // 动态设置检测器输入尺寸为当前帧大小
        // 必须每帧设置，因为摄像头分辨率可能变化
        fd->setInputSize(img.size());

        // 执行人脸检测
        // faces 矩阵每行包含一个人脸的信息：
        // [x, y, w, h, 右眼x, 右眼y, 左眼x, 左眼y, 鼻子x, 鼻子y, 右嘴角x, 右嘴角y, 左嘴角x, 左嘴角y, 置信度]
        fd->detect(img, faces);

        junge_flag = 0;

        // 遍历当前帧中检测到的每一个人脸
        for(r = 0; r < faces.rows; r++)
        {
            // 对当前人脸进行识别，返回用户ID或"unknow"
            userId = recognize_face(img, faces.row(r));

            // 获取人脸区域的坐标和尺寸
            face_x = faces.at<float>(r, 0);
            face_y = faces.at<float>(r, 1);
            face_w = faces.at<float>(r, 2);
            face_h = faces.at<float>(r, 3);

            // 根据识别结果绘制不同的标注
            if(userId == "unknow")
            {
                // 未知人脸：红色矩形框（BGR: 0,0,255）
                rectangle(img, Point(face_x, face_y), Point(face_x + face_w, face_y + face_h), Scalar(0, 0, 255), 2);
            }
            else
            {
                // 已知人脸：绿色矩形框（BGR: 0,255,0）+ 蓝色用户ID文字（BGR: 255,0,0）
                rectangle(img, Point(face_x, face_y), Point(face_x + face_w, face_y + face_h), Scalar(0, 255, 0), 2);
                putText(img, userId, Point(face_x, face_y - 10), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 0, 0), 2);
            }

            // 检测到目标用户 "Ww"，设置标记
            if(userId == "Ww")
            {
                    junge_flag = 1;
            }
        }

        // ==================== MQTT 业务逻辑 ====================
        // 根据目标用户（Ww）的在场状态发送 MQTT 消息

        if(junge_flag == 1)
        {
            // 当前帧检测到目标用户，更新最后检测时间
            junge_time = time(NULL);

            // 如果之前标记为不在场，则发送上线消息
            // 只在状态变化时发送，避免重复发送
            if(!junge_exists)
            {
                mqtt_publish("a");   // "a" 表示目标用户出现
                junge_exists = true;
            }
        }
        else
        {
            // 当前帧未检测到目标用户
            // 检查是否已离开超过10秒（避免短暂遮挡导致误判离线）
            if(time(NULL) - junge_time > 10)
            {
                // 只在状态变化时发送离线消息
                mqtt_publish("b");   // "b" 表示目标用户离开
                junge_exists = false;
            }
        }

        // 在 "junge" 窗口中显示带标注的画面
        imshow("junge", img);

        // waitKey(1) 等待1毫秒检测按键，同时让 OpenCV 有机会刷新窗口
        keyCode = waitKey(1);

        // 按空格键：保存当前帧图像到文件
        if (keyCode == ' ')
            imwrite("../photos/1.jpg", img);

        // 按 R 键：进入人脸注册流程
        if (keyCode == 'r' || keyCode == 'R')
            registerFace();

        // 按 Q 键：退出主循环
        if (keyCode == 'q')
            break;
    }

    // ==================== 资源释放 ====================
    camera.release();              // 释放摄像头
    destroyAllWindows();           // 关闭所有 OpenCV 窗口
    mysql_close_connection();      // 关闭 MySQL 连接

    return 0;
}
