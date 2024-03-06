#define _GNU_SOURCE

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <semaphore.h>		

#define NUM_RUOTE 11                    //numeri delle ruote presenti nel gioco
#define NUMERI_ESTRAZIONE 5             //numeri da estrarre ad ogni estrazione per ogni ruota
#define MAX_DIM_USPSW 20 				//massima dimensione per username e psw
#define MAX_NUM_GIOCATI 10				//numero massimo dei numeri che si possono giocare
#define MAX_NUM_SCOMMESSE 5				//numero massimo delle tipologie di puntate estratto,ambo,terno,quaterna,cinquina
#define DIM_SESSION_ID	10				//dimensione del session ID
#define DIM_ARRAY_INIZIALE 10			//dimensione iniziale dell'array destinato a contenere le informazioni dei clienti connessi
#define MAX_DIM_RIS	10000				//dimensione massima della stringa contenente ilrisultato dei metodi

//variabili globali
int porta_server;
unsigned int periodo = 300;      	//variabile che conterrà il valore in secondi del periodo impostato dall'utente, di default 300=5 min
unsigned int* numeroEstrazioni;		//variabile che conterrà il numero di estrazioni fatte dall'inizio del programma, sarà utile per controlli di sicurezza e per la lettura dei numeri delle estrazioni delle ruote

char* err;							//stringa che verrà usata per memorizzare i messaggi di errori da mandare al client
char* ruote[NUM_RUOTE]={"Bari","Cagliari","Firenze","Genova","Milano","Napoli","Palermo","Roma","Torino","Venezia","Nazionale"};     //array che conterrà il nome delle ruote
char* tipologia_giocata[MAX_NUM_SCOMMESSE]={"estratto","ambo","terno","quaterna","cinquina"};			//conterrà il nome delle tipologie di giocate in ordine
int estrazione [NUM_RUOTE][NUMERI_ESTRAZIONE];              //matrice che avrà una riga per ogni ruota e le colonne conterranno i numeri estratti nell'estrazione corrente
                                                 //le riga i rappresenta la ruota che sta alla posizione i dell'array ruote
FILE* desFileRuote [NUM_RUOTE];					//array dei descrittori dei file per salvare in memoria le estrazioni sulle ruote
fd_set* set_lista;
fd_set set_temp;			//2 liste di fd per la select, una temporanea che verrà modificata dalla select
int fdmax;

float moltiplicatore_vincite[] = { 11.23 , 250 , 4500 , 120000 , 6000000};	//quanto si vince per ogni euro giocato, la prima posizione indica per l'estratto, la seconda per l'ambo e così via fino alla cinquina

//struttura per tenere i dati utili dei vari client
struct client
{
	char user[MAX_DIM_USPSW] , psw[MAX_DIM_USPSW];		//array che conterranno l'utente e la psw del client
	char sessionID[DIM_SESSION_ID+1];					//conterrà il session id per quel client nella sessione corrente
	char indirizzoIP[16];								//conterrà l'indirizzo ip del client
};

//formato della schedina
struct schedina
{
	char user[MAX_DIM_USPSW];		//user del client che ha giocato la schedina
	int attiva;						//valore che è ad '1' se la schedina è in attesa prossima estrazione, '0' giocata relativa ad estrazione già avvenuta
	time_t orarioGiocata;			//valore che conterrà la data in cui viene giocata la schedina
	int ruote [NUM_RUOTE];			//se posizione i ha il valore '1' vuol dire che la giocata è sulla ruota di posizione i della matrice delle ruote
	int numeri [MAX_NUM_GIOCATI];				//sono i numeri giocati nella schedina, sono massimo 10 e vanno da 1 a 90 compresi
	float importi_giocata [MAX_NUM_SCOMMESSE];	//rappresentano le puntate effettuate sull'uscita di: estratto, ambo, terna, quaterna, cinquina
	float vincite [MAX_NUM_SCOMMESSE];			//rappresentano le vincite ottenute con questa schedina con: estratto, ambo, terna, quaterna, cinquina. I valori delle vincite hanno senso solo se il campo attiva della stessa schedina è = 0, che indica estrazione avvenuta e controllo per la vincita e aggiornamento dei valori già avvenuto.
};

struct client* client_connessi;			//array di client che conterrà le informazioni dei vari utenti connessi al server
struct schedina* schedine_giocate;		//array che conterrà tutte le schedine giocate dai client
int num_client_conn = DIM_ARRAY_INIZIALE;		//la dimensione attuale dell'array dei clienti connessi,all'inizio sarà uguale alla dimensione minima
int num_schedine_giocate = DIM_ARRAY_INIZIALE;	//la dimensione attuale dell'array delle schedine giocate,all'inizio sarà uguale alla dimensione minima

//struttura per tenere i dati utili dei vari socket che si connettono
struct sock_client
{
	int sock;						//conterrà descrittore del socket	
	char indirizzoIP[16];			//conterrà l'indirizzo ip del client
};

struct sock_client* sock_conn;					//array che conterrà i socket connessi
int num_sock_conn = DIM_ARRAY_INIZIALE;			//la dimensione attuale dell'array dei socket connessi,all'inizio sarà uguale alla dimensione minima

//semafori
sem_t* lista_mutex;							//semaforo per set_lista
sem_t* client_mutex;						//semaforo per client_connessi	
sem_t* schedine_mutex;						//semaforo per schedine_giocate		
sem_t* estrazione_mutex;					//semaforo per file delle estrazioni
sem_t* sock_mutex;							//semaforo per sock_conn
sem_t* sicurezza_mutex;						//semaforo per sicurezza.txt

void inizializzo_sock_conn (int min, int max)				//funzione che inizializza sock_conn
{
	int i;
	
	if (min < 0 || max > num_sock_conn)			//controllo validità dei parametri
	{
		printf("Errore passaggio di parametri in 'inizializza_sock_conn'\n");
		exit(-1);
	}
	
	sem_wait(sock_mutex);							//lock su sock_conn
	for (i=min; i < max; i++)		//scorro tutte le posizioni
	{
		sock_conn[i].sock = -1;							//valore default per sock
		strcpy(sock_conn[i].indirizzoIP,"vuoto");		//valore default per indirizzoIP
	}
	sem_post(sock_mutex);							//unlock su sock_conn
}

void aumenta_dimensione_array_sock_conn (int nuove_pos)		//funzione che aumenta la dimensione dell'array dei sock_conn ed inizializza le nuove
{															//posizioni con i valori di defaul richiamando 'inizializza_client_connessi'
	num_sock_conn += nuove_pos;								//aggiorna la dimensione dell'array dei clienti connessi
	
	sem_wait(sock_mutex);									//lock su client_connessi
	sock_conn = mremap(sock_conn,sizeof(struct sock_client)*(num_sock_conn - nuove_pos),sizeof(struct sock_client)*num_sock_conn,0);			//realloc
	if (!client_connessi)		//errore calloc
    {
		printf("Errore in fase di riallocazione della memoria per strutture interne del server\n");		//errore e chiusura del programma
		sem_post(sock_mutex);								//unlock su client_connessi
		exit(-1);
	}
	else   			//nessun errore devo inizializzare le nuove posizioni inserite
	{
		sem_post(sock_mutex);								//unlock su client_connessi
		inizializzo_sock_conn ((num_sock_conn - nuove_pos) , num_sock_conn);			//inizializzo le nuove posizioni, intervallo [vecchia dim, nuova dim] = [num_client_conn - nuove_pos, num_client_conn]
	}
}

int aggiungi_sock_conn (int sock, char* ip_client)			//funzione che aggiunge un socket e l'indirizzo ip del client nella prima posizione libera di sock_conn
{					//ritorna 1 in caso di successo e -1 in caso di errore
	int i;
	int vecchia_dim;
	
	sem_wait(sock_mutex);									//lock su sock_conn

	for (i=0; i<num_sock_conn; i++)							//scorro tutte le posizioni
	{
		if (sock_conn[i].sock == -1)						//posizione libera
		{
			sock_conn[i].sock = sock;						//aggiorno valore sock
			strcpy(sock_conn[i].indirizzoIP,ip_client);		//copio indirizzoIP
			sem_post(sock_mutex);								//unlock su sock_conn
			return 1;
		}
	}
	sem_post(sock_mutex);					//unlock su sock_conn
	
	sem_wait(sock_mutex);					//lock su sock_conn
	if (i == num_sock_conn)					//caso in cui sock_conn è pieno, devo aumentare le dimensioni
	{
		vecchia_dim = num_sock_conn;		//mi salvo l'attuale numero di posizioni di sock_conn
		
		sem_post(sock_mutex);					//unlock su sock_conn
		aumenta_dimensione_array_sock_conn(DIM_ARRAY_INIZIALE);		//aumento la dimensione di sock_conn, aggiungo DIM_ARRAY_INIZIALE posizionia sock_conn
		sem_wait(sock_mutex);					//lock su sock_conn
		
		for (i=vecchia_dim; i<num_sock_conn; i++)		//scorro tutte le nuove posizioni in cerca di una libera
		{
			if (sock_conn[i].sock == -1)		//posizione libera
			{
				sock_conn[i].sock = sock;						//aggiorno valore sock
				strcpy(sock_conn[i].indirizzoIP,ip_client);		//copio indirizzoIP
				sem_post(sock_mutex);								//unlock su sock_conn
				return 1;
			}
		}
	}
	sem_post(sock_mutex);					//unlock su sock_conn
	
	return -1;
}

int elimina_sock_conn (int sock)			//funzione che aggiunge un socket e l'indirizzo ip del client nella prima posizione libera di sock_conn
{					//ritorna 1 in caso di successo e -1 in caso di errore
	int i;
	
	sem_wait(sock_mutex);							//lock su sock_conn
	for (i=0; i<num_sock_conn; i++)		//scorro tutte le posizioni
	{
		if (sock_conn[i].sock == sock)		//trovato socket cercato
		{
			sock_conn[i].sock = -1;						//rimetto valore default a sock
			strcpy(sock_conn[i].indirizzoIP,"vuoto");	//rimetto valore default ad indirizzoIP
			sem_post(sock_mutex);							//unlock su sock_conn
			return 1;
		}
	}
	sem_post(sock_mutex);							//unlock su sock_conn
	
	return -1;
}

void recupera_ip_sock_conn (int sock, char* ip_client)		//funzione 
{
	int i;
	
	sem_wait(sock_mutex);							//lock su sock_conn
	for (i=0; i<num_sock_conn; i++)			//scorro tutte le posizioni
	{
		if (sock_conn[i].sock == sock)		//socket cercato
		{
			strcpy(ip_client,sock_conn[i].indirizzoIP);		//copio indirizzoIP associato al socket in ip_client
			sem_post(sock_mutex);							//unlock su sock_conn
			return;					//esco dal metodo
		}
	}
	sem_post(sock_mutex);							//unlock su sock_conn
	
	//se sono arrivato fino a qui vuol dire che non c'è il socket con identificativo uguale a sock passato come parametro
	strcpy(ip_client,"vuoto");
}

int ricerca_utente_psw (char* user,char* psw)		//ritorno -1 per esito negativo e la pos dell'utente corrispondente in client_connessi per esito positivo	
{
	int pos;							//variabile che conterrà il risultato da ritornare
	int i;
	
	pos = -1;							//valore di default di pos, esito negativo

	sem_wait(client_mutex);						//lock su client_connessi		
	//scorro le posizioni e ricerco una posizione che abbia utente e psw uguali a quelli passati come parametro
	for (i=0; i<num_client_conn;i++)
	{
		if (!strcmp(user,client_connessi[i].user))		//utente uguale
		{
			if (!strcmp(psw,client_connessi[i].psw))		//psw uguale
			{
				pos = i;					//trovato l'utente corrispondente, aggiorno il valore di pos
			}
		}
	}
	sem_post(client_mutex);						//unlock su clienti_connessi

	return pos;
}

void crea_file_registro_utente (char* utente)		//funzione che crea il file di testo registro per il cliente passato come parametro
{		
	//il nome del file registro dovrà avere questo formato registroUtente.txt
	char finale[]="datiSalvati/registriUtenti/registro";						//prima parte del nome del file
	char txt[]=".txt"; 								//parte finale del nome del file
	FILE* fd;
	
	//concateno con nome utente passato come parametro per fare il nome finale del file
	strcat(finale,utente);
	strcat(finale,txt);
	
	//creo il file
	if ((fd = fopen( finale , "a" ))==NULL)
	{
		printf("Errore,apertura file in memoria %s non riuscita.",finale);
		exit(-1);
	}
	if (fclose(fd) != 0)
	{
		printf("Errore, chiusura dei file in memoria %s non riuscita.\n",finale);
		exit(-1);
	}
}

void aggiungi_schedina_file_registro_utente (struct schedina s)		//funzione che aggiunge la schedina s al file registro corrispondente all'utente che l'ha giocata
{
	char finale[]="datiSalvati/registriUtenti/registro";		//prima parte del nome del file
	char txt[]=".txt"; 											//parte finale del nome del file
	FILE* fd;
	int ret;
	
	//concateno con nome utente presente nella schedina per fare il nome finale del file
	strcat(finale,s.user);
	strcat(finale,txt);
	
	//apro il file
	if ((fd = fopen( finale , "a" ))==NULL)
	{
		printf("Errore,apertura file in memoria %s non riuscita.",finale);
		exit(-1);
	}
	//il formato di una riga del file registro che corrisponde ad una schedina è il seguente: 
	//attiva  orarioGiocata  ruote  numeri  importi_giocata  vincite
	//aggiungo la schedina nel file registro
	ret = fprintf(fd, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %f %f %f %f %f %f %f %f %f %f\n", s.attiva, s.orarioGiocata,s.ruote[0],s.ruote[1],s.ruote[2],s.ruote[3],s.ruote[4],s.ruote[5],s.ruote[6],s.ruote[7],s.ruote[8],s.ruote[9],s.ruote[10],s.numeri[0],s.numeri[1],s.numeri[2],s.numeri[3],s.numeri[4],s.numeri[5],s.numeri[6],s.numeri[7],s.numeri[8],s.numeri[9],s.importi_giocata[0],s.importi_giocata[1],s.importi_giocata[2],s.importi_giocata[3],s.importi_giocata[4],s.vincite[0],s.vincite[1],s.vincite[2],s.vincite[3],s.vincite[4]);		
	if (ret <=0 )					//errore nella scrittura
	{
		printf ("Errore nella scrittura del file %s\n",finale);
		exit(-1);
	}
	
	//chiudo il file
	if (fclose(fd) != 0)
	{
		printf("Errore, chiusura dei file in memoria %s non riuscita.\n",finale);
		exit(-1);
	}
}

void aggiungi_schedina_schedine_giocate (int pos, struct schedina s)	//metodo che aggiunge una schedina passata per parametro all'array schedine_giocate
{
	int i;
	
	sem_wait(schedine_mutex);								//lock su schedine_mutex
	
	//copio i valori della schedina nella posizione pod dell'array schedine_giocate
	strcpy(schedine_giocate[pos].user,s.user);
	schedine_giocate[pos].attiva = s.attiva;				
	schedine_giocate[pos].orarioGiocata = s.orarioGiocata;
	for (i = 0; i < NUM_RUOTE; i++)
		schedine_giocate[pos].ruote[i] = s.ruote[i];
	for (i = 0; i < MAX_NUM_GIOCATI; i++)
		schedine_giocate[pos].numeri[i] = s.numeri[i];
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)
		schedine_giocate[pos].importi_giocata[i] = s.importi_giocata[i];
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)
		schedine_giocate[pos].vincite[i] = s.vincite[i];
	
	sem_post(schedine_mutex);								//unlock su schedine_mutex
}

void aggiorna_vincite_schedina_in_registro (char* utente, long int orario, float* vincite)	  //funzione che dato un user e un orario va a cercare nel registro in memoria relativo a quel user la schedina
{		//giocata in quell'orario e andrà ad aggiornare i valori delle vincite e il valore di attiva, si vanno a modificare schedine non ancora analizzate per l'estrazione con i nuovi valori dopo l'estrazione
	struct schedina schedina_appoggio;						//servirà per metterci i dati di appoggio
	//variabili per ricreare il corretto nome del file registro
	char finale[]="datiSalvati/registriUtenti/registro";								//prima parte del nome del file
	char txt[]=".txt"; 										//parte finale del nome del file

	long int posizione_sovrascrivere;						//conterrà la posizione del puntatore all'interno del file in corrispondenza dell'inizio della riga da sostituire
	int i;
	int ret;												//variabile per il valore di ritorno delle operazioni su file
	int trovato;											//variabile che dirà se ho trovato la riga cercato o meno, 0 non trovato 1 trovato
	FILE* fd;												//conterrà il file descriptor del registro	
	
	trovato = 0;											//valore di default
	//calcolo il nome del registro
	strcat(finale,utente);
	strcat(finale,txt);
	
	if ((fd = fopen( finale , "r+" ))==NULL)					//apro il file registro
	{
		printf("Errore apertura file in memoria");
		exit(-1);
	}
	//devo scorrere tutte le righe del file ed analizzarne il contenuto fino a che non trovo la schedina desiderata o non arrivo in fondo
	do
	{
		posizione_sovrascrivere = ftell(fd);							//prendo la posizione del puntatore ad inizio riga
		
		if ((ret = fscanf(fd , "%d",&schedina_appoggio.attiva)) == EOF)							//controllo di non essere arrivato in fondo al file
			break;
		if (ret <=0 )					//errore nella lettura della variabile attiva
		{
			printf ("Errore nella lettura del file %s parametro attiva\n",finale);
			exit(-1);
		}
		
		//leggo orario
		ret = fscanf(fd , "%ld",&schedina_appoggio.orarioGiocata);
		if (ret <=0 )					//errore nella lettura
		{
			printf ("Errore nella lettura del file %s parametro orario\n",finale);
			exit(-1);
		}
		if (schedina_appoggio.orarioGiocata == orario)//controllo che questa sia la giocata che cerco
			trovato = 1;					//aggiorno il valore, ho trovato la schedina che cercavo
		
		//leggo informazioni sulle ruote
		for (i=0; i<NUM_RUOTE; i++)				//devo leggere un numero di interi (compresi fra 0 e 1) uguale al numero delle ruote
		{
			ret = fscanf(fd , "%d",&schedina_appoggio.ruote[i]);		//leggo ruota di posizione i e copio il valore
			if (ret <=0 )						//errore nella lettura
			{
				printf ("Errore nella lettura del file %s parametro ruote\n",finale);
				exit(-1);
			}
		}
		
		//leggo i numeri giocati
		for (i=0; i<MAX_NUM_GIOCATI; i++)				//devo leggere un numero di interi uguale al numero massimo dei numeri giocabili in una schedina
		{
			ret = fscanf(fd , "%d",&schedina_appoggio.numeri[i]);		//leggo numero giocato di posizione i e copio il valore
			if (ret <= 0 )					//errore nella lettura
			{
				printf ("Errore nella lettura del file %s parametro numeri giocati\n",finale);
				exit(-1);
			}
		}
		
		//leggo gli importi giocati
		for (i=0; i<MAX_NUM_SCOMMESSE; i++)				//devo leggere un numero di float uguale al numero massimo delle tipologie di giocata in una schedina
		{
			ret = fscanf(fd , "%f",&schedina_appoggio.importi_giocata[i]);		//leggo importo di posizione i e lo copio
			if (ret <= 0 )						//errore nella lettura
			{
				printf ("Errore nella lettura del file %s parametro importi giocate\n",finale);
				exit(-1);
			}
		}
		
		//leggo le vincite della schedina
		for (i=0; i<MAX_NUM_SCOMMESSE; i++)				//devo leggere un numero di float uguale al numero massimo delle tipologie di giocata in una schedina
		{
			ret = fscanf(fd , "%f",&schedina_appoggio.vincite[i]);		//leggo vincita di posizione i e lo copio
			if (ret <= 0 )					//errore nella lettura
			{
				printf ("Errore nella lettura del file %s parametro vincite %d\n",finale,i);
				exit(-1);
			}
		}
		//ho letto tutta la riga
	}while(trovato == 0);
	
	if (posizione_sovrascrivere != 0)			//la riga che cerco non è la prima del file devo tener conto del \n a fine di ogni riga
		posizione_sovrascrivere++;
	
	if (trovato == 1)//controllo se ho trovato o meno la schedina cercata
	{					//schedina trovata ora devo sostituirla
		if (fseek(fd, posizione_sovrascrivere, SEEK_SET) != 0)		//porto il puntatore del file ad inizio della riga da sovrascrivere
		{
			printf("Errore gestione diretta del puntatore del file %s\n",finale);
			exit(-1);
		}
		
		//ora sovrascrivo la riga vecchia con i dati aggiornati, con le vincite passate come parametro
		ret = fprintf(fd, "%d %ld ", 0, schedina_appoggio.orarioGiocata);		//metto a 0 attiva perchè ora conterrà i dati di quando è già stata fatta l'estrazione su cui era stata giocata
		if (ret <=0 )					//errore nella scrittura
		{
			printf ("Errore nella scrittura del file %s\n",finale);
			exit(-1);
		}
		//scrivo le ruote
		for (i=0; i<NUM_RUOTE; i++)				//devo scrivere un numero di interi (compresi fra 0 e 1) uguale al numero delle ruote
		{
			ret = fprintf(fd , "%d ",schedina_appoggio.ruote[i]);		//scrivo ruota di posizione i
			if (ret <=0 )					//errore nella scrittura
			{
				printf ("Errore nella scrittura del file %s\n",finale);
				exit(-1);
			}
		}
		//scrivo i numeri giocati
		for (i=0; i<MAX_NUM_GIOCATI; i++)				//devo scrivere un numero di interi uguale al numero massimo dei numeri giocabili in una schedina
		{
			ret = fprintf(fd , "%d ",schedina_appoggio.numeri[i]);		//scrivo numero giocato di posizione i
			if (ret <= 0 )					//errore nella scrittura
			{
				printf ("Errore nella scrittura del file %s\n",finale);
				exit(-1);
			}
		}
		
		//scrivo gli importi giocati
		for (i=0; i<MAX_NUM_SCOMMESSE; i++)				//devo scrivere un numero di float uguale al numero massimo delle tipologie di giocata in una schedina
		{
			ret = fprintf(fd , "%f ",schedina_appoggio.importi_giocata[i]);		//scrivo importo di posizione i
			if (ret <= 0 )					//errore nella scrittura
			{
				printf ("Errore nella scrittura del file %s\n",finale);
				exit(-1);
			}
		}
		
		//scrivo i nuovi valori passati come parametri delle vincite
		for (i=0; i<MAX_NUM_SCOMMESSE; i++)				//devo scrivere un numero di float uguale al numero massimo delle tipologie di giocata in una schedina
		{
			ret = fprintf(fd , "%f",vincite[i]);		//sovrascrivo vecchi valori
			if (i != (MAX_NUM_SCOMMESSE -1))
				ret = fprintf(fd , "%c",' ');
			if (ret <= 0 )					//errore nella lettura
			{
				printf ("Errore nella lettura del file %s\n",finale);
				exit(-1);
			}
		}
		ret = fprintf(fd ,"\n");		//metto l'accapo per mantenere il formato scelto nel documento
		if (ret <= 0 )					//errore nella lettura
		{
			printf ("Errore nella lettura del file %s\n",finale);
			exit(-1);
		}
	}
	
	if (fclose(fd) != 0)									//chiudo il registro
	{
		printf("Errore nella chiusura dei file in memoria\n");
		exit(1);
	}
	
	if (trovato == 0)		//non ho trovato la schedina nel file di testo, errore ogni schedina giocata di quel client deve trovarsi anche nel file registro
	{
		printf("Inconsistenza dei dati nel file %s\n",finale);
		exit(-1);						
	}
}

int fattoriale (int n)				//funzione che calcola il fattoriale del numero passato come parametro
{
	if (n < 0)			//errore nel passaggio dei parametri
	{
		printf("Errore nel passaggio di parametri nella funzione per il calcolo del fattoriale\n");
		return-1;
	}
	if (n == 0)			//caso base
		return 1;
	else
		return n*fattoriale(n-1);		//ricorsione
}

void inizializza_client_connessi (int min, int max)			//funzione che inizializza con i valori di default tutte le posizioni di client_connessi
{															//min e max rappresentano l'intervallo delle posizioni dell'array che vengono inizializzate
	int i,j;
	
	if (min < 0 || max > num_client_conn)			//controllo validità dei parametri
	{
		printf("Errore passaggio di parametri in 'inizializza_client_connessi'.\n");		
		exit(-1);
	}
	
	sem_wait(client_mutex);									//lock su clienti_connessi
	
	for (i = min; i < max; i++)					//for per scorrere tutte le posizioni dell'array
	{
		for (j = 0; j < MAX_DIM_USPSW; j++)				//metto i valori di default
		{
			client_connessi[i].user[j] = 0;
			client_connessi[i].psw[j]= 0;
		}
		for (j = 0; j < DIM_SESSION_ID+1; j++)				//metto i valori di default
		{
			client_connessi[i].sessionID[j] = 0;			//metto i valori di sessionID
		}
		strcpy(client_connessi[i].indirizzoIP,"vuoto");			//metto valore default per ip
	}
	sem_post(client_mutex);									//unlock su clienti_connessi
}

void elimina_client_connessi (char* utente,char* ip_client)			//funzione che cerca il client che ha utente ed indirizzoIp uguali a quelli passati come parametro e lo elimina
{														
	int i,j;
	
	sem_wait(client_mutex);									//lock su client_connessi
	for (i = 0; i < num_client_conn; i++)					//for per scorrere tutte le posizioni dell'array
	{
		if (!strcmp(client_connessi[i].user , utente) && !strcmp(client_connessi[i].indirizzoIP , ip_client))				//è client da eliminare
		{
			for (j = 0; j < MAX_DIM_USPSW; j++)				//metto i valori di default
			{
				client_connessi[i].user[j] = 0;
				client_connessi[i].psw[j]= 0;
			}
			for (j = 0; j < DIM_SESSION_ID+1; j++)				//metto i valori di default
			{
				client_connessi[i].sessionID[j] = 0;			//metto i valori di sessionID
			}
			strcpy(client_connessi[i].indirizzoIP,"vuoto");			//metto valore default per ip
		}
	}
	sem_post(client_mutex);									//unlock su client_connessi
}

void inizializza_schedine_giocate (int min, int max)			//funzione che inizializza con i valori di default tutte le posizioni di schedine_giocate
{															//min e max rappresentano l'intervallo delle posizioni dell'array che vengono inizializzate
	int i,j;
	
	if (min < 0 || max > num_schedine_giocate)			//controllo validità dei parametri
	{
		printf("Errore passaggio di parametri in 'inizializza_schedine_giocate'.\n");
		exit(-1);
	}
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	for (i = min; i < max; i++)					//for per scorrere tutte le posizioni dell'array
	{
		//metto i valori di default per user
		schedine_giocate[i].user[0] = 'v';
		schedine_giocate[i].user[1] = 'u';
		schedine_giocate[i].user[2] = 'o';
		schedine_giocate[i].user[3] = 't';
		schedine_giocate[i].user[4] = 'o';
		
		schedine_giocate[i].attiva = -1;			//valore di default diverso da 0 o 1
		schedine_giocate[i].orarioGiocata = 0;		//valore di default
		
		for (j = 0; j < NUM_RUOTE; j++)						//ruote
			schedine_giocate[i].ruote[j] = 0;
		for (j = 0; j < MAX_NUM_GIOCATI; j++)				//numeri giocati
			schedine_giocate[i].numeri[j] = 0;
		for (j = 0; j < MAX_NUM_SCOMMESSE; j++)				//tipi scommessa
			schedine_giocate[i].importi_giocata[j] = 0;
		for (j = 0; j < MAX_NUM_SCOMMESSE; j++)				//vincite
			schedine_giocate[i].vincite[j] = 0;
	}
	sem_post(schedine_mutex);						//unlock su schedine_giocate
}

void aumenta_dimensione_array_clienti_connessi (int nuove_pos)		//funzione che aumenta la dimensione dell'array dei client connessi ed inizializza le nuove
{																	//posizioni con i valori di defaul richiamando 'inizializza_client_connessi'
	num_client_conn += nuove_pos;					//aggiorna la dimensione dell'array dei clienti connessi
	
	sem_wait(client_mutex);											//lock su client_connessi
	client_connessi = mremap(client_connessi,sizeof(struct client)*(num_client_conn - nuove_pos),sizeof(struct client)*num_client_conn,0);			//realloc
	if (!client_connessi)		//errore calloc
    {
		printf("Errore in fase di riallocazione della memoria per strutture interne del server\n");		//errore e chiusura del programma
		sem_post(client_mutex);								//unlock su client_connessi
		exit(-1);
	}
	else   			//nessun errore devo inizializzare le nuove posizioni inserite
	{
		sem_post(client_mutex);								//unlock su client_connessi
		inizializza_client_connessi ((num_client_conn - nuove_pos) , num_client_conn);			//inizializzo le nuove posizioni, intervallo [vecchia dim, nuova dim] = [num_client_conn - nuove_pos, num_client_conn]
	}
}

void aumenta_dimensione_array_schedine_giocate (int nuove_pos)		//funzione che aumenta la dimensione dell'array delle schedine giocate ed inizializza le nuove
{																	//posizioni con i valori di defaul richiamando 'inizializza_schedine_giocate'
	num_schedine_giocate += nuove_pos;					//aggiorna la dimensione dell'array delle schedine giocate
	
	sem_wait(schedine_mutex);					//lock su schedine_giocate
	schedine_giocate = mremap(schedine_giocate,sizeof(struct schedina)*(num_schedine_giocate - nuove_pos),sizeof(struct schedina)*num_schedine_giocate,0);			//realloc
	if (!schedine_giocate)		//errore calloc
    {
		printf("Errore in fase di riallocazione della memoria per strutture interne del server\n");		//errore e chiusura del programma
		sem_post(schedine_mutex);					//unlock su schedine_giocate
		exit(1);
	}
	else   			//nessun errore devo inizializzare le nuove posizioni inserite
	{
		sem_post(schedine_mutex);					//unlock su schedine_giocate
		inizializza_schedine_giocate ((num_schedine_giocate - nuove_pos) , num_schedine_giocate);			//inizializzo le nuove posizioni, intervallo [vecchia dim, nuova dim] = [num_schedine_gicoate - nuove_pos, num_schedine_giocate]
	}
}


void apertura_chiusura_file_estrazioni_ruote (int apertura)			//funzione che apre e chiude i file in cui vengono salvate le giocate, 
{																	//se apertura=1 aprirà i file se =0 chiuderà i file
	int i;					//variabile indice per il for

	if (apertura==1)		//apro i file e salvo i file descriptor nell'array desFileRuote
	{
		if (*numeroEstrazioni == 0)				//caso di prima apertura dei file, apro i file in scrittura così eventuali vecchi dati presenti nei file verranno cancellati
		{					//inizio if 1
			if ((desFileRuote[0] = fopen("datiSalvati/estrazioniRuote/Bari.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria");
				exit(-1);
			}
			if ((desFileRuote[1] = fopen("datiSalvati/estrazioniRuote/Cagliari.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[2] = fopen("datiSalvati/estrazioniRuote/Firenze.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[3] = fopen("datiSalvati/estrazioniRuote/Genova.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[4] = fopen("datiSalvati/estrazioniRuote/Milano.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[5] = fopen("datiSalvati/estrazioniRuote/Napoli.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[6] = fopen("datiSalvati/estrazioniRuote/Palermo.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[7] = fopen("datiSalvati/estrazioniRuote/Roma.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[8] = fopen("datiSalvati/estrazioniRuote/Torino.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[9] = fopen("datiSalvati/estrazioniRuote/Venezia.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[10] = fopen("datiSalvati/estrazioniRuote/Nazionale.txt", "w" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
		}					//fine if 1
		else 				//inizio else 1
		{
			if ((desFileRuote[0] = fopen("datiSalvati/estrazioniRuote/Bari.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria");
				exit(-1);
			}
			if ((desFileRuote[1] = fopen("datiSalvati/estrazioniRuote/Cagliari.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[2] = fopen("datiSalvati/estrazioniRuote/Firenze.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[3] = fopen("datiSalvati/estrazioniRuote/Genova.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[4] = fopen("datiSalvati/estrazioniRuote/Milano.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[5] = fopen("datiSalvati/estrazioniRuote/Napoli.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[6] = fopen("datiSalvati/estrazioniRuote/Palermo.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[7] = fopen("datiSalvati/estrazioniRuote/Roma.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[8] = fopen("datiSalvati/estrazioniRuote/Torino.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[9] = fopen("datiSalvati/estrazioniRuote/Venezia.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
			if ((desFileRuote[10] = fopen("datiSalvati/estrazioniRuote/Nazionale.txt", "a+" ))==NULL)
			{
				printf("Errore apertura file in memoria\n");
				exit(-1);
			}
		}			//chiusura else 1
	}
	else				//chiudo i file
	{
		for (i=0; i<NUM_RUOTE;i++)//for per chiudere tutti i descrittori di file
		{
			if (fclose(desFileRuote[i])!=0)		//chiusura file di salvataggio delle estrazioni delle ruote
			{
				printf("Errore nella chiusura dei file in memoria\n");
				exit(1);
			}
		}
	}
}

void leggi_estrazione_lotto(int pos, int num, char* output)		//metodo che va a leggere le ultime num estrazioni della ruota di indice pos passati come parametro
{		//per leggere le ultime num estrazioni dovrò andare a leggere prima 'numeroestrazioni - num' che equivale ad andare a leggere il numero
		//totale delle estrazioni avvenute meno quelle che mi interessano, queste letture non mi interessano, fatte queste potrò ad andare a 
		//leggere effettivamente le estrazioni che mi interessano ordinate dalla più vecchia alla più recente. (implementazione della lettura in testa al file)
		//vado a mettere il risultato che devo stampare a video in output e lo restituisco
	int i,lVuote,estr[5],ret;			//variabili per il for e per le letture
	char valori[14];					//conterrà i valori letti da lettura e convertiti in char
	lVuote = *numeroEstrazioni - num;			//quanti cicli di lettura da 5 devo fare prima di arrivare ai dati che mi interessano
	
	//controlli per parametri con valori non corretti
	if ((pos < 0) || (pos >= NUM_RUOTE) || (num <= 0) || (num > *numeroEstrazioni) || (*numeroEstrazioni == 0))
	{
		strcpy(output,"Errore, estrazioni richieste maggiori delle estrazioni avvenute fino ad ora.\n");			//assegnazione stringa di errore
		return;
	} 
	
	//metto prima parte della ruota in output
	strcat(output,ruote[pos]);
	strcat(output,"\n");
	
	apertura_chiusura_file_estrazioni_ruote(1);			//apertura dei file in memoria
	for (i=0;i<lVuote;i++)				//letture che non interessano
	{
		ret = fscanf(desFileRuote[pos],"%d %d %d %d %d",&estr[0],&estr[1],&estr[2],&estr[3],&estr[4]);
		if (ret == EOF)
			printf("Errore: lettura dell'estrazioni da file non riuscita\n");
	}
	for (i=lVuote;i<(lVuote + num);i++)
	{
		ret = fscanf(desFileRuote[pos],"%d %d %d %d %d",&estr[0],&estr[1],&estr[2],&estr[3],&estr[4]);
		if (ret == EOF)
			printf("Errore: lettura dell'estrazioni da file non riuscita\n");
		
		//metto nella stringa valori col giusto formato
		sprintf(valori,"%d %d %d %d %d",estr[0],estr[1],estr[2],estr[3],estr[4]);
	
		strcat(output,valori);			//aggiungo alla stringa di output
		strcat(output,"\n");
	}
	apertura_chiusura_file_estrazioni_ruote(0);		//chiusura dei file in memoria
}

int ruote_giocate_schedina (int pos_schedina)		//ritorna il numero delle ruote giocate nella schedina di posizione pos_schedina in schedine_giocate
{
	int cont;				//var contatore per il numero di ruote su cui si è puntato nella schedina di posizione pos_schedina in schedine_giocate
	int i;
	
	cont = 0;				//setto il valore iniziale
	
	sem_wait(schedine_mutex);							//lock su schedine_giocate
	for (i=0; i<NUM_RUOTE; i++)							//scorro tutte le ruote e vedo quali sono state giocate
	{
		if (schedine_giocate[pos_schedina].ruote[i])
			cont++;										//ha puntato su questa ruota incremento cont
	}
	sem_post(schedine_mutex);							//unlock su schedine_giocate
	
	return cont;			
}

int numeri_giocati_schedina (int pos_schedina)		//ritorna il numero dei numeri giocati nella schedina di posizione pos_schedina in schedine_giocate
{
	int cont;
	int i;
	
	cont = 0;				//setto il valore iniziale
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	for (i=0; i<MAX_NUM_GIOCATI; i++)			//scorro tutti i numeri giocati e li conto
	{
		if (schedine_giocate[pos_schedina].numeri[i] != 0)
			cont++;								//incremento cont
	}
	sem_post(schedine_mutex);								//unlock su schedine_giocate
	
	return cont;			
}

int numeri_vincenti_ruota (int pos_ruota, int pos_schedina)		//dice quanti numeri vincenti sono stati giocati nella schedina di posizione pos_schedina in schedine_giocate nella ruota di posizione pos_ruota
{
	int cont;
	int i,j;
	
	cont = 0;				//setto il valore iniziale
	j = 0;
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	do  				//inizio do while per scorrere tutti i numeri gioati nella schedina
	{
		if (schedine_giocate[pos_schedina].numeri[j] == 0)			//non ci sono più numeri giocati nella schedina
			break;
		for (i = 0; i < NUMERI_ESTRAZIONE; i++)
		{					
			sem_wait(estrazione_mutex);									//lock su estrazione
			
			if  (estrazione[pos_ruota][i] == schedine_giocate[pos_schedina].numeri[j])		//confronto il numero estratto nella ruota 'pos_ruota' in posizione i con il numero giocato
				cont++;														//numero estratto incremento num
			
			sem_post(estrazione_mutex);									//unlock su estrazione
		}
		j++;
	} while (j < MAX_NUM_GIOCATI);		//fine do while
	sem_post(schedine_mutex);							//unlock su schedine_giocate
	
	return cont;				
}

int numeri_vincenti (int pos)			//funzione che dato come parametro un intero che rappresenta la posizione della schedina da esaminare nell'array schedine_giocate restituisce il massimo numero di numeri vincenti nelle varie ruote
{
	int numeri_vincenti;				//variabile che conterrà quanti numeri vincenti ci sono nella ruota con più numeri vincenti fra quelle giocate
	int num;							//contatore per i numeri vincenti
	int i;							//variabili contatori
	numeri_vincenti = num = 0;			//all'inizio 0
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	//controlla su quali ruote ha giocato e su ciascuna se ci sono numeri vincenti e salva il numero massimo di numeri vincenti
	for (i=0; i < NUM_RUOTE ; i++)
	{
		if (schedine_giocate[pos].ruote[i])			//ruota su cui si è puntato
		{
			sem_post(schedine_mutex);								//unlock su schedine_giocate
			//controllo quanti sono i numeri vincenti
			num = numeri_vincenti_ruota(i,pos);			//devo confrontare tutti i numeri giocati sulla schedina con i numeri usciti per la ruota giocata
			sem_wait(schedine_mutex);								//lock su schedine_giocate
			
			if (num > numeri_vincenti)//questa ruota presenta più numeri vincenti di quelle già visionate quindi aggiorno numeri_vincenti
				numeri_vincenti = num;
		}
	}
	sem_post(schedine_mutex);								//unlock su schedine_giocate
	
	return numeri_vincenti;				//ritorna quanti numeri vincenti
}

int tipologia_minore_giocata_schedina (int pos)			//funzione che va a vedere la schedina di indice pos in schedine_giocate e ritorna il numero della tipologia giocata che necessita di minor numeri vincenti
{
	int tip;					//conterrà il valore da ritornare
	int i;						//variabile contatore
	
	tip = -1;					//valore di default che se ritornato segnala errore
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	
	for (i=0; i<MAX_NUM_SCOMMESSE; i++)
	{
		if (schedine_giocate[pos].importi_giocata[i] != 0)			//si è giocato su quella tipologia
		{
			tip = i;
			break;
		}
	}
	sem_post(schedine_mutex);								//unlock su schedine_giocate
	
	return tip;					//ritorna 0:estratto, 1:ambo, 2:terna, 3:quaterna, 4:cinquina, -1:nessuna tipologia giocata errore
}

void calcola_vincite_schedina (int pos)		//funzione che calcola tutte le vincite della schedina di posizione pos in schedine_giocate e li salva in schedine_giocate
{
	int i,j;									//var contatore
	float cont_vincite[MAX_NUM_SCOMMESSE];		//array che conterrà il totale delle giocate per ogni tipologia
	int num_vinc_ruota,num_giocati,num_ruote_giocate;
	float n_vinc;				//sarà il numero di combinazioni vincenti ottenute dal numero di numeri vincenti e la tipologia presa in esame
	float comb_poss;			//sarà il numero di combinazioni semplici possibili delle possibili combinazioni vincenti dato i numeri giocati e la tipologia presa in esame
	
	//inizializzo valori di defaul di cont_vincite	
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)
		cont_vincite[i] = 0;
		
	num_giocati = numeri_giocati_schedina(pos);
	num_ruote_giocate = ruote_giocate_schedina(pos);
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)//inizio for0 per scorrimento delle tipologie giocate nella schedina
	{	//i indicherà la tipologia della giocata che si sta prendendo in considerazione
		if (schedine_giocate[pos].importi_giocata[i] != 0)			//si è giocato su quella tipologia
		{
			for (j = 0; j < NUM_RUOTE; j++)			//scorro tutte le ruote
			{									//inizio for1
				//devo calcolare le vincite per la ruota solo se ho giocato quella ruota
				if (schedine_giocate[pos].ruote[j] == 0)
				{
					continue;			//non ho giocato questa ruota salto alla prossima
				}
				sem_post(schedine_mutex);								//unlock su schedine_giocate
				num_vinc_ruota = numeri_vincenti_ruota(j,pos);			//calcolo i numeri vincenti in questa ruota
				sem_wait(schedine_mutex);								//lock su schedine_giocate
				
				if (num_vinc_ruota < i+1)							//controllo che i numeri vincenti su questa ruota siano >= tipologia giocata
					continue;		//non posso avere vincite su questa ruota passo alla successiva
				
				//calcolo della vincita per la tipologia i nella ruota j
				n_vinc = fattoriale(num_vinc_ruota) / (fattoriale(num_vinc_ruota -(i+1)) * fattoriale(i+1));
				comb_poss = fattoriale(num_giocati) / (fattoriale(num_giocati -(i+1)) * fattoriale(i+1));
				
				cont_vincite[i] += (schedine_giocate[pos].importi_giocata[i] * moltiplicatore_vincite[i] * n_vinc) / (comb_poss * num_ruote_giocate);
			}									//fine for1
		}
	}				//fine for0
	//aggiorno le schedine con gli importi vinti
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)//inizio for0 per scorrimento delle tipologie giocate nella schedina
	{	//i indicherà la tipologia della giocata che si sta prendendo in considerazione
		if (cont_vincite[i] != 0)			//si è vinto per quella tipologia
		{
			schedine_giocate[pos].vincite[i] = cont_vincite[i];		//metto vincite
		}
	}
	sem_post(schedine_mutex);								//lock su schedine_giocate
}

void calcolo_vincite()			//funzione che viene richiamata dopo l'estrazione e deve andare a controllare se ci sono schedine vincenti e calcolare le loro vincite
{
	int i;
	int num_vinc;
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	//fare per ogni schedina giocata in memoria le seguenti operazioni
	for (i=0 ; i < num_schedine_giocate ; i++)			//devo scorrere tutto l'array ed ignorare le posizioni vuote
	{
		if (!strcmp("vuoto",schedine_giocate[i].user) && schedine_giocate[i].orarioGiocata == 0)				//posizione vuota
		{
			continue;									//passo alla prossima posizione dell'array
		}
		
		//la schedina è presente, devo controllare che sia attiva cioè che sia una schedina di cui devo calcolare la vincita e di cui non ho già calcolato la vincita precedentemente
		if (schedine_giocate[i].attiva == 0)
			continue;									//schedina riferita ad un estrazione passata, passo alla prossima posizione dell'array
		
		sem_post(schedine_mutex);								//unlock su schedine_giocate
		
		//controllo che se ci sono o meno numeri vincenti
		num_vinc = numeri_vincenti(i);
		
		sem_wait(schedine_mutex);								//lock su schedine_giocate
		
		if (num_vinc == 0)
		{
			schedine_giocate[i].attiva = 0;				//aggiorno valore attivo, la schedina risulterà processata per il controllo della vincita
			continue;									//questa schedina non ha numeri vincenti passare avanti
		}
		
		sem_post(schedine_mutex);								//unlock su schedine_giocate	
		//altro controllo, ogni tipologia di scommessa avrà un numero minimo di numeri vincenti nella ruota per far si che la schedina risulti vincente
		//estratto minimo 1 numero vincete, ambo minimo 2, terna minimo 3, quaterna minimo 4, cinquina minimo 5
		if (num_vinc < tipologia_minore_giocata_schedina(i))
		{
			sem_wait(schedine_mutex);							//lock su schedine_giocate
			schedine_giocate[i].attiva = 0;						//aggiorno valore attivo, la schedina risulterà processata per il controllo della vincita
			sem_post(schedine_mutex);							//unlock su schedine_giocate
			continue;			//non ci sono i numeri vincenti necessari a causare una vincita della schedina si passa alla successiva
		}
		
		//la schedina è vincente ora bisogna calcolare le vincite e aggiornare i dati della schedina
		calcola_vincite_schedina(i);
		//aggiorno dati della schedina nel file registro
		aggiorna_vincite_schedina_in_registro(schedine_giocate[i].user,schedine_giocate[i].orarioGiocata,schedine_giocate[i].vincite);
		
		sem_wait(schedine_mutex);					//lock su schedine_giocate
		schedine_giocate[i].attiva = 0;				//non dovrà più essere attiva la schedina
		sem_post(schedine_mutex);					//unlock su schedine_giocate
	}												//fine for di scorrimento di schedine_giocate
	sem_post(schedine_mutex);						//unlock su schedine_giocate
}

void estrazione_lotto(int sig)         //funzione che si occupa di fare una nuova estrazione, sarà messa come handler per il segnale di alarm, così verrà gestita la temporizzazione di ogni estrazione
{
    int i,j,ret;                            //variabili per il for e per il ritorno delle scritture
    char* str = "\n";						//mi server per l'ordinamento a matrice nei file in memoria
    
    apertura_chiusura_file_estrazioni_ruote(1);		//apertura dei file in memoria
    //estraggo i numeri e li assegno alla matrice estrazione
    for (i=0 ; i< NUM_RUOTE ; i++)
    {
        for(j=0 ; j< NUMERI_ESTRAZIONE ; j++)
        {
            estrazione [i][j] = ((rand()%90)+1);     //estrazione di un numero compreso tra [1-90] e salvataggio nella matrice

            ret = fprintf(desFileRuote[i], "%d ", estrazione [i][j]);		//vado a scrivere le estrazioni nel relativo file in memoria
            if (ret <=0 )
            {
				printf ("Errore nella scrittura delle estrazioni nei file in memoria\n");
				exit(-1);
			}
        }
        ret = fprintf(desFileRuote[i], "%s", str);		//vado a scrivere le estrazioni nel relativo file in memoria
        if (ret <=0 )
        {
			printf ("Errore nella scrittura delle estrazioni nei file in memoria\n");
			exit(-1);
		}
    }    
    *numeroEstrazioni = *numeroEstrazioni + 1;				//incremento il numero delle estrazioni avvenuto sulle ruote 
    apertura_chiusura_file_estrazioni_ruote(0);				//chiusura dei file in memoria
    
    //richiamare il metodo per controllare le vincite
    calcolo_vincite();
    
    printf("Estrazione numero %d avvenuta.\n",*numeroEstrazioni);		//segnala l'avvenuta estrazione 			
    
    alarm (periodo);                        //setto l'allarme che mi dirà lo scadere di ogni periodo
}

void crea_sessionID (int posUtente)         //crea il sessionID per l'utente alla posizione posUtente nell'array di struttura per i dati degli utente
{
    int i,scelta;              //variabili per il for
    
    for(i=0;i<DIM_SESSION_ID;i++)               //inizio for
    {       
        //ad ogni ciclo verrà generata una cifra del sessionID, questa potrà essere numerica o un carattere minuscolo o maiuscolo
        //il primo valore randomico generato andrà a stabilire fra quali di questi 3 insiemi sarà scelta la cifra casuale
        scelta = rand()%3;
 
        sem_wait(client_mutex);							//lock su client_connessi
        switch(scelta)                               	//inizio switch
        {
            case 0:         //l'insieme da cui estrarre casualmente è quello dei numeri da 0 a 9
                  {
                   client_connessi[posUtente].sessionID[i]=48 + rand()%10;
                   break;
                  }
            case 1:         //l'insieme da cui estrarre casualmente è quello delle lettere maiuscole
                  {
                   client_connessi[posUtente].sessionID[i]=65 + rand()%26;
                   break;
                  }
            default:        //l'insieme da cui estrarre casualmente è quello delle lettere minuscole
                  {
                   client_connessi[posUtente].sessionID[i]=97 + rand()%26;
                  }
        };                       //fine switch
 
		sem_post(client_mutex);									//unlock su client_connessi
    }               //fine for
}

void creazione_cartelle_programma ()		//funzione che crea le cartelle necessarie per la disposizione dei file txt che dovranno contenere i dati che verranno salvati dal programma
{	
	FILE* fd;								//conterrà il descrittore del file 'sicurezza.txt'
	//creare cartella datiSalvati
	if (mkdir("datiSalvati",0777) == -1)			//se da errore devo controllare che errore sia
	{
		if (errno != EEXIST)						//caso in cui la cartella è già presente, non deve portare alla terminazione del programma ne ad una segnalazione di errore
		{
			printf ("Errore nella creazione delle cartelle necessarie al salvataggio dei dati prodotti dall'applicazione.\n");
			exit(-1);
		}
	}
	//all'interno della cartella datiSalvati ci saranno
	//creare cartella estrazioniRuote
	if (mkdir("datiSalvati/estrazioniRuote",0777) == -1)		//se da errore devo controllare che errore sia
	{
		if (errno != EEXIST)						//caso in cui la cartella è già presente, non deve portare alla terminazione del programma ne ad una segnalazione di errore
		{
			printf ("Errore nella creazione delle cartelle necessarie al salvataggio dei dati prodotti dall'applicazione.\n");
			exit(-1);
		}
	}
	//creare cartella registriUtenti
	if (mkdir("datiSalvati/registriUtenti",0777) == -1)			//se da errore devo controllare che errore sia
	{
		if (errno != EEXIST)						//caso in cui la cartella è già presente, non deve portare alla terminazione del programma ne ad una segnalazione di errore
		{
			printf ("Errore nella creazione delle cartelle necessarie al salvataggio dei dati prodotti dall'applicazione.\n");
			exit(-1);
		}
	}
	//creo il file 'sicurezza.txt'
	//apertura file
	if ((fd = fopen("datiSalvati/sicurezza.txt", "w" ))==NULL)			//apertura file in lettura
	{
		printf("Errore apertura file in memoria di 'sicurezza.txt'.\n");				//errore
		sem_post(sicurezza_mutex);										//unlock su sicurezza.txt
		exit(-1);
	}
	//chiusura file
	if (fclose(fd)!=0)		//chiusura file sicurezza
	{
		printf("Errore nella chiusura dei file in memoria 'sicurezza.txt'.\n");		//errore
		exit(-1);
	}
}

void aggiungi_blocco_account_sicurezza (char* utente, char* ip)		//funzione che andrà a creare i dati del blocco dell'account ed andrà ad inserirli nel file di testo sicurezza
{																	//per ogni blocco il file conterrà una riga con ilseguente formato: NomeUtente  IPBloccato  OrarioInizioBlocco  OrarioFineBlocco
	time_t orarioInizioBlocco, orarioFineBlocco;					//variabile che conterrà l'orario corrente che sarà quello di inizio blocco e variabile che conterrà l'orario di fine blocco
    FILE* fd;
    int ret;                            					//variabile per il ritorno delle scritture
    char* str = "\n";										//mi server per l'ordinamento a tabella nei file in memoria
    
    orarioInizioBlocco = time(0);							//inizializzo con orario corrente
	orarioFineBlocco = orarioInizioBlocco + 1800;			//inizializzon con orario in cui finirà il blocco dell'utente
    
    sem_wait(sicurezza_mutex);								//lock su sicurezza.txt
    
    if ((fd = fopen("datiSalvati/sicurezza.txt", "a" ))==NULL)		//apertura file in append
	{
		printf("Errore apertura file in memoria.\n");				//errore			
		sem_post(sicurezza_mutex);							//unlock su sicurezza.txt
		exit(-1);
	}
	//fase di scrittura nel file 'sicurezza.txt'
	ret = fprintf(fd, "%s %s %ld %ld %s", utente, ip, orarioInizioBlocco, orarioFineBlocco, str);		//vado a scrivere le estrazioni nel relativo file in memoria
	if (ret <=0 )					//errore nella scrittura
    {
		printf ("Errore nella scrittura delle estrazioni nel file in memoria.\n");
		sem_post(sicurezza_mutex);							//unlock su sicurezza.txt
		exit(-1);
	}
	if (fclose(fd)!=0)		//chiusura file sicurezza
	{
		printf("Errore nella chiusura dei file in memoria\n");		//errore
		sem_post(sicurezza_mutex);									//unlock su sicurezza.txt
		exit(-1);
	}
	sem_post(sicurezza_mutex);								//unlock su sicurezza.txt
}

int controllo_blocco_ip (char* ipCercato)			//funzione che controlla nel file 'sicurezza.txt' che non esista un blocco per l'indirizzo ip passato come parametro, restituisce 1 in caso ci sia un blocco e 0 altrimenti 
{
	FILE* fd;							//conterrà il descrittore del file 'sicurezza.txt'
	int ret,risultato;					//variabile per il ritorno delle letture e variabile che conterrà il valore da ritornare
	time_t orarioCorrente;				//variabile che conterrà il tempo corrente da confrontare con l'orarioFineBlocco		
	//variabili che conterranno i valori letti dal file
	char utente[MAX_DIM_USPSW] , ipLetto[16];
	long int orarioInizioBlocco, orarioFineBlocco;
	
	orarioCorrente = time(0);
	risultato = 0;									//di default l'esito della ricerca è negativo
	orarioInizioBlocco = orarioFineBlocco = 0;
	
	sem_wait(sicurezza_mutex);											//lock su sicurezza.txt
	//apertura file sicurezza.txt
	if ((fd = fopen("datiSalvati/sicurezza.txt", "r" ))==NULL)			//apertura file in lettura
	{
		printf("Errore apertura file in memoria di 'sicurezza.txt'.\n");				//errore
		sem_post(sicurezza_mutex);										//unlock su sicurezza.txt
		exit(-1);
	}
	//lettura file
	do							//leggo fino a quando non ci sono più dati
	{
		ret = fscanf(fd,"%s %s %ld %ld",utente,ipLetto,&orarioInizioBlocco,&orarioFineBlocco);
		if (ret <= 0 && ret!= EOF)							//controllo errore 
			printf("Errore: lettura dell'estrazioni da file non riuscita\n");
		
		if (!strcmp(ipCercato, ipLetto))					//controllo che ip passato sia quello letto
		{
			if (orarioCorrente <= orarioFineBlocco)			//controllo che il blocco sia già scaduto o sia ancora in vigore
				risultato = 1;								//blocco ancora attivo
		}
	}while ((ret != EOF) && (risultato == 0));
	//chiusura file
	if (fclose(fd)!=0)		//chiusura file sicurezza
	{
		printf("Errore nella chiusura dei file in memoria\n");		//errore
		sem_post(sicurezza_mutex);									//unlock su sicurezza.txt
		exit(-1);
	}
	sem_post(sicurezza_mutex);										//unlock su sicurezza.txt
	
	return risultato;			//ritorno del valore 0 o 1
}

int pos_libera_client_connessi ()	//funzione che ritorna la prima posizione libera di client_connessi, in caso non ci sono posizioni libere aggiorna la dimensione dell'array e poi ritorna la prima posizione libera
{
	int pos, i,num_client_vecchi;
	pos = -1;				//caso di default
					
	sem_wait(client_mutex);								//lock su client_connessi
	num_client_vecchi = num_client_conn;
	
	for (i=0; i<num_client_conn; i++)				//scorre client_connessi in cerca di una posizione libera
	{
		if (!strcmp("vuoto",client_connessi[i].indirizzoIP))
		{
			pos = i;
			break;
		}
	}
	
	sem_post(client_mutex);								//unlock su client_connessi
	if (i == num_client_conn)			//sono arrivato in fondo senza trovare una posizione libera
		aumenta_dimensione_array_clienti_connessi(DIM_ARRAY_INIZIALE);		//aumento l'array di un numero di posizioni uguale al numero iniziale delle posizioni dell'array
	sem_wait(client_mutex);								//lock su client_connessi
	
	for (i=num_client_vecchi; i<num_client_conn; i++)				//scorre le nuove posizioni di client_connessi in cerca della prima libera
	{
		if (!strcmp("vuoto",client_connessi[i].indirizzoIP))
		{
			pos = i;
			break;
		}
	}
	sem_post(client_mutex);								//unlock su client_connessi
	
	return pos;
}

int pos_libera_schedine_giocate ()	//funzione che ritorna la prima posizione libera di schedine_giocate, in caso non ci sono posizioni libere aggiorna la dimensione dell'array e poi ritorna la prima posizione libera
{
	int pos, i,num_schedine_vecchi;
	pos = -1;				//caso di default
	num_schedine_vecchi = num_schedine_giocate;
	
	sem_wait(schedine_mutex);							//lock su schedine_giocate
	
	for (i=0; i<num_schedine_giocate; i++)				//scorre schedine_giocate in cerca di una posizione libera
	{
		if (!strcmp("vuoto",schedine_giocate[i].user))
		{
			pos = i;
			break;
		}
	}
	sem_post(schedine_mutex);							//unlock su schedine_giocate
	if (i == num_schedine_giocate)						//sono arrivato in fondo senza trovare una posizione libera
		aumenta_dimensione_array_schedine_giocate(DIM_ARRAY_INIZIALE);		//aumento l'array di un numero di posizioni uguale al numero iniziale delle posizioni dell'array

	sem_wait(schedine_mutex);							//lock su schedine_giocate
	for (i=num_schedine_vecchi; i<num_schedine_giocate; i++)				//scorre le nuove posizioni di client_connessi in cerca della prima libera
	{
		if (!strcmp("vuoto",schedine_giocate[i].user))
		{
			pos = i;
			break;
		}
	}
	sem_post(schedine_mutex);							//unlock su schedine_giocate
	
	return pos;
}

void inizializza_semafori ()			//funzione che inizializza i semafori
{
	if (sem_init(client_mutex , 1 , 1) == -1)
	{
		printf("Errore nell'inizializzazione del semaforo per client_connessi\n");		//errore nella creazione del semaforo
		exit(-1);
	}
	if (sem_init(schedine_mutex , 1 , 1) == -1)
	{
		printf("Errore nell'inizializzazione del semaforo per schedine_giocate\n");		//errore nella creazione del semaforo
		exit(-1);
	}
	if (sem_init(estrazione_mutex , 1 , 1) == -1)
	{
		printf("Errore nell'inizializzazione del semaforo per estrazione.\n");			//errore nella creazione del semaforo
		exit(-1);
	}
	if (sem_init(sock_mutex , 1 , 1) == -1)
	{
		printf("Errore nell'inizializzazione del semaforo per sock_conn.\n");			//errore nella creazione del semaforo
		exit(-1);
	}
	if (sem_init(sicurezza_mutex , 1 , 1) == -1)
	{
		printf("Errore nell'inizializzazione del semaforo per 'sicurezza.txt'\n");		//errore nella creazione del semaforo
		exit(-1);
	}
	if (sem_init(lista_mutex , 1 , 1) == -1)
	{
		printf("Errore nell'inizializzazione del semaforo per set_lista\n");			//errore nella creazione del semaforo
		exit(-1);
	}
}

int controllo_presenza_utente (char* utente)		//funzione che ricerca in client_connessi se c'è già un utente registrato con user uguale a quello passato come parametro
{
	int res, i;			//variabile che conterrà il risultato da ritornare 1 se utente corretto e 0 se utente già presente
	
	res = 1;			//valore default

	sem_wait(client_mutex);								//lock su client_connessi
	//ricerco in client_connessi se c'è già un utente registrato con user uguale a quello passato come parametro
	for (i = 0; i<num_client_conn; i++)
	{
		if (!strcmp(client_connessi[i].user,utente))				//c'è già un utente registrato con lo stesso user
		{
			res = 0;				//aggiorno valore di res
			break;
		}
	}
	sem_post(client_mutex);								//unlock su client_connessi
	return res;
}

int controllo_utente_associato_sessioID (char* utente,char* id)		//funzione che ricerca in client_connessi se c'è già un utente registrato con user uguale a quello passato come parametro e se il sessionID associato a quell'utente sia uguale a quello passato come parametro
{
	int res, i;			//variabile che conterrà il risultato da ritornare: 1 se utente presente ed associato ad id passato, 0 se utente non presente o non associato ad id passato
	
	res = 0;			//valore default
						
	sem_wait(client_mutex);							//lock su client_connessi
	//ricerco in client_connessi se c'è già un utente registrato con user uguale a quello passato come parametro
	for (i = 0; i<num_client_conn; i++)
	{
		if (!strcmp(client_connessi[i].user,utente))				//c'èun utente registrato con lo stesso user
		{
			if (!strcmp(client_connessi[i].sessionID,id))				//id associato all'user uguale a quello passato come parametro
			{
				res = 1;				//aggiorno valore di res
				break;
			}
		}
	}				
	sem_post(client_mutex);							//unlock su client_connessi
	
	return res;					
}

int controllo_sessionID (char* id)				//funzione che cerca se il sessionID mandato come parametro è associato ad un utente e quindi che sia valido. ritorna 1 se valido, 0 se non valido
{
	int res, i;			//variabile che conterrà il risultato da ritornare 1 se id presente e 0 se non è presente
	
	res = 0;			//valore default
						
	sem_wait(client_mutex);								//lock su client_connessi
	//ricerco in client_connessi se c'è un sessionID uguale a id passato come parametro
	for (i = 0; i<num_client_conn; i++)
	{
		if (!strcmp(client_connessi[i].sessionID,id))				//trovato un sessionID uguale ad id
		{
			res = 1;				//aggiorno valore di res
			break;
		}
	}
	sem_post(client_mutex);								//unlock su client_connessi
	
	return res;
}

void signup(int sock, char* ip_client)		//funzione lato server di signup, ha come parametro l'identificativo del socket connesso al client e l'ip del client
{
	int ret, len, pos;						//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	int utente_corretto;					//sarà = 0 se non è corretto, = 1 se corretto
	uint16_t lmsg;
	char utente[MAX_DIM_USPSW], psw[MAX_DIM_USPSW];
	
	utente_corretto = 0;					//valore default	
	do
	{
		//ricevo utente 
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 											// Rinconverto in formato host
		ret = recv(sock, (void*)utente, len, 0);					//ricevo utente
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		utente_corretto = controllo_presenza_utente(utente);		//controllo che utente sia valido
		//devo inviare il messaggio per dire se l'utente è corretto(1) o meno(0)
		len = sizeof(utente_corretto);								//prendo lunghezza del messaggio
		lmsg = htons(len);											//converto nel formato network
		ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
		ret	= send (sock, (void*)&utente_corretto, len,0);			//invio utente_corretto
		if(ret < 0)
		{
		   perror("Errore in fase di invio: in signup\n");
		   exit(-1);
		}
	}while (!utente_corretto);			//while che continua a ricevere un utente ed una psw fino a che l'utente non è corretto
	//ricevo psw
	ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 											// Rinconverto in formato host
	ret = recv(sock, (void*)psw, len, 0);						//ricevo psw
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	//aggiungo utente in client_connessi		
	pos = pos_libera_client_connessi();		//prendo la prima posizione libera di client_connessi
	
	sem_wait(client_mutex);							//lock su client_connessi
	if (pos == -1)							//caso di errore non posso procedere con il signup
	{
		//invio messaggio di errore
		len = sizeof(pos);										//prendo lunghezza del messaggio
		lmsg = htons(len);										//converto nel formato network
		ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);	//invio dimensione
		ret	= send (sock, (void*)&pos, len,0);					//invio utente_corretto
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");			
		   sem_post(client_mutex);						//unlock su client_connessi
		   exit(-1);
		}
	}
	else 									//ho trovato la posizione libera posso procedere senza problemi
	{										//copio i valori in client_connessi
		strcpy(client_connessi[pos].user,utente);
		strcpy(client_connessi[pos].psw,psw);
		strcpy(client_connessi[pos].indirizzoIP,ip_client);
	}
	sem_post(client_mutex);									//unlock su client_connessi
	
	crea_file_registro_utente(utente);						//creo file registro utente		
		
	//mando mex dicendo che tutto è andato bene e signup è giunto al termine, utente_corretto sarà = 1
	len = sizeof(utente_corretto);								//prendo lunghezza del messaggio
	lmsg = htons(len);											//converto nel formato network
	ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
	if(ret < 0)
	{
	   perror("Errore in fase di invio di lsmg: finale\n");
	   exit(-1);
	}
	ret	= send (sock, (void*)&utente_corretto, len,0);			//invio utente_corretto
	if(ret < 0)
	{
	   perror("Errore in fase di invio: finale\n");
	   exit(-1);
	}
}

void login (int sock, char* ip_client)		//funzione lato server di login, ha come parametro l'identificativo del socket connesso al client e l'ip del client
{
	int ret, len, i,pos;					//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	int tentativo;							//sarà = 0 se non è corretto e bisogna continuare il ciclo, = 1 se corretto
	int bloccato;							// = 1 ip bloccato , = 0 ip non bloccato
	uint16_t lmsg;
	char utente[MAX_DIM_USPSW], psw[MAX_DIM_USPSW];
	
	tentativo = 0;						//setta valore default
	
	//controllo che ip non sia bloccato
	bloccato = controllo_blocco_ip(ip_client);					//restituisce 1 in caso di blocco e 0 in caso di non blocco
	//invio risultato del controllo del blocco al client
	len = sizeof(bloccato);										//prendo lunghezza del messaggio
	lmsg = htons(len);											//converto nel formato network
	ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
	ret	= send (sock, (void*)&bloccato, len,0);					//invio bloccato
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	//prendere dati e controllo se validi, ripetere per altre 2 volte
	for (i=0; i<3 ;i++)
	{
		//ricevo utente
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);	//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 										// Rinconverto in formato host
		ret = recv(sock, (void*)utente, len, 0);				//ricevo utente
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		//ricevo psw
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);	//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 										// Rinconverto in formato host
		ret = recv(sock, (void*)psw, len, 0);					//ricevo psw
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		//controllo correttezza, ricevo -1 per esito negativo e la pos dell'utente immesso in client_connessi per esito positivo
		pos = ricerca_utente_psw(utente,psw);
		if (pos != -1)				//esito positivo			
			tentativo = 1;
		//invio esito
		len = sizeof(tentativo);								//prendo lunghezza del messaggio
		lmsg = htons(len);										//converto nel formato network
		ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);	//invio dimensione
		ret	= send (sock, (void*)&tentativo, len,0);			//invio tentativo
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   exit(-1);
		}
		if (tentativo)				//utente e psw corretti
		{					
			break;
		}
	}
	if (tentativo)					//utente e psw corretti
	{
		crea_sessionID(pos);					//creazione sessionID
		//invio session id al client					
		sem_post(client_mutex);							//lock su client_connessi
		
		len = sizeof(client_connessi[pos].sessionID);			//prendo lunghezza del messaggio
		lmsg = htons(len);										//converto nel formato network
		ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);	//invio dimensione
		ret	= send (sock, (void*)client_connessi[pos].sessionID, len,0);		//invio tentativo
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   sem_post(client_mutex);								//unlock su client_connessi
		   exit(-1);
		}
		strcpy(client_connessi[pos].indirizzoIP,ip_client);					//associare ip all'utente nel client
		
		sem_post(client_mutex);									//unlock su client_connessi
	}
	else     //3 tentativi di login falliti, devo bloccare l'ip
	{
			//aggiungi_blocco_account
			//devo ricercare l'utente associato all'ip che deve essere bloccato
			sem_wait(client_mutex);								//lock su client_connessi
				
			for (i = 0; i < num_client_conn; i++)					//for per scorrere tutte le posizioni dell'array
			{
				if (strcmp(client_connessi[i].indirizzoIP,ip_client)==0)			//posizione dell' utente cercato associato all'ip
				{
					strcpy(utente,client_connessi[i].user);
					break;
				}			
			}

			sem_post(client_mutex);									//unlock su client_connessi
			//richiamo la funzione per bloccare l'account
			aggiungi_blocco_account_sicurezza(utente,ip_client);
	}
}

void invia_giocata (int sock)			//funzione lato server di invia_giocata, ha come parametro l'identificativo del socket connesso al client
{
	time_t orario;						//var per orario della giocata della schedina 
	int ret, len;						//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	uint16_t lmsg,esito;				//esito = 1 funzione conclusa con successo, = 0  errore 
	uint16_t id_valido;					//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0
	int pos_schedina;					//conterrà la posizione di schedine giocate in cui verrà salvata la schedina
	char id_client[DIM_SESSION_ID+1];	//conterrà sessionID ricevuto dal client
	struct schedina s;					//conterrà la schedina ricevuta dal client
	
	//ricevo sessionID
	ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 											// Rinconverto in formato host
	ret = recv(sock, (void*)id_client, len, 0);					//ricevo sessionID dal client
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	id_valido = controllo_sessionID(id_client);			//controllo sessionID
	//invio risultato controllo sessionID
	len = sizeof(uint16_t);
	ret	= send (sock, (void*)&id_valido, len,0);				//invio id_valido:1 se valido, 0 se non valido
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	if (id_valido)
	{			//inizio if.0  controllo sessionID
		//sessionID inviato dal client valido, procedo con la funzione
		//ricevo schedina
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 											// Rinconverto in formato host
		ret = recv(sock, (void*)&s, len, 0);						//ricevo s
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		
		//calcolo orario in cui è arrivata la schedina
		orario = time(NULL); 
		//aggiungo alla schedina arrivata l'orario corrente
		s.orarioGiocata = orario;
		pos_schedina = pos_libera_schedine_giocate();		//trovare posizione libera in schedine_giocate
		
		if (pos_schedina != -1)
		{											//niente errori
			//salvare in schedine_giocate (passo posizione e schedine)
			aggiungi_schedina_schedine_giocate(pos_schedina,s);
			
			aggiungi_schedina_file_registro_utente(s);		//salvo s nel file registro dell'user		
			//mandare mex che sia tutto a posto =1
			esito = 1;
			len = sizeof(uint16_t);
			ret	= send (sock, (void*)&esito, len,0);		//invio tentativo
			if(ret < 0)
			{
			   perror("Errore in fase di invio: \n");
			   exit(-1);
			}
		}
		else
		{											//errore nessuna posizione disponibile per memorizzare la nuova schedina
			esito = 0;//inviare mex di errore
			len = sizeof(uint16_t);
			ret	= send (sock, (void*)&esito, len,0);		//invio tentativo
			if(ret < 0)
			{
			   perror("Errore in fase di invio: \n");
			   exit(-1);
			}
		}
	}			//fine if.0  controllo sessionID
}

void stringa_giocate_richieste (char* output, char* utente, int n)		//funzione che scorrerà schedine giocate in cerca delle schedine con utente e attiva uguali ai valori di utente e n passati come parametro e che inserirà gli opportuni valori da stampare a video in output
{
	int i,j,cont;			//cont conterrà il numero di schedine trovate che soddisfano i requisiti della richiesta
	char num[20];			//array che conterrà i numeri giocati o gli importi giocati o le vincite
	char gioc [6];			//conterrà il numero giocato
	
	cont=0;						//inizializzo a 0
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	
	//scorro le posizioni di schedine_giocate in cerca di schedine che soddisfano i requisiti richiesti
	for (i=0; i<num_schedine_giocate; i++)
	{				//inizio for.0
		if (!strcmp(utente,schedine_giocate[i].user) && (schedine_giocate[i].attiva == n))			//trovata schedina con caratteristiche cercate
		{			//inizio if.0
			if (cont == 0)									//se è la prima schedina trovata				
				strcat(output,"------------------------------------------------------------------------\n");			//aggiungo divisione fra giocate
			//ora scorriamo i campi della schedina e aggiungiamo le varie parti all'output
			for (j=0; j<NUM_RUOTE; j++)		//scorro le ruote
			{		//inizio for.0.1
				if (schedine_giocate[i].ruote[j] == 1)		//ruota giocata va aggiunta
				{
					strcat(output,ruote[j]);		//aggiungo ruota giocata
					strcat(output," ");				//aggiungo uno spazio
				}
			}		//fine for.0.1
			
			for(j=0; j<MAX_NUM_GIOCATI;j++)		//scorro i numeri giocati
			{		//inizio for.0.2
				if (schedine_giocate[i].numeri[j] != 0)		//se diverso da 0 vuol dire che è valido
				{
					//devo aggiungere carattere per carattere all'array e anche lo spazio in mezzo
					sprintf(num,"%d ",schedine_giocate[i].numeri[j]);
					strcat(output,num);
				}
				else  	//se il numero non c'è devo uscire dal for
				{
					break;
				}
			}		//fine for.0.2
			
			//ora devo mettere gli importi delle giocate
			for (j=0; j<MAX_NUM_SCOMMESSE; j++)
			{		//inizio for.0.3
				if (schedine_giocate[i].importi_giocata[j] != 0)			//tipologia di giocata su cui si è scommesso
				{
					strcat(output,tipologia_giocata[j]);						//concateno con nome tipologia giocata
					strcat(output," ");											//aggiungo uno spazio
					sprintf(gioc,"%f",schedine_giocate[i].importi_giocata[j]); 	//metto in array gioc
					strcat(output,gioc);										//concateno con array gioc
					strcat(output," ");											//aggiungo uno spazio
				}
				
			}		//fine for.0.3
			if (n == 0)				//se n = 0 vincite già calcolate vanno messe
			{
				strcat(output,"\n");
				
				for (j=0; j<MAX_NUM_SCOMMESSE; j++)			//devo mettere le vincite
				{		//inizio for.0.4
					strcat(output,"vincite su ");
					strcat(output,tipologia_giocata[j]);						//concateno con nome tipologia giocata
					strcat(output,": ");										//aggiungo uno spazio
					sprintf(gioc,"%f",schedine_giocate[i].vincite[j]); 			//metto in array gioc
					strcat(output,gioc);										//concateno con array gioc
					if (j != (MAX_NUM_SCOMMESSE - 1))							//se non è l'ultima riga
						strcat(output,"\n");									//aggiungo l'accapo 		
				}		//fine for.0.4
			}
			//se n = 1 vincite ancora non estratte non vanno messe
			strcat(output,"\n------------------------------------------------------------------------\n");			//aggiungo l'accapo a fine riga e divisione fra giocate
			cont++;						//aggiorno valore di cont
		}			//fine if.0
	}			//fine for.0
	sem_post(schedine_mutex);							//unlock su schedine_giocate
	
	if (cont==0)
		strcpy(output,"Non ci sono schedine giocate con le caratteristiche selezionate\n\0");		//cont==0 non ci sono giocate che soddisfano requisiti richiesti invierò il messaggio di segnalazione
}

void vedi_giocate (int sock)		//funzione lato server di vedi_giocate, ha come parametro l'identificativo del socket connesso al client
{
	int ret, len, n;						//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	uint16_t lmsg;						
	uint16_t id_valido;						//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0
	uint16_t user_valido;					//conterrà il valore che dirà se user inviato è valido = 1 o non valido = 0
	char id_client[DIM_SESSION_ID+1];		//conterrà sessionID ricevuto dal client
	char utente_client[MAX_DIM_USPSW+1];	//conterrà utente ricevuto dal client
	char risultato [MAX_DIM_RIS];			//conterrà il risultato che deve essere stampato a video dal client
	
	//ricevo sessionID
	ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 											// Rinconverto in formato host
	ret = recv(sock, (void*)id_client, len, 0);					//ricevo sessionID dal client
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	id_valido = controllo_sessionID(id_client);//controllo sessionID
	//invio risultato controllo sessionID
	len = sizeof(uint16_t);
	ret	= send (sock, (void*)&id_valido, len,0);				//invio id_valido:1 se valido, 0 se non valido
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	if (id_valido)					//sessionID inviato dal client valido, procedo con la funzione
	{			//inizio if.0  controllo sessionID
		//ricevo utente del client
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 											// Rinconverto in formato host
		ret = recv(sock, (void*)utente_client, len, 0);				//ricevo user dal client
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		user_valido = controllo_utente_associato_sessioID(utente_client,id_client);		//controllo che utente sia presente in client_connessi e che abbia associato il sessionID ricevuto prima
		//invio risultato controllo user
		len = sizeof(uint16_t);
		ret	= send (sock, (void*)&user_valido, len,0);				//invio user_valido:1 se valido, 0 se non valido
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   exit(-1);
		}
		if (user_valido)			//user ricevuto dal client valido
		{		//inizio if.1  user valido
			//ricevo tipo di giocate che vogliono essere viste (n) = 1 giocate attive, = 0 giocate passate
			ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
			len = ntohs(lmsg); 											// Rinconverto in formato host
			ret = recv(sock, (void*)&n, len, 0);						//ricevo n dal client
			if(ret < 0)				//gestione errore in fase di ricezione
			{
				perror("Errore in fase di ricezione: \n");
				exit(-1);
			}
			stringa_giocate_richieste(risultato,utente_client,n);		//creo stringa con il risultato
			//invio il risultato
			len = strlen(risultato)+1;					//prendo lunghezza del messaggio
			lmsg = htons(len);							//converto nel formato network
			ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
			ret	= send (sock, (void*)risultato, len,0);					//invio risultato
			if(ret < 0)
			{
				perror("Errore in fase di invio: \n");
				exit(-1);
			}
		}		//fine if.1  user valido
	}			//fine if.0  controllo sessionID
}

void vedi_estrazione (int sock)		//funzione lato server di vedi_estrazione, ha come parametro l'identificativo del socket connesso al client
{
	int ret, len, n,pos_ruota,i;		//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	uint16_t lmsg;	
	uint16_t id_valido;					//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0	
	char id_client[DIM_SESSION_ID+1];	//conterrà sessionID ricevuto dal client				
	char risultato [MAX_DIM_RIS];		//conterrà il risultato che deve essere stampato a video dal client
	
	//ricevo sessionID
	ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);	//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 										// Rinconverto in formato host
	ret = recv(sock, (void*)id_client, len, 0);				//ricevo sessionID dal client
	if(ret < 0)												//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	id_valido = controllo_sessionID(id_client);				//controllo sessionID
	//invio risultato controllo sessionID
	len = sizeof(uint16_t);
	ret	= send (sock, (void*)&id_valido, len,0);			//invio id_valido:1 se valido, 0 se non valido
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	if (id_valido)					//sessionID inviato dal client valido, procedo con la funzione
	{			//inizio if.0  controllo sessionID
		//ricevo n, il numero delle ultime n estrazioni che il client vuole visualizzare
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 											// Rinconverto in formato host
		ret = recv(sock, (void*)&n, len, 0);						//ricevo n dal client
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		//ricevo pos_ruota = -1 voglio vedere tutte le ruote, altri valori la specifica ruota di pos pos_ruota
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 											// Rinconverto in formato host
		ret = recv(sock, (void*)&pos_ruota, len, 0);				//ricevo pos_ruota dal client
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		//calcolo risposta
		if (pos_ruota == -1)		//caso in cui vedo estrazioni di tutte le ruote
		{
			for (i=0; i<NUM_RUOTE; i++)				//for che scorre tutte le ruote e di volta in volta aggiorna risultato
			{
				leggi_estrazione_lotto(i,n,risultato);		//prendo i numeri estratti per ciascuna ruota e li metto in risultato
			}
		}
		else 						//caso in cui vedo estrazioni di una sola ruota
		{
			leggi_estrazione_lotto(pos_ruota,n,risultato);	//prendo i numeri estratti della ruota voluta e li meto in risultato
		}
		//invio il risultato
		len = strlen(risultato)+1;									//prendo lunghezza del messaggio
		lmsg = htons(len);											//converto nel formato network
		ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
		ret	= send (sock, (void*)risultato, len,0);					//invio risultato
		if(ret < 0)
		{
			perror("Errore in fase di invio: \n");
			exit(-1);
		}
	}			//fine if.0  controllo sessionID
}

void vedi_vincite_schedine_utente (char* utente, char* output)		//funzione che va a mettere in risultato gli opportuni dati sulle vincite che il client dovrà stampare a video relativi alle schedine dell'utente passato come parametro
{
	float estratto,ambo,terno,quaterna,cinquina;		//var che conterranno la somma di tutte le vincite di tutte le schedine per ciascuna categoria di giocata
	int i,j;
	char num[2*MAX_NUM_GIOCATI+1];						//array che conterrà i numeri giocati o gli importi giocati o le vincite
	char gioc [6];										//conterrà il numero giocato
	
	int cont;											//cont conterrà il numero di schedine trovate che soddisfano i requisiti della richiesta
	struct tm* current_time; 							//servirà per visualizzare orario di giocata della schedina
	
	cont=0;													//inizializzo a 0
	estratto = ambo = terno = quaterna = cinquina = 0;		//inizializzo a zero
	
	sem_wait(schedine_mutex);								//lock su schedine_giocate
	//scorro tutte le posizioni di schedine_giocate in cerca di schedine del giocatore con delle vincite
	for (i=0; i<num_schedine_giocate; i++)
	{				//inizio for.0
		if (!strcmp(utente,schedine_giocate[i].user) && (schedine_giocate[i].attiva == 0) && ((schedine_giocate[i].vincite[0] != 0) || (schedine_giocate[i].vincite[1] != 0) || (schedine_giocate[i].vincite[2] != 0) || (schedine_giocate[i].vincite[3] != 0) || (schedine_giocate[i].vincite[4] != 0)))			//trovata schedina con caratteristiche cercate
		{			//inizio if.0
			cont++;							//aggiorno valore di cont
			//aggiorno i valori delle variabili per la somma delle vincite
			estratto += schedine_giocate[i].vincite[0];
			ambo += schedine_giocate[i].vincite[1];
			terno += schedine_giocate[i].vincite[2];
			quaterna += schedine_giocate[i].vincite[3];
			cinquina += schedine_giocate[i].vincite[4];
			
			//concatenare data
			current_time = localtime(&schedine_giocate[i].orarioGiocata); 		//metto data nel formato più utile
			
			strcat(output,"Estrazione del ");
			sprintf(gioc,"%d",current_time->tm_mday); 					//metto in array gioc il giorno
			strcat(output,gioc);										//concateno con array gioc
			strcat(output,"-");											//aggiungo uno spazio		
			sprintf(gioc,"%d",(current_time->tm_mon+1)); 				//metto in array gioc il mese
			strcat(output,gioc);										//concateno con array gioc
			strcat(output,"-");											//aggiungo uno spazio	
			sprintf(gioc,"%d",(1900 + current_time->tm_year)); 			//metto in array gioc l'anno
			strcat(output,gioc);										//concateno con array gioc
			strcat(output," ore ");										//aggiungo
			sprintf(gioc,"%d",current_time->tm_hour); 					//metto in array gioc l'ora
			strcat(output,gioc);										//concateno con array gioc
			strcat(output,":");											//aggiungo
			sprintf(gioc,"%d",current_time->tm_min); 					//metto in array gioc i minuti
			strcat(output,gioc);										//concateno con array gioc
			strcat(output,"\n");										//aggiungo

			for (j=0; j<NUM_RUOTE; j++)		//scorro le ruote
			{		//inizio for.0.1
				if (schedine_giocate[i].ruote[j] == 1)		//ruota giocata va aggiunta
				{
					strcat(output,ruote[j]);		//aggiungo ruota giocata
					strcat(output," ");				//aggiungo uno spazio
				}
			}		//fine for.0.1
			
			for(j=0; j<MAX_NUM_GIOCATI;j++)		//scorro i numeri giocati
			{		//inizio for.0.2
				if (schedine_giocate[i].numeri[j] != 0)		//se diverso da 0 vuol dire che è valido
				{
					//devo aggiungere carattere per carattere all'array e anche lo spazio in mezzo
					num[2*i] = (char)schedine_giocate[i].numeri[j];
					num[(2*i)+1] = ' ';
				}
				//se il numero non c'è devo mettere carattere fine stringa e uscire dal for
				if (schedine_giocate[i].numeri[j] == 0)
				{
					num[2*i] = '\0';
					break;
				}
			}		//fine for.0.2
			//devo concatenare con la stringa contenente i numeri
			strcat(output,num);				//aggiungo numeri giocati concatenando con num
			
			//concatenare divisore
			strcat(output,">> ");
			//concatenare vincite
			for (j=0; j<MAX_NUM_SCOMMESSE; j++)			//scorro le vincite
			{		//inizio for.0.3
				//devo mettere le vincite su cui ho puntato
				if (schedine_giocate[i].importi_giocata[j] != 0)					//vincita su cui ho giocato
				{
					strcat(output,"vincite: ");
					strcat(output,tipologia_giocata[j]);						//concateno con nome tipologia giocata
					strcat(output," ");											//aggiungo uno spazio
					sprintf(gioc,"%f",schedine_giocate[i].vincite[j]); 			//metto in array gioc
					strcat(output,gioc);										//concateno con array gioc
					strcat(output," ");											//aggiungo uno spazio		
				}
			}		//fine for.0.3
			
			//concateno con simboli di fine schedina e preparo per prossima schedina
			strcat(output,"\n-----------------------------------------------------\n");
			
		}			//fine if.0
	}				//fine for.0

	sem_post(schedine_mutex);								//unlock su schedine_giocate
	
	if (cont==0)		//se cont==0 vuol dire che non c'era nessuna schedina con i requisiti cercati
	{
		strcpy(output,"Non sono state giocate schedine o non sono state giocate schedine relative ad estrazioni passate o non ci sono schedine giocate vincenti.\n");
	}
	else  				//concateno ad output il resoconto delle varie vincite
	{
		strcat(output,"\nEstratto: ");
		sprintf(gioc,"%f",estratto); 					//metto in array gioc il consuntivo per l'estratto
		strcat(output,gioc);							//concateno con array gioc
		strcat(output,"\nAmbo: ");
		sprintf(gioc,"%f",ambo); 						//metto in array gioc il consuntivo per l'ambo
		strcat(output,gioc);							//concateno con array gioc
		strcat(output,"\nTerno: ");
		sprintf(gioc,"%f",terno); 						//metto in array gioc il consuntivo per il terno
		strcat(output,gioc);							//concateno con array gioc
		strcat(output,"\nQuaterna: ");
		sprintf(gioc,"%f",quaterna); 					//metto in array gioc il consuntivo per la quaterna
		strcat(output,gioc);							//concateno con array gioc
		strcat(output,"\nCinquina: ");
		sprintf(gioc,"%f",cinquina); 					//metto in array gioc il consuntivo per la cinquina
		strcat(output,gioc);							//concateno con array gioc
		strcat(output,"\n");							//concateno con array gioc
	}
	
}

void vedi_vincite (int sock)		//funzione lato server di vedi_vincite, ha come parametro l'identificativo del socket connesso al client
{
	int ret, len;							//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	uint16_t lmsg;						
	uint16_t id_valido;						//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0
	uint16_t user_valido;					//conterrà il valore che dirà se user inviato è valido = 1 o non valido = 0
	char id_client[DIM_SESSION_ID+1];		//conterrà sessionID ricevuto dal client
	char utente_client[MAX_DIM_USPSW+1];	//conterrà utente ricevuto dal client
	char risultato [MAX_DIM_RIS];			//conterrà il risultato che deve essere stampato a video dal client
	
	//ricevo sessionID
	ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 											// Rinconverto in formato host
	ret = recv(sock, (void*)id_client, len, 0);					//ricevo sessionID dal client
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	id_valido = controllo_sessionID(id_client);					//controllo sessionID
	//invio risultato controllo sessionID
	len = sizeof(uint16_t);
	ret	= send (sock, (void*)&id_valido, len,0);				//invio id_valido:1 se valido, 0 se non valido
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	if (id_valido)					//sessionID inviato dal client valido, procedo con la funzione
	{			//inizio if.0  controllo sessionID
		
		//ricevo utente del client
		ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 											// Rinconverto in formato host
		ret = recv(sock, (void*)utente_client, len, 0);				//ricevo user dal client
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		user_valido = controllo_utente_associato_sessioID(utente_client,id_client);		//controllo che utente sia presente in client_connessi e che abbia associato il sessionID ricevuto prima
		//invio risultato controllo user
		len = sizeof(uint16_t);
		ret	= send (sock, (void*)&user_valido, len,0);				//invio user_valido:1 se valido, 0 se non valido
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   exit(-1);
		}
		if (user_valido)			//user ricevuto dal client valido
		{		//inizio if.1  user valido
			
			vedi_vincite_schedine_utente(utente_client,risultato);		//fare risultato
			//invio il risultato
			len = strlen(risultato)+1;									//prendo lunghezza del messaggio
			lmsg = htons(len);											//converto nel formato network
			ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
			ret	= send (sock, (void*)risultato, len,0);					//invio risultato
			if(ret < 0)
			{
				perror("Errore in fase di invio: \n");
				exit(-1);
			}
			
		}		//fine if.1  user valido
	}			//fine if.0  controllo sessionID
}

void esci (int sock)
{
	int ret, len;							//variabile per contenere il valore di ritorno delle primitive e la lunghezza dei messaggi da inviare
	uint16_t lmsg;						
	char utente_client[MAX_DIM_USPSW+1];	//conterrà utente ricevuto dal client
	char ip_client[16];						//conterrà l'ip associato al socket sock
	char risultato [MAX_DIM_RIS];			//conterrà il risultato che deve essere stampato a video dal client
	
	strcpy(risultato,"Chiusura del socket avvenuta con successo.\n");		//metto in risultato il mex di default di chiusura riuscita
	
	//ricevo utente
	ret = recv(sock, (void*)&lmsg, sizeof(uint16_t), 0);		//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 											// Rinconverto in formato host
	ret = recv(sock, (void*)utente_client, len, 0);				//ricevo user dal client
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	
	recupera_ip_sock_conn(sock,ip_client);		//recupero indirizzoIP associato al socket sock
	elimina_client_connessi(utente_client,ip_client);	//ricavo client_conn da utente e lo cancello
	
	if (elimina_sock_conn(sock) == -1)			//cancello posizione relativa a sock i in sock_con
	{
		printf("Errore, cancellazione socket in fase di chiusura non riuscita.\n");
		strcpy(risultato,"Errore, cancellazione socket in fase di chiusura non riuscita. Chiusura non possibile.\n");
	}
	//invio messaggio che chiusura è andata a buon fine
	//invio il risultato
	len = strlen(risultato)+1;									//prendo lunghezza del messaggio
	lmsg = htons(len);											//converto nel formato network
	ret = send (sock, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
	ret	= send (sock, (void*)risultato, len,0);					//invio risultato
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
		exit(-1);
	}
}

void handler_SIGCHLD(int sig)		//funzione che viene avviata quando arriva il segnale di terminazione di uno dei figli al padre
{
}

int main(int argc, char* argv[])
{
    time_t t;                  	 		//variabile che servirà all'inizializzazione del generatore casuale di numeri
    int identificativo_comando;			//variabile che conterrà l'identificativo del comando che viene mandato al server
    int ret, sd, new_sd, len,i; 
    uint16_t lmsg;
    pid_t pid;							//conterrà il pid dopo la fork, servirà per la distinzione fra processo padre e figlio
	socklen_t len_sock;
    struct sockaddr_in my_addr, cl_addr;
    char ip_client[16];
    
    if (argc < 2 || argc >3)		//controllo che siano stati passati il numero corretto di parametri
    {
       printf("Numero argomenti errato.\nUsa %s <porta server> <periodo>\n", argv[0]);
       exit(1);
    }
    if (argc == 3)                      //è stato passato il parametro del periodo
    {
        if (atol(argv[1]) < 1025)
        {
            printf("Il numero di porta passato come parametro non è valido\n");
            exit(1);
        }
        periodo = atol(argv[2])*60;             //assegno il valore passato come parametro sovrascrivendo il valore di default(conversione da minuti a secondi)
    }
    //rendo memoria condivisa i semafori
    lista_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sicurezza_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    schedine_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sock_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    estrazione_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    client_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    //rendo memoria condivisa le variabili globali
    set_lista = mmap(NULL, sizeof(fd_set), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    client_connessi = mmap(NULL, sizeof(struct client)*DIM_ARRAY_INIZIALE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    schedine_giocate = mmap(NULL, sizeof(struct schedina)*DIM_ARRAY_INIZIALE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sock_conn = mmap(NULL, sizeof(struct sock_client)*DIM_ARRAY_INIZIALE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    numeroEstrazioni = mmap(NULL, sizeof(unsigned int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	inizializza_semafori();						//inizializzazione semafori
	creazione_cartelle_programma();				//creazione delle cartelle necessarie per la disposizione dei file txt che dovranno contenere i dati che verranno salvati dal programma
    *numeroEstrazioni = 0;						//programma appena aperto il numero delle estrazioni fatte nelle ruote sarà = 0
    srand((unsigned) time(&t));                 //inizializzazione del generatore causale di numeri
    signal(SIGALRM, estrazione_lotto);          //assegno estrazione_lotto come handler per il segnale di SIGALRM
    signal(SIGCHLD,handler_SIGCHLD);			//assegno handler_SIGCHLD come handler per il segnale di SIGCHLD
        
    inizializza_client_connessi(0 , DIM_ARRAY_INIZIALE);		//inizializzo client_connessi, tutte le posizioni che all'inizio vanno da 0 a quella iniziale di default
    inizializza_schedine_giocate(0 , DIM_ARRAY_INIZIALE);		//inizializzo schedine_giocate, tutte le posizioni che all'inizio vanno da 0 a quella iniziale di default
    inizializzo_sock_conn(0 , DIM_ARRAY_INIZIALE);				//inizializzo sock_con con valori di default
    
    alarm (periodo);                        	//setto l'allarme che mi dirà lo scadere di ogni periodo
    porta_server=atoi(argv[1]);					//inizializzazione con il numero di porta passato come parametro
	
    sd = socket(AF_INET, SOCK_STREAM, 0);		// Creazione socket
    // Creazione indirizzo di bind
    memset(&my_addr, 0, sizeof(my_addr)); 		// Pulizia 
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(porta_server);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    
    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr) );
    ret = listen(sd, 10);						//coda richieste lunga 10
    if(ret < 0)
    {
        perror("Errore in fase di bind: \n");
        exit(-1);
    }
    
    //settaggio liste select
	FD_ZERO(set_lista);						//azzero
	FD_ZERO(&set_temp);						//azzero
	FD_SET(sd, set_lista);					//aggiungo sd (il listener)
	fdmax = sd;								//setto il valore più alto nella lista da passare alla select
    
    printf("Aspetto connessioni client.\n");
  
    while(1)				//inizio while 0
	{
		set_temp = *set_lista;			//la lista temp prenderà stessi valori della lista organizzata prima
		switch (select (fdmax+1 , &set_temp , NULL , NULL, NULL))		//mi blocco in attesa
		{			//inizio switch
		case -1: 		// errore della select
               {
					if (errno == EINTR)
						continue;
					perror("Errore nella select(): \n");
                    exit(-1);
               }
        case 0: 		// timer scaduto, non deve accadere coi parametri passati alla select
               {
					printf("Timer scaduto!\n");
                    exit(-1);
               }
        default:		//caso di default, ci sono fd pronti
               {
							
					for (i=0; i<=fdmax; i++)		//scorro il set dei fd pronti
					{		//inizio for.0 scorrimento set fd pronti
						if (FD_ISSET(i, &set_temp))		//trovato fd pronto
						{		//inizio if.0

							if (i == sd)		//ho richiesta di nuova connessione da un client
							{			//inizio if.1 sd
								
								len_sock = sizeof(cl_addr);			
								new_sd = accept(sd, (struct sockaddr*) &cl_addr, &len_sock);	// Accetto nuove connessioni
								
								sem_wait(lista_mutex);											//lock su set_lista
								FD_SET(new_sd,set_lista);										//aggiungo socket alla lista
								sem_post(lista_mutex);											//unlock su set_lista
								if (new_sd > fdmax)												//aggiorno fdmax
									fdmax = new_sd;
										
								inet_ntop(AF_INET,&cl_addr,ip_client,len_sock);				//recupero l'ind ip del client connesso
								aggiungi_sock_conn(new_sd,ip_client);						//aggiungo il socket e l'indirizzoIp del client associato a sock_conn
							}			//fine if.1 sd
							else   			//altro socket dal listener, devo eseguire richiesta
							{	//inizio else.1
								sem_wait(lista_mutex);											//lock su set_lista
								FD_CLR(i,set_lista);		// tolgo da set_lista il descrittore su cui è arrivata la richiesta
								sem_post(lista_mutex);											//unlock su set_lista
								
								pid = fork();			//genero un processo figlio per gestire la richiesta
								if ( pid == -1)			//errore nella pid
								{
									printf("Errore nella fork per gestire la nuova richiesta.\n");
									exit(-1);
								}
								if ( pid == 0) 		//processo figlio che dovrà gestire richiesta di connessione
								{	//inizio if processo figlio
									close(sd);						//chiudo il socket listener
									
									ret = recv(i, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
									len = ntohs(lmsg); 												// Rinconverto in formato host
									ret = recv(i, (void*)&identificativo_comando, len, 0);		//ricevo l'identificativo del comando
									if(ret < 0)				//gestione errore in fase di ricezione
									{
										perror("Errore in fase di ricezione: \n");
									}
									
									recupera_ip_sock_conn(i,ip_client);		//richiamo funzione per ricavare ip associato al socket i e metterlo in ip_client
									switch(identificativo_comando)			//inizio switch identificativo_comando
									{
									case 0:				//caso del comando signup
											{
												signup(i,ip_client);					//richiamo il comando che dovrà eseguire la parte server di signup
												break;
											}
									case 1:				//caso del comando login
											{
												login(i,ip_client);					//richiamo il comando che dovrà eseguire la parte server di login
												break;
											}
									case 2:				//caso del comando invia_giocata
											{
												invia_giocata(i);						//richiamo il comando che dovrà eseguire la parte server di invia_giocata
												break;
											}
									case 3:				//caso del comando vedi_giocate
											{
												vedi_giocate(i);						//richiamo il comando che dovrà eseguire la parte server di vedi_giocate
												break;
											}
									case 4:				//caso del comando vedi_estrazione
											{
												vedi_estrazione(i);						//richiamo il comando che dovrà eseguire la parte server di vedi_estrazioni
												break;
											}
									case 5:				//caso del comando vedi_vincite
											{
												vedi_vincite(i);						//richiamo il comando che dovrà eseguire la parte server di vedi_estrazioni
												break;
											}
									case 6:				//caso del comando esci
											{
												esci(i);				//richiamo il comando che dovrà eseguire la parte server di esci
												close(i);				//chiudo il socket
												exit(0);				//termino il processo figlio			
												break;
											}
									default:  			//caso di un comando non riconosciuto
											{
												printf("Comando ricevuto non riconosciuto.\n");
												printf("Sono %d e faccio il comando è %d\n",getpid(),identificativo_comando);// *********************************
											}
									}			//fine switch identificativo_comando	
							
									sem_wait(lista_mutex);											//lock su set_lista
									FD_SET(i, set_lista);					//aggiungo i prima di terminare il processo
									sem_post(lista_mutex);											//unlock su set_lista

									exit(0);		//termino processo figlio
								}	//fine if processo figlio
							}	//fine else.1
							//fine processo padre
						} // fine if.0
					} // fine for.0 scorrimento set fd pronti
			} 		// fine default
		}		//fine switch
    }		//fine while 0
}
