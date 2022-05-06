#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "ThreadPool.hpp"
#include <fstream>
#include <vector>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <stack>

using namespace std;

#define BUF_FILENAME_SIZE 32
char file_name_buf[BUF_FILENAME_SIZE];

void error_handling(const string& buf); // 处理错误
int pre_processed_sock(char* argv[]); // 预处理套接字
void sock_task(const string& dir_root, int sock); // 每个客户端的通信作为子线程的任务
string get_all_file_names(const string& dir_path); // 获取某个目录下的所有文件名和文件夹名
vector<string> fuzzy_matching(const string& all_file_names, const string& file_name); // 模糊匹配
void send_message(int flag, const string& message, int sock); // 传输指令对应的消息
void send_file(stack<string>& dir_skt, int sock); // 传输文件

int main(int argc, char* argv[]) {
    if (argc != 2) { // 设定端口号
        cerr << "Usage: " << argv[0] << " <port>" << endl;
        exit(1);
    }

    int serv_socket = pre_processed_sock(argv); // 预处理用于监听的套接字
    ThreadPool pool(5); // 初始化线程池
    string dir_root = "./file"; // 初始化根目录

    while (true) { // 服务器一直运行
        sockaddr_in clnt_addr;
        socklen_t clnt_addr_size = sizeof(clnt_addr);
        int str_len = 0;
        int clnt_socket = accept(serv_socket, reinterpret_cast<sockaddr*>(&clnt_addr), &clnt_addr_size);
        if (clnt_socket == -1) {
            error_handling("accept() error!");
        } else {
            cout << inet_ntoa(clnt_addr.sin_addr) << " connected client......" << endl;
            pool.commit(sock_task, dir_root, clnt_socket); // 新连接的通信任务交给子线程
        }
    }
    close(serv_socket);
    return 0;
}

string get_all_file_names(const string& dir_path) {
    DIR* dir;
    struct dirent* ptr;
    char base[BUF_FILENAME_SIZE];
    string files;

    dir = opendir(dir_path.c_str());
    while ((ptr = readdir(dir)) != NULL) {
        if(strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) {
            continue;
        } else if (ptr->d_type == 8) { // file
            files += (ptr->d_name);
            files += '\n';
        } else if(ptr->d_type == 4) { // dir
            files += (ptr->d_name);
            files += '\n';
        } else {
            continue;
        }
    }
    closedir(dir);
    return files;
}

void sock_task(const string& dir_root, int sock) {
    stack<string> dir_skt;
    dir_skt.push(dir_root);
    send_file(dir_skt, sock);
    char bye[] = "bye!\n";
    write(sock, bye, sizeof(bye));
    close(sock);
}

vector<string> fuzzy_matching(const string& all_file_names, const string& file_name) {
    istringstream iss(all_file_names);
    string full_file_name;
    vector<string> all_match;
    while (getline(iss, full_file_name)) {
        if (full_file_name.find(file_name) != string::npos) {
            all_match.push_back(full_file_name);
        }
    }
    return all_match;
}

int pre_processed_sock(char* argv[]) {
    int serv_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_socket == -1) {
        error_handling("socket() error!");
    }
    int option = 1;
    int opt_len = sizeof(option);
    setsockopt(serv_socket, SOL_SOCKET, SO_REUSEADDR, &option, opt_len);

    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
    if (bind(serv_socket, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) == -1) {
        error_handling("bind() error!");
    }
    if (listen(serv_socket, 5) == -1) {
        error_handling("listen() error!");
    }
    return serv_socket;
}

void send_message(int flag, const string& message, int sock) {
    int length = message.size();
    char* buffer = new char[2 * sizeof(int) + length];
    memcpy(buffer, &flag, sizeof(int));
    memcpy(buffer + sizeof(int), &length, sizeof(int));
    memcpy(buffer + 2 * sizeof(int), message.c_str(), length);
    write(sock, buffer, 2 * sizeof(int) + length);
    delete [] buffer;
}

void send_file(stack<string>& dir_skt, int sock) {
    int str_len = read(sock, file_name_buf, BUF_FILENAME_SIZE);
    if (str_len == 0) { // 断开连接则结束通信
        return;
    }
    file_name_buf[str_len] = 0;
    string instruction(file_name_buf); // 获取指令
    string dir_cur = dir_skt.top(); // 获取当前目录
    if (instruction == "$ls") { // 如果指令要求当前目录下的所有文件
        string all_file_names = get_all_file_names(dir_cur); // 获取所有文件名
        send_message(-1, all_file_names, sock); // 发送文件名
        send_file(dir_skt, sock); // 递归处理本层目录
    } else if (instruction == "..") { // 如果指令要求返回上级目录
        if (dir_skt.size() > 1) { // 如果不是根目录，则目录栈出栈
            dir_skt.pop();
        }
        string all_file_names = get_all_file_names(dir_skt.top()); // 获取上层目录的所有文件名
        send_message(-1, all_file_names, sock); // 发送文件名
        send_file(dir_skt, sock); // 递归处理上级目录
    } else if (instruction == "$pwd") {
        send_message(-1, dir_cur + '\n', sock); // 发送本层目录地址
        send_file(dir_skt, sock); // 递归处理上级目录
    } else { // 如果指令指定的是某一文件
        struct stat s;
        string file_name = dir_cur + '/' + instruction; // 与上层目录连接
        if (stat(file_name.c_str(), &s) == 0) {
            if (s.st_mode & S_IFREG) { // 如果指定的是文件
                ifstream is(file_name, ifstream::in | ios::binary);
                is.seekg(0, is.end); // 计算文件长度
                int length = is.tellg();
                is.seekg(0, is.beg);
                char* buffer = new char[2 * sizeof(int) + length]; // 创建内存缓存区
                int flag = 0;
                memcpy(buffer, &flag, sizeof(int));
                memcpy(buffer + sizeof(int), &length, sizeof(int));
                is.read(buffer + 2 * sizeof(int), length); // 读取文件
                write(sock, buffer, 2 * sizeof(int) + length);
                delete [] buffer;
                is.close();
                send_file(dir_skt, sock); // 递归处理本层目录
            } else if (s.st_mode & S_IFDIR) { // 如果指定的是目录
                dir_skt.push(file_name); // 目录入栈
                string all_file_names = get_all_file_names(file_name); // 获取下一层目录所有文件名
                send_message(-1, all_file_names, sock); // 发送所有文件名
                send_file(dir_skt, sock); // 递归处理下层目录
            }
        } else {
                string all_file_names = get_all_file_names(dir_skt.top());
                auto vec = fuzzy_matching(all_file_names, instruction); // 模糊匹配
                if (vec.size() == 0) { // 文件不存在
                    string fail = "file does not exist!\n";
                    send_message(-1, fail.c_str(), sock);
                    send_file(dir_skt, sock); // 递归处理本层目录
                } else {
                    string possible_files;
                    for (auto& str : vec) {
                        possible_files += (str + '\n');
                    }
                    possible_files = "the file you're looking for may be:\n" + possible_files;
                    send_message(-1, possible_files, sock);
                    send_file(dir_skt, sock); // 递归处理本层目录
                }
            }
    }
}

void error_handling(const string& buf) {
    cerr << buf << endl;
    exit(1);
}