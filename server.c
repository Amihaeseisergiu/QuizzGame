#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sqlite3.h>

/* portul folosit */
#define PORT 2908
#define BLACK "\033[0;90m"
#define RED "\033[0;91m"
#define GREEN "\033[0;92m"
#define YELLOW "\033[0;93m"
#define RESET "\033[2J\033[H"
#define RSC "\033[0m"

pthread_mutex_t update_round = PTHREAD_MUTEX_INITIALIZER, update_online = PTHREAD_MUTEX_INITIALIZER;

/* codul de eroare returnat de anumite apeluri */
extern int errno;

int roundinprogress, MAXONLINE = 0;
sqlite3 *db;
char *zErrMsg = 0;

typedef struct thData
{
	int idThread; //id-ul thread-ului tinut in evidenta de acest program
	int cl; //descriptorul intors de accept
}thData;

struct rounddata
{
	int cl;
	char username[64];
	int correctanswer;
	int myanswer;
	int myturn;
	int score;
};
struct rounddata* round_data;

struct onlinedata
{
	int cl;
	int in_round;
	int loggedIn;
	char username[64];
};

struct onlinedata* online;

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
static void *roundmanager(void *); //functia executata pentru dirijarea rundelor

static int callback(void *data, int argc, char **argv, char **azColName)
{
   	int i;
	char** result = (char**)(data);
  	*result = (char*)malloc(strlen(azColName[0]) + (argv[0] ? strlen(argv[0]) : 5) + 10);
	sprintf(*result, "%s = %s\n", azColName[0], argv[0] ? argv[0] : "NULL");
   
   	for(i = 1; i<argc; i++)
   	{
		char buffer[strlen(*result)];
		strcpy(buffer, *result);
		free(*result);
		*result = (char*)malloc(strlen(buffer) + strlen(azColName[i]) + (argv[i] ? strlen(argv[i]): 5) + 10);
		strcpy(*result, buffer);
		strcat(*result, azColName[i]);
		strcat(*result, " = ");
		strcat(*result, argv[i] ? argv[i] : "NULL");
		strcat(*result, "\n");
   	}
   	
   	return 0;
}

char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    count += last_comma < (a_str + strlen(a_str) - 1);

    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        *(result + idx) = 0;
    }

    return result;
}


void free_lines(char **lines)
{
    int i = 0;
	while(lines[i])
	{
		free(lines[i]);
		i++;
	}
	free(lines);
}

char** readusr(int fd)
{
	int received_size;
	if(read(fd, &received_size, sizeof(int)) < 0)
		perror("err read\n");
	char received[received_size];
	if( read(fd, received, sizeof(received)) < 0)
		perror("err read\n");
	received[received_size-1] = '\0';
	return str_split(received, ' ');
}

void writeusr(int fd, char* message, int size)
{
	char buffer[size];
	int size2 = size;
	strcpy(buffer, message);
	if( write(fd, &size2, sizeof(int)) < 0)
		perror("err write\n");

	if( write(fd, buffer, strlen(buffer)) < 0)
		perror("err write\n");
}

double get_time(struct timespec* start, struct timespec* finish)
{
	double elapsed;
	clock_gettime(CLOCK_MONOTONIC, finish);

	elapsed = (finish->tv_sec - start->tv_sec);
	elapsed += (finish->tv_nsec - start->tv_nsec) / 1000000000.0;
	return elapsed;
}

int nr_online()
{
	int nronline = 0;
	for(int i = 0; i < MAXONLINE; i++)
	{
		pthread_mutex_lock(&update_online);
		if(online[i].cl && online[i].loggedIn) nronline++;
		pthread_mutex_unlock(&update_online);
	}
	return nronline;
}

int main ()
{
	struct sockaddr_in server;	// structura folosita de server
	struct sockaddr_in from;
	int sd;		//descriptorul de socket 
	pthread_t* th = (pthread_t*) malloc(sizeof(pthread_t));    //Identificatorii thread-urilor care se vor crea
	round_data = (struct rounddata*) malloc(sizeof(struct rounddata));
	online = (struct onlinedata*) malloc(sizeof(struct onlinedata));

	//thread responsabil pentru dirijarea rundelor
	pthread_t rm[1];
	pthread_create(&rm[0], NULL, &roundmanager, NULL);	  
	pthread_detach(rm[0]);
	
	int rc;

	/* Open database */
	rc = sqlite3_open("proiect.db", &db);
	   
	if( rc ) 
	{
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		  return(0);
    }

  	/* crearea unui socket */
  if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("[server]Eroare la socket().\n");
      return errno;
    }
  /* utilizarea optiunii SO_REUSEADDR */
  int on=1;
  setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
  
  /* pregatirea structurilor de date */
  bzero (&server, sizeof (server));
  bzero (&from, sizeof (from));
  
  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;	
  /* acceptam orice adresa */
    server.sin_addr.s_addr = htonl (INADDR_ANY);
  /* utilizam un port utilizator */
    server.sin_port = htons (PORT);
  
  /* atasam socketul */
  if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)
    {
      perror ("[server]Eroare la bind().\n");
      return errno;
    }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen (sd, 2) == -1)
    {
      perror ("[server]Eroare la listen().\n");
      return errno;
    }
  /* servim in mod concurent clientii...folosind thread-uri */
  while (1)
    {
      int client;
      thData * td; //parametru functia executata de thread     
      unsigned int length = sizeof (from);

      printf ("[server]Asteptam la portul %d...\n",PORT);
      fflush (stdout);

      // client= malloc(sizeof(int));
      /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
      if ( (client = accept (sd, (struct sockaddr *) &from, &length)) < 0)
	{
	  perror ("[server]Eroare la accept().\n");
	  continue;
	}
	
    /* s-a realizat conexiunea, se astepta mesajul */
    
	// int idThread; //id-ul threadului
	// int cl; //descriptorul intors de accept

	td=(struct thData*)malloc(sizeof(struct thData));	
	td->idThread=MAXONLINE++;
	td->cl=client;
	th = (pthread_t*) (realloc(th, sizeof(pthread_t)*MAXONLINE));
	round_data = (struct rounddata*) (realloc(round_data, sizeof(struct rounddata)*MAXONLINE));
	online = (struct onlinedata*) (realloc(online, sizeof(struct onlinedata)*MAXONLINE));
	pthread_mutex_lock(&update_online);
	online[MAXONLINE-1].cl = client;
	bzero(online[MAXONLINE-1].username, 64);
	online[MAXONLINE-1].loggedIn = 0;
	online[MAXONLINE-1].in_round = 0;
	pthread_mutex_unlock(&update_online);

	pthread_create(&th[MAXONLINE-1], NULL, &treat, td);	      
				
	}//while
	sqlite3_close(db);
	free(online);
	free(round_data);
	free(th);
};

static void *treat(void * arg)
{
	struct thData tdL; 
	tdL= *((struct thData*)arg);
	printf ("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
	fflush (stdout);		 
	int loggedIn = 0;
	
	while(1)
	{	
		char** lines = readusr(tdL.cl);
		int i = 0;
		while(lines[i])
		{
			printf("%s \n", lines[i]);
			i++;
			fflush(stdout);
		}

		if(lines[0] && strcmp(lines[0], "register") == 0 && !loggedIn)
		{
			if(lines[1] && lines[2])
			{
				int rc;
				char *data = NULL;
				char sql[strlen("SELECT username from users where username = '%s';") + strlen(lines[1])];
				sprintf(sql, "SELECT username from users where username = '%s';", lines[1]);

				rc = sqlite3_exec(db, sql, callback, (void*)(&data), &zErrMsg);
				   
				if( rc != SQLITE_OK )
                {
			 		fprintf(stderr, "SQL error: %s\n", zErrMsg);
					sqlite3_free(zErrMsg);
				}

				char check[100] = "username = "; strcat(check, lines[1]);
				if(data && strstr(data, check))
					writeusr(tdL.cl, YELLOW "Username already taken! Please choose another one" RSC, strlen(YELLOW "Username already taken! Please choose another one" RSC));
				else
				{
					char sql2[strlen("insert into users (username, password) values ('%s', '%s');") + strlen(lines[1]) + strlen(lines[2])];
					sprintf(sql2, "insert into users (username, password) values ('%s', '%s');", lines[1], lines[2]);
					rc = sqlite3_exec(db, sql2, callback, (void*)(&data), &zErrMsg);
				   
					if( rc != SQLITE_OK )
		            {
				 		fprintf(stderr, "SQL error: %s\n", zErrMsg);
						sqlite3_free(zErrMsg);
					}
					writeusr(tdL.cl, GREEN "Account created!" RSC, strlen(GREEN "Account created!" RSC));
				}
				free(data);
			}
			else writeusr(tdL.cl, RED "A username and a password are required" RSC, strlen(RED "A username and a password are required" RSC));
		}
		if(lines[0] && strcmp(lines[0], "login") == 0 && !loggedIn)
		{
			if(lines[1] && lines[2])
			{
				int rc;
				char *data=NULL;
				char sql[strlen("SELECT username from users where username = '%s' and password = '%s';") + strlen(lines[1]) + strlen(lines[2])];
				sprintf(sql, "SELECT username from users where username = '%s' and password = '%s';", lines[1], lines[2]);

				rc = sqlite3_exec(db, sql, callback, (void*)(&data), &zErrMsg);
				   
				if( rc != SQLITE_OK )
                {
			 		fprintf(stderr, "SQL error: %s\n", zErrMsg);
					sqlite3_free(zErrMsg);
				}

				char check[100] = "username = "; strcat(check, lines[1]);
				if(data && strstr(data, check))
				{
					char msg[strlen(RESET GREEN "Successfully logged in! Welcome %s" RSC) + strlen(lines[1])+1];
					sprintf(msg, RESET GREEN "Successfully logged in! Welcome %s" RSC, lines[1]);
					writeusr(tdL.cl, msg, strlen(msg));
					pthread_mutex_lock(&update_online);
					strcpy(online[tdL.idThread].username, lines[1]);
					online[tdL.idThread].in_round = 0;
					online[tdL.idThread].loggedIn = 1;
					pthread_mutex_unlock(&update_online);
					loggedIn = 1;
					if(roundinprogress)
						writeusr(tdL.cl, RESET YELLOW "Match in progress.Please wait for it to end" RSC, strlen(RESET YELLOW "Match in progress.Please wait for it to end" RSC));
				}
				else
					writeusr(tdL.cl,RESET RED "Incorrect username or password! Please try again" RSC, strlen(RED "Incorrect username or password! Please try again" RSC));
				free(data);
			}
			else writeusr(tdL.cl,RESET RED "A username and a password are required" RSC, strlen(RED "A username and a password are required" RSC));
		}
		if(lines[0] && strcmp(lines[0], "leave") == 0)
		{
			writeusr(tdL.cl, lines[0], strlen(lines[0]));
			free_lines(lines);
			pthread_mutex_lock(&update_online);
			online[tdL.idThread].cl = 0;
			bzero(online[tdL.idThread].username, 64);
			online[tdL.idThread].in_round = 0;
			online[tdL.idThread].loggedIn = 0;
			pthread_mutex_unlock(&update_online);
			pthread_mutex_lock(&update_round);
			round_data[tdL.idThread].cl = 0;
			round_data[tdL.idThread].myanswer = 0;
			round_data[tdL.idThread].myturn = 0;
			round_data[tdL.idThread].score = 0;
			round_data[tdL.idThread].correctanswer = 0;
			pthread_mutex_unlock(&update_round);
			break;
		}
		else if(roundinprogress && loggedIn)
		{
			pthread_mutex_lock(&update_round);
			if(round_data[tdL.idThread].cl && round_data[tdL.idThread].myturn)
			{
				if(lines[0] && (strcmp(lines[0], "A") == 0 || strcmp(lines[0], "a") == 0 || strcmp(lines[0], "1") == 0))round_data[tdL.idThread].myanswer = 1;
				else if(lines[0] && (strcmp(lines[0], "B") == 0 || strcmp(lines[0], "b") == 0 || strcmp(lines[0], "2") == 0))round_data[tdL.idThread].myanswer = 2;
				else if(lines[0] && (strcmp(lines[0], "C") == 0 || strcmp(lines[0], "c") == 0 || strcmp(lines[0], "3") == 0))round_data[tdL.idThread].myanswer = 3;
				else if(lines[0] && (strcmp(lines[0], "D") == 0 || strcmp(lines[0], "d") == 0 || strcmp(lines[0], "4") == 0))round_data[tdL.idThread].myanswer = 4;
				else round_data[tdL.idThread].myanswer = 5;
			}
			pthread_mutex_unlock(&update_round);
		}
		free_lines(lines);
	}
	
	pthread_mutex_lock(&update_online);
	online[tdL.idThread].cl = 0;
	pthread_mutex_unlock(&update_online);
	/* am terminat cu acest client, inchidem conexiunea */
	close ((intptr_t)arg);
	return(NULL);			
};

static void *roundmanager(void *arg)
{
	struct timespec start, finish;
	clock_gettime(CLOCK_MONOTONIC, &start); //initializare ceas
	
	
	while(1)
	{	
		if(nr_online() >= 2) //daca doi sau mai multi useri logati sunt conectati incepe jocul
		{
			struct timespec start2, finish2;
			clock_gettime(CLOCK_MONOTONIC, &start2);

			while(1)
			{
				if(nr_online() < 2) //daca s-a deconectat vreun user ne intoarcem sa asteptam
				{
					for(int i = 0; i < MAXONLINE; i++)
					{
						pthread_mutex_lock(&update_online);
						if(online[i].cl && online[i].loggedIn) writeusr(online[i].cl,RESET YELLOW "Not enough players.Returning to matchmaking..." RSC, strlen(RESET YELLOW "Not enough players.Returning to matchmaking..." RSC));
						pthread_mutex_unlock(&update_online);
					}
					break;
				}
				if((int)get_time(&start2, &finish2) >= 30 && nr_online() >= 2) //asteptam 30 de secunde poate se conecteaza alti useri
				{
					pthread_mutex_lock(&update_round);
					roundinprogress = 1;
					for(int i = 0; i < MAXONLINE; i++)
					{
						pthread_mutex_lock(&update_online);
						if(online[i].cl && online[i].loggedIn)
						{
							strcpy(round_data[i].username, online[i].username);
							round_data[i].cl = online[i].cl;
						}
						pthread_mutex_unlock(&update_online);
						round_data[i].myturn = 0;
						round_data[i].score = 0;
						round_data[i].myanswer = 0;
						round_data[i].correctanswer = 0;
					}
					pthread_mutex_unlock(&update_round);

					for(int i = 0; i < 3; i++) //3 runde; fiecare jucator o intrebare pe runda
					{
						int nrinmatch = 0;
						for(int j = 0; j < MAXONLINE; j++) //daca nu mai joaca nimeni inchidem meciul
							if(round_data[j].cl) nrinmatch++;
						if(nrinmatch == 0)
						{
							for(int j = 0; j < MAXONLINE; j++)
								if(online[j].cl) writeusr(online[j].cl, RESET YELLOW "Everybody has left the match. Closing match..." RSC, strlen(RESET YELLOW "Everybody has left the match. Closing match..." RSC));
							pthread_mutex_lock(&update_round);
							roundinprogress = 0;
							for(int i = 0; i < MAXONLINE; i++)
							{
								bzero(round_data[i].username, 64);
								round_data[i].cl = 0;
								round_data[i].myturn = 0;
								round_data[i].score = 0;
								round_data[i].myanswer = 0;
								round_data[i].correctanswer = 0;
							}
							pthread_mutex_unlock(&update_round);
							break;
						}
						for(int j = 0; j < MAXONLINE; j++) //luam userii pe rand in ordinea in care s-au logat
						{
							pthread_mutex_lock(&update_round);
							int check = 0;
							if(round_data[j].cl) check = 1;
							pthread_mutex_unlock(&update_round);

							if(check)
							{
								
								struct timespec start3, finish3;
								clock_gettime(CLOCK_MONOTONIC, &start3);

								int rc;								
								char *data=NULL;
								char sql[strlen("SELECT * from questions ORDER BY RANDOM() LIMIT 1;")];
								sprintf(sql, "SELECT * from questions ORDER BY RANDOM() LIMIT 1;");

								rc = sqlite3_exec(db, sql, callback, (void*)(&data), &zErrMsg);
								   
								if( rc != SQLITE_OK )
								{
							 		fprintf(stderr, "SQL error: %s\n", zErrMsg);
									sqlite3_free(zErrMsg);
								}
						
								writeusr(round_data[j].cl, RESET GREEN "You have 20 sec to answer the following question\n------------------------------------------------" RSC, strlen(RESET GREEN "You have 20 sec to answer the following question\n------------------------------------------------" RSC));
								for(int k = 0; k < MAXONLINE; k++) //notificam ceilalti useri ca este randul userului x
								{
									if(k != j && round_data[k].cl)
									{
										char msg[strlen(RESET YELLOW "%s is answering...please wait" RSC) + strlen(round_data[j].username)+1];
										sprintf(msg, RESET YELLOW "%s is answering...please wait" RSC, round_data[j].username);
										writeusr(round_data[k].cl, msg, strlen(msg));
									}
								}

								char** lines = str_split(data, '\n');
								for(int k = 0; k <= 4; k++)
								{
									char* get = strchr(lines[k], '=') + 2;
									if(k == 0)
									{
										char dashes[strlen(get)+1];
										strcpy(dashes, "-");
										for(int l = 0; l < strlen(get); l++) strcat(dashes, "-");
										for(int l = 0; l < MAXONLINE; l++)
											if(round_data[l].cl)
											{
												writeusr(round_data[l].cl, get, strlen(get));
												writeusr(round_data[l].cl, dashes, strlen(dashes));
											}
									}
									else
									{
										char choice[strlen(get) + 4];
										choice[0] = lines[k][0];
										choice[1] = '\0';
										strcat(choice, ") ");
										strcat(choice, get);
										for(int l = 0; l < MAXONLINE; l++)
											if(round_data[l].cl) writeusr(round_data[l].cl, choice, strlen(choice));
									}
									
								}
								char dashes[strlen(strchr(lines[0], '=') + 2)+1];
								strcpy(dashes, "-");
								for(int l = 0; l < strlen(strchr(lines[0], '=') + 2); l++) strcat(dashes, "-");
								writeusr(round_data[j].cl, dashes, strlen(dashes));
								char* get = strchr(lines[5], '=') + 2;
								pthread_mutex_lock(&update_round);
								if(strstr(get, "A")) round_data[j].correctanswer = 1;
								else if(strstr(get, "B")) round_data[j].correctanswer = 2;
								else if(strstr(get, "C")) round_data[j].correctanswer = 3;
								else if(strstr(get, "D")) round_data[j].correctanswer = 4;
								round_data[j].myturn = 1;
								round_data[j].myanswer = 0;
								pthread_mutex_unlock(&update_round);

								free_lines(lines);
								free(data);

								while(1)
								{
									if((int)get_time(&start3,&finish3) >= 20)
									{
										pthread_mutex_lock(&update_round);
										round_data[j].myturn = 0;
										if(round_data[j].cl)writeusr(round_data[j].cl, RESET RED "Time has expired" RSC, strlen(RESET RED "Time has expired" RSC));
										for(int k = 0; k < MAXONLINE; k++) //notificam ceilalti useri ca este randul userului x
										{
											if(k != j && round_data[k].cl)
											{
												char msg[strlen(RESET YELLOW "%s didn't give any answer" RSC) + strlen(round_data[j].username)+1];
												sprintf(msg, RESET YELLOW "%s didn't give any answer" RSC, round_data[j].username);
												writeusr(round_data[k].cl, msg, strlen(msg));
											}
										}
										pthread_mutex_unlock(&update_round);
										sleep(5);
										break;
									}
									pthread_mutex_lock(&update_round);
									if(round_data[j].myanswer && round_data[j].correctanswer == round_data[j].myanswer)
									{
										round_data[j].score += 10;
										round_data[j].myturn = 0;
										if(round_data[j].cl)writeusr(round_data[j].cl, RESET GREEN "Correct answer +10 points!" RSC, strlen(RESET GREEN "Correct answer +10 points!" RSC));
										for(int k = 0; k < MAXONLINE; k++) //notificam ceilalti useri ca este randul userului x
										{
											if(k != j && round_data[k].cl)
											{
												char msg[strlen(RESET YELLOW "%s gave a correct answer" RSC) + strlen(round_data[j].username)+1];
												sprintf(msg, RESET YELLOW "%s gave a correct answer" RSC, round_data[j].username);
												writeusr(round_data[k].cl, msg, strlen(msg));
											}
										}
										pthread_mutex_unlock(&update_round);
										sleep(5);
										break;
									}
									else if(round_data[j].myanswer)
									{
										round_data[j].myturn = 0;
										if(round_data[j].cl)
										{
											char buffer[2]; buffer[1] = '\0';
											if(round_data[j].correctanswer == 1) buffer[0] = 'A';
											else if(round_data[j].correctanswer == 2) buffer[0] = 'B';
											else if(round_data[j].correctanswer == 3) buffer[0] = 'C';
											else if(round_data[j].correctanswer == 4) buffer[0] = 'D';
											char msg1[strlen(RESET RED "Incorrect answer. The correct answer was %s" RSC) + strlen(buffer) + 2];
											sprintf(msg1, RESET RED "Incorrect answer. The correct answer was %s" RSC, buffer); 
											writeusr(round_data[j].cl, msg1, strlen(msg1));
										}
										for(int k = 0; k < MAXONLINE; k++) //notificam ceilalti useri ca este randul userului x
										{
											if(k != j && round_data[k].cl)
											{
												char msg[strlen(RESET YELLOW "%s gave an incorrect answer" RSC) + strlen(round_data[j].username)+1];
												sprintf(msg, RESET YELLOW "%s gave an incorrect answer" RSC, round_data[j].username);
												writeusr(round_data[k].cl, msg, strlen(msg));
											}
										}
										pthread_mutex_unlock(&update_round);
										sleep(5);
										break;
									}
									pthread_mutex_unlock(&update_round);
								}
							}
						}
						if(i!=2)
						{
							for(int j = 0; j < MAXONLINE; j++)
							{
								char buffer1[33], buffer2[33];
								snprintf(buffer1, 33, "%d", i+1);
								snprintf(buffer2, 33, "%d", i+2);
								char msg1[strlen(RESET YELLOW "Round %s ended. Starting round %s..." RSC) + strlen(buffer1) + strlen(buffer2) + 4];
								sprintf(msg1, RESET YELLOW "Round %s ended. Starting round %s..." RSC, buffer1, buffer2);
								if(round_data[j].cl)writeusr(round_data[j].cl, msg1, strlen(msg1));
							}
							sleep(5);
						}
					}
					int maxscore = 0, nrwinners = 0; //afisam castigatorul / castigatorii si scorul lor
					char winners[100][64];
					for(int i = 0; i < MAXONLINE; i++)
					{
						if(round_data[i].username && round_data[i].score > maxscore)
						{
							nrwinners = 1;
							strcpy(winners[0], round_data[i].username);
							maxscore = round_data[i].score;
						}
						else if(round_data[i].username && round_data[i].score == maxscore)
						{
							strcpy(winners[nrwinners++], round_data[i].username);
						}
					}
					char buffer[33];
					snprintf(buffer, 33, "%d", maxscore);
					char winnermsg[(nrwinners > 1? strlen(RESET GREEN "The winners are with a score of ") : strlen(RESET GREEN "The winner is with a score of")) + nrwinners*65+strlen(buffer)+1];
					strcpy(winnermsg, (nrwinners > 1? RESET GREEN "The winners are" : RESET GREEN "The winner is"));
					strcat(winnermsg, " ");
					strcat(winnermsg, winners[0]);
					for(int i = 1; i < nrwinners; i++)
					{
						strcat(winnermsg, ", ");
						strcat(winnermsg, winners[i]);
					}
					strcat(winnermsg, " with a score of ");
					strcat(winnermsg, buffer);
					strcat(winnermsg, " points" RSC);
					for(int i = 0; i < MAXONLINE; i++)
						if(round_data[i].cl)
						{
							writeusr(round_data[i].cl,winnermsg, strlen(winnermsg));
						}
					sleep(5); // asteptam 5 secunde sa vada toti mesajul
					pthread_mutex_lock(&update_round);
					roundinprogress = 0;
					for(int i = 0; i < MAXONLINE; i++)
					{
						bzero(round_data[i].username, 64);
						round_data[i].cl = 0;
						round_data[i].myturn = 0;
						round_data[i].score = 0;
						round_data[i].myanswer = 0;
						round_data[i].correctanswer = 0;
					}
					pthread_mutex_unlock(&update_round);
					break;
				}
				else //trimitem mesaj de notificare cat asteapta
				{
					if((int)(get_time(&start2,&finish2))%10 == 0) //mesaj o data la 10 sec
					{
						for(int i = 0; i < MAXONLINE; i++)
						{
							pthread_mutex_lock(&update_online);
							if(online[i].cl && online[i].loggedIn)
							{
								char buffer[33];
								snprintf(buffer, 33, "%d", 30-(int)(get_time(&start2, &finish2)));
								char msg[strlen(RESET YELLOW "Match starting in %s seconds" RSC) + strlen(buffer) +1];
								sprintf(msg,RESET YELLOW "Match starting in %s seconds" RSC, buffer);
								writeusr(online[i].cl, msg, strlen(msg));
							}
							pthread_mutex_unlock(&update_online);
						}
						sleep(1);
					}
				}
			}
		}
		else //altfel asteptam jucatori
		{
			if((int)(get_time(&start,&finish))%30 == 0) //notificare la 30 sec ca asteptam jucatori
			{			
				for(int i = 0; i < MAXONLINE; i++)
				{
					pthread_mutex_lock(&update_online);
					if(online[i].cl && online[i].loggedIn) writeusr(online[i].cl,RESET YELLOW "Waiting for players..." RSC, strlen(RESET YELLOW "Waiting for players..." RSC));
					else if(online[i].cl) writeusr(online[i].cl,RESET YELLOW "Waiting for players...Log in to play" RSC, strlen(RESET YELLOW "Waiting for players...Log in to play" RSC));
					pthread_mutex_unlock(&update_online);
				}
				sleep(1);
			}
		}
	}
	close ((intptr_t)arg);
	return(NULL);
}


