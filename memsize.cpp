#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <bitset>
#include <stdexcept>
#include <expat.h>
#include <pthread.h>

using namespace std;

#define BUFFSZ 512

static char outputfile[BUFFSZ];
static pthread_mutex_t countLock = PTHREAD_MUTEX_INITIALIZER;
static map<long, bitset<4096> > overallCount;
static map<long, long> touched;
static map<long, bitset<4096> > overallMemory;
static map<long, long> memoryTouched;
static map<long, bitset<4096> > overallCode;
static map<long, long> codeTouched;

//use this class to pass data to threads and parser
class SetPointers
{
	public:
	map<long, bitset<4096> >* lCount;
	map<long, long>* lTouched;
	map<long, bitset<4096> >* lMemory;
	map<long, long>* lMemoryTouched;
	map<long, bitset<4096> >* lCode;
	map<long, long>* lCodeTouched;
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
		bool modify = (strcmp(name, "modify") == 0);
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
		map<long, long>::iterator itTouched;
		itLocal = sets->lCount->find(page);
		if (itLocal == sets->lCount->end()) {
			sets->lCount->insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			sets->lTouched->insert(pair<long, long>(page, 0));
			itLocal = sets->lCount->find(page);
		}
		try {
			itTouched = sets->lTouched->find(page);
			itTouched->second++;
			if (modify) {
				itTouched->second++;
			}
			for (i = 0; i < size; i++) {
				(itLocal->second).set(i + offset);
			}
		}
		catch (const out_of_range& oor) {
			long nextPage = page + 1;
			itLocal = sets->lCount->find(nextPage);
			if (itLocal == sets->lCount->end()) {
				sets->lCount->insert(pair<long, bitset<4096> >
					(nextPage, bitset<4096>()));
				sets->lTouched->insert(pair<long, long>
					(nextPage, 0));
				itLocal = sets->lCount->find(nextPage);
			}
			itTouched = sets->lTouched->find(nextPage);
			itTouched->second++;
			if (modify) {
				itTouched->second++;
			}
			for (;i < size; i++) {
				(itLocal->second).set(i);
			}
		}
			
		if (strcmp(name, "instruction") == 0) {
			itLocal = sets->lCode->find(page);
			if (itLocal == sets->lCode->end()) {
				sets->lCode->insert(pair<long, bitset<4096> >
					(page, bitset<4096>()));
				sets->lCodeTouched->insert(pair<long, long>
					(page, 0));
				itLocal = sets->lCode->find(page);
			}

			try {
				itTouched = sets->lCodeTouched->find(page);
				itTouched->second++;
				for (i = 0; i < size; i++) {
					(itLocal->second).set(i + offset);
				}
			}
			catch (const out_of_range& oor) {
				long nextPage = page + 1;
				itLocal = sets->lCode->find(nextPage);
				if (itLocal == sets->lCode->end()) {
					sets->lCode->insert(
						pair<long, bitset<4096> >
						(nextPage, bitset<4096>()));
					sets->lCodeTouched->insert(
						pair<long, long>
						(nextPage, 0));
					itLocal = sets->lCode->find(nextPage);
				}
				itTouched = sets->lCodeTouched->find(nextPage);
				itTouched->second++;
				for (;i < size; i++) {
					(itLocal->second).set(i);
				}
			}

		} else {
			itLocal = sets->lMemory->find(page);
			if (itLocal == sets->lMemory->end()) {
				sets->lMemory->insert(
					pair<long, bitset<4096> >
					(page, bitset<4096>()));
				sets->lMemoryTouched->insert(
					pair<long, long>
					(page, 0));
				itLocal = sets->lMemory->find(page);
			}
			try {
				itTouched = sets->lMemoryTouched->find(page);
				itTouched->second++;
				if (modify) {
					itTouched->second++;
				}
				for (i = 0; i < size; i++) {
					(itLocal->second).set(i + offset);
				}
			}
			catch (const out_of_range& oor) {
				long nextPage = page + 1;
				itLocal = sets->lMemory->find(nextPage);
				if (itLocal == sets->lMemory->end()) {
					sets->lMemory->insert(
						pair<long, bitset<4096> >
						(nextPage, bitset<4096>()));
					sets->lMemoryTouched->insert(
						pair<long, long>
						(nextPage, 0));
					itLocal = sets->
						lMemory->find(nextPage);
				}
				itTouched = sets->lMemoryTouched->
					find(nextPage);
				itTouched->second++;
				if (modify) {
					itTouched->second++;
				}
				for (;i < size; i++) {
					(itLocal->second).set(i);
				}
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
	map<long, long>::iterator itLocalTouch;
	map<long, bitset<4096> >::iterator itGlobal;
	map<long, long>::iterator itGlobalTouch;

	for (itLocal = threadSets->lCount->begin();
		itLocal != threadSets->lCount->end(); itLocal++) {
		long page = itLocal->first;
		itGlobal = overallCount.find(page);
		if (itGlobal == overallCount.end()){
			overallCount.insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			touched.insert(pair<long, long>(page, 0));
			itGlobal = overallCount.find(page);
		}
		itLocalTouch = threadSets->lTouched->find(page);
		itGlobalTouch = touched.find(page);
		itGlobalTouch->second += itLocalTouch->second;
		itGlobal->second |= itLocal->second;
	}
	
	for (itLocal = threadSets->lMemory->begin();
		itLocal != threadSets->lMemory->end(); itLocal++) {
		long page = itLocal->first;
		itGlobal = overallMemory.find(page);
		if (itGlobal == overallMemory.end()){
			overallMemory.insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			memoryTouched.insert(pair<long, long>(page, 0));
			itGlobal = overallMemory.find(page);
		}
		itLocalTouch = threadSets->lMemoryTouched->find(page);
		itGlobalTouch = memoryTouched.find(page);
		itGlobalTouch->second += itLocalTouch->second;
		itGlobal->second |= itLocal->second;
	}

	for (itLocal = threadSets->lCode->begin();
		itLocal != threadSets->lCode->end(); itLocal++) {
		long page = itLocal->first;
		itGlobal = overallCode.find(page);
		if (itGlobal == overallCode.end()){
			overallCode.insert(pair<long, bitset<4096> >
				(page, bitset<4096>()));
			codeTouched.insert(pair<long, long>(page, 0));
			itGlobal = overallCode.find(page);
		}
		itLocalTouch = threadSets->lCodeTouched->find(page);
		itGlobalTouch = codeTouched.find(page);
		itGlobalTouch->second += itLocalTouch->second;
		itGlobal->second |= itLocal->second;
	}
	cout << "Thread memory mapping complete.\n";
	pthread_mutex_unlock(&countLock);
	delete threadSets->lCount;
	delete threadSets->lTouched;
	delete threadSets->lMemory;
	delete threadSets->lMemoryTouched;
	delete threadSets->lCode;
	delete threadSets->lCodeTouched;
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
	threadSets->lTouched = new map<long, long>();
	threadSets->lMemoryTouched = new map<long, long>();
	threadSets->lCodeTouched = new map<long, long>();
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
	map<long, long>::iterator itTouched;
	ofstream overallFile;
	ofstream memoryFile;
	ofstream codeFile;
	cout << "Writing results to " << argv[2] << "\n";
	overallFile.open(argv[2]);
	for (it = overallCount.begin(); it != overallCount.end(); it++)
	{
		itTouched = touched.find(it->first);
		overallFile << it->first << "," << it->second.count() << ",";
		overallFile << itTouched->second << "\n";
	}
	overallFile.close();

	memoryFile.open("/usr/userfs/a/acm538/memmap.txt");
	for (it = overallMemory.begin(); it != overallMemory.end(); it++)
	{
		memoryFile << it->first << "," << it->second << "\n";
	}
	memoryFile.close();

	codeFile.open("/usr/userfs/a/acm538/codemap.txt");
	for (it = overallCode.begin(); it != overallCode.end(); it++)
	{
		codeFile << it->first << "," << it->second.count() << "\n";
	}
	codeFile.close();

	cout << "Program completed \n";
	

}

