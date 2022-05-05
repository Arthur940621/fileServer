## 用套接字加线程池实现的一个并发文件传输

服务器端:
- 将文件保存在 `file` 文件夹下
- `g++ server.cpp -o server -lpthread` 编译
- `./server <端口>` 运行

客户端:
- `g++ client.cpp -o client` 编译
- `./client <IP> <端口>` 运行
- `$pwd` 得到当前所在目录
- `$ls` 得到当前所在目录所有文件与文件夹
- `..` 返回上层目录
- `xxx` 下载 `xxx` 文件