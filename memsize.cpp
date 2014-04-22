#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <bitset>
#include <expat.h>
#include <pthread.h>

using namespace std;

#define BUFFSZ 512

static char outputfile[BUFFSZ];
static pthread_mutex_t countLock = PTHREAD_MUTEX_INITIALIZER;
static map<long, bitset<4096> > overallCount;
static map<long, bitset<4096> > overallMemory;
static map<long, bitset<4096> > overallCode;

//use this class to pass data to threads and parser
class SetPointers
{
	public:
	map<long, bitset<4096> >* lCount;
	map<long, bitset<4096> >* lMemory;
	map<long, bitset<4096> >* lCode;
	char* threadPath;
	int threadID;
};

void usage()
{
	cout << "USAGE:\n";
	cout << "pagestats [control file] [output file]\n";
}

static void XMLCALL
hackHandler(void *data, const XML_Char *name, const XML_Char **attr)
{
	SetPointers* sets = static_cast<SetPointers*>(data);
	if (strcmp(name, "instruction") == 0 || strcmp(name, "load") == 0 ||
		strcmp(name, "modify") == 0||strcmp(name, "store") == 0) {
		long address(0);
		long page(0);
		int offset(0);
		long size(0);
		int i(0);
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "address") == 0) {
				address = strtol(attr[i+1], NULL, 16);
				page = address >> 12;
				offset = address & 0xFFF;
				continue;
			}
			if (strcmp(attr[i], "size") == 0) {
				size = strtol(attr[i + 1], NULL, 16);
			}
		}
		map<long, bitset<4096> >::iterator itLocal;

		itLocal = sets->lCount->find(page);
		if (itLocal == sets->lCount->end()) {
			sets->lCount->insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			itLocal = sets->lCount->find(page);
		}
		//now mark the bitmap
		for (i = 0; i < size; i++) {
			(itLocal->second)[i + offset] = 1;
		}
			
		if (strcmp(name, "instruction") == 0) {
			itLocal = sets->lCode->find(page);
			if (itLocal == sets->lCode->end()) {
				sets->lCode->insert(pair<long, bitset<4096> >
					(page, bitset<4096>()));
				itLocal = sets->lCode->find(page);
			}
			for (i = 0; i < size; i++) {
				(itLocal->second)[i + offset] = 1;
			}
		} else {
			itLocal = sets->lMemory->find(page);
			if (itLocal == sets->lMemory->end()) {
				sets->lMemory->insert(pair<long, bitset<4096> >
					(page, bitset<4096>()));
				itLocal = sets->lMemory->find(page);
			}
			for (i = 0; i < size; i++) {
				(itLocal->second)[i + offset] = 1;
			}
		}
	}
}

static void* hackMemory(void* tSets)
{
	//parse the file
	size_t len = 0;
	bool done;
	char data[BUFFSZ];
	SetPointers* threadSets = (SetPointers*) tSets; 
	XML_Parser parser_Thread = XML_ParserCreate("UTF-8");
	if (!parser_Thread) {
		cerr << "Could not create thread parser\n";
		return NULL;
	}
	XML_SetUserData(parser_Thread, tSets);
	XML_SetStartElementHandler(parser_Thread, hackHandler);
	FILE* threadXML = fopen(threadSets->threadPath, "r");
	if (threadXML == NULL) {
		cerr << "Could not open " << threadSets->threadPath << "\n";
		XML_ParserFree(parser_Thread);
		return NULL;
	}

	do {
		len = fread(data, 1, sizeof(data), threadXML);
		done = len < sizeof(data);
		if (XML_Parse(parser_Thread, data, len, 0) == 0) {
			enum XML_Error errcde = XML_GetErrorCode(parser_Thread);
			printf("ERROR: %s\n", XML_ErrorString(errcde));
			printf("Error at column number %lu\n",
				XML_GetCurrentColumnNumber(parser_Thread));
			printf("Error at line number %lu\n",
				XML_GetCurrentLineNumber(parser_Thread));
			return NULL;
		}
	} while(!done);

	pthread_mutex_lock(&countLock);
	cout << "Thread handled \n";
	map<long, bitset<4096> >::iterator itLocal;
	map<long, bitset<4096> >::iterator itGlobal;

	for (itLocal = threadSets->lCount->begin();
		itLocal != threadSets->lCount->end(); itLocal++) {
		long page = itLocal->first;
		itGlobal = overallCount.find(page);
		if (itGlobal == overallCount.end()){
			overallCount.insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			itGlobal = overallCount.find(page);
		}
		itGlobal->second |= itLocal->second;
	}
	
	for (itLocal = threadSets->lMemory->begin();
		itLocal != threadSets->lMemory->end(); itLocal++) {
		long page = itLocal->first;
		itGlobal = overallMemory.find(page);
		if (itGlobal == overallMemory.end()){
			overallMemory.insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			itGlobal = overallMemory.find(page);
		}
		itGlobal->second |= itLocal->second;
	}

	for (itLocal = threadSets->lCode->begin();
		itLocal != threadSets->lCode->end(); itLocal++) {
		long page = itLocal->first;
		itGlobal = overallCode.find(page);
		if (itGlobal == overallCode.end()){
			overallCode.insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			itGlobal = overallCode.find(page);
		}
		itGlobal->second |= itLocal->second;
	}
	cout << "Thread memory mapping complete.\n";
	pthread_mutex_unlock(&countLock);
	threadSets->lCount->clear();
	delete threadSets->lCount;
	threadSets->lMemory->clear();
	delete threadSets->lMemory;
	threadSets->lCode->clear();
	delete threadSets->lCode;
	return NULL;
}



pthread_t* 
countThread(int threadID, char* threadPath)
{
	cout << "Handling thread " << threadID << "\n";
	//parse each file in parallel
	SetPointers* threadSets = new SetPointers();
	threadSets->lCount = new map<long, bitset<4096> >();
	threadSets->lMemory = new map<long, bitset<4096> >();
	threadSets->lCode = new map<long, bitset<4096> >();
	threadSets->threadPath = threadPath;
	threadSets->threadID = threadID;
	
	pthread_t* aThread = new pthread_t();
	
	pthread_create(aThread, NULL, hackMemory, (void*)threadSets);
	return aThread;
	
}

void joinup(pthread_t* t)
{
	pthread_join(*t, NULL);
}

void killoff(pthread_t* t)
{
	delete t;
}

static void XMLCALL
fileHandler(void *data, const XML_Char *name, const XML_Char **attr)
{

	vector<pthread_t*>* pThreads = static_cast<vector<pthread_t*>*>(data);
	
	int i;
	int threadID = 0;
	char* threadPath = NULL; 
	if (strcmp(name, "file") == 0) {
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "thread") == 0) {
				threadID = atoi(attr[i + 1]);
				break;
			}
		}
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "path") == 0) {
				threadPath = new char[BUFFSZ];		
				strcpy(threadPath, attr[i + 1]);
				break;
			}
		}
		pThreads->push_back(countThread(threadID, threadPath));
	}
}

int main(int argc, char* argv[])
{
	FILE* inXML;
	char data[BUFFSZ]; 
	size_t len = 0;
	int done;
	vector<pthread_t*> threads;

	if (argc < 3) {
		usage();
		exit(-1);
	}

	strcpy(outputfile, argv[2]);

	XML_Parser p_ctrl = XML_ParserCreate("UTF-8");
	if (!p_ctrl) {
		fprintf(stderr, "Could not create parser\n");
		exit(-1);
	}
	

	XML_SetUserData(p_ctrl, &threads);
	XML_SetStartElementHandler(p_ctrl, fileHandler);
	inXML = fopen(argv[1], "r");
	if (inXML == NULL) {
		fprintf(stderr, "Could not open %s\n", argv[1]);
		XML_ParserFree(p_ctrl);
		exit(-1);
	}


	cout << "Memsize: how much of pages are being touched\n";
	cout << "Copyright (c), Adrian McMenamin, 2014 \n";
	cout << "See https://github.com/mcmenaminadrian for licence details.\n";
	do {
		len = fread(data, 1, sizeof(data), inXML);
		done = len < sizeof(data);

		if (XML_Parse(p_ctrl, data, len, 0) == 0) {
			enum XML_Error errcde = XML_GetErrorCode(p_ctrl);
			printf("ERROR: %s\n", XML_ErrorString(errcde));
			printf("Error at column number %lu\n",
				XML_GetCurrentColumnNumber(p_ctrl));
			printf("Error at line number %lu\n",
				XML_GetCurrentLineNumber(p_ctrl));
			exit(-1);
		}
	} while(!done);
	for_each(threads.begin(), threads.end(), joinup);
	for_each(threads.begin(), threads.end(), killoff);
	
	map<long, bitset<4096> >::iterator it;
	ofstream overallFile;
	ofstream memoryFile;
	ofstream codeFile;
	cout << "Writing results to " << argv[2] << "\n";
	overallFile.open(argv[2]);
	for (it = overallCount.begin(); it != overallCount.end(); it++)
	{
		overallFile << it->first << "," << it->second << "\n";
	}
	overallFile.close();

	memoryFile.open("~/memmap.txt");
	for (it = overallMemory.begin(); it != overallMemory.end(); it++)
	{
		memoryFile << it->first << "," << it->second << "\n";
	}
	memoryFile.close();

	codeFile.open("~/codemap.txt");
	for (it = overallCode.begin(); it != overallCode.end(); it++)
	{
		codeFile << it->first << "," << it->second << "\n";
	}
	codeFile.close();

	cout << "Program completed \n";
	

}

