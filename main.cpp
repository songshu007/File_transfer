#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <winsock2.h>
#include <commdlg.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

#define BUFFER_SIZE 1024

// 返回文件大小
uint64_t fsize(FILE* stream)
{
	fseek(stream, 0, SEEK_END);
	uint64_t size = _ftelli64(stream);
	rewind(stream);
	return size;
}

// 获取局域网ip
int GetLocalIP(std::string& local_ip)
{
	char szHostName[MAX_PATH] = { 0 };
	int nRetCode;
	nRetCode = gethostname(szHostName, sizeof(szHostName));
	PHOSTENT hostinfo;
	if (nRetCode != 0)
		return WSAGetLastError();
	hostinfo = gethostbyname(szHostName);
	local_ip = inet_ntoa(*(struct in_addr*)*hostinfo->h_addr_list);
	return 1;
}

// 读指定大小字节
int readn(SOCKET s, char* buf, int len)
{
	int single = 0;
	int accruad = 0;

	while (accruad < len)
	{
		single = recv(s, buf, len, 0);
		if (single <= 0) 
		{
			printf("client disconnect or failed\n");
			return -1;
		}
		accruad += single;
	}
	return accruad;
}

// 获取用户选择的文件
std::string Getfile()
{
	char szFile[MAX_PATH];
	OPENFILENAMEA ofn;
	memset(&ofn, 0, sizeof(OPENFILENAME));
	memset(szFile, 0, sizeof(szFile));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = "All Files\0*.*\0\0";
	ofn.lpstrFile = szFile;
	ofn.lpstrInitialDir = "";
	ofn.lpstrDefExt = "";
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	const BOOL success = GetOpenFileNameA(&ofn);

	if (success) return szFile;
	else return "";
}

// 接收端
int server(uint16_t port)
{
	auto listen_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建监听套接字
	if (INVALID_SOCKET == listen_fd)
	{
		printf("socket failed: %d\n", WSAGetLastError());
		return -1;
	}
	const char opt = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) // 设置端口复用
	{
		printf("setsockopt failed: %d\n", WSAGetLastError());
		return -1;
	}
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port); // 监听端口
	addr.sin_addr.s_addr = INADDR_ANY; // 接收所有ip地址
	if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) // 绑定
	{
		printf("bind failed: %d\n", WSAGetLastError());
		return -1;
	}
	if (listen(listen_fd, 5) != 0)  // 监听
	{
		printf("listen failed: %d\n", WSAGetLastError());
		return -1;
	}
	printf("Listen from any ip, port:%d\n", port);
	sockaddr_in client_addr;
	SOCKET c_fd = accept(listen_fd, (sockaddr*)&client_addr, NULL); // 接受连接，阻塞
	if (c_fd == INVALID_SOCKET)
	{
		printf("accept failed: %d\n", WSAGetLastError());
		return -1;
	}
	printf("New connect: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

	char buf[BUFFER_SIZE] = { 0 };
	uint64_t filesize = 0;
	int readsize = 0;
	uint64_t writesize = 0;
	readn(c_fd, (char*)&filesize, sizeof(filesize)); // 读取文件大小
	readn(c_fd, buf, BUFFER_SIZE); // 读取文件名称
	printf("New File: %s File size: %llu\n", buf, filesize);

	FILE* outfile = fopen(buf, "wb"); // 打开文件
	if (outfile == NULL)
	{
		printf("err: fopen outfile failed\n");
		return -1;
	}

	using namespace std::chrono;
	static float time = 0.0f;
	static int secbit = 0;
	system_clock::time_point cp1, cp2;
	cp1 = system_clock::now();
	cp2 = system_clock::now();

	while (filesize > writesize)
	{
		memset(buf, 0, sizeof(buf));
		readsize = recv(c_fd, buf, BUFFER_SIZE, 0);
		if (readsize <= 0)
		{
			printf("err: client disconnect or failed\n");
			return -1;
		}
		writesize += readsize; secbit += readsize;
		fwrite(buf, sizeof(char), readsize, outfile);

		// 进度
		cp1 = system_clock::now();
		duration<float> t = cp1 - cp2;
		float dt = t.count();
		cp2 = cp1; time += dt;

		if (time >= 1.0f)
		{
			float speed = 0.0f;
			if (secbit < 1024)
			{
				speed = secbit / 1024.0f;
				printf("\rWrite %lld / %lld [%%%.2f] %.2f kb/s                 ", writesize, filesize, 100.f * (static_cast<float>(writesize) / static_cast<float>(filesize)), speed);
			}
			if (secbit >= 1024)
			{
				speed = secbit / 1024.0f / 1024.0f;
				printf("\rWrite %lld / %lld [%%%.2f] %.2f mb/s                 ", writesize, filesize, 100.f * (static_cast<float>(writesize) / static_cast<float>(filesize)), speed);
			}
			time = 0.0f;
			secbit = 0;
		}
	}

	printf("\rWrite %lld / %lld [%%100.00]                   ", writesize, filesize);

	printf("\nRecv Success\n");
	fclose(outfile);
	closesocket(listen_fd);
	closesocket(c_fd);
	return 0;
}

// 发送端
int client(const char* ip, uint16_t port, std::string file)
{
	auto client_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建客户端套接字
	if (INVALID_SOCKET == client_fd)
	{
		printf("socket failed: %d\n", WSAGetLastError());
		return -1;
	}
	const char opt = 1;
	if (setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) // 设置端口复用
	{
		printf("setsockopt failed: %d\n", WSAGetLastError());
		return -1;
	}
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port); // 端口
	addr.sin_addr.s_addr = inet_addr(ip); // ip
	printf("Connect...\n");
	if (connect(client_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		printf("connect failed: %d\n", WSAGetLastError());
		return -1;
	}
	printf("Connect Succeed!\n");

	auto lastpos = file.find_last_of('\\');
	std::string filename = file.substr(lastpos + 1, file.size() - lastpos); // 获取文件名

	FILE* infile = fopen(file.c_str(), "rb"); // 打开文件
	if (infile == NULL)
	{
		printf("err: fopen infile failed\n");
		return -1;
	}

	printf("Sending...\n");

	char buf[BUFFER_SIZE] = { 0 };
	uint64_t filesize = fsize(infile);
	send(client_fd, (char*)&filesize, sizeof(filesize), 0); //  发送文件大小
	memcpy(buf, filename.data(), filename.size());
	send(client_fd, buf, BUFFER_SIZE, 0); // 发送文件名称

	using namespace std::chrono;
	static float time = 0.0f;
	static int secbit = 0;
	system_clock::time_point cp1, cp2;
	cp1 = system_clock::now();
	cp2 = system_clock::now();
	uint64_t sendsize = 0;

	while (!feof(infile))
	{
		memset(buf, 0, BUFFER_SIZE);
		size_t len = fread(buf, sizeof(char), BUFFER_SIZE, infile);
		send(client_fd, buf, len, 0);
		sendsize += len; secbit += len;

		// 进度
		cp1 = system_clock::now();
		duration<float> t = cp1 - cp2;
		float dt = t.count();
		cp2 = cp1; time += dt;

		if (time >= 1.0f)
		{
			float speed = 0.0f;
			if (secbit < 1024)
			{
				speed = secbit / 1024.0f;
				printf("\rSend %lld / %lld [%%%.2f] %.2f kb/s                 ", sendsize, filesize, 100.f * (static_cast<float>(sendsize) / static_cast<float>(filesize)), speed);
			}
			if (secbit >= 1024)
			{
				speed = secbit / 1024.0f / 1024.0f;
				printf("\rSend %lld / %lld [%%%.2f] %.2f mb/s                 ", sendsize, filesize, 100.f * (static_cast<float>(sendsize) / static_cast<float>(filesize)), speed);
			}
			time = 0.0f;
			secbit = 0;
		}
	}

	printf("\rSend %lld / %lld [%%100.00]                   ", sendsize, filesize);

	fclose(infile);
	closesocket(client_fd);
	return 0;
}

int main()
{
	WSADATA wsaData;
	int iResult = 0;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed: %d\n", iResult);
		return -1;
	}

	printf("1.接收\n2.发送\n");
	int choose = 0;
	std::cin >> choose;
	if (choose == 1)
	{
		std::string localIp;
		GetLocalIP(localIp);
		std::cout << "本机ip: " << localIp << '\n';
		server(5000);
	}
	if (choose == 2)
	{
		std::string file = Getfile();
		std::cout << file << '\n';
		std::cout << "输入对端ip: \n";
		std::string serverIp;
		std::cin >> serverIp;
		client(serverIp.c_str(), 5000, file);
	}

	WSACleanup();

	std::cin.get();
	std::cin.get();

	return 0;
}