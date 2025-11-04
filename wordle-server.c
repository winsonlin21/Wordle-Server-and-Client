#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define NUM_THREADS 256

int total_guesses = 0;
int total_wins = 0;
int total_losses = 0;
char **words = NULL;

// Global variables (for main)
extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char ** words;
int wordsCount = 0;

// Global variables (for the dictionary)
char ** vocabulary;
int vocabularyCount;

// Global variables (for threads)
pthread_t * threads = NULL;
int activeThreads = 0;

// Mutexes for critical sections/variables
pthread_mutex_t wordsMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t statisticsMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threadsMutex = PTHREAD_MUTEX_INITIALIZER;

// Synchronization server variable
volatile int serverStatus = 0;

int listen_sd;

// Thread Information (Data Structure)
struct clientInfo {
  int sockFd;
  struct sockaddr_in addr;
};

// Function that sends a signal to shut down the server
void sigusr1_handler(int signal)
{
  if (signal == SIGUSR1)
  {
    serverStatus = 1;
    close(listen_sd);
  }
}

int is_valid_word(const char *guess, char **vocabulary, int vocabularyCount) {
  for (int i = 0; i < vocabularyCount; i++) {
    if (strcmp(guess, *(vocabulary + i)) == 0) {
      return 1;
    }
  }
  return 0;
}

int char_in_word(char c, const char *word) {
  for (int i = 0; i < strlen(word); i++) {
    if (*(word+i) == c) {
      return 1;
    }
  }
  return 0;
}

// Function that contains code for the Wordle gameplay for each thread
void * client_thread_routine(void * arg)
{
  struct clientInfo *tInfo = (struct clientInfo *)arg;
  int threadSd = tInfo->sockFd;
  //struct sockaddr_in remote_client = tInfo->addr; Might need this to receive answers

  free(tInfo);

  // Choose a random word per client thread
  int hiddenWordIndex = rand() % vocabularyCount;
  char * hiddenWord = *(vocabulary + hiddenWordIndex);

  // CRITICAL SECTION
  pthread_mutex_lock(&wordsMutex);

  int hiddenWordCount = 0;

  while (words != NULL && *(words + hiddenWordCount) != NULL)
  {
    hiddenWordCount++;
  }

  char ** moreHiddenWords = realloc(words, (hiddenWordCount + 2) * sizeof(char *));

  words = moreHiddenWords;

  *(words + hiddenWordCount) = (char *)calloc(strlen(hiddenWord) + 1, sizeof(char));

  for (int i = 0; i < strlen(hiddenWord); i++)
  {
    *(*(words + hiddenWordCount) + i) = *(hiddenWord + i);
  }

  *(*(words + hiddenWordCount) + strlen(hiddenWord)) = '\0';
  *(words + hiddenWordCount + 1) = NULL;

  wordsCount++;

  pthread_mutex_unlock(&wordsMutex);
  
  short guessesRemaining = 6;
  char * guess = calloc(16, sizeof(char));
  char * guessToSentBack = calloc(16, sizeof(char));
  char * packet = calloc(8, 1);
  
  const char *invalid_reply = "?????";

  while(guessesRemaining>0){
    // reads from client and store the client's guess in guess
    printf("THREAD %lu: waiting for guess\n", pthread_self());
    ssize_t n = recv(threadSd, guess, 15, 0);
    *(guess+n) = '\0';

    //checks disconnects
    if(n==0){
      printf("THREAD %lu: client gave up; closing TCP connection...\n", pthread_self());
    
      for (int i = 0; i < 5; i++) {
        *(guessToSentBack + i) = toupper((unsigned char)*(hiddenWord+i));
      }

      printf("THREAD %lu: game over; word was %s!\n", pthread_self(), guessToSentBack);

      pthread_mutex_lock(&statisticsMutex);
      total_losses++;
      pthread_mutex_unlock(&statisticsMutex);

      close(threadSd);
      free(guess);
      free(guessToSentBack);
      free(packet);
      return NULL;
    }

    //everything to lowercase
    for (int i = 0; i < n && *(guess+i) != '\0'; i++) {
      *(guess+i) = tolower((unsigned char)*(guess+i));
    }
    strcpy(guessToSentBack, guess);
    printf("THREAD %lu: rcvd guess: %s\n", pthread_self(), guess);


    //handels when incorrect words. Non 5 letter words and non words in dictionary
    if(n!=5 || !is_valid_word(guess, vocabulary, vocabularyCount)){
      if(guessesRemaining !=1){
        printf("THREAD %lu: invalid guess; sending reply: ????? (%d guesses left)\n", pthread_self() , guessesRemaining);
      }
      else{
        printf("THREAD %lu: invalid guess; sending reply: ????? (%d guess left)\n", pthread_self() , guessesRemaining);
      }
      *(packet) = 'N';
      short guesses_net = htons(guessesRemaining);
      memcpy(packet + 1, &guesses_net, 2);
      memcpy(packet + 3, invalid_reply, 5);
      send(threadSd, packet, 8, 0);
      continue;
    }

    //handels when the guess is valid
    int correctLetters = 0;
    int * used = calloc(5, sizeof(int)); // to track used letters in hiddenWord
    for(int i = 0; i < 5; i++){
      if(*(guess+i) == *(hiddenWord+i)){
        *(guessToSentBack + i) = toupper(*(guess + i));
        *(used + i) = 1;
        correctLetters++;
      } 
      else if(!char_in_word(*(guess+i), hiddenWord)){
        *(guessToSentBack + i) = '-';
      }
    }
    for (int i = 0; i < 5; i++) {
      if (*(guessToSentBack + i) == '-' || *(used + i)) {
        continue;
      }
      char currentChar = *(guess + i);
      int belongs = 0;
      for (int j = 0; j < 5; j++) {
        if(currentChar == *(hiddenWord + j) &&  !*(used+j)){
          belongs = 1;
        }
      }
      if(!belongs){
        *(guessToSentBack + i) = '-';
      }
    }
    free(used);

    pthread_mutex_lock(&statisticsMutex);
    total_guesses++;
    pthread_mutex_unlock(&statisticsMutex);
    guessesRemaining--;

    //sending info back to client
    if(guessesRemaining != 1){
      printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), guessToSentBack, guessesRemaining);
    }
    else{
      printf("THREAD %lu: sending reply: %s (%d guess left)\n", pthread_self(), guessToSentBack, guessesRemaining);
    }
    *(packet) = 'Y';
    short guesses_net = htons(guessesRemaining);
    memcpy(packet + 1, &guesses_net, 2);
    memcpy(packet + 3, guessToSentBack, 5);
    send(threadSd, packet, 8, 0);

    // If the user has guessed the word correctly
    if(correctLetters == 5){
      printf("THREAD %lu: game over; word was %s!\n", pthread_self(), guessToSentBack);

      pthread_mutex_lock(&statisticsMutex);
      total_wins++;
      pthread_mutex_unlock(&statisticsMutex);

      close(threadSd);
      free(guess);
      free(guessToSentBack);
      free(packet);
      return NULL;
    }
    // If the user has run out of guesses
    if(guessesRemaining == 0){
      //makes all capital
      for (int i = 0; i < n && *(guess+i) != '\0'; i++) {
        *(guessToSentBack + i) = toupper((unsigned char)*(hiddenWord+i));
      }

      printf("THREAD %lu: game over; word was %s!\n", pthread_self(), guessToSentBack);

      pthread_mutex_lock(&statisticsMutex);
      total_losses++; 
      pthread_mutex_unlock(&statisticsMutex);
      close(threadSd);
      free(guess);
      free(guessToSentBack);
      free(packet);

      return NULL;
    }
  }

  close(threadSd);
  free(guess);
  free(guessToSentBack);
  free(packet);

  return NULL;
}

// Function that contains code in relation to the server that will host the Wordle game
int wordle_server( int argc, char ** argv )
{
  setvbuf(stdout, NULL, _IONBF, 0);

  // Argument error handling
  if (argc != 5)
  {
    fprintf(stderr, "ERROR: Invalid argument(s)\n");
    fprintf(stderr, "USAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");

    return EXIT_FAILURE;
  }

  int listener_port = atoi(*(argv + 1));
  int seed = atoi(*(argv + 2));
  char * filename = *(argv + 3);
  int num_words = atoi(*(argv + 4));

  // Argument value(s) error handling
  if (listener_port <= 0)
  {
    fprintf(stderr, "ERROR: Invalid port address argument\n");

    return EXIT_FAILURE;
  }

  if (seed < 0)
  {
    fprintf(stderr, "ERROR: Invalid seed argument\n");

    return EXIT_FAILURE;    
  }

  if (num_words <= 0)
  {
    fprintf(stderr, "ERROR: Invalid number of words argument\n");

    return EXIT_FAILURE;    
  }

  threads = (pthread_t *)calloc(NUM_THREADS, sizeof(pthread_t));

  // Signal handlers
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);
  signal(SIGUSR1, sigusr1_handler);

  printf("MAIN: opened %s (%d words)\n", filename, num_words);

  // Set seed
  srand(seed);

  printf("MAIN: seeded pseudo-random number generator with %d\n", seed);

  // Load the dictionary file into the server
  int dictFd = open(filename, O_RDONLY);

  // Open file error handling
  if (dictFd == -1)
  {
    fprintf(stderr, "ERROR: \"%s\" could not be opened\n", filename);
    perror("  reason");

    return EXIT_FAILURE;
  }

  vocabulary = calloc(num_words, sizeof(char *));

  char * buffer = calloc(1024, sizeof(char));
  ssize_t currBytes;
  char * word = calloc(48, sizeof(char));

  int validWordsCount = 0;
  int wordLength = 0;

  while (((currBytes = read(dictFd, buffer, 1024)) > 0) && (validWordsCount < num_words))
  {
    for (ssize_t i = 0; i < currBytes; i++)
    {
      char c = *(buffer + i);

      // EOL check
      if (c == '\n')
      {
        *(word + wordLength) = '\0';

        // Word length of 5 check
        if (wordLength == 5)
        {
          *(word + wordLength) = '\0';

          for (int j = 0; j < wordLength; j++)
          {
            *(word + j) = tolower(*(word + j));
          }

          char * copyWord = calloc(strlen(word) + 1, sizeof(char));
          
          for (int k = 0; k < wordLength; k++)
          {
            *(copyWord + k) = *(word + k);
          }

          *(copyWord + wordLength) = '\0';

          *(vocabulary + validWordsCount) = copyWord;
          validWordsCount++;
          if (validWordsCount >= num_words) {
            /* we've filled the vocabulary buffer; stop parsing further bytes */
            break;
          }
        }

        wordLength = 0;
      }

      // Write to word buffer
      else 
      {
        *(word + wordLength) = c;
        wordLength++;
      }
    }
    if (validWordsCount >= num_words) {
      break;
    }
  }

  close(dictFd);
  free(buffer);
  free(word);

  vocabularyCount = validWordsCount;

  // Create the TCP socket
  listen_sd = socket(AF_INET, SOCK_STREAM, 0);

  // Socket creation error handling
  if (listen_sd == -1)
  {
    perror("ERROR: socket() failed\n");

    return EXIT_FAILURE;
  }

  struct sockaddr_in tcp_server;

  // Prepare server address struct
  memset(&tcp_server, 0, sizeof(tcp_server));
  tcp_server.sin_family = AF_INET;
  tcp_server.sin_addr.s_addr = htonl(INADDR_ANY);
  tcp_server.sin_port = htons(listener_port);

  // Bind
  if (bind(listen_sd, (struct sockaddr *)&tcp_server, sizeof(tcp_server)) == -1) 
  {
    perror("ERROR: bind() failed\n");
    close(listen_sd);

    return EXIT_FAILURE;
  }

  // Listen for connections
  if (listen(listen_sd, NUM_THREADS) == -1) 
  {
    perror("ERROR: listen() failed\n");
    close(listen_sd);

    return EXIT_FAILURE;
  }

  printf("MAIN: Wordle server listening on port {%d}\n", listener_port);

  // MAIN SERVER LOOP
  while (!(serverStatus))
  {
    struct sockaddr_in remote_client;
    socklen_t addrLength = sizeof(remote_client);

    int conn_sd = accept(listen_sd, (struct sockaddr *)&remote_client, &addrLength);

    // Connection error handling
    if (conn_sd == -1) 
    {
      continue;
    }

    printf("MAIN: rcvd incoming connection request\n");

    // Track thread info
    struct clientInfo *cInfo = calloc(1, sizeof(struct clientInfo));

    // Client info creation error handling
    if (!cInfo)
    {
      perror("ERROR: calloc() failed\n");
      close(conn_sd);

      continue;
    }

    cInfo->sockFd = conn_sd;
    cInfo->addr = remote_client;

    // Create thread to run the Wordle game per client
    pthread_t tid;

    if (pthread_create(&tid, NULL, client_thread_routine, cInfo) != 0)
    {
      perror("ERROR: pthread_create() failed\n");
      close(conn_sd);
      free(cInfo);
    }

    // Clean up resources
    else
    {
      /* keep thread joinable so shutdown can cancel/join it safely */
    }

    // Track thread IDs
    // CRITICAL SECTION
    pthread_mutex_lock(&threadsMutex);

    if (activeThreads < NUM_THREADS)
    {
      *(threads + activeThreads) = tid;
      activeThreads++;
    }

    pthread_mutex_unlock(&threadsMutex);
  }
    
  // Shutdown: cancel and join all active client threads first
  pthread_mutex_lock(&threadsMutex);
  for (int i = 0; i < activeThreads; i++)
  {
    pthread_cancel(*(threads + i));
    pthread_join(*(threads + i), NULL);
  }
  pthread_mutex_unlock(&threadsMutex);

  /* Now it's safe to touch and free the words array: all client threads have
     been cancelled/joined and won't be concurrently realloc'ing or writing it. */
  pthread_mutex_lock(&wordsMutex);
  if (words != NULL) {
    for (int i = 0; i < wordsCount; i++) {
      if (*(words + i) != NULL) {
        for (int j = 0; j < (int)strlen(*(words + i)); j++) {
          *(*(words + i) + j) = toupper((unsigned char)*(*(words + i) + j));
        }
        free(*(words + i));
      }
    }
    free(words);
    words = NULL;
    wordsCount = 0;
  }
  pthread_mutex_unlock(&wordsMutex);

  // Deallocate dynamically allocated memory
  if (threads != NULL)
  {
    free(threads);
    threads = NULL;
  }

  if (vocabulary != NULL)
  {
    for (int i = 0; i < vocabularyCount; i++)
    {
      free(*(vocabulary + i));
    }

    free(vocabulary);
    vocabulary = NULL;
  }

  close(listen_sd);

  return EXIT_SUCCESS;
}

/* Simple main wrapper to call wordle_server */
int main(int argc, char **argv)
{
    return wordle_server(argc, argv);
}
// ...existing code...