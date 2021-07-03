#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

#define BLACK "\033[0;90m"
#define RED "\033[0;91m"
#define GREEN "\033[0;92m"
#define YELLOW "\033[0;93m"
#define RESET "\033[2J\033[H"

extern int errno;

char* readusr(void)
{
    char *line =(char*) malloc(100), *linep = line;
    size_t lenmax = 100, len = lenmax;
    int c;

    if(line == NULL)
        return NULL;

    for(;;)
	{
        c = fgetc(stdin);
        if(c == EOF)
            break;

        if(--len == 0) {
            len = lenmax;
            char * linen =(char*) realloc(linep, lenmax *= 2);

            if(linen == NULL) {
                free(linep);
                return NULL;
            }
            line = linen + (line - linep);
            linep = linen;
        }

        if((*line++ = c) == '\n')
            break;
    }
    *line = '\0';
    return linep;
}

/* portul de conectare la server*/
int port, exits;
pthread_mutex_t exit_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct thData
{
	int idThread; //id-ul thread-ului tinut in evidenta de acest program
	int cl; //descriptorul intors de accept
}thData;

static void* reader(void* arg)
{
	struct thData tdL; 
	tdL= *((struct thData*)arg);
	while(1)
	{
		int message_size = 0;
		if (read (tdL.cl, &message_size, sizeof(int)) < 0)
    	{
     		perror ("err read\n");
      		return NULL;
    	}
		char* msg = (char*)malloc(message_size+1);
		if(read (tdL.cl, msg, message_size) < 0)
		{
     		perror ("err read\n");
      		return NULL;
    	}
		msg[message_size] = '\0';
		if(strcmp(msg, "leave") == 0)
		{
			pthread_mutex_lock(&exit_lock);
			exits = 1;
			pthread_mutex_unlock(&exit_lock);
			break;
		}
		printf("%s\n", msg);
		fflush(stdout);
		free(msg);
	}	

	close ((intptr_t)arg);
	return(NULL);
}

static void* writer(void* arg)
{
	struct thData tdL; 
	tdL= *((struct thData*)arg);
	while(1)
  	{
		/* citirea mesajului */

		char* msg = NULL;
		msg = readusr();
		int len = strlen(msg);

  		/* trimiterea mesajului la server */
		if (write (tdL.cl, &len, sizeof(int)) <= 0)
  		{	
  			perror ("err write\n");
  			return NULL;
  		}
  		if (write (tdL.cl, msg, len) <= 0)
  		{	
  			perror ("err write\n");
  			return NULL;
  		}
		free(msg);
	}

	close ((intptr_t)arg);
	return(NULL);
}

int main (int argc, char *argv[])
{
  	int sd;			// descriptorul de socket
  	struct sockaddr_in server;	// structura folosita pentru conectare 

  	/* exista toate argumentele in linia de comanda? */
  	if (argc != 3)
    {
    	printf ("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
    	return -1;
    }

  	/* stabilim portul */
  	port = atoi (argv[2]);

  	/* cream socketul */
  	if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
    	perror ("Eroare la socket().\n");
    	return errno;
    }

  	/* umplem structura folosita pentru realizarea conexiunii cu serverul */
  	/* familia socket-ului */
  	server.sin_family = AF_INET;
  	/* adresa IP a serverului */
  	server.sin_addr.s_addr = inet_addr(argv[1]);
  	/* portul de conectare */
  	server.sin_port = htons (port);
  
  	/* ne conectam la server */
 	if (connect (sd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1)
    {
    	perror ("[client]Eroare la connect().\n");
    	return errno;
    }

	pthread_t th[2];
	for(int i = 0; i <= 1; i++)
	{
		thData *td;
		td=(struct thData*)malloc(sizeof(struct thData));	
		td->idThread = i;
		td->cl = sd;

		if(i==0)pthread_create(&th[i], NULL, &writer, td);	      
			else pthread_create(&th[i], NULL, &reader, td);	  
	}
	while(exits == 0) ;
	printf(RESET);
	fflush(stdout);
}

