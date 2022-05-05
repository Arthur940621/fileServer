#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fstream>

#define BUF_SIZE 1024

using namespace std;

void error_handling(const string& buf);

int main(int argc, char* argv[]) {
    char buf[BUF_SIZE];

    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <IP> <port>" << endl;
        exit(1);
    }

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        error_handling("socket() error!");
    }

    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));
    if (connect(sock, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) == -1) {
        error_handling("connect() error!");
    } else {
        cout << "Connected......" << endl;
    }

    while (true) {
        string file_name;
        cout << "Please enter the file name and press Q to stop: ";
        cin >> file_name;
        if (file_name == "Q" || file_name == "q") {
            break;
        }
        send(sock, file_name.c_str(), file_name.size(), 0);
        recv(sock, buf, sizeof(int), 0);
        int flag = reinterpret_cast<int*>(buf)[0];
        if (flag == -1) { // 接收消息
            recv(sock, buf, sizeof(int), 0);
            int length = reinterpret_cast<int*>(buf)[0];
            int recv_len = 0;
            string dir_info;
            while (recv_len < length) {
                int recv_cnt = recv(sock, buf, BUF_SIZE, 0);
                recv_len += recv_cnt;
                dir_info += buf;
            }
            dir_info = dir_info.substr(0, length);
            cout << dir_info;
        } else { // 接收文件
            ofstream out(file_name, ofstream::out | ios::app | ios::binary);
            recv(sock, buf, sizeof(int), 0);
            int length = reinterpret_cast<int*>(buf)[0];
            int recv_len = 0;
            while (recv_len < length) {
                int recv_cnt = recv(sock, buf, BUF_SIZE, 0);
                recv_len += recv_cnt;
                out.write(buf, recv_cnt);
            }
            out.close();
        }

    }
    shutdown(sock, SHUT_WR);
    recv(sock, buf, BUF_SIZE, 0);
    cout << buf;
    close(sock);
}

void error_handling(const string& buf) {
    cerr << buf << endl;
    exit(1);
}