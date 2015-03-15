﻿
//#include <vld.h> 
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>

// link with Ws2_32.lib
#pragma comment(lib,"Ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <io.h>
#include "pthread.h"
#include <algorithm>
#include <lmerr.h>
#include <map>

#include "sph_shabal.c"
//#include "mshabal_sse4.c"
//#include "mshabal_avx1.c"
//#include "mshabal_avx2.c"
//#include "mshabal256_avx2.c"

#include "InstructionSet.h"
// Initialize static member data
const InstructionSet::InstructionSet_Internal InstructionSet::CPU_Rep;

#include "rapidjson/document.h"		// rapidjson's DOM-style API
#include "rapidjson/prettywriter.h"	// for stringify JSON
#include "rapidjson/filestream.h"	// wrapper of C stream for prettywriter as output
using namespace rapidjson;

//#include "GPU\OpenclDevice.cpp"
//#include <CL\opencl.h>
#include "PurgeStandbyList.cpp"

#define strtoll     _strtoi64
#define strtoull    _strtoui64

#pragma warning( disable : 4715 4996)

#define MAX_FILES		1000 
#define MaxTreads		24
unsigned threads[MaxTreads] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23}; 
#define MaxAccounts		100

HANDLE hConsole;

char *version = "v1.150223";

unsigned long long startnonce = 0;
unsigned long nonces = 0;
unsigned int scoop = 0;
 
unsigned long long deadline = 0;

unsigned long long all_send_msg = 0;
unsigned long long all_rcv_msg = 0;
unsigned long long err_send_msg = 0;
unsigned long long err_rcv_msg = 0;

unsigned long long all_send_dl = 0;
unsigned long long all_rcv_dl = 0;
unsigned long long err_send_dl = 0;
unsigned long long err_rcv_dl = 0;
 
char signature[33];
char str_signature[65];
char oldSignature[33];
 
char nodeaddr[100] = "localhost";	// адрес пула
char nodeip[50] = "";				// IP пула
unsigned nodeport = 8125;			// порт пула

char updateraddr[100] = "localhost";// адрес пула
char updaterip[50] = "";			// IP пула
unsigned updaterport = 8125;		// порт пула

unsigned proxyport = 8126;			// порт пула

char *p_minerPath;					// Path to the miner folder
unsigned miner_mode = 0;			// miner mode. 0=solo, 1=pool
unsigned cache_size = 100000;		// read cache size
unsigned paths_num = 0;				// number of parameter file paths
char* paths_dir[MaxTreads];
bool use_sorting = false;			// Использовать сортировку в отправщике
bool show_msg = false;				// Показать общение с сервером в отправщике
bool show_updates = false;			// Показать общение с сервером в апдейтере
FILE * fp_Log;						// указатель на лог-файл
FILE * fp_Stat;						// указатель на стат-файл
unsigned int can_connect = 1;		// уже установлено соединение?
unsigned int send_interval = 300;	// время ожидания между отправками
unsigned int update_interval = 1000;// время ожидания между апдейтами
bool use_fast_rcv = false;
bool use_debug = false;
bool enable_proxy = false;
bool use_generator = false;
bool send_best_only = true;
bool use_log = true;
bool skip_bad_plots = true;			// Пропускать плохие или поврежденные плоты

unsigned int my_target_deadline = 4294967295;
SYSTEMTIME cur_time;				// Текущее время
unsigned int working_threads = 0;	// Поток закончил работу
unsigned long long total_size = 0;		// Общий объем плотов

std::map <char*, unsigned long long> satellite_size; // Структура с объемами плотов сателлитов

struct t_files{
	char* Path = NULL; 
	char* Name = NULL;
	unsigned long long Size = 0;
	unsigned State = 0;
};

struct t_shares{
	unsigned long long account_id;
	char * file_name;
	unsigned long long best;
	unsigned long long nonce;
	unsigned to_send;
	bool operator<(t_shares ob2)
	{
		return (best < ob2.best);
	}
} shares[100000*MaxTreads];


struct t_best{
	unsigned long long account_id;
	unsigned long long best;
	unsigned long long nonce;
	unsigned long long DL;
	unsigned long long targetDeadline;
} bests[MaxAccounts];

struct t_session{
	SOCKET Socket;
	unsigned long long ID;
	unsigned long long deadline;
};

std::vector<t_session> sessions;

unsigned long long num_shares = 0;
unsigned sent_num_shares = 0;

unsigned long long bytesRead = 0;
unsigned long long height = 0;
unsigned long long baseTarget = 0;
unsigned long long targetDeadlineInfo = 0; // Максимальный дедлайн пула
int stopThreads = 0;
char *pass = (char*)malloc(300);		// пароль
 
pthread_mutex_t byteLock = PTHREAD_MUTEX_INITIALIZER;


void cls(void)
{
	//printf_s("\r                                                                               \r");
	printf_s("\r\t\t\t\t\t\t\t\t\t       \r");
}

void Log_init(void)
{
	if (use_log)
	{

		char * filename = (char*)malloc(255);
		if (CreateDirectory(L"Logs", NULL) == ERROR_PATH_NOT_FOUND)		printf("CreateDirectory failed (%d)\n", GetLastError());
		if (filename == NULL)
		{
			SetConsoleTextAttribute(hConsole, 12);
			printf_s("\nLOG: Memory allocation error\n");
			SetConsoleTextAttribute(hConsole, 7);
			use_log = false;
		}
		GetLocalTime(&cur_time);
		sprintf(filename, "Logs\\%02d-%02d-%02d_%02d_%02d_%02d.log", cur_time.wYear, cur_time.wMonth, cur_time.wDay, cur_time.wHour, cur_time.wMinute, cur_time.wSecond);
		fopen_s(&fp_Log, filename, "wt");
		if (fp_Log == NULL) {
			printf_s("\nLOG: file openinig error\n");
			use_log = false;
		}
		free(filename);
	}
}

void Log(char * strLog)
{
	if (use_log)
	{
		// если строка содержит интер, то добавить время  
		if (strLog[0] == '\n')
		{
			GetLocalTime(&cur_time);
			fprintf(fp_Log, "\n%02d:%02d:%02d %s", cur_time.wHour, cur_time.wMinute, cur_time.wSecond, strLog + 1);
		}
		else fprintf(fp_Log, "%s", strLog);
		fflush(fp_Log);
	}
}

void Log_server(char * strLog)
{
	if((strlen(strLog) > 0) && use_log)
	{
		char * Msg_log = (char *) malloc(sizeof(char)*strlen(strLog)*2);
		if (Msg_log == NULL)
		{
			SetConsoleTextAttribute(hConsole, 12);
			printf_s("Memory allocation error");
			SetConsoleTextAttribute(hConsole, 7);
			exit(2);
		}
		memset(Msg_log, 0, strlen(strLog)*2);
		for(int i=0, j=0; i<strlen(strLog); i++, j++)
		{
			if(strLog[i] == '\r')
			{	
				Msg_log[j] = '\\'; 
				j++;
				Msg_log[j] = 'r';}
			else 
				if(strLog[i] == '\n') 
				{ 
					Msg_log[j] = '\\'; 
					j++;
					Msg_log[j] = 'n';
				}
				else
				if (strLog[i] == '%')
				{
					Msg_log[j] = '%';
					j++;
					Msg_log[j] = '%';
				}
				else Msg_log[j] = strLog[i];
		}
		fprintf(fp_Log, "%s", Msg_log);
		free(Msg_log);
		fflush(fp_Log);
	}
}

void Log_llu(unsigned long long llu_num)
{
	if (use_log)
	{
		fprintf(fp_Log, "%llu", llu_num);
		fflush(fp_Log);
	}
}

void Log_u(unsigned u_num)
{
	if (use_log)
	{
		fprintf(fp_Log, "%u", u_num);
		fflush(fp_Log);
	}
}

int load_config(char *filename)
{
	FILE * pFile;
	size_t len;
	
	char json[2048];
	fopen_s(&pFile, filename, "rt");

	if (pFile == NULL)
	{
		SetConsoleTextAttribute(hConsole, 12);
		printf_s("\nminer.conf not found");
		SetConsoleTextAttribute(hConsole, 7);
		return -1;
	}
	
	//len = 0;
	memset(json, 0, 2048);
	_fseeki64(pFile, 0, SEEK_END);
	__int64 size = _ftelli64(pFile);
	_fseeki64(pFile, 0, SEEK_SET);
	len = fread_s(&json[0], 2048, 1, size-1, pFile);
	fclose(pFile);

	Document document;	// Default template parameter uses UTF8 and MemoryPoolAllocator.
	if (document.Parse<0>(json).HasParseError()){
		SetConsoleTextAttribute(hConsole, 12);
		printf("\nJSON format error\n");
		SetConsoleTextAttribute(hConsole, 7);
		return 0;
	}

	if (document.ParseInsitu<0>(json).HasParseError()){
		SetConsoleTextAttribute(hConsole, 12);
		printf("\nJSON format error (Insitu)\n");
		SetConsoleTextAttribute(hConsole, 7);
		return 0;
	}
	

	if(document.IsObject())
	{	// Document is a JSON value represents the root of DOM. Root can be either an object or array.

		if (document.HasMember("UseLog") && (document["UseLog"].IsBool()))
			use_log = document["UseLog"].GetBool();

		Log_init();

		if(document.HasMember("Mode") && document["Mode"].IsString())
		{
			Log("\nMode: ");
			//printf("\n-----------\Mode = %s\n", document["Mode"].GetString());
			if(strcmp(document["Mode"].GetString(), "solo")==0) miner_mode = 0;
			else 
				if(strcmp(document["Mode"].GetString(), "pool")==0) miner_mode = 1;
				else
					if (strcmp(document["Mode"].GetString(), "poolV2") == 0) miner_mode = 2;
			Log_u(miner_mode);
		}

		Log("\nServer: "); 
		if(document.HasMember("Server") &&	document["Server"].IsString())
			strcpy(nodeaddr, document["Server"].GetString());
		Log(nodeaddr);

			Log("\nPort: "); 
		if(document.HasMember("Port") && (document["Port"].IsUint()))		// In this case, IsUint()/IsInt64()/IsUInt64() also return true.
			nodeport = document["Port"].GetUint();
		Log_u(nodeport);

		if(document.HasMember("Paths") && document["Paths"].IsArray()){
			const Value& Paths = document["Paths"];	// Using a reference for consecutive access is handy and faster.
			paths_num = (unsigned)Paths.Size();
			Log("\nPaths: "); Log_u(paths_num);
			for (SizeType i = 0; i < Paths.Size(); i++){	// rapidjson uses SizeType instead of size_t.
				//memset(&paths_dir[i][0], 0, 255);
				//strcpy(&paths_dir[i][0], Paths[i].GetString());
				//if((paths_dir[i][strlen(paths_dir[i])]) != '\\') paths_dir[i][strlen(paths_dir[i])] = '\\';
				paths_dir[i] = (char*) malloc(strlen(Paths[i].GetString())+1);
				strcpy(paths_dir[i], Paths[i].GetString());
				Log("\nPath: "); Log(paths_dir[i]);
			}
		}
		
		if(document.HasMember("CacheSize") && (document["CacheSize"].IsUint()))		// In this case, IsUint()/IsInt64()/IsUInt64() also return true.
			cache_size = document["CacheSize"].GetUint();
			//printf("Port = %d\n", document["CashSize"].GetUint());		// Alternative (int)document["i"]
		Log("\nCacheSize: "); Log_u(cache_size);
		
		if(document.HasMember("UseSorting") && (document["UseSorting"].IsBool()))		
			use_sorting = document["UseSorting"].GetBool();
		Log("\nUseSorting: "); Log_u(use_sorting);

		if(document.HasMember("ShowMsg") && (document["ShowMsg"].IsBool()))		
			show_msg = document["ShowMsg"].GetBool();
		Log("\nShowMsg: "); Log_u(show_msg);

		if(document.HasMember("ShowUpdates") && (document["ShowUpdates"].IsBool()))		
			show_updates = document["ShowUpdates"].GetBool();
		Log("\nShowUpdates: "); Log_u(show_updates);

		Log("\nSendInterval: "); 
		if(document.HasMember("SendInterval") && (document["SendInterval"].IsUint()))		// In this case, IsUint()/IsInt64()/IsUInt64() also return true.
			send_interval = document["SendInterval"].GetUint();
		Log_u(send_interval);

		Log("\nUpdateInterval: "); 
		if(document.HasMember("UpdateInterval") && (document["UpdateInterval"].IsUint()))		// In this case, IsUint()/IsInt64()/IsUInt64() also return true.
			update_interval = document["UpdateInterval"].GetUint();
		Log_u(update_interval);

		//Log("\nResponseMaxTime: "); 
		//if(document.HasMember("ResponseMaxTime") && (document["ResponseMaxTime"].IsUint()))		// In this case, IsUint()/IsInt64()/IsUInt64() also return true.
		//	response_max_time = document["ResponseMaxTime"].GetUint();
		//Log_u(response_max_time);

		//if(document.HasMember("UseResponseMaxTime") && (document["UseResponseMaxTime"].IsBool()))		
		//	use_response_max_time = document["UseResponseMaxTime"].GetBool();
		//Log("\nUseResponseMaxTime: "); Log_u(use_response_max_time);

		if(document.HasMember("UseFastRcv") && (document["UseFastRcv"].IsBool()))		
			use_fast_rcv = document["UseFastRcv"].GetBool();
		Log("\nUseFastRcv: "); Log_u(use_fast_rcv);

		if(document.HasMember("Debug") && (document["Debug"].IsBool()))		
			use_debug = document["Debug"].GetBool();
		Log("\nDebug: "); Log_u(use_debug);
				
		if (document.HasMember("UpdaterAddr") && document["UpdaterAddr"].IsString())
			strcpy(updateraddr, document["UpdaterAddr"].GetString());
		Log("\nUpdater address: "); Log(updateraddr);

		if (document.HasMember("UpdaterPort") && (document["UpdaterPort"].IsUint()))		
			updaterport = document["UpdaterPort"].GetUint();
		Log("\nUpdater port: "); Log_u(updaterport);

		if (document.HasMember("EnableProxy") && (document["EnableProxy"].IsBool()))
		enable_proxy = document["EnableProxy"].GetBool();
		Log("\nEnableProxy: "); Log_u(enable_proxy);

		Log("\nProxyPort: ");
		if (document.HasMember("ProxyPort") && (document["ProxyPort"].IsUint()))		// In this case, IsUint()/IsInt64()/IsUInt64() also return true.
			proxyport = document["ProxyPort"].GetUint();
		Log_u(proxyport);

		//if (document.HasMember("Generator") && (document["Generator"].IsBool()))
		//	use_generator = document["Generator"].GetBool();
		//Log("\nGenerator: "); Log_u(use_generator);

		if (document.HasMember("SendBestOnly") && (document["SendBestOnly"].IsBool()))
			send_best_only = document["SendBestOnly"].GetBool();
		Log("\nSendBestOnly: "); Log_u(send_best_only);

		if (document.HasMember("TargetDeadline") && (document["TargetDeadline"].IsInt64()))
			my_target_deadline = document["TargetDeadline"].GetUint();
		Log("\nTargetDeadline: "); Log_llu(my_target_deadline);

		if (document.HasMember("SkipBadPlots") && (document["SkipBadPlots"].IsBool()))
			skip_bad_plots = document["SkipBadPlots"].GetBool();
		Log("\nSkipBadPlots: "); Log_u(skip_bad_plots);
		

	}
	// параметры по-умолчанию
	Log("\nConfig loaded");
	return 1;
}


LPSTR DisplayErrorText( DWORD dwLastError )
{ 
	HMODULE hModule = NULL; // default to system source 
	LPSTR MessageBuffer; 
	DWORD dwBufferLength; 
	DWORD dwFormatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM ;

	if(dwLastError >= NERR_BASE && dwLastError <= MAX_NERR) 
	{ 
		hModule = LoadLibraryEx( TEXT("netmsg.dll"), NULL, LOAD_LIBRARY_AS_DATAFILE );
		if(hModule != NULL) dwFormatFlags |= FORMAT_MESSAGE_FROM_HMODULE;
	} 
	dwBufferLength = FormatMessageA(dwFormatFlags, hModule, dwLastError, MAKELANGID(LANG_SYSTEM_DEFAULT, SUBLANG_SYS_DEFAULT), (LPSTR)&MessageBuffer, 0, NULL);
	if(hModule != NULL) FreeLibrary(hModule); 
	return MessageBuffer;
}


// Helper routines taken from http://stackoverflow.com/questions/1557400/hex-to-char-array-in-c
int xdigit( char digit ){
  int val;
       if( '0' <= digit && digit <= '9' ) val = digit -'0';
  else if( 'a' <= digit && digit <= 'f' ) val = digit -'a'+10;
  else if( 'A' <= digit && digit <= 'F' ) val = digit -'A'+10;
  else                                    val = -1;
  return val;
}
 
int xstr2strr(char *buf, unsigned bufsize, const char *in) {
  if( !in ) return -1; // missing input string
 
  unsigned inlen = (unsigned)strlen(in);
  if( inlen%2 != 0 ) inlen--; // hex string must even sized
 
  unsigned int i,j;
  for(i=0; i<inlen; i++ )
    if( xdigit(in[i])<0 ) return -3; // bad character in hex string
 
  if( !buf || bufsize<inlen/2+1 ) return -4; // no buffer or too small
 
  for(i=0,j=0; i<inlen; i+=2,j++ )
    buf[j] = xdigit(in[i])*16 + xdigit(in[i+1]);
 
  buf[inlen/2] = '\0';
  return inlen/2+1;
}
// End helper routines
 
char* GetPass(char* p_strFolderPath)
{
  FILE * pFile;
  long lSize;
  unsigned char * buffer;
  size_t len_pass;
  char * filename = (char*) malloc(255);
  if (filename == NULL)
  {
	  SetConsoleTextAttribute(hConsole, 12);
	  printf_s("Memory allocation error");
	  SetConsoleTextAttribute(hConsole, 7);
	  exit(2);
  }
  sprintf(filename,"%s%s", p_strFolderPath, "passphrases.txt");
  
//  printf_s("\npass from: %s\n",filename);
  fopen_s(&pFile, filename, "rt");
  if (pFile==NULL) 
  {
	  SetConsoleTextAttribute(hConsole, 12);
	  printf_s("passphrases.txt not found"); 
	  SetConsoleTextAttribute(hConsole, 7);
	  exit (1);
  }

  fseek (pFile , 0 , SEEK_END);
  lSize = ftell (pFile);
  rewind (pFile);

  buffer = (unsigned char*)malloc(sizeof(unsigned char)*(lSize));
  if (buffer == NULL) 
  {
	  SetConsoleTextAttribute(hConsole, 12);
	  printf_s("Memory error"); 
	  SetConsoleTextAttribute(hConsole, 7);
	  exit (2);
  }
  
  len_pass = fread(buffer, sizeof(unsigned char), lSize, pFile);
  //if (result != lSize) {fputs ("Reading error",stderr); exit (3);}
  
  //printf("\n%i\n",result);
  fclose(pFile);
  free (filename);

  char *str = (char*)malloc(lSize * 3);
  if (str == NULL)
  {
	  SetConsoleTextAttribute(hConsole, 12);
	  printf_s("Memory allocation error");
	  SetConsoleTextAttribute(hConsole, 7);
	  exit(2);
  }
  memset(str, 0, sizeof(char)*(lSize*3));
  
	for(int i=0, j=0; i<len_pass; i++, j++) 
	{
		if (buffer[i] == '\n') j--;
		else
			if (buffer[i] == ' ') str[j] = '+';
			else 
				if(isalnum(buffer[i])==0)
				{
					sprintf(str + j, "%%%x", (unsigned char)buffer[i]);
					j = j+2;
				}
				else memcpy(&str[j],&buffer[i],1);
  }
  printf("\n%s\n",str);
  free (buffer);
  return str;

}


int GetFiles(char* p_strFolderPath, t_files p_files[])
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA   FindFileData;
	int i=0;

	char* start = p_strFolderPath;
	char* end = NULL;
	std::vector<char*> path;
	do
	{
		end = strpbrk(start, "+");
		if (end == NULL) end = p_strFolderPath + strlen(p_strFolderPath);
		
			char *p = (char*)malloc(end-start+1);
			memcpy(p, start, end - start);
			p[end - start] = 0;
			if (p[strlen(p)] != '\\') strcat(p, "\\"); // Добавляем "\" если не указан в пути
			path.push_back(p);
			start = end+1;
	} while (end != p_strFolderPath + strlen(p_strFolderPath));
	

	for (unsigned int iter = 0; iter < path.size(); iter++)
	{

		char *str = (char*)malloc(255);
		if (str == NULL)
		{
			SetConsoleTextAttribute(hConsole, 12);
			printf_s("Memory allocation error");
			SetConsoleTextAttribute(hConsole, 7);
			exit(2);
		}
		memset(str, 0, sizeof(char)*(255));
		strcpy(str, path.at(iter));
		//if(str[strlen(str)] != '\\') strcat(str,"\\"); // Добавляем "\" если не указан в пути
		strcat(str, "*");
		hFile = FindFirstFileA(LPCSTR(str), &FindFileData);
		if (INVALID_HANDLE_VALUE != hFile)
		{
			do
			{
				//Skip directories
				if (FILE_ATTRIBUTE_DIRECTORY & FindFileData.dwFileAttributes) continue;

				char *test = (char*)malloc(255);
				if (test == NULL)
				{
					SetConsoleTextAttribute(hConsole, 12);
					printf_s("Memory allocation error");
					SetConsoleTextAttribute(hConsole, 7);
					exit(2);
				}
				strcpy(test, FindFileData.cFileName);
				char* ekey = strstr(test, "_");
				if (ekey != NULL){
					char* estart = strstr(ekey + 1, "_");
					if (estart != NULL){
						char* enonces = strstr(estart + 1, "_");
						if (enonces != NULL){
							char* skey = test;
							char* sstart = ekey + 1;
							char* snonces = estart + 1;
							char* sstagger = enonces + 1;
							memset(ekey, 0, 1);
							memset(estart, 0, 1);
							memset(enonces, 0, 1);
							//printf_s("\n%s-%s-%s-%s", skey, sstart, snonces ,sstagger);
							p_files[i].Name = (char*)malloc((strlen(FindFileData.cFileName)));
							p_files[i].Path = (char*)malloc((strlen(path.at(iter))));
							if ((p_files[i].Name == NULL) || (p_files[i].Path == NULL))
							{
								SetConsoleTextAttribute(hConsole, 12);
								printf_s("Memory allocation error");
								SetConsoleTextAttribute(hConsole, 7);
								exit(2);
							}
							memset(p_files[i].Name, 0, sizeof(char)*(strlen(FindFileData.cFileName)));
							memset(p_files[i].Path, 0, sizeof(char)*(strlen(path.at(iter))));
							p_files[i].Size = (((static_cast<ULONGLONG>(FindFileData.nFileSizeHigh) << sizeof(FindFileData.nFileSizeLow) * 8) | FindFileData.nFileSizeLow));

							strcpy(p_files[i].Name, FindFileData.cFileName);
							strcpy(p_files[i].Path, path.at(iter));
							p_files[i].State = 0;
							i++;
						}
					}
				}
				free(test);
			} while (FindNextFileA(hFile, &FindFileData));

			FindClose(hFile);
		}
		free(str);
	}
	path.clear();
	return i;
}

int Get_index_acc(unsigned long long key)
{
			int acc_index = -1;
			unsigned acc_count = 0;
			pthread_mutex_lock(&byteLock);
			for(unsigned a=0; a < MaxAccounts; a++)
			{
				if(bests[a].account_id != 0) acc_count++;
				if(bests[a].account_id == key) acc_index = a;
			}
			if(acc_index < 0)
			{
				acc_index = acc_count;
				bests[acc_index].account_id = key;
			}
			pthread_mutex_unlock(&byteLock);
			return acc_index;
}


void gen_nonce(unsigned long long addr, unsigned long long n, unsigned long long size) {
	#define PLOT_SIZE	(4096 * 64)
	#define HASH_SIZE	32
	#define HASH_CAP	4096
	char * final = (char *)malloc(sizeof(char)*32);
	char * gendata = (char *)malloc(sizeof(char)*(16 + PLOT_SIZE));
	char * cache = (char *)malloc(sizeof(char)*(64*1000));
	char *xv = (char*)&addr;
	
	gendata[PLOT_SIZE] = xv[7]; gendata[PLOT_SIZE+1] = xv[6]; gendata[PLOT_SIZE+2] = xv[5]; gendata[PLOT_SIZE+3] = xv[4];
	gendata[PLOT_SIZE+4] = xv[3]; gendata[PLOT_SIZE+5] = xv[2]; gendata[PLOT_SIZE+6] = xv[1]; gendata[PLOT_SIZE+7] = xv[0];

	sph_shabal_context x;
	unsigned int i, len;
	unsigned long long z;
	for (z = n; (z < (n + size)) && (!stopThreads); z++)
	{
		xv = (char*)&z;
		gendata[PLOT_SIZE + 8] = xv[7]; gendata[PLOT_SIZE + 9] = xv[6]; gendata[PLOT_SIZE + 10] = xv[5]; gendata[PLOT_SIZE + 11] = xv[4];
		gendata[PLOT_SIZE + 12] = xv[3]; gendata[PLOT_SIZE + 13] = xv[2]; gendata[PLOT_SIZE + 14] = xv[1]; gendata[PLOT_SIZE + 15] = xv[0];

		for (i = PLOT_SIZE; i > 0; i -= HASH_SIZE)
		{
			shabal_init(&x, 256);
			len = PLOT_SIZE + 16 - i;
			if (len > HASH_CAP)	len = HASH_CAP;
			shabal_core(&x, (const unsigned char*)&gendata[i], len);
			shabal_close(&x, 0, 0, &gendata[i - HASH_SIZE], 8);
		}

		shabal_init(&x, 256);
		shabal_core(&x, (const unsigned char*)gendata, 16 + PLOT_SIZE);
		shabal_close(&x, 0, 0, final, 8);

		// XOR with final
		//for(i = 0; i < PLOT_SIZE; i ++)
		//	gendata[i] ^= (final[i % HASH_SIZE]);
		for (i = scoop * 64; i < ((scoop + 1) * 64); i++)
			gendata[i] ^= (final[i % HASH_SIZE]);
		memmove(&cache[(z-n)*64], &gendata[scoop * 64], 64);
	}
	free(final);
	free(gendata);
	


        char sig[32 + 64];
		unsigned acc = 0;
		char res[32];
        memmove(sig, signature, 32);
		
		for (z = 0; (z < size) && (!stopThreads); z++)
        {
			memmove(&sig[32], &cache[z*64], 64);
                //memmove(&sig[32], &gendata[scoop*64], 64);
				
                
				shabal_init(&x, 256);
                shabal_core(&x, (const unsigned char*) sig, 64 + 32);
                shabal_close(&x, 0, 0, res, 8);
				
                unsigned long long *wertung = (unsigned long long*)res;
				//printf("\r[%llu][%llu]\tfound deadline %llu\n", bests[acc].account_id, nonce, *wertung / baseTarget);
              
				if (bests[acc].nonce == 0 || *wertung <= bests[acc].best)  
				{
                    bests[acc].best = *wertung;
					bests[acc].nonce = n;
					if ((bests[acc].best / baseTarget) <= bests[acc].targetDeadline) 
					{       
							Log("\nMEM: found deadline=");	Log_llu(bests[acc].best/baseTarget); Log(" nonce=");	Log_llu(bests[acc].nonce); Log(" for account: "); Log_llu(bests[acc].account_id);
							pthread_mutex_lock(&byteLock);
								shares[num_shares].best = bests[acc].best;
								shares[num_shares].nonce = bests[acc].nonce;
								shares[num_shares].to_send = 1;
								shares[num_shares].account_id = bests[acc].account_id;
								shares[num_shares].file_name = "Memory";
								//if(use_debug)
								{
									cls();
									SetConsoleTextAttribute(hConsole, 2);
									printf("\r[%llu]\tMEM: found deadline %llu\n", bests[acc].account_id, shares[num_shares].best / baseTarget);
									SetConsoleTextAttribute(hConsole, 7);
								}
								num_shares++;
							pthread_mutex_unlock(&byteLock);
                     }
					
				}
        }
		free(cache);
		
}

void *generator_i(void *x_void_ptr)
{
	unsigned long long nonce = 100000000;// * (local_num + 1);
	unsigned long long size = 100;
	clock_t start_work_time;
	do
	{
		start_work_time = clock();
		gen_nonce(bests[0].account_id, nonce, size);
		cls();
		printf_s("\r[%llu]  noces/min:  %llu \n", bests[0].account_id, ((clock() - start_work_time)*size / CLOCKS_PER_SEC) / 60);
		nonce = nonce + size;
	} while (!stopThreads);
	return 0;
}


int _cdecl comp_min(const void *a, const void *b)
{
	struct t_shares *ia = (struct t_shares *) a;
	struct t_shares *ib = (struct t_shares *) b;
	if(ib->best > ia->best) return -1;
	else return 1;
}

int _cdecl comp_max(const void *a, const void *b)
{
	struct t_shares *ia = (struct t_shares *) a;
	struct t_shares *ib = (struct t_shares *) b;
	if (ib->best > ia->best) return 1;
	else return -1;
}

void *proxy_i(void *x_void_ptr)
{
	
	int iResult;
	char buffer[1000];
	char tmp_buffer[1000];
	char tbuffer[9];
	SOCKET ServerSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;
	struct addrinfo *result = NULL;
	struct addrinfo hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET; //AF_UNSPEC;  // использовать IPv4 или IPv6, нам неважно
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	char pport[6];
	_itoa(proxyport, pport, 10);
	iResult = getaddrinfo(NULL, pport, &hints, &result);
	if (iResult != 0) {
		cls();
		printf("\rgetaddrinfo failed with error: %d\n", iResult);
	}

	ServerSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ServerSocket == INVALID_SOCKET) {
		printf("\rsocket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
	}
	BOOL l = TRUE;
	iResult = bind(ServerSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("\rbind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ServerSocket);
	}
	freeaddrinfo(result);
	iResult = ioctlsocket(ServerSocket, FIONBIO, (unsigned long*)&l);
	if (iResult == SOCKET_ERROR)
	{
		Log("\nProxy: ! Error ioctlsocket's: "); Log(DisplayErrorText(WSAGetLastError()));
		cls();
		SetConsoleTextAttribute(hConsole, 12);
		printf_s("\rioctlsocket failed: %ld\n", WSAGetLastError());
		SetConsoleTextAttribute(hConsole, 7);
	}

	iResult = listen(ServerSocket, 8);
	if (iResult == SOCKET_ERROR) {
		printf("\rlisten failed with error: %d\n", WSAGetLastError());
		closesocket(ServerSocket);
	}
	Log("\nProxy thread started");

	do{
		
		if (can_connect == 1 )
		{
			pthread_mutex_lock(&byteLock);
			can_connect = 0;
			pthread_mutex_unlock(&byteLock);

			struct sockaddr_in client_socket_address;
			int iAddrSize = sizeof(client_socket_address);
			ClientSocket = accept(ServerSocket, (struct sockaddr *)&client_socket_address, &iAddrSize);
			if (ClientSocket == INVALID_SOCKET)
			{
				if (WSAGetLastError() != WSAEWOULDBLOCK)
				{
					Log("\nProxy:! Error Proxy's accept: "); Log(DisplayErrorText(WSAGetLastError()));
					cls();
					SetConsoleTextAttribute(hConsole, 12);
					printf_s("\rProxy can't accept. Error: %ld\n", WSAGetLastError());
					SetConsoleTextAttribute(hConsole, 7);
				}
			}
			else
			{
				memset(buffer, 0, 1000);
				do{
					memset(tmp_buffer, 0, 1000);
					iResult = recv(ClientSocket, tmp_buffer, 999, 0);
					strcat(buffer, tmp_buffer);
				}while ((iResult > 0) && !use_fast_rcv);

				
				Log("\nProxy get info: ");  Log_server(buffer);
				unsigned long long get_accountId = 0;
				unsigned long long get_nonce = 0;
				unsigned long long get_deadline = 0;
				unsigned long long get_totalsize = 0;
				// locate HTTP header
				char *find = strstr(buffer, "\r\n\r\n");
				if (find != NULL)
				{

					char *startaccountId = strstr(buffer, "accountId=");
					if (startaccountId != NULL)
					{
						startaccountId = strpbrk(startaccountId, "0123456789");
						char *endaccountId = strpbrk(startaccountId, "& }\"");

						char *startnonce = strstr(buffer, "nonce=");
						char *startdl = strstr(buffer, "deadline=");
						char *starttotalsize = strstr(buffer, "TotalSize");
						if ((startnonce != NULL) && (startdl != NULL))
						{
							startnonce = strpbrk(startnonce, "0123456789");
							char *endnonce = strpbrk(startnonce, "& }\"");
							startdl = strpbrk(startdl, "0123456789");
							char *enddl = strpbrk(startdl, "& }\"");

							endaccountId[0] = 0;
							endnonce[0] = 0;
							enddl[0] = 0;

							get_accountId = strtoull(startaccountId, 0, 10);
							get_nonce = strtoull(startnonce, 0, 10);
							get_deadline = strtoull(startdl, 0, 10);

							if (starttotalsize != NULL)
							{
								starttotalsize = strpbrk(starttotalsize, "0123456789");
								char *endtotalsize = strpbrk(starttotalsize, "& }\"");
								endtotalsize[0] = 0;
								get_totalsize = strtoull(starttotalsize, 0, 10);
								satellite_size.insert(std::pair <char*, unsigned long long>(inet_ntoa(client_socket_address.sin_addr), get_totalsize));
							}

							pthread_mutex_lock(&byteLock);
							shares[num_shares].best = get_deadline;
							shares[num_shares].nonce = get_nonce;
							shares[num_shares].to_send = 1;
							shares[num_shares].account_id = get_accountId;
							shares[num_shares].file_name = inet_ntoa(client_socket_address.sin_addr);
							{
								cls();
								SetConsoleTextAttribute(hConsole, 2);
								_strtime(tbuffer);
								printf_s("\r%s [%20llu]\treceived DL: %11llu {%s}\n", tbuffer, get_accountId, shares[num_shares].best / baseTarget, inet_ntoa(client_socket_address.sin_addr));
								SetConsoleTextAttribute(hConsole, 7);
								
							}
							num_shares++;
							pthread_mutex_unlock(&byteLock);
							Log("Proxy: received DL "); Log_llu(get_deadline); Log(" from "); Log(inet_ntoa(client_socket_address.sin_addr));

							//Подтверждаем
							memset(buffer, 0, 1000);
							int acc = Get_index_acc(get_accountId);
							int bytes = sprintf_s(buffer, "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n{\"result\": \"proxy\",\"accountId\": %llu,\"deadline\": %llu,\"targetDeadline\": %llu}", get_accountId, get_deadline / baseTarget, bests[acc].targetDeadline);
							iResult = send(ClientSocket, buffer, bytes, 0);
							if (iResult == SOCKET_ERROR)
							{
								err_send_dl++;
								Log("\nProxy: ! Error sending to client: "); Log(DisplayErrorText(WSAGetLastError()));
								cls();
								SetConsoleTextAttribute(hConsole, 12);
								printf_s("\rfailed sending to client: %ld\n", WSAGetLastError());
								SetConsoleTextAttribute(hConsole, 7);
							}
							else
							{
								if (use_debug)
								{
									cls();
									SetConsoleTextAttribute(hConsole, 9);
									_strtime(tbuffer);
									printf_s("\r%s [%llu]\tsent confirmation to %s\n", tbuffer, get_accountId, inet_ntoa(client_socket_address.sin_addr));
									SetConsoleTextAttribute(hConsole, 7);
								}
								Log("\nProxy: sent confirmation to "); Log(inet_ntoa(client_socket_address.sin_addr));
							}
						}
					}
					else
					{
						if (strstr(buffer, "getMiningInfo") != NULL)
						{
							memset(buffer, 0, 1000);
							int bytes = sprintf_s(buffer, "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n{\"baseTarget\":\"%llu\",\"height\":\"%llu\",\"generationSignature\":\"%s\",\"targetDeadline\":%llu}", baseTarget, height, str_signature, targetDeadlineInfo);
							iResult = send(ClientSocket, buffer, bytes, 0);
							if (iResult == SOCKET_ERROR)
							{
								//err_send_dl++;
								Log("\nProxy: ! Error sending to client: "); Log(DisplayErrorText(WSAGetLastError()));
								cls();
								SetConsoleTextAttribute(hConsole, 12);
								printf_s("\rfailed sending to client: %ld\n", WSAGetLastError());
								SetConsoleTextAttribute(hConsole, 7);
							}
							else
							{
								//cls();
								//SetConsoleTextAttribute(hConsole, 9);
								//_strtime(tbuffer);
								//printf_s("\r%s [%llu]\tsent confirmation to %s\n", tbuffer, get_accountId, inet_ntoa(client_socket_address.sin_addr));
								//SetConsoleTextAttribute(hConsole, 7);
								Log("\nProxy: sent update to "); Log(inet_ntoa(client_socket_address.sin_addr));
							}
						}

						else
						{
							SetConsoleTextAttribute(hConsole, 15);
							find[0] = 0;
							printf_s("\r%s\n", buffer);
							SetConsoleTextAttribute(hConsole, 7);
						}
					}
				}
				iResult = closesocket(ClientSocket);
			}
			pthread_mutex_lock(&byteLock);
			can_connect = 1;
			pthread_mutex_unlock(&byteLock);
		}
		Sleep(200);
	}while (true);

}

void *send_i(void *x_void_ptr) 
{
	SOCKET ConnectSocket;
	
	int iResult;
	char buffer[1000];
	char tmp_buffer[1000];
	
	char tbuffer[9];

	struct addrinfo *result = NULL;

	struct addrinfo hints;
	for (unsigned long long i = 0;; i++)
	{
		if (i > num_shares) i = 0;
		while ((num_shares == sent_num_shares) && sessions.empty()) // || (working_threads > 0))
		{
			Sleep(10);
		}

				unsigned long long part = num_shares;
				if((part - i > 1) && (sent_num_shares != part))
				{
					if (use_sorting) qsort(&shares[0], part, sizeof(t_shares), comp_min);
					else  qsort(&shares[0], part, sizeof(t_shares), comp_max);
					for(unsigned long long j=0; j < part; j++) 
						if(shares[j].to_send == 1) 
						{
							i=j;
							break;
						}
				}
			if (send_best_only)
			{
				int acc = Get_index_acc(shares[i].account_id);
				if((bests[acc].targetDeadline > 0) && ((shares[i].best / baseTarget) > bests[acc].targetDeadline) && (shares[i].to_send == 1))
				{
					pthread_mutex_lock(&byteLock);
					shares[i].to_send = 0;
					sent_num_shares++;
					pthread_mutex_unlock(&byteLock);
					if(use_debug)
					{
						cls();
						SetConsoleTextAttribute(hConsole, 4);
						_strtime(tbuffer);
						printf_s("\r%s [%llu]\t%llu > %llu  discarded\n", tbuffer, shares[i].account_id, shares[i].best / baseTarget, bests[acc].targetDeadline);
						SetConsoleTextAttribute(hConsole, 7);						
					}
				}
			}
			

			if( (can_connect == 1) && (shares[i].to_send == 1))   
			{
				pthread_mutex_lock(&byteLock);
				can_connect = 0;
				pthread_mutex_unlock(&byteLock);
				
				ZeroMemory(&hints, sizeof(hints));
				hints.ai_family = AF_INET; //AF_UNSPEC;  // использовать IPv4 или IPv6, нам неважно
				hints.ai_socktype = SOCK_STREAM;
				hints.ai_protocol = IPPROTO_TCP;
				hints.ai_flags = AI_PASSIVE;
				char nport[6];
				_itoa(nodeport, nport, 10);
				iResult = getaddrinfo(nodeaddr, nport, &hints, &result);
				if (iResult != 0) {
					cls();
					printf("\rgetaddrinfo failed with error: %d\n", iResult);
				}
				ConnectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
				if (ConnectSocket == INVALID_SOCKET) {
					printf("\rsocket failed with error: %ld\n", WSAGetLastError());
					freeaddrinfo(result);
				}
												
				iResult = connect(ConnectSocket, result->ai_addr, (int)result->ai_addrlen);
				if (iResult == SOCKET_ERROR) 
				{
					Log("\nSender:! Error Sender's connect: "); Log(DisplayErrorText(WSAGetLastError()));
					cls();
					SetConsoleTextAttribute(hConsole, 12);
					printf_s("\rSender can't connect. Error: %ld\n", WSAGetLastError());
					SetConsoleTextAttribute(hConsole, 7);
				}
				else
				{
					freeaddrinfo(result);

					int bytes = 0;
					memset(buffer, 0, 1000);
					if (miner_mode == 0)
					{
						//bytes = sprintf_s(buffer, "POST /burst?requestType=submitNonce&secretPhrase=%s&nonce=%llu HTTP/1.0\r\nConnection: close\r\n\r\n", pass, shares[i].nonce);
						bytes = sprintf_s(buffer, "POST /burst?requestType=submitNonce&secretPhrase=");
						strcat(buffer,pass);
						bytes = bytes + (int)strlen(pass);
						char tmp[100];
						bytes += sprintf_s(tmp, "&nonce=%llu HTTP/1.0\r\nConnection: close\r\n\r\n", shares[i].nonce);
						strcat(buffer, tmp);
						//printf("\n%s\n",pass);
					}
					if (miner_mode == 1)
					{
						
						unsigned long long total = total_size / 1024 / 1024 / 1024;
						for (std::map <char*, unsigned long long>::iterator Iter = satellite_size.begin(); Iter != satellite_size.end(); Iter++) total = total + Iter->second;
						bytes = sprintf_s(buffer, "POST /burst?requestType=submitNonce&accountId=%llu&nonce=%llu&deadline=%llu HTTP/1.0\r\nminer: Blago %s\r\nTotalSize: %llu\r\nConnection: close\r\n\r\n", shares[i].account_id, shares[i].nonce, (shares[i].best), version, total);
					}
					if(miner_mode == 2)
					{
						char *f1 = (char*) malloc(100);
						char *str_len = (char*) malloc(10);
						int len = sprintf(f1, "%llu:%llu:%llu\n", shares[i].account_id, shares[i].nonce, height);
						_itoa_s(len, str_len, 10, 10);
			
						bytes = sprintf_s(buffer, "POST /pool/submitWork HTTP/1.0\r\nHost: %s:%i\r\nContent-Type: text/plain;charset=UTF-8\r\nContent-Length: %i\r\n\r\n%s", nodeip, nodeport, len, f1);
						free(f1);
						free(str_len);
					}   
					
					// Sending to server
					all_send_dl++;
					iResult = send(ConnectSocket, buffer, bytes, 0);
					if (iResult == SOCKET_ERROR)
					{
						err_send_dl++;
						Log("\nSender: ! Error deadline's sending: "); Log(DisplayErrorText(WSAGetLastError()));
						cls();
						SetConsoleTextAttribute(hConsole, 12);
						printf_s("\rsend failed: %ld\n", WSAGetLastError());
						SetConsoleTextAttribute(hConsole, 7);
					}
					else
					{
						cls();
						SetConsoleTextAttribute(hConsole, 9);
						unsigned long long dl = shares[i].best / baseTarget;
						_strtime(tbuffer);
						printf_s("\r%s [%20llu] sent DL: %15llu %5llud %02llu:%02llu:%02llu\n", tbuffer, shares[i].account_id, dl, (dl) / (24 * 60 * 60), (dl % (24 * 60 * 60)) / (60 * 60), (dl % (60 * 60)) / 60, dl % 60);
						SetConsoleTextAttribute(hConsole, 7);

						if (show_msg) printf_s("\nsend: %s\n", buffer); // показываем послание
						Log("\nSender:   Sent to server: "); Log_server(buffer);
						
						t_session *to = (t_session*) malloc(sizeof(t_session));
						to->Socket = ConnectSocket;
						to->ID = shares[i].account_id;
						to->deadline = dl;
						sessions.push_back(*to);
						free(to);
						shares[i].to_send = 0;
						sent_num_shares++;
						if (send_best_only) bests[Get_index_acc(shares[i].account_id)].targetDeadline = dl;
					}
				}
				pthread_mutex_lock(&byteLock);
				can_connect = 1;
				pthread_mutex_unlock(&byteLock);
			}


			if ((can_connect == 1) && !sessions.empty())
			{
				pthread_mutex_lock(&byteLock);
				can_connect = 0;
				pthread_mutex_unlock(&byteLock);
				
				
				for (unsigned int iter = 0; iter < sessions.size(); iter++)
				{
					ConnectSocket = sessions.at(iter).Socket;
					
					BOOL l = TRUE;
					iResult = ioctlsocket(ConnectSocket, FIONBIO, (unsigned long*)&l);
					if (iResult == SOCKET_ERROR)
					{
						Log("\nSender: ! Error ioctlsocket's: "); Log(DisplayErrorText(WSAGetLastError()));
						cls();
						SetConsoleTextAttribute(hConsole, 12);
						printf_s("\rioctlsocket failed: %ld\n", WSAGetLastError());
						SetConsoleTextAttribute(hConsole, 7);
					}

					memset(buffer, 0, 1000);
					unsigned resp = 0;
					do{
						memset(tmp_buffer, 0, 1000);
						iResult = recv(ConnectSocket, tmp_buffer, 999, 0);
						strcat(buffer, tmp_buffer);
						resp++;
					} while ((iResult > 0) && !use_fast_rcv);

					if (iResult == SOCKET_ERROR)
					{
						if (WSAGetLastError() != WSAEWOULDBLOCK)
						{
							err_rcv_dl++;
							Log("\nSender: ! Error getting deadline's confirmation: "); Log(DisplayErrorText(WSAGetLastError()));
							cls();
							SetConsoleTextAttribute(hConsole, 12);
							printf_s("\rreceiving confirmation failed: %ld\n", WSAGetLastError());
							SetConsoleTextAttribute(hConsole, 7);							
						}
					}
					else
					{
						if (show_msg) printf_s("\nReceived: %s\n", buffer);
						Log("\nSender:   Received from server: "); Log_server(buffer); Log("\nCount responses: "); Log_u(resp);

						// locate HTTP header
						char *find = strstr(buffer, "\r\n\r\n");
						if (find != NULL)
						{
							char *rdeadline = strstr(find + 4, "\"deadline\"");
							if (rdeadline != NULL)
							{
								rdeadline = strpbrk(rdeadline, "0123456789");
								char *enddeadline = strpbrk(rdeadline, ",}\"");
								
								char* rtargetDeadline = strstr(buffer, "\"targetDeadline\":");
								char* raccountId = strstr(buffer, "\"accountId\":");
								if (enddeadline != NULL)
								{
									// Parse and check if we have a better deadline
									enddeadline[0] = 0;
									unsigned long long ndeadline = strtoull(rdeadline, 0, 10);
									unsigned long long naccountId = 0;
									unsigned long long ntargetDeadline = 0;
									if ((rtargetDeadline != NULL) && (raccountId != NULL))
									{
										rtargetDeadline = strpbrk(rtargetDeadline, "0123456789");
										char* endBaseTarget = strpbrk(rtargetDeadline, ",}\"");
										endBaseTarget[0] = 0;
										
										raccountId = strpbrk(raccountId, "0123456789");
										char* endaccountId = strpbrk(raccountId, ",}\"");
										endaccountId[0] = 0;
										naccountId = strtoull(raccountId, 0, 10);
										int acc = Get_index_acc(naccountId);
										ntargetDeadline = strtoull(rtargetDeadline, 0, 10);
										bests[acc].targetDeadline = ntargetDeadline;
									}
									all_rcv_dl++;
									Log("\nSender: confirmed deadline: "); Log_llu(ndeadline);
									cls();
									//unsigned long long years = (ndeadline % (24*60*60))/(60*60)
									//unsigned month = (ndeadline % (365*24*60*60))/(24*60*60);
									unsigned long long days = (ndeadline) / (24 * 60 * 60);
									unsigned hours = (ndeadline % (24 * 60 * 60)) / (60 * 60);
									unsigned min = (ndeadline % (60 * 60)) / 60;
									unsigned sec = ndeadline % 60;
									cls();
									_strtime(tbuffer);
									SetConsoleTextAttribute(hConsole, 10);
									if ((naccountId != 0) && (ntargetDeadline != 0))
									{
										printf_s("\r%s [%llu] confirmed DL: %9llu\t%llud %2u:%2u:%2u\n", tbuffer, naccountId, ndeadline, days, hours, min, sec);
										if (use_debug) printf("\r%s [%llu] Set targetDL\t%llu\n", tbuffer, naccountId, ntargetDeadline);
									}
									else printf_s("\r%s [%20llu] confirmed DL: %10llu %5llud %02u:%02u:%02u\n", tbuffer, sessions.at(iter).ID , ndeadline, days, hours, min, sec);
									SetConsoleTextAttribute(hConsole, 7);
									if (ndeadline < deadline || deadline == 0)  deadline = ndeadline;
									
									if (ndeadline != sessions.at(iter).deadline)
									{
										SetConsoleTextAttribute(hConsole, 6);
										printf_s("----Fast block or corrupted file?----\nSent deadline:\t%llu\nServer's deadline:\t%llu \n----------------------- file: %s\n", sessions.at(iter).deadline, ndeadline, shares[i].file_name);
										SetConsoleTextAttribute(hConsole, 7);
									}
								}
							}
							else
								if (strstr(find + 4, "Received share") != NULL)
								{
									cls();
									_strtime(tbuffer);
									SetConsoleTextAttribute(hConsole, 10);
									deadline = bests[Get_index_acc(sessions.at(iter).ID)].DL;
									all_rcv_dl++;
									printf_s("\r%s [%20llu] confirmed DL\n", tbuffer, sessions.at(iter).ID);
									SetConsoleTextAttribute(hConsole, 7);
								}
								else
								{
									SetConsoleTextAttribute(hConsole, 15);
									printf_s("\r%s\n", find + 4);
									SetConsoleTextAttribute(hConsole, 7);
								}
							iResult = closesocket(ConnectSocket);
							Log("\nSender:Close socket. Code = "); Log_u(WSAGetLastError());
							if (sessions.size() >= iter)
							{
								sessions.erase(sessions.begin() + iter);
								iter--;
							}
						}
					}
				}
				pthread_mutex_lock(&byteLock);
				can_connect = 1;
				pthread_mutex_unlock(&byteLock);
			}

			Sleep(send_interval);
	}
	return 0;
}

/*
unsigned long long procscoop4(unsigned long long nonce, unsigned long long n, char *data, int acc, char * file_name) {
        char *cache;
        char sig0[32 + 64];
		char sig1[32 + 64];
		char sig2[32 + 64];
		char sig3[32 + 64];
        cache = data;
        unsigned long long v;
 
        memmove(sig0, signature, 32);
		memmove(sig1, signature, 32);
		memmove(sig2, signature, 32);
		memmove(sig3, signature, 32);
 
        for( v=0; v<n; v+=4) {
                memmove(&sig0[32], &cache[(v+0)*64], 64);
				memmove(&sig1[32], &cache[(v+1)*64], 64);
				memmove(&sig2[32], &cache[(v+2)*64], 64);
				memmove(&sig3[32], &cache[(v+3)*64], 64);
                char res0[32];
				char res1[32];
				char res2[32];
				char res3[32];
 
                mshabal_context x;
				avx2_mshabal_init(&x, 256);
                avx2_mshabal(&x, (const unsigned char*) sig0, (const unsigned char*) sig1, (const unsigned char*) sig2, (const unsigned char*) sig3, 64 + 32);
                avx2_mshabal_close(&x, 0, 0, 0, 0, 0, res0, res1, res2, res3);

                unsigned long long *wertung = (unsigned long long*)res0;
				unsigned long long *wertung1 = (unsigned long long*)res1;
				unsigned long long *wertung2 = (unsigned long long*)res2;
				unsigned long long *wertung3 = (unsigned long long*)res3;
				unsigned posn = 0;
				if(*wertung1 < *wertung) 
				{
					*wertung = *wertung1;
					posn=1;
				}
				if(*wertung2 < *wertung) 
				{
					*wertung = *wertung2;
					posn=2;
				}
				if(*wertung3 < *wertung) 
				{
					*wertung = *wertung3;
					posn=3;
				}
				
 
                if(bests[acc].nonce == 0 || *wertung < bests[acc].best) 
				{
                    bests[acc].best = *wertung;
					bests[acc].nonce = nonce + v + posn;
					if((bests[acc].best / baseTarget) <= bests[acc].targetDeadline) // Has to be this good before we inform the node
						{       
							Log("\nfound deadline=");	Log_llu(bests[acc].best/baseTarget); Log(" nonce=");	Log_llu(bests[acc].nonce); Log(" for account: "); Log_llu(bests[acc].account_id);
							//printf("\rbestn_local: %llu  bestn_local: %llu  n:%llu\n", best_local, bestn_local, n);
							pthread_mutex_lock(&byteLock);
								shares[num_shares].best = bests[acc].best;
								shares[num_shares].nonce = bests[acc].nonce;
								shares[num_shares].to_send = 1;
								shares[num_shares].account_id = bests[acc].account_id;
								shares[num_shares].file_name = file_name;
								if(use_debug)
								{
									cls();
									SetConsoleTextAttribute(hConsole, 2);
									printf("\r[%llu]\tfound deadline %llu\n", bests[acc].account_id, shares[num_shares].best / baseTarget);
									SetConsoleTextAttribute(hConsole, 7);
								}
								num_shares++;
							pthread_mutex_unlock(&byteLock);
                        }
				}
                //nonce++;
                //cache += 64*4;
        }
		
		return v;
}


unsigned long long procscoop8(unsigned long long nonce, unsigned long long n, char *data, int acc, char * file_name) {
	char *cache;
	char sig0[32 + 64];
	char sig1[32 + 64];
	char sig2[32 + 64];
	char sig3[32 + 64];
	char sig4[32 + 64];
	char sig5[32 + 64];
	char sig6[32 + 64];
	char sig7[32 + 64];
	cache = data;
	unsigned long long v;
	char tbuffer[9];

	memmove(sig0, signature, 32);
	memmove(sig1, signature, 32);
	memmove(sig2, signature, 32);
	memmove(sig3, signature, 32);
	memmove(sig4, signature, 32);
	memmove(sig5, signature, 32);
	memmove(sig6, signature, 32);
	memmove(sig7, signature, 32);

	for (v = 0; v<n; v += 8) {
		memmove(&sig0[32], &cache[(v + 0) * 64], 64);
		memmove(&sig1[32], &cache[(v + 1) * 64], 64);
		memmove(&sig2[32], &cache[(v + 2) * 64], 64);
		memmove(&sig3[32], &cache[(v + 3) * 64], 64);
		memmove(&sig4[32], &cache[(v + 4) * 64], 64);
		memmove(&sig5[32], &cache[(v + 5) * 64], 64);
		memmove(&sig6[32], &cache[(v + 6) * 64], 64);
		memmove(&sig7[32], &cache[(v + 7) * 64], 64);
		char res0[32];
		char res1[32];
		char res2[32];
		char res3[32];
		char res4[32];
		char res5[32];
		char res6[32];
		char res7[32];

		mshabal256_context x;
		mshabal256_init(&x, 256);
		mshabal256(&x, (const unsigned char*)sig0, (const unsigned char*)sig1, (const unsigned char*)sig2, (const unsigned char*)sig3, (const unsigned char*)sig4, (const unsigned char*)sig5, (const unsigned char*)sig6, (const unsigned char*)sig7, 64 + 32);
		mshabal256_close(&x, 0, 0, 0, 0, 0, 0, 0, 0, 0, res0, res1, res2, res3, res4, res5, res6, res7);

		unsigned long long *wertung = (unsigned long long*)res0;
		unsigned long long *wertung1 = (unsigned long long*)res1;
		unsigned long long *wertung2 = (unsigned long long*)res2;
		unsigned long long *wertung3 = (unsigned long long*)res3;
		unsigned long long *wertung4 = (unsigned long long*)res4;
		unsigned long long *wertung5 = (unsigned long long*)res5;
		unsigned long long *wertung6 = (unsigned long long*)res6;
		unsigned long long *wertung7 = (unsigned long long*)res7;
		unsigned posn = 0;
		if (*wertung1 < *wertung)
		{
			*wertung = *wertung1;
			posn = 1;
		}
		if (*wertung2 < *wertung)
		{
			*wertung = *wertung2;
			posn = 2;
		}
		if (*wertung3 < *wertung)
		{
			*wertung = *wertung3;
			posn = 3;
		}
		if (*wertung4 < *wertung)
		{
			*wertung = *wertung4;
			posn = 4;
		}
		if (*wertung5 < *wertung)
		{
			*wertung = *wertung5;
			posn = 5;
		}
		if (*wertung6 < *wertung)
		{
			*wertung = *wertung6;
			posn = 6;
		}
		if (*wertung7 < *wertung)
		{
			*wertung = *wertung7;
			posn = 7;
		}

		Log("\nЗашли");
		if (bests[acc].nonce == 0 || *wertung < bests[acc].best)
		{
			bests[acc].best = *wertung;
			bests[acc].nonce = nonce + v + posn;
			if ((bests[acc].best / baseTarget) <= bests[acc].targetDeadline) // Has to be this good before we inform the node
			{
				Log("\nfound deadline=");	Log_llu(bests[acc].best / baseTarget); Log(" nonce=");	Log_llu(bests[acc].nonce); Log(" for account: "); Log_llu(bests[acc].account_id);
				//printf("\rbestn_local: %llu  bestn_local: %llu  n:%llu\n", best_local, bestn_local, n);
				pthread_mutex_lock(&byteLock);
				shares[num_shares].best = bests[acc].best;
				shares[num_shares].nonce = bests[acc].nonce;
				shares[num_shares].to_send = 1;
				shares[num_shares].account_id = bests[acc].account_id;
				shares[num_shares].file_name = file_name;
				if (use_debug)
				{
					cls();
					_strtime(tbuffer);
					SetConsoleTextAttribute(hConsole, 2);
					printf("\r%s [%llu]\tfound deadline %llu\n", tbuffer, bests[acc].account_id, shares[num_shares].best / baseTarget);
					SetConsoleTextAttribute(hConsole, 7);
				}
				num_shares++;
				pthread_mutex_unlock(&byteLock);
			}
		}
		//nonce++;
		//cache += 64*4;
	}

	return v;
}
*/


unsigned long long procscoop(unsigned long long nonce, unsigned long long n, char *data, int acc, char * file_name) {
        char *cache;
        char sig[32 + 64];
        cache = data;
        unsigned long long v;
		char tbuffer[9];
        memmove(sig, signature, 32);
 
        for( v=0; v<n; v++) {
                memmove(&sig[32], cache, 64);
                char res[32];
 
                sph_shabal_context x;
				shabal_init(&x, 256);
                shabal_core(&x, (const unsigned char*) sig, 64 + 32);
                shabal_close(&x, 0, 0, res, 8);


                unsigned long long *wertung = (unsigned long long*)res;
 
				if ((*wertung / baseTarget) <= bests[acc].targetDeadline)
				{
					
					if (send_best_only)
					{
						if (bests[acc].nonce == 0 || *wertung < bests[acc].best)
						{
							Log("\nfound deadline=");	Log_llu(*wertung / baseTarget); Log(" nonce=");	Log_llu(nonce + v); Log(" for account: "); Log_llu(bests[acc].account_id); Log(" file: "); Log(file_name);
							pthread_mutex_lock(&byteLock);
							bests[acc].best = *wertung;
							bests[acc].nonce = nonce + v;
							bests[acc].DL = *wertung / baseTarget;
							shares[num_shares].best = bests[acc].best;
							shares[num_shares].nonce = bests[acc].nonce;
							shares[num_shares].to_send = 1;
							shares[num_shares].account_id = bests[acc].account_id;
							shares[num_shares].file_name = file_name;
							if (use_debug)
							{
								cls();
								_strtime(tbuffer);
								SetConsoleTextAttribute(hConsole, 2);
								printf("\r%s [%llu]\tfound deadline:    %llu\n", tbuffer, bests[acc].account_id, shares[num_shares].best / baseTarget);
								SetConsoleTextAttribute(hConsole, 7);
							}
							num_shares++;
							pthread_mutex_unlock(&byteLock);
						}
					}
					else
					{
						pthread_mutex_lock(&byteLock);
						if (bests[acc].nonce == 0 || *wertung < bests[acc].best)
						{
							bests[acc].best = *wertung;
							bests[acc].nonce = nonce + v;
							bests[acc].DL = *wertung / baseTarget;
						}
						shares[num_shares].best = *wertung;
						shares[num_shares].nonce = nonce + v;
						shares[num_shares].to_send = 1;
						shares[num_shares].account_id = bests[acc].account_id;
						shares[num_shares].file_name = file_name;
						if (use_debug)
						{
							cls();
							_strtime(tbuffer);
							SetConsoleTextAttribute(hConsole, 2);
							printf("\r%s [%llu]\tfound deadline %llu\n", tbuffer, bests[acc].account_id, shares[num_shares].best / baseTarget);
							SetConsoleTextAttribute(hConsole, 7);
						}
						num_shares++;
						pthread_mutex_unlock(&byteLock);
					}
				}
                //nonce++;
                cache += 64;
        }
		
		return v;
}


void *work_i(void *ii) {  //void *x_void_ptr
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	//pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
		unsigned local_num = *(unsigned *)ii;
		Log("\nStart thread #");	Log_u(local_num); Log(" with parameter: ");	Log(paths_dir[local_num]);
		clock_t start_work_time, end_work_time;
		start_work_time = clock();
		FILE * pFile;
		char *x_ptr = (char*)malloc(sizeof(char)*255);
		memset(x_ptr, 0, sizeof(char)*(255));
		strcpy(x_ptr, paths_dir[local_num]);
		char *cache = (char*) malloc(cache_size * 64);
		clock_t start_time, end_time;			// Текущее время
		if(cache == NULL) {
			cls();
			SetConsoleTextAttribute(hConsole, 12);
            printf_s("\nError allocating memory\n");
			SetConsoleTextAttribute(hConsole, 7);
            exit(-1);
        }
				
		
		t_files* files = (t_files*)malloc(sizeof(t_files)* MAX_FILES);
		
		int f_count = GetFiles(x_ptr, files);
		//printf("\n   %i\n   %i  %s\n   %i  %s\n   %i  %s\n ",f_count, strlen(files[0]),files[0], strlen(files[1]), files[1], strlen(files[2]), files[2]);
		int files_count = 0;
        for(files_count = 0; files_count < f_count; files_count++)
		{
			unsigned long long key, nonce, nonces, stagger, n;
						
            char fullname[255];
			strcpy(fullname, files[files_count].Path);
			strcat(fullname, files[files_count].Name);
			sscanf_s(files[files_count].Name, "%llu_%llu_%llu_%llu", &key , &nonce, &nonces, &stagger);
						
			if (nonces != (files[files_count].Size)/(1024*256))
			{
				if (skip_bad_plots)
				{
					SetConsoleTextAttribute(hConsole, 12);
					cls();
					printf_s("\r! File \"%s\" file name/size mismatch, skipped\n", files[files_count].Name);
					SetConsoleTextAttribute(hConsole, 7);
					continue;   // если размер файла не соответствует нонсам в имени файла - пропускаем
				}
				else
				{
					SetConsoleTextAttribute(hConsole, 12);
					cls();
					printf_s("\r! File \"%s\" file name/size mismatch\n", files[files_count].Name);
					SetConsoleTextAttribute(hConsole, 7);
				}
			}
			Log("\nRead file: ");	Log(files[files_count].Name);
			start_time = clock();

			_set_fmode(_O_BINARY);
			fopen_s(&pFile, fullname, "rb");
			if (pFile==NULL)
			{
				SetConsoleTextAttribute(hConsole, 12);
				cls();
				printf_s("\rfile \"%s\" error opening\n", files[files_count]);
				SetConsoleTextAttribute(hConsole, 7);
				continue;
			}
                        
			unsigned acc_index = Get_index_acc(key);

			//printf("\r%i\n", acc_index);
			unsigned long long size = stagger * 64;
												
			for(n=0; n<nonces; n+=stagger) 
			{
				// Read one Scoop out of this block:
                // start to start+size in steps of CACHESIZE * 64
				unsigned long long start = n * 4096 * 64 + scoop * size;
				unsigned long long i;
				unsigned long long noffset = 0;
                                        
				for(i = start; i < start + size; i += cache_size * 64) 
				{
					
					unsigned long long readsize = cache_size * 64;
					if(readsize > start + size - i)	readsize = (unsigned int)(start + size - i);
					unsigned long long bytes = 0, b;

					rewind (pFile);
					_fseeki64(pFile , i , SEEK_SET);
					do {
						//b = fread (&cache[bytes],sizeof(char),readsize - bytes,pFile);
						b = _fread_nolock_s(&cache[bytes], cache_size * 64, sizeof(char), readsize - bytes, pFile);
						bytes += b;
					} while(bytes < readsize && b > 0);     // Read until cache is filled (or file ended)
					//printf("\rstart: %llu end:%llu step:%llu  readsize:%llu bytes:%llu\n",i/64 ,(start + size)/64, cache_size, readsize, bytes);
					if(b > 0)
					{
						procscoop(n + nonce + noffset, readsize / 64, cache, acc_index, files[files_count].Name);// Process block
						// Lock and add to totals
						pthread_mutex_lock(&byteLock);
						bytesRead += readsize;
						pthread_mutex_unlock(&byteLock);
					}
					else 
					{
						SetConsoleTextAttribute(hConsole, 12);
						printf("\rFile %s locked?\n", files[files_count].Name);
						SetConsoleTextAttribute(hConsole, 7);
					}
					noffset += cache_size;
				}
				//pthread_testcancel();
				if(stopThreads) // New block while processing: Stop.
				{       
					fclose(pFile);
					//free(files_size);
					//for (files_count = 0; files_count < f_count; files_count++) 	
					//	free(*files);
					free(files);
					free(x_ptr);
					free(cache);
					return 0;
				}
			}
			end_time = clock();
			Log("\nClose file: ");	Log(files[files_count].Name); Log(" [@ "); Log_llu((end_time-start_time)*1000/CLOCKS_PER_SEC); Log(" ms]");
			fclose(pFile);			
		}
		if (use_debug)
		{
			end_work_time = clock();
			cls();
			char tbuffer[9];
			_strtime(tbuffer);
			printf_s("\r%s Thread \"%s\" done! [~%llu sec] (%u files)\n", tbuffer, x_ptr, (unsigned long long)((end_work_time - start_work_time) / CLOCKS_PER_SEC), files_count);
		}
		working_threads--;
		//free(files_size);
		//for (files_count = 0; files_count < f_count; files_count++) 		free(*files[i]);
		free(files);
		free(x_ptr);
        free(cache);
}

void pollLocal(void) {
	char *buffer = (char*)malloc(sizeof(char)*1000);
	char *tmp_buffer = (char*)malloc(sizeof(char)* 1000);
	int iResult;
	struct addrinfo *result = NULL;
	struct addrinfo hints;
	SOCKET UpdaterSocket = INVALID_SOCKET;
	pthread_mutex_lock(&byteLock);
	can_connect = 0;
	pthread_mutex_unlock(&byteLock);

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET; //AF_UNSPEC;  // использовать IPv4 или IPv6, нам неважно
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	char uport[6];
	_itoa(updaterport, uport, 10);
	iResult = getaddrinfo(updateraddr, uport, &hints, &result);
	if (iResult != 0) {
		cls();
		printf("\rgetaddrinfo failed with error: %d\n", iResult);
	}
	else {
		// Get Mininginfo from node:
		UpdaterSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (UpdaterSocket == INVALID_SOCKET)
		{
			cls();
			printf_s("\rsocket function failed with error: %ld\n", WSAGetLastError());
			Log("\n*! Socket error: "); Log(DisplayErrorText(WSAGetLastError()));
		}
		else {
			//iResult = bind(UpdaterSocket, result->ai_addr, (int)result->ai_addrlen);
			//if (iResult == SOCKET_ERROR) {
			//	printf("\rbind failed with error: %d\n", WSAGetLastError());
			//	freeaddrinfo(result);
			//	closesocket(UpdaterSocket);
			//}
				unsigned t = 60000;
				setsockopt(UpdaterSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&t, sizeof(unsigned));
			Log("\n*Connecting to server: "); Log(updaterip); Log(":"); Log_u(updaterport);
			iResult = connect(UpdaterSocket, result->ai_addr, (int)result->ai_addrlen);
			if (iResult == SOCKET_ERROR) {
				cls();
				printf_s("\rconnect function failed with error: %ld\n", WSAGetLastError());
				Log("\n*! Connect server error "); //Log(DisplayErrorText(WSAGetLastError()));
			}
			else {
				// wrire some bytes
				all_send_msg++;
				if (all_send_msg >= 1000) all_send_msg = 0;

				int bytes;
				memset(buffer, 0, 1000);
				if (miner_mode == 2) bytes = sprintf(buffer, "GET /pool/getMiningInfo HTTP/1.0\r\nHost: %s:%i\r\nContent-Type: text/plain;charset=UTF-8\r\n\r\n", nodeip, nodeport);
				else bytes = sprintf(buffer, "POST /burst?requestType=getMiningInfo HTTP/1.0\r\nConnection: close\r\n\r\n");

				iResult = send(UpdaterSocket, buffer, bytes, 0);
				if (iResult == SOCKET_ERROR)
				{
					err_send_msg++;
					Log("\n*! Error sending request: "); Log(DisplayErrorText(WSAGetLastError())); Log("Errors: "); Log_llu(err_send_msg);
					cls();
					printf_s("\rsend request failed: %ld\n", WSAGetLastError());
				}
				else{
					if (show_updates) printf_s("\nSent: \n%s\n", buffer);
					Log("\n*Sent to server: "); Log_server(buffer);

					// read some bytes
					all_rcv_msg++;
					if (all_rcv_msg >= 1000) all_rcv_msg = 0;

					memset(buffer, 0, 1000);
					unsigned resp = 0;
					do{
						memset(tmp_buffer, 0, 1000);
						iResult = recv(UpdaterSocket, tmp_buffer, 999, 0);
						strcat(buffer, tmp_buffer);
						resp++;
					} while ((iResult > 0) && !use_fast_rcv);

					if (iResult == SOCKET_ERROR)
					{
						err_rcv_msg++;
						Log("\n*! Error response: "); Log(DisplayErrorText(WSAGetLastError())); Log("Errors: "); Log_llu(err_rcv_msg);
						cls();
						printf_s("\rget mining info failed: %ld\n", WSAGetLastError());
					}
					else {
						Log("\n*Received from server: "); Log_server(buffer); Log("\n*Count packets of response: "); Log_u(resp);
						if (show_updates)  printf_s("\nReceived: \n%s\n", buffer);

						// locate HTTP header
						char *find = strstr(buffer, "\r\n\r\n");
						if (find == NULL)
						{
							Log("\n*! error in message from pool\n");
						}
						else {
							char *rbaseTarget = strstr(buffer, "\"baseTarget\":");
							char *rheight = strstr(buffer, "\"height\":");
							char *generationSignature = strstr(buffer, "\"generationSignature\":");
							if (generationSignature != NULL) generationSignature = strstr(generationSignature + strlen("\"generationSignature\":"), "\"") + 1; //+1 убираем начальные ковычки
							char* rtargetDeadline = strstr(buffer, "\"targetDeadline\"");

							if (rbaseTarget == NULL || rheight == NULL || generationSignature == NULL)
							{
								Log("\n*! error parsing (1) message from server");
							}
							else {
								rbaseTarget = strpbrk(rbaseTarget, "0123456789");
								char *endBaseTarget = strpbrk(rbaseTarget, "\"");
								rheight = strpbrk(rheight, "0123456789");
								char *endHeight = strpbrk(rheight, "\"");
								char *endGenerationSignature = strstr(generationSignature, "\"");

								if (endBaseTarget == NULL || endHeight == NULL || endGenerationSignature == NULL){
									Log("\n*! error parsing (2) message from server");
								}
								else {
									// Set endpoints
									endBaseTarget[0] = 0;
									endHeight[0] = 0;
									endGenerationSignature[0] = 0;

									if (rtargetDeadline != NULL) 
									{
										rtargetDeadline = strpbrk(rtargetDeadline, "0123456789");
										char* endBaseTarget = strpbrk(rtargetDeadline, "\"");
										if (endBaseTarget == NULL) endBaseTarget = strpbrk(rtargetDeadline, "}");
										endBaseTarget[0] = 0;

										targetDeadlineInfo = strtoull(rtargetDeadline, 0, 10);
										Log("\ntargetDeadlineInfo: "), Log_llu(targetDeadlineInfo);
									}

									unsigned long long  heightInfo = strtoull(rheight, 0, 10);
									//if (heightInfo > height)
									{
										height = heightInfo;
										baseTarget = strtoull(rbaseTarget, 0, 10);
										// Parse
										strcpy(str_signature, generationSignature);
										//str_signature[65] = 0;
										if (xstr2strr(signature, 33, generationSignature) < 0) 
											Log("\n*! Node response: Error decoding generationsignature\n");
									}
								}
							}
						}
					}
				}
			}
			//printf("\rblock: %llu  BT: %llu   %s\n", height, baseTarget, signature);
			iResult = closesocket(UpdaterSocket);
		}
		freeaddrinfo(result);
	}
	pthread_mutex_lock(&byteLock);
	can_connect = 1;
	pthread_mutex_unlock(&byteLock);
	//Log("\n*End update");
	
	free(buffer);
	free(tmp_buffer);
}


void *updater_i(void * path) {
	do{
		if ((can_connect == 1) && (strlen(updaterip) > 3) && (updaterport > 0)) pollLocal();
		//Log("\nСпим -");
		Sleep(update_interval);
		//Log(" Проснулись");
	} while (true);
	Log("\n ?");
	return path;
}
 

char * hostname_to_ip(char * hostname)
{
    struct hostent *remoteHost = NULL;
    struct in_addr addr;
    int i;
	DWORD dwError;
	char str[50];
	memset(str, 0, 50);

    if ( (remoteHost = gethostbyname( hostname ) ) == NULL)
    {
        // get the host info
        printf_s("\n ERROR gethostbyname %s\n", hostname);
        dwError = WSAGetLastError();
        if (dwError != 0) {
            if (dwError == WSAHOST_NOT_FOUND) {
                printf_s("Host not found\n");
                return NULL;
            } else if (dwError == WSANO_DATA) {
                printf_s("No data record found\n");
                return NULL;
            } else {
                printf_s("Function failed with error: %ld\n", dwError);
                return NULL;
            }
        }
		return NULL;
    }
	
    i = 0;
    if (remoteHost->h_addrtype == AF_INET)
    {
            while (remoteHost->h_addr_list[i] != 0) {
                addr.s_addr = *(u_long *) remoteHost->h_addr_list[i++];
				std::strcpy(str, inet_ntoa(addr));
				Log("\nAddress: "); Log(hostname); Log(" defined as: "); Log(str);
            }
    }
    else if (remoteHost->h_addrtype == AF_NETBIOS) printf_s("NETBIOS address was returned\n");  
    
	return  &str[0];
}


void GetCPUInfo(void)
{
		bool bIsSSE4Available = InstructionSet::SSE42();
		bool bIsAVX1Available = InstructionSet::AVX();
		bool bIsAVX2Available = InstructionSet::AVX2();
		bool bIsAESAvailable = InstructionSet::AES();
		ULONGLONG  TotalMemoryInKilobytes = 0;
		SetConsoleTextAttribute(hConsole, 15);
		printf_s("\rCPU support: ");
		if (bIsAESAvailable)    SetConsoleTextAttribute(hConsole, 0xA0);
		else SetConsoleTextAttribute(hConsole, 0xC0);
		printf_s(" AES ");
		if (bIsSSE4Available)    SetConsoleTextAttribute(hConsole, 0xA0);
		else SetConsoleTextAttribute(hConsole, 0xC0);
		printf_s(" SSE4 ");
		if (bIsAVX1Available)    SetConsoleTextAttribute(hConsole, 0xA0);
		else SetConsoleTextAttribute(hConsole, 0xC0);
		printf_s(" AVX ");
		if (bIsAVX2Available)    SetConsoleTextAttribute(hConsole, 0xA0);
		else SetConsoleTextAttribute(hConsole, 0xC0);
		printf_s(" AVX2 ");
		SetConsoleTextAttribute(hConsole, 7);
		
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		printf("\n%s", InstructionSet::Vendor().c_str());
		printf(" %s  [%u cores]", InstructionSet::Brand().c_str(), sysinfo.dwNumberOfProcessors);

		if (GetPhysicallyInstalledSystemMemory(&TotalMemoryInKilobytes))
			printf_s("\nRAM: %llu Mb", (unsigned long long)TotalMemoryInKilobytes / 1024);
		
		printf_s("\n");
}


int ClearMem(void)
{
	
		HANDLE hProcess = GetCurrentProcess();
		HANDLE hToken;
		if (!OpenProcessToken(hProcess, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
		{
			fprintf(stderr, "\nCan't open current process token\n");
			return 0;
		}

		if (!GetPrivilege(hToken, "SeProfileSingleProcessPrivilege", 1))
		{
			fprintf(stderr, "\nCan't get SeProfileSingleProcessPrivilege\n");
			return 0;
		}

		CloseHandle(hToken);

		HMODULE ntdll = LoadLibrary(L"ntdll.dll");
		if (!ntdll)
		{
			fprintf(stderr, "\nCan't load ntdll.dll, wrong Windows version?\n");
			return 0;
		}

		typedef DWORD NTSTATUS; // ?
		NTSTATUS(WINAPI *NtSetSystemInformation)(INT, PVOID, ULONG) = (NTSTATUS(WINAPI *)(INT, PVOID, ULONG))GetProcAddress(ntdll, "NtSetSystemInformation");
		NTSTATUS(WINAPI *NtQuerySystemInformation)(INT, PVOID, ULONG, PULONG) = (NTSTATUS(WINAPI *)(INT, PVOID, ULONG, PULONG))GetProcAddress(ntdll, "NtQuerySystemInformation");
		if (!NtSetSystemInformation || !NtQuerySystemInformation)
		{
			fprintf(stderr, "\nCan't get function addresses, wrong Windows version?\n");
			return 0;
		}

		SYSTEM_MEMORY_LIST_COMMAND command = MemoryPurgeStandbyList;
		NTSTATUS status = NtSetSystemInformation(
			SystemMemoryListInformation,
			&command,
			sizeof(SYSTEM_MEMORY_LIST_COMMAND)
			);
		if (status == STATUS_PRIVILEGE_NOT_HELD)
		{
			fprintf(stderr, "\nInsufficient privileges to execute the memory list command");
		}
		else if (!NT_SUCCESS(status))
		{
			fprintf(stderr, "\nUnable to execute the memory list command %s", status);
		}
		return NT_SUCCESS(status);
	
}



int main(int argc, char **argv) {

		pthread_t worker[MaxTreads];
		pthread_t sender;
		pthread_t proxy;
		pthread_t updater;
		unsigned i = 0;
		char tbuffer[9];
		bool cleared = false;

		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		

		SetConsoleTextAttribute(hConsole, 11);
		printf_s("\nBURST miner, %s\nProgramming: dcct (Linux) & Blago (Windows) & modded by d34th (Win64)\n", version);
		SetConsoleTextAttribute(hConsole, 7);

		// path to miner
		
		DWORD cwdsz = GetCurrentDirectoryA(0, 0);
		p_minerPath = (char*)malloc(cwdsz);
		GetCurrentDirectoryA(cwdsz, LPSTR(p_minerPath));
		strcat(p_minerPath, "\\");
		
		
		char* conf_filename = (char*)malloc(255);;
		memset(conf_filename, 0, sizeof(conf_filename));
		if((argc >= 2) && (strcmp(argv[1], "-config")==0)){
			if(strstr(argv[2], ":\\")) sprintf(conf_filename,"%s", argv[2]);
			else sprintf(conf_filename,"%s%s", p_minerPath, argv[2]);
		}
		else sprintf(conf_filename,"%s%s", p_minerPath, "miner.conf");

		load_config(conf_filename);
		free(conf_filename);
		Log("\nMiner path: "); Log(p_minerPath);

		GetCPUInfo();

		if(miner_mode == 0) pass = GetPass(p_minerPath);

		// адрес и порт сервера
		Log("\nSearching servers...");
		WSADATA wsaData;
		
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			printf_s("WSAStartup failed\n");
			exit(-1);
		}
		
		memset(updaterip, 0, 50);
		memset(nodeip, 0, 50);
		if (strlen(updateraddr) > 3) strcpy(updaterip, hostname_to_ip(updateraddr));
		SetConsoleTextAttribute(hConsole, 5);
		printf_s("Updater address: %s (ip: %s)\n", updateraddr, updaterip);

		strcpy(nodeip, hostname_to_ip(nodeaddr));
		printf_s("Pool address: %s (ip: %s)\n", nodeaddr, nodeip);
		SetConsoleTextAttribute(hConsole, 7);


		
        // обнуляем сигнатуру
		memset(oldSignature, 0, 33);
		memset(signature, 0, 33);
 
 		// Инфа по файлам
		SetConsoleTextAttribute(hConsole, 15);
		printf_s("Using plots:\n");
		SetConsoleTextAttribute(hConsole, 7);
		total_size = 0;
		for (i = 0; i < paths_num; i++)
		{
			t_files *files = (t_files*)malloc(sizeof(t_files)* MAX_FILES);
			int count = GetFiles(paths_dir[i], files);
			unsigned long long tot_size = 0;
			for (int j = 0; j < count; j++) 	tot_size += files[j].Size;
			SetConsoleTextAttribute(hConsole, 7);
			printf_s("%s\tfiles: %u\t size: %llu Gb\n", paths_dir[i], count, tot_size / 1024 / 1024 / 1024);
			total_size += tot_size;
			free(files);
		}
		SetConsoleTextAttribute(hConsole, 15);
		printf_s("TOTAL: %llu Gb", total_size/1024/1024/1024);
		SetConsoleTextAttribute(hConsole, 7);

		// Run Proxy
		if (enable_proxy)
		{
			if (pthread_create(&proxy, NULL, proxy_i, p_minerPath))
			{
				SetConsoleTextAttribute(hConsole, 12);
				printf_s("\nError creating thread. Out of memory?\n");
				SetConsoleTextAttribute(hConsole, 7);
				exit(-1);
			}
			SetConsoleTextAttribute(hConsole, 0x9F);
			printf_s("\nProxy thread started\n");
			SetConsoleTextAttribute(hConsole, 7);
		}

		Log("\nUpdate mining info");
		
        // Run updater;
		if (pthread_create(&updater, NULL, updater_i, p_minerPath))
		{
			SetConsoleTextAttribute(hConsole, 12);
			printf_s("\nError creating thread. Out of memory?\n");
			SetConsoleTextAttribute(hConsole, 7);
			exit(-1);
		}
		Log("\nUpdater thread started");
		
		do{ ; } while (height == 0);
		
		
		// Run Sender
		if(pthread_create(&sender, NULL, send_i, p_minerPath)) 
		{
			SetConsoleTextAttribute(hConsole, 12);
			printf_s("\nError creating thread. Out of memory?\n");
			SetConsoleTextAttribute(hConsole, 7);
            exit(-1);
        }
		
		// Main loop
		for(;;) {
	
				working_threads = 0;

                // Get scoop:
			    Log("\nCalculate scoop (Shabal) for \"New block\"");
                char scoopgen[40];
                memmove(scoopgen, signature, 32);
 
                char *mov = (char*)&height;
 
                scoopgen[32] = mov[7]; scoopgen[33] = mov[6]; scoopgen[34] = mov[5]; scoopgen[35] = mov[4]; scoopgen[36] = mov[3]; scoopgen[37] = mov[2]; scoopgen[38] = mov[1]; scoopgen[39] = mov[0];

				sph_shabal_context x;
				shabal_init(&x, 256);
                shabal_core(&x, (const unsigned char*)scoopgen, 40);
				char xcache[32];
                shabal_close(&x, 0, 0, xcache, 8);
				
                scoop = (((unsigned char)xcache[31]) + 256 * (unsigned char)xcache[30]) % 4096;
 
 				deadline = 0;
				bytesRead = 0;
 
				Log("\n------------------------    New block: "); Log_llu(height);

				_strtime(tbuffer);
				SetConsoleTextAttribute(hConsole, 13);
				printf_s("\n\n--- %s ---    New block %llu, basetarget %llu    ------------\n", tbuffer, height, baseTarget);
				SetConsoleTextAttribute(hConsole, 7);
				if (miner_mode == 0) printf_s("*** Chance to find a block: %.5f%%\n", ((double)(total_size / 1024 / 1024 * 100 * 60)*(double)baseTarget) / 1152921504606846976);
				if (num_shares > sent_num_shares) printf_s("\rshares lost due to new block\n");
				for (i = 0; i < sessions.size(); i++) closesocket(sessions.at(i).Socket);
				sessions.clear();
				num_shares = 0;  // все шары, которые неуспели отправить сгорают
				sent_num_shares = 0;
				memset(&shares[0], 0, sizeof(t_shares)* 100000 * MaxTreads); // Обнуляем буфер шар
				memset(&bests[0], 0, sizeof(t_best)*MaxAccounts); // Обнуляем буфер шар


				//unsigned long long targetDeadline;
				if ((targetDeadlineInfo > 0) && (targetDeadlineInfo < my_target_deadline)){
					Log("\nUpdate targetDeadline: "); Log_llu(targetDeadlineInfo);
				}
				else {
					targetDeadlineInfo = my_target_deadline;
					//Log("\ntargetDeadline not found, targetDeadline: "); Log_llu(targetDeadlineInfo);
				}
				for(unsigned a=0; a < MaxAccounts; a++)
					bests[a].targetDeadline = targetDeadlineInfo;
				

				for(i = 0; i < paths_num; i++) 
				{
					if(pthread_create(&worker[i], NULL, work_i, &threads[i])) 
					{
						printf_s("\nError creating thread. Out of memory? Try lower stagger size\n");
						Log("\n! Error thread starting: ");	Log_u(i);
                        exit(-1);
                    }
					working_threads++;
				}


                memmove(oldSignature, signature, 32);
				unsigned long long old_baseTarget = baseTarget;
				unsigned long long old_height = height;
 
                // Wait until block changes:
                do {
                        cls();
                        SetConsoleTextAttribute(hConsole, 14);
						if(deadline == 0) printf_s("\r[%llu%%] %llu GB. no deadline\tsdl:%llu(%llu) cdl:%llu(%llu) ss:%llu(%llu) rs:%llu(%llu)", (bytesRead*4096*100 / total_size), (bytesRead / (256 * 1024)), all_send_dl, err_send_dl, all_rcv_dl, err_rcv_dl, all_send_msg, err_send_msg, all_rcv_msg, err_rcv_msg);
                        else              printf_s("\r[%llu%%] %llu GB. DL: %llus\tsdl:%llu(%llu) cdl:%llu(%llu) ss:%llu(%llu) rs:%llu(%llu)", (bytesRead*4096*100 / total_size), (bytesRead / (256 * 1024)), deadline, all_send_dl, err_send_dl, all_rcv_dl, err_rcv_dl, all_send_msg, err_send_msg, all_rcv_msg, err_rcv_msg);
						//printf_s("\r[%llu%%] %llu GB. tDL[%llu][%llu] sdl:%llu/%llu(%llu) cdl:%llu(%llu) ss:%llu(%llu) rs:%llu(%llu)", (bytesRead*4096*100 / total_size), (bytesRead / (256 * 1024)), bests[0].targetDeadline, bests[1].targetDeadline, all_send_dl, num_shares-sent_num_shares, err_send_dl, all_rcv_dl, err_rcv_dl, all_send_msg, err_send_msg, all_rcv_msg, err_rcv_msg);
						SetConsoleTextAttribute(hConsole, 7);
                        Sleep(500);
						if ((working_threads == 0) && (cleared == false))
						{
							ClearMem();
							cleared = true;
						}
                } while(memcmp(signature, oldSignature, 32) == 0);      // Wait until signature changed
						
				cleared = false;

                // Tell all threads to stop:
                stopThreads = 1;
				for(i = 0; i < paths_num; i++)
				{
					Log("\nFlow Inturruption: ");	Log_u(i);
					if( pthread_join(worker[i], NULL) > 0 )
					//if (pthread_cancel(worker[i]) > 0)
					{
						cls();
						SetConsoleTextAttribute(hConsole, 12);
						printf_s("\r Error stoping thread #%u of %u", i, paths_num);
						SetConsoleTextAttribute(hConsole, 7);
						Log("\n! Error thread stoping: ");	Log_u(i);
					}
				}
				Log("\nWriting to stat-log.csv ");
				fopen_s(&fp_Stat, "stat-log.csv", "at+");
				if (fp_Stat != NULL)
				{
					for (int a = 0; a < MaxAccounts; a++) if (bests[a].account_id != 0) fprintf_s(fp_Stat, "%llu,%llu,%llu,%llu\n", bests[a].account_id, old_height, old_baseTarget, bests[a].DL);
					fclose(fp_Stat);
					Log("\nWrote stat-log.csv ");
				}
			
				stopThreads = 0;
        }
		pthread_join(sender, NULL);
		free(p_minerPath);
		WSACleanup();
}
