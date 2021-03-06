#include "main.h"

// Please keep these as global variables, we need them as variables in order to send them!
const int PSW_FOUND = 0;
const int KEY_PRESSED = 1;


// HINTS for compiling (with required links to static libraries):
// mpicc -o main.out main.c -pthread -lcrypt -lcrypto

// HINTS for running on lan:
// mpirun -np N ./main.out input-handler/passwd.txt
// mpirun -np 12 -hosts <master_ip>,<slave_1_ip>,...,<slave_m_ip> ./main.out input-handler/passwd.txt

bool incremental_flag = false;
int incremental_min_len = -1;
int incremental_max_len = -1;
bool rule_flag = false;
int add_n = -1;
bool single_flag = false;
bool out_file_flag = false;
char* dict_path = NULL;
char* output_file_path = NULL;
char* input_file_path = NULL;
Range* ranges = NULL;
int rangesLen = 0;
CrackingStatus crackingStatus = {0,0,0,NULL};
PasswordList *passGuessed = NULL;
PasswordList* passwordList = NULL;

int main(int argc, char const *argv[]) {

    ThreadData *data = initData();

    if(handleUserOptions(argc, argv, data)){
        MPI_Finalize();
        free(data);
        if(out_file_flag){
            free(output_file_path);
        }
        return 0;
    };

    crackingStatus.guess=data->worldRank;
    crackingStatus.try=data->worldRank;

    trace("Threads have been successfully started.\n", data->worldRank);

    // Run John the Ripper
    crackThemAll(data);
    
    // To prevent others threads to become zombie threads, join them with their master thread.
    pthread_join(data->threadId, NULL);
    if (data->worldRank==ROOT) {
        pthread_join(data->thread2Id, NULL);
    }

    // TODO: Before printing the msg bpasswordelow check if there is data to be saved into a file.
    if(out_file_flag){
        trace("\nYour results were stored in the output file... \n", data->worldRank);
        write_final_output(passGuessed, passwordList, output_file_path, data->worldRank, data->worldSize);
    }

    trace("\nExecution terminated correctly \n", data->worldRank);


    // Terminate MPI and hence the program
    MPI_Finalize();
    free(data);
    free(ranges);
    free(dict_path);
    if(out_file_flag){
        free(output_file_path);
    }

    return 0;
}

void *crackThemAll(ThreadData *data) {

    trace("Running JTR ... \n", data->worldRank);

    // printf("Cracking passwords in range [x,y] ...\n"); 
    
    // This variable can be removed, is here for simulating a job that finishes in 'count' seconds

    trace("You can stop the program at any time by pressing 'q'.", data->worldRank);

    passwordList = createStruct(input_file_path);

    //incremental mode 
    if(incremental_flag){

        char* res = NULL;
        int resLen; // is initialized by mapRangeIntoArray function
        res = mapRangeIntoArray(ranges,rangesLen,&resLen); //maps ranges into a linear array

        if(incremental_min_len==-1) incremental_min_len=1;
        if(incremental_max_len==-1) incremental_max_len=8; //if not provided = 8

        char* alphaWord = NULL;

        for(int l=incremental_min_len; l<=incremental_max_len;l++){
            int* word = calloc(sizeof(int),l); // this is the initial word of charset, initialized at 0 with calloc

            alphaWord = wordFromRange(word,alphaWord,res,l);


            int * chk;  // only used for check if incremental returns null, 
                        // we should use word but the word's content will be lost

            word[l-1] += data->worldRank;

            int counter = 0;

            char* digest = NULL;

            PasswordList* passwordListPointer = passwordList;
            while(passwordListPointer != NULL){
                counter++;
                digest = realloc(digest,(sizeof(char)*2*getDigestLen(passwordListPointer->obj->hashType)+1));
                digestFactory(alphaWord,passwordListPointer->obj->salt, passwordListPointer->obj->hashType, digest);

                if(strcmp(digest,passwordListPointer->obj->hash)==0) {
                    passwordFound((passwordListPointer->obj),counter,alphaWord,data,false);
                }
                passwordListPointer = passwordListPointer->next;
            }

            while (data->shouldCrack) {

                chk = parallel_incrementalNextWord(word,l,res,resLen,data->worldRank,data->worldSize);

                alphaWord = wordFromRange(word,alphaWord,res,l);
                crackingStatus.try++;
                crackingStatus.currentWord = alphaWord;

                if(chk == NULL){
                    break;
                }

                counter=0;

                passwordListPointer = passwordList;
                while(passwordListPointer != NULL){
                    counter++;
                    digest = realloc(digest,(sizeof(char)*2*getDigestLen(passwordListPointer->obj->hashType)+1));
                    digestFactory(alphaWord,passwordListPointer->obj->salt, passwordListPointer->obj->hashType, digest);

                    if(strcmp(digest,passwordListPointer->obj->hash)==0) {
                        passwordFound((passwordListPointer->obj),counter,alphaWord,data,true);
                    }
                    passwordListPointer = passwordListPointer->next;
                }

            }
            free(word);
            free(digest);
        }
        // sleep(1);
        free(alphaWord);
        free(res);

    }
    else if(rule_flag){ //dictionary mode (eventually) with rules
        DictList* dictList = importFileDict(dict_path);

        PasswordList* copyList;

        for (int g = 0; g < data->worldRank; g++) dictList = dictList->next;


        while(dictList != NULL){

            char* digest = NULL;

            int counter = 0;
            copyList = passwordList;

            while(copyList != NULL){
                RULES rule = NO_RULE;
                if(add_n!=-1) rule = ADD_N_NUMBERS;

                counter ++;
                
                if(dictWordCrack(
                    copyList->obj,
                    dictList->word,
                    copyList->obj->hashType,
                    rule,
                    ranges,
                    rangesLen,
                    add_n,
                    &crackingStatus
                )){
                    passwordFound((copyList->obj),counter,dictList->word,data,false);
                }

                copyList = copyList->next;
            }

            for (int g = 0; g < data->worldSize && dictList!=NULL; g++) dictList = dictList->next;

        }

        freeDict(dictList);
    }
    else if(single_flag){

        int counter = 0;

        PasswordList* passwordListPointer = passwordList;

        for (int g = 0; g < data->worldRank; g++) passwordListPointer = passwordListPointer->next;

        while(passwordListPointer != NULL){
            counter++;
            
            if(singleCrack((passwordListPointer->obj),passwordListPointer->obj->hashType,&crackingStatus)==true){
                passwordFound((passwordListPointer->obj),counter,passwordListPointer->obj->username,data,false);
            }

            for (int g = 0; g < data->worldSize && passwordListPointer!=NULL; g++) passwordListPointer = passwordListPointer->next;

        }

    }else{ //fake execution

        int count = 10;
        trace("This simulation will last approximately 10 secs.", data->worldRank);

        while (data->shouldCrack && count>0) {
            
            count--;
            sleep(1);

        }
    }

    // Once here, the work is ended and the other threads are no longer necessary.
    killThemAll(data);

    return NULL;
}

// reverse the mapping on the ranges into a string
char* wordFromRange(int word[], char* resultString, char map[], int wordlen){
    resultString = (char*)realloc(resultString,sizeof(char)*(wordlen+1));
    for (int i = 0; i < wordlen; i++){
        resultString[i] = map[word[i]];
    }
    resultString[wordlen] = '\0';

    return resultString;
}


void killThemAll(ThreadData *data) {
    pthread_cancel(data->threadId);
    if (data->thread2Id != NULL) {
        pthread_cancel(data->thread2Id);
    }
}

void passwordFound(Password* password, int index,char* word,ThreadData* data,bool setPassword){
    if(setPassword){
        password->password = calloc(sizeof(char),strlen(word)+1);
        strcpy(password->password,word);
    }
    markAsFound(index,data);
    crackingStatus.guess++;
    notifyPasswordFound(data, index);  // notify other cores
    printMatch(password);
}

// This is a very inefficient way to bcast to all other nodes the found psw but I had no clue how.
// to manage to send mpi messages using different threads. This uses a miniprotocol for communicating:
// first the type of the msg is sent as well as the data that can be then safely parsed.
void notifyPasswordFound(ThreadData *data, int passwordIndex) {
    for (int i=0; i<data->worldSize; i++) {
        if (i!=data->worldRank){
            MPI_Send(&PSW_FOUND, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            MPI_Send(&passwordIndex, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
        }
    }
}

void printMatch(Password* password){
    printf("%s\t\t(%s)\n",password->password,password->username);
    fflush(stdout);
}

// This function is run by each thread which listens for user input
void *threadFun(void *vargp) {
    ThreadData *data = (ThreadData *)vargp;
    while (1) {
        //sleep(0.2);   This is probably no more needed, each call inside if/else is a blocking call.
        char c;
        int msgType;
        if (data->worldRank == ROOT && pthread_self() == data->firstThread) {
            // The first thread (2) of core n.0 must listen for key pressed
            c = fgetc(stdin);
            for (int i=0; i<data->worldSize; i++) {
                MPI_Send(&KEY_PRESSED, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            }
            msgType = KEY_PRESSED;  // only this thread already knows the real value of msgType
        } else {
            // All the other threads must listen for an incoming msg
            MPI_Recv(&msgType, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        if (msgType == KEY_PRESSED) {
            if (handleKeyPressed(c, data)) {
                trace("\nQuitting the program ...", data->worldRank);
                // The user has pressed letter 'q' to exit the program
                return NULL;
            }
        } else if (msgType == PSW_FOUND) {
            // MPI_Status status;
            // MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);    // probe the msg before collecting it
            int passwordIndex = -1;
            MPI_Recv(&passwordIndex, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            markAsFound(passwordIndex,data);
            //TODO there is an error with received passwordIndex
        }
    }   
}

void markAsFound(int passwordIndex,ThreadData* data) {

    PasswordList *currentPointer = passwordList;

    if(passwordIndex == 1){
        currentPointer->found = true;
    }else{
        //faccio -2 in quanto l'index parte da 1 e poi voglio fermarmi 1 posizione prima
        for (int i = 0; i < passwordIndex-1; i++){
            currentPointer = currentPointer->next;
        }
        /*PasswordList *bridge = currentPointer;
        currentPointer = currentPointer->next;
        bridge->next = currentPointer->next;
        currentPointer->next = NULL;
        bridge = NULL;
        free(bridge);*/
        currentPointer->found = true;
    }

    if( passGuessed == NULL ){
        passGuessed = (struct passwordList *)malloc(sizeof(struct passwordList));
        passGuessed->obj = currentPointer->obj;
        passGuessed->next = NULL;
    }else{
        PasswordList* current = passGuessed;
        while( current->next ){
            current = current->next;
        }
        PasswordList* node = (struct passwordList *)malloc(sizeof(struct passwordList));
        node->obj = currentPointer->obj;
        node->next = NULL;
        current->next = node;
    }

    //printf("[%d] received notification for password n. %d\n",data->worldRank,passwordIndex);
}


int handleUserOptions(int argc, char const *argv[],ThreadData *data) {
    int opt; 

    while(true)
    // while((opt = getopt(argc, argv, ":o:r:i:")) != -1)  
    {  

        int this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"min-len", required_argument, 0,  0 },
            {"max-len", required_argument, 0,  0 },
            {"add-n"  , required_argument, 0,  0 },
            {"charset"  , required_argument, 0,  0 },
        };

        opt = getopt_long(argc, argv, ":so:w:i",long_options, &option_index);
        
        if (opt == -1) break;

                    
        switch(opt)  
        {  
            case 0:
                if(strcmp(long_options[option_index].name,"max-len")==0){
                    if(atoi(optarg) < 0){
                        trace("--max-len must be a positive integer\n",data->worldRank);
                        return 1;
                    }
                    incremental_max_len = atoi(optarg);
                }
                else if(strcmp(long_options[option_index].name,"min-len")==0){
                    if(atoi(optarg) < 0){
                        trace("--min-len must be a positive integer\n",data->worldRank);
                        return 1;
                    }
                    incremental_min_len = atoi(optarg);
                }
                else if(strcmp(long_options[option_index].name,"add-n")==0){
                    if(atoi(optarg) < 0){
                        trace("--add-n must be a positive integer\n",data->worldRank);
                        return 1;
                    }
                    add_n = atoi(optarg);
                }
                else if(strcmp(long_options[option_index].name,"charset")==0){
                    int rlen;
                    int* r = decodeRanges(optarg, &rlen);
                    rangesLen = (int)rlen/2; //number of couples
                    if(r==NULL) {
                        trace("Usage: --charset bad format:\n    try with: --charset=48-57,65-90,97-122.\n",data->worldRank); 
                        return 1;
                    }
                    ranges = calloc(sizeof(Range),rangesLen);
                    for (int i = 0; i < rlen; i+=2){
                        ranges[i/2].min = r[i];
                        ranges[i/2].max = r[i+1];
                    }
                    
                    free(r);
                }else{
                    trace("Usage: Unknow option\n",data->worldRank); 
                    return 1;
                }
                break;
            case 'o':  
                out_file_flag = true;
                output_file_path = calloc(sizeof(char),strlen(optarg)+1);
                strcpy(output_file_path,optarg);
                break;
            case 'w':
                rule_flag = true;
                dict_path = calloc(sizeof(char),strlen(optarg)+1);
                strcpy(dict_path,optarg);
                // if optarg isn't a correct string atoi returns 0
                // add_n = atoi(optarg); 
                break;  
            case 's':
                single_flag = true;
                // if optarg isn't a correct string atoi returns 0
                // add_n = atoi(optarg); 
                break;  
            case 'i':
                incremental_flag = true;
                break; 
            case ':':  
                if (opt == 'o') {
                    trace("Usage: The option -o needs an output file as an argument.\n",data->worldRank);
                    return 1;
                }
                if (opt == 'r') {
                    trace("Usage: The option -r needs an integer as an argument.\n",data->worldRank);  
                    return 1;

                }
                return 1;  
            case '?':  
                printf("Usage: Unknown option: %c\n", optopt); 
                return 1;
        }  
        
    }

    if (optind == argc){
        trace("Usage: Missing parameter for input_file\n",data->worldRank); 
        return 1;
    }
    
    for(; optind < argc; optind++){      
        input_file_path = (char *)calloc(sizeof(char), strlen(argv[optind])+1);
        strncpy(input_file_path, argv[optind], strlen(argv[optind]));
        input_file_path[strlen(argv[optind])+1] = '\0';
    }  

    if(incremental_flag && rule_flag && single_flag){
        trace("Usage: -i and -r and -s cannot be used together.\n",data->worldRank);
        return 1;
    }

    if(add_n!=-1 && ranges == NULL){
        trace("Usage: --add-n needs --charset to generate words.\n    try with: --add-n=2 --charset=48-57,65-90,97-122.\n",data->worldRank);
        return 1;
    }

    if(!incremental_flag && (incremental_min_len!=-1 || incremental_max_len!=-1)){
        trace("Usage: --max-len and --min-len can only be used with -i option.\n",data->worldRank);
        return 1;
    }

    if((!rule_flag && add_n!=-1)){
        trace("Usage: --add-n can only be used with -r option.\n",data->worldRank);
        return 1;
    }

    // if(rule_flag && (add_n==-1 /* || other rules*/) ){
    //     trace("Usage: -w needs a rule type parameter.\n    try -w dict_file -add-n=5.\n",data->worldRank);
    //     return 1;
    // }

    return 0;
}

// Disclaimer: this function assumes WORLD_SIZE > 1
int handleKeyPressed(char key, ThreadData *data) {
    // Broadcast the character read ONCE to each MPI process
    if (pthread_self() == data->firstThread) {
        MPI_Bcast(&key, 1, MPI_BYTE, ROOT, MPI_COMM_WORLD);
    }

    if (key == QUIT) {
        data->shouldCrack = 0;
        return 1;

    } else if (key == STATUS) {
        // The core responsible for holding data is not the ROOT since it is running 3 threads in total
        // and could remain stucked when calling MPI_Gather (because would call it twice).
        const int CHOSEN_CORE = ROOT+1;    

        // Here we have the data to be sent from each process
        int sendData[2] = {};
        getDataFromProcess(&sendData);

        // Main process gathers information processed by the others
        int *receiveBuffer = NULL;
        if (data->worldRank == CHOSEN_CORE) {
            if ((receiveBuffer = malloc(sizeof(int)*data->worldSize*2)) == NULL) {
                printf("Some error occourred when allocating memory ...\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
        MPI_Gather(&sendData, 2, MPI_INT, receiveBuffer, 2, MPI_INT, CHOSEN_CORE, MPI_COMM_WORLD); 
        if (data->worldRank == CHOSEN_CORE) {
            int guess = 0;
            int try = 0;
            for (int i=0; i<data->worldSize; i+=2) {
                    guess+=receiveBuffer[i];
                    try+=receiveBuffer[i+1];
            }
            printStatus(crackingStatus.starting_time,guess,try);
            free(receiveBuffer);
        }
    }
    return 0;
}

void printStatus(starting_time,guess,try){
    int elapsed_sec = time(0) - starting_time;

    int days = elapsed_sec/86400;
    int hours = (elapsed_sec-(days*86400))/3600;
    int minutes = (elapsed_sec - (hours*3600*86400)) / 60;
    int seconds = elapsed_sec % 60;

    printf("%dg %d:%d:%d:%d %dg/s %ldt/s %s\n",
        guess,
        days,
        hours,
        minutes,
        seconds,
        guess/(try+1),
        (long)try/elapsed_sec,
        crackingStatus.currentWord

    );

}

// This function is responsible for collecting the useful information to be printed for the status
void getDataFromProcess(int* sendData) {
    sendData[0] = crackingStatus.guess;
    sendData[1] = crackingStatus.try;

    return;
}


ThreadData *initData() {
    // This structure contains info about program status to be shared among threads
    ThreadData *data;
    if ((data = malloc(sizeof(ThreadData))) == NULL) {
        printf("Some error occourred when allocating memory ...\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    };
    
    // Default behaviour specifies the program to crack the passwords
    data->shouldCrack = 1;

    crackingStatus.starting_time = time(0);

    // Initialize MPI
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &(data->worldRank));

    MPI_Comm_size(MPI_COMM_WORLD, &(data->worldSize));

    if (pthread_create(&(data->threadId), NULL, threadFun, data)) {
        // Abort the execution if threads cannot be started
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    // If the thread has been correctly created, mark it as the first thread created
    // because we want to distinguish the 2 threads running threadFun() wihtin core num. 0
    data->firstThread = data->threadId;

    if (data->worldRank == ROOT && pthread_create(&(data->thread2Id), NULL, threadFun, data)) {
        // Same, but also for the second (3) thread of the main process.
        MPI_Abort(MPI_COMM_WORLD, 1);
    };
    return data;
}

// Useful function used to print just once messages onto screen
void trace(char *msg, int rank) {
    if (rank == ROOT) {
        puts(msg);
    }
}
