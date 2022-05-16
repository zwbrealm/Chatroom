#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/in.h>
#include <sqlite3.h>
#include <unistd.h>
#include <unistd.h>

#define DB_NAME "info.db"            //定义数据库文件名称
#define BUFFER_SIZE 1024             //Max size of one single part of a file
#define FILE_NAME_MAX_SIZE 512       //Max size of a file name(including address)

static pthread_t thread;             //mark main thread, process
static pthread_t threadClient[100];  //mark clients' thread
static int ServerSock;               //socket of server
static int clientNumber;             //use to count thr number of clients
static int fileDistributing;         //use to mark whrther filr is distributing
static int password_len;
int online_users=0;
sqlite3* db=NULL;

char *zErrMsg =NULL;
/*************************创建数据库*********************************/
sqlite3* create_table(){
	//打开sql文件
	int len;
	len = sqlite3_open(DB_NAME,&db);
	if(len)//如果出错
	{
		/*  fprintf函数格式化输出错误信息到指定的stderr文件流中  */
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));//sqlite3_errmsg(db)用以获得数据库打开错误码的英文描述。
		sqlite3_close(db);
		exit(1);
	}
	else 
		printf("你创建了一个叫做 'User' 的sqlite3 数据库!\n");

	char *sql = " CREATE TABLE UserTable(\
					username TEXT PRIMARY KEY,\
					password TEXT,\
					state INTEGER,\
					socketaddr INTERGER);" ;
	//第五个参数储存error msg	
	//建表		
	sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
	return db;
}
//将用户名，密码，用户是否在线，通信标识符插入数据库
void table_insert(char *username,char *password,int state,int socket,sqlite3 * db){
	char  *sqlstr;
	sqlstr = sqlite3_mprintf("insert into UserTable values ('%s','%s',%d,%d)", username, password,state,socket);
	if (sqlite3_exec(db, sqlstr, NULL, NULL, &zErrMsg) != 0)
	{
		printf("error : %s\n", sqlite3_errmsg(db));
	}
}
void table_update(char *username,sqlite3 * db){
	char  *sqlstr;
	sqlstr = sqlite3_mprintf("UPDATE UserTable SET state = 0 WHERE username = '%s'",username);
	if (sqlite3_exec(db, sqlstr, NULL, NULL, &zErrMsg) != 0)
	{
		printf("error : %s\n", sqlite3_errmsg(db));
	}
}


//用户信息的结构体
typedef struct
{
	pthread_t threadNumber;
	int sock;
	char UserName[50]; 
	char password[50];
	struct sockaddr address;
	int addr_len;
} connection_t;
//默认人数上限为100人
static connection_t conn[100];

//this function distributes the messsage/status of single client to the other
//这个函数可以将一个用户的消息、状态分发到其他用户那里
//第一个参数Info就是要分发的信息
int SendInfo(void* Info, int exception)
{
	char *info = Info;
	for(int i = 0; i < 100; ++i){
		//send to the client that exists and doesn't quit room
		if(conn[i].addr_len != -1 && conn[i].addr_len != 0 && conn[i].sock != exception){
			if(send (conn[i].sock, info , strlen(info) + 1, 0) == -1)
				printf("error occured, send to %s fail", conn[i].UserName);
			if(fileDistributing == 0)
				printf("--- [%s] to [%s] 发送成功!\n", info, conn[i].UserName);
		}	
	}
	return 0;	
}


//该函数实现一个用户向其他用户发送文件
int SendFile(connection_t* clientStruct)
{
	int size;
	int filesize;
	char buffer[1024];
	int len;
	//指示是否发送文件变量
	fileDistributing = 1;

	//读取文件大小
	read(clientStruct->sock, &size, sizeof(int));
    read(clientStruct->sock, &filesize, sizeof(int));

	//向所有用户分发文件大小的信息
	char filesizeString[20];
	char filesizeStringsize[2];
	sprintf(filesizeString, "%d", filesize);
	//文件大小的长度
	sprintf(filesizeStringsize, "%ld", strlen(filesizeString));
	SendInfo(filesizeStringsize, clientStruct->sock);
	SendInfo(filesizeString, clientStruct->sock);
	
	//分部分发送文件，直到末尾
	for(int i=0; i < filesize/1024+1; ++i)
	{
		read(clientStruct->sock, &len, sizeof(int));
		read(clientStruct->sock, buffer, len);
		printf("收到了 %ld 字节\n", strlen(buffer));
        //将收到的发送到缓冲区
		SendInfo(buffer, clientStruct->sock);
		printf("发送第 %d 部分成功!\n", i + 1);
        //刷新缓冲区
		bzero(buffer, BUFFER_SIZE);
	}
	
	printf("成功发送所有部分!\n");	
	fileDistributing = 0;
	return 0;
}

//用来处理与一个用户的通信
void* Receive(void* clientStruct)
{

	connection_t* clientInfo = (connection_t *)clientStruct;
	while(1)
	{
		//如果服务器在发送文件，就不再读取缓冲区数据
		if(fileDistributing) continue;
		//从客户端读取数据到缓冲区
		char *Buffer;
		int messageLen = 0;
		read(clientInfo->sock, &messageLen, sizeof(int));  
		//消息长度有效
		if(messageLen > 0)
		{
			Buffer = (char *)malloc((messageLen+1)*sizeof(char));
			//从缓冲区读取消息
			read(clientInfo->sock, Buffer, messageLen); 
			//输入的第一个字符必须是冒号
			if(Buffer[0] != ':') continue;
			Buffer[messageLen] = '\0';
			//当客户端收到退出信息
			if( Buffer[1] == 'q' && Buffer[2] == '!' )
			{
				//拼接退出的消息并把用户从聊天室移除
				char quit[] = " quit the chat room\n";
				char quitMessage[50];		
				char quitNumber[50];
				quitMessage[0] = '\0';
				//更新数据库，有用户退出
				table_update(clientInfo->UserName,db);
				sprintf(quitNumber, "There are %d people in the Chatroom now!!\n", --clientNumber);
				strcat(quitMessage, clientInfo->UserName);
				strcat(quitMessage, quit);	
				strcat(quitMessage, quitNumber);
				//把这个用户退出的消息发送给所有用户端
				SendInfo(quitMessage, -1);
				//设置使得用户信息结构体无效
				clientInfo->addr_len = -1;
				//把用户线程关掉
				pthread_exit(&clientInfo->threadNumber);
			}
			//从客户端收到上传文件的信息
			else if ( Buffer[1] == 'f' && Buffer[2]  =='w')
			{	
				char sign[] = "!!";
                char file[] = " send you a file：";
				char fileMessage[50];
				char Filename[FILE_NAME_MAX_SIZE];
				fileMessage[0] = '\0';
				strcat(fileMessage, clientInfo->UserName);
				strcat(fileMessage, file);
				//从缓存区读取文件名
				for(int t = 4; t < messageLen-1; t++)
					Filename[t-4] = Buffer[t];
				Filename[messageLen-5]='\0';
				//拼接字符串，发送 文件传送 相关的日志信息
				strcat(fileMessage, Filename);
				strcat(sign, fileMessage);
				SendInfo(sign, -1);
				//函数参数为要上传文件的那个用户
				SendFile(clientInfo);
			}
			else{
				//聊天信息发送
				//拼接信息
				char begin[] = " says";
				char messageDistribute[200];
				messageDistribute[0] = '\0';
				strcat(messageDistribute, clientInfo->UserName);
				strcat(messageDistribute, begin);
				strcat(messageDistribute, Buffer);
				//向所有用户发送聊天信息
				SendInfo(messageDistribute, -1);
			}
			free(Buffer);
		}
		else
			continue;
	}
}

//判断用户名是否存在
int usernameExisted(char userName[], int clientnumber)
{
	for(int i = 0; i < 100 && i != clientnumber; ++i)
	{
		if(conn[i].addr_len != 0 && conn[i].addr_len != -1)
			if(strcmp(conn[i].UserName, userName) == 0)
				return 1;

	}	
	return 0;
}

//当有一个客户机要连接时，就connect
void * process(void * ptr)
{
	char* buffer;
	
	int len;
	clientNumber = 0;   //初始化客户端人数   
	long addr = 0;
	while(1){
		//等待连接
		if(clientNumber < 100)
		{
			conn[clientNumber].sock = accept(ServerSock, &conn[clientNumber].address, &conn[clientNumber].addr_len);
		}
		else
			break;
		//读取消息长度
		read(conn[clientNumber].sock, &len, sizeof(int));
		if (len > 0)
		{
			//neccessary information of a client
			addr = (long)((struct sockaddr_in *)&conn[clientNumber].address)->sin_addr.s_addr;
			buffer = (char *)malloc((len+1)*sizeof(char));
			buffer[len] = '\0';
			read(conn[clientNumber].sock, buffer, len);
			//提取用户名和密码
			int i=0;
			for(i=0;i<len;i++){
				if(buffer[i] ==' '){			
					break;
				}
			}
			strcpy(conn[clientNumber].password, buffer+i+1);
			buffer[i] ='\0';
			strcpy(conn[clientNumber].UserName, buffer);
			
			//判断用户名是否存在
			if(usernameExisted(conn[clientNumber].UserName, clientNumber))
				//reject connection and send reject information to the clients
			{
				send(conn[clientNumber].sock,  "Reject", 6, 0);
				--clientNumber;
			}
			else
			{
				//数据库插入值
				//完成用户注册，将用户加入数据库
				table_insert(conn[clientNumber].UserName, conn[clientNumber].password, 1,conn[clientNumber].sock ,db);
				
				//向连接的客户端发送成功消息
				send (conn[clientNumber].sock, "You have entered the chatroom, Start CHATTING Now!\n", 51, 0);
				
				//向所有在线用户广播发送日志信息
				char mesStart[50] = "User ";
				char mesMiddle[30] = " has entered the Chatroom!\n";
				char mesNumber[50];
				sprintf(mesNumber, "There are %d people in the Chatroom now!!\n", clientNumber+1);
				strcat(mesStart, conn[clientNumber].UserName);
				strcat(mesStart, mesMiddle);
				strcat(mesStart, mesNumber);
				printf("%s", mesStart);
				SendInfo(mesStart, -1);
				//创建一个线程，执行Recieve函数，传递的实参是用户信息结构体
				pthread_create(&threadClient[clientNumber], 0, Receive, &conn[clientNumber]);
				conn[clientNumber].threadNumber = threadClient[clientNumber];
			}
			//释放缓冲区
			free(buffer);
		}
		clientNumber += 1;//已经用掉的连接数量+1
	}
	//服务端线程停止
	pthread_exit(0);
}


int main(int argc, char ** argv){
	struct sockaddr_in address;
	int port = 8888;
	connection_t* connection;

	//创建TCP流式套接字
	ServerSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	//将套接字与端口绑定
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(8888);
	//绑定失败，发送失败信息
	if (bind(ServerSock, (struct sockaddr *)&address, sizeof(struct sockaddr_in)) < 0)
	{
		fprintf(stderr, "error: cannot bind socket to port %d\n", port);
		return -4;
	}

	//listen for connections
	//the second parameter marks max number of clients
	listen(ServerSock, 100);
	printf("the server is ready and listening\n");

	//创建数据库
	db = create_table();
	//为每一个客户端连接创建一个线程
	//第三个参数是指的创建线程要执行的函数，第四个参数connection中含有要传递给process的实参
	pthread_create(&thread, 0, process, (void *)connection);

	//让程序长期保持不退出
	
		sleep(9999*99);

	//关闭套接字和线程
	pthread_detach(thread);
	close(ServerSock);
	return 0;
}
