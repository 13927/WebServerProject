#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <pthread.h>

#define PRINTF(str) printf("[%s - %d]" #str " = %s\r\n", __func__, __LINE__, str);

void error_die(const char *str)
{
    perror(str);
    exit(1);
}

// 网络初始化
// 返回值：服务器端的套接字
// 参数：port表示端口 如果port为0则动态分配端口
int startup(unsigned short *port)
{
    //     网络通信的初始化

    // 创建套接字
    int server_sock = socket(AF_INET,     // socket类型
                             SOCK_STREAM, // 数据流
                             IPPROTO_TCP);
    if (server_sock == -1)
    { // 打印错误提示，结束程序
        error_die("套接字");
    }

    // 设置套接字属性 端口复用
    int opt = 1;
    if (setsockopt(server_sock,
                   SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1)
    {
        error_die("setsockopt");
    }

    // 配置服务器端网络地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(*port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定套接字
    if (bind(server_sock,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
    {
        error_die("bind");
    }

    // 动态分配一个端口
    int nameLen = sizeof(server_addr);
    if (*port == 0)
    {
        if (getsockname(server_sock,
                        (struct sockaddr *)&server_addr,
                        (socklen_t *)&nameLen) == -1)
        {
            error_die("getsockname");
        }

        *port = server_addr.sin_port;
    }

    // 创建监听队列
    if (listen(server_sock, 5) < 0)
    {
        error_die("listen");
    }

    return server_sock;
}

// 从指定客户端socket读取一行数据，保存到buff
// 返回实际读取到的字节数
int get_line(int sock, char *buff, int size)
{
    char c = '\0';
    int i = 0;
    while (i < size - 1 && c != '\n')
    {
        int n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if (n > 0 && c == '\n')
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buff[i++] = c;
        }
        else
        {
            c = '\n';
        }
    }

    buff[i] = '\0';
    return i;
}

// 向指定的套接字发送一个提示未实现的错误页面
void unimplement(int client)
{
}

//
void not_found(int client)
{
}

// 发送响应包的头信息
void headers(int client)
{
    char buff[1024];

    strcpy(buff, "HTTP/1.0 200 OK\r\n");
    send(client, buff, strlen(buff), 0);

    strcpy(buff, "Server: MyHttpd/0.1\r\n");
    send(client, buff, strlen(buff), 0);

    strcpy(buff, "Content-type:text/html\n");
    send(client, buff, strlen(buff), 0);

    strcpy(buff, "\r\n");
    send(client, buff, strlen(buff), 0);
}

void cat(int client, FILE *resource)
{
    char buff[4096];
    int count = 0;

    while (1)
    {
        int ret = fread(buff, sizeof(char), sizeof(buff), resource);
        if (ret <= 0)
        {
            break;
        }
        send(client, buff, ret, 0);
        count += ret;
    }
    printf("一共发送[ %d ]Bytes给浏览器", count);
}

void server_file(int client, const char *fileName)
{
    int num_chars = 1;
    char buff[1024];
    // 读取请求包的剩余数据
    while (num_chars > 0 && strcmp(buff, "\n"))
    {
        num_chars = get_line(client, buff, sizeof(buff));
        PRINTF(buff);
    }

    FILE *resource = fopen(fileName, "r");
    if (resource == NULL)
    {
        not_found(client);
    }
    else
    { // 正式发送资源给浏览器
        headers(client);
        // 发送请求的资源信息
        cat(client, resource);

        printf("资源发送完毕！\n");
    }
    fclose(resource);
}

// 处理用户请求的线程函数
void *accept_request(void *arg)
{
    int client = *(int *)arg;
    char buff[1024];

    // 读一行数据
    // “GET / HTTP/1.1”
    int num_chars = get_line(client, buff, sizeof(buff));
    PRINTF(buff); // [accept_request - 53]buff = "GET..."

    char method[155];
    int j = 0, i = 0;
    while (!isspace(buff[j]) && i < sizeof(method) - 1)
    {
        method[i++] = buff[j++];
    }
    method[i] = '\0';
    PRINTF(method);

    // 检查请求的方法，本服务器是否支持
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        // 向浏览器返回错误提示页面
        unimplement(client);
    }

    // 解析资源文件的路径
    char url[255]; // 存放请求资源的完整路径
    i = 0;
    while (isspace(buff[j]) && j < sizeof(buff))
        j++;

    while (!isspace(buff[j]) && i < sizeof(url) - 1 && j < sizeof(buff))
        url[i++] = buff[j++];
    url[i] = '\0';
    PRINTF(url);

    char path[512] = "";
    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    PRINTF(path);

    struct stat status;
    if (stat(path, &status) == -1)
    {
        // 读取请求包的剩余数据
        while (num_chars > 0 && strcmp(buff, "\n"))
            num_chars = get_line(client, buff, sizeof(buff));

        not_found(client);
    }
    else
    {
        if ((status.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");

        server_file(client, path);
    }

    close(client);
    return 0;
}

int main()
{
    unsigned short port = 8230;
    int server_sock = startup(&port);
    printf("httpd服务已经启动，正在监听%d端口...\n", port);

    struct sockaddr client_addr;
    int client_addr_len = sizeof(client_addr);

    // to do
    while (1)
    {
        // 阻塞式等待用户通过浏览器发起访问
        int client_sock = accept(server_sock,
                                 (struct sockaddr *)&client_addr,
                                 (socklen_t *)&client_addr_len);
        if (client_sock == -1)
        {
            error_die("accept");
        }

        // 创建一个新的线程 使用client_sock对用户进行访问
        pthread_t threadId;
        pthread_create(&threadId, NULL, accept_request, &client_sock);
    }

    close(server_sock);

    // pause();
    return 0;
}
