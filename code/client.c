
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024           //传送文件的一部分的最大字节数
#define FILE_NAME_MAX_SIZE 512     //文件名最大长度 
static char username_and_password[80];          //用户名与密码

static int fileReading = 0;        //是否进行文件读取的标志位

void Sendfile(char* Filename, void* Socket)
{
	int *SocketCopy = Socket; 
	char buffer[1025];
	FILE *fp;
	fp = fopen(Filename, "r");

	if(NULL == fp)
	{
		printf("目标文件:%s 不存在\n", Filename);
		return;
	}
	else
	{
		//读取文件，循环发送
		int length =0;
		while((length = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
		{
			write(*SocketCopy, &length, sizeof(int));
			if(write(*SocketCopy, buffer, length) < 0)
			{
				printf("上传文件:%s 失败.\n", Filename);
				break;
			}
			//缓存区置零
			bzero(buffer, BUFFER_SIZE);
		}
	}
	fclose(fp);
	printf("文件:%s 上传成功!\n", Filename);
	
}

void ReceiveFile(char* dest, int Socket);
//终端接收收入，发送给服务器
void* Send(void* Socket)
{
	char sender[80];
	char Filename[FILE_NAME_MAX_SIZE];
	int *SocketCopy = Socket;
	while(1)
	{
		//如果正在读文件，则跳过
		if(fileReading) continue;
		//接受输入
		fgets(sender, sizeof(sender), stdin);
		
		//如果要接收文件
		if(sender[1] == 'f' && sender[2] == 's')
		{
			fileReading = 1;
			char destination[50];
			//读取目标文件地址并保存到destination
			for(int i = 4; i <  strlen(sender) - 1; ++i)
				destination[i - 4] = sender[i];
			destination[strlen(sender) - 5] = '\0';
			//接收文件，SocketCopy端的将文本内容存入destination文件中
			ReceiveFile(destination, *SocketCopy);
			continue;
		}	
		//如果这是一条普通消息，就发送它到服务端
		int messageSize = strlen(sender) + 1;
		write(*SocketCopy, &messageSize, sizeof(int));
 		write(*SocketCopy, sender, messageSize);      
		//检查这是否是一条退出的消息
		if(strcmp(sender, ":q!\n") == 0)
			exit(1);
		
		//检查这是否是发送文件
		else if(sender[1] == 'f' && sender[2] == 'w')
		{	
			printf("请再次输入文件名(包括地址):\n");
			scanf("%s", Filename);
			
			//打开文件同时将文件大小发到服务端
			FILE *fp=fopen(Filename, "r");
			fseek(fp, 0L, SEEK_END);
			int Filesize=ftell(fp); 
			int intSize = sizeof(int);
			write(*SocketCopy, &intSize, sizeof(int));
			write(*SocketCopy, &Filesize, sizeof(int));

			//发送文件，同时将flag置0
	        Sendfile( Filename, SocketCopy );
			fileReading = 0;
		}		
	}
}


//从服务器端接收文件
void ReceiveFile(char* dest, int Socket)
{
	//be prepared to receive file
	char buffer[BUFFER_SIZE];
	printf("你想把文件存储在 %s\n", dest);
	FILE *fp = fopen(dest, "w");
	if(NULL == fp)
	{
		printf("文件:\t%s 打不开\n", dest);
		exit(1);
	}
	bzero(buffer, BUFFER_SIZE);

	//读取文件大小，把字符串转Int类型
	char filesize[20];
	char filesizeStringSize[2];
	int L1 = read(Socket, filesizeStringSize, 2);
	int L2 = read(Socket, filesize, atoi(filesizeStringSize) + 1);
	int filesizeInt = atoi(filesize);

	//prepare to receive the file
	int length = 0;
	int i = 0;
	fileReading = 1;

	//receiving the file in parts according to file size
	while(i < filesizeInt/1024 + 1)
	{	
		length = read(Socket, buffer, BUFFER_SIZE); 
		if(fwrite(buffer, sizeof(char), length - 2, fp) < length - 2)
		{
			printf("文件:\t%s 写入失败\n", dest);
			return;
		}
		printf("成功接收文件第 %d 部分!\n", ++i);
		bzero(buffer, BUFFER_SIZE);
	}

	//print success message and free neccessary things
	printf("从服务器成功收到文件并存入 %s!\n", dest);
	fileReading = 0;
	fclose(fp);
}

//输出从server端收到的信息
void* Receive(void* Socked)
{
	int *SockedCopy = Socked;
	char Receiver[80];

	while(1){
		if(fileReading == 1)
			continue;
		//不断地读取消息
		int reveiverEnd = 0;
		//读取的内容保存在Reciever
		reveiverEnd  = read (*SockedCopy, Receiver, 1000);
		if(Receiver[0] == '!' && Receiver[1] == '!')
			fileReading = 1;
		Receiver[reveiverEnd] = '\0';	
		//输出接收到的内容
		fputs(Receiver, stdout);
		Receiver[0] = '\0';
	}
}
int main ()
{
	int sockfd, n, MessageSize,PasswordSize;
	pthread_t threadSend;
	pthread_t threadReceive;
	struct sockaddr_in serv, cli;
	char rec[1000];
	char send[80];
	char serAddress[80];
	
	//输入客户端IP
	//如果直接回车，代表默认
	printf("输入客户端IP(type <Enter> to use default): ");
	fgets(serAddress, sizeof(serAddress), stdin);
	if(serAddress[0] == '\n')
	{
		//默认address
		strcpy(serAddress, "127.0.0.1\n");
	}
	serAddress[strlen(serAddress) - 1] = '\0';

	//input UserName
	Start: printf("输入用户名和密码，用空格隔开: " );
	//用户名
	//fgets函数读取到回车才会停止
	fgets(username_and_password, sizeof(username_and_password), stdin);
	username_and_password[strlen(username_and_password) ] = '\0'; //cut the '\n' ending
 	int input_len= strlen(username_and_password);
	//建立套接字
 	sockfd = socket (PF_INET, SOCK_STREAM, 0);
 	bzero (&serv, sizeof (serv));
	serv.sin_family = PF_INET;
	//8888端口
	serv.sin_port = htons (8888);
 	serv.sin_addr.s_addr = inet_addr (serAddress /*server address*/);

	//连接服务端
	if(connect (sockfd, (struct sockaddr *) &serv, sizeof (struct sockaddr)) == -1)
	{
		printf("connect %s failed\n", serAddress);
		exit(1);
	}

	//把用户名和密码发到server
	write(sockfd, &input_len, sizeof(int));
 	write(sockfd, username_and_password, input_len);


	//收到成功连接的信息
	n = read (sockfd, rec, 1000);
 	rec[n] = '\0';	
	//判断是否收到Reject
	if(rec[0] == 'R')
	{
		//重新接收输入
		rec[0] = '\0';
		printf("用户名已存在，请换一个.\n");
	 	goto Start; 
	}
	else
	{
		//doesn't been rejected, open threads that are needed
		
		// //用户名没有重复，就设置密码
		fputs(rec, stdout); 	
		//开始发送
		pthread_create(&threadSend, 0, Send, &sockfd);
		//然后接收
		pthread_create(&threadReceive, 0, Receive, &sockfd);
	}
	//让程序维持较长时间
	
	sleep(9999*99);
	
	//close and free everything then quit
	pthread_exit(&threadSend);
	pthread_exit(&threadReceive);
	close(sockfd);
	
	return 0;
}
