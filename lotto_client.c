#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NUM_GIOCATI 10			//numero massimo dei numeri che si possono giocare
#define MAX_NUM_SCOMMESSE 5			//numero massimo delle tipologie di puntate estratto,ambo,terna,quaterna,cinquina
#define MAX_DIM_COMANDI 17
#define MAX_DIM_PARAMETRI 20   		
#define NUM_COMANDI 8
#define MAX_DIM_USPSW 20 			//massima dimensione per username e psw
#define MAX_NUM_OPZIONI 24			//massimo numero di opzioni che può avere un comando
#define MAX_DIM_RUOTE 9				//massimo dimensione per le ruote
#define NUM_RUOTE 11				//massimo numero delle ruote
#define DIM_SESSION_ID	10			//dimensione del session ID
#define MAX_DIM_RIS	10000			//dimensione massima della stringa contenente ilrisultato dei metodi

//varie define contentenenti le spiegazioni da stampare pe ril comando help
#define HELP "Sono disponibili i seguenti comandi:\n  !help <comando> --> mostra i dettagli di un comando \n  !signup <username> <password> --> crea un nuovo utente\n  !login <username> <password> --> autentica un utente\n  !invia_giocata g --> invia una giocata g al server, le ruote devono essere scritte con lettere tutte minuscole\n  !vedi_giocate tipo --> visualizza le giocate precedenti dove tipo = {0,1}  e permette di visualizzare le giocate passate ‘0’ oppure le giocate attive ‘1’ (ancora non estratte)\n  !vedi_estrazione <n> <ruota> --> mostra i numeri delle ultime n estrazioni sulla ruota specificata\n  !vedi_vincite --> visualizza tutte le vincite, l'estrazione in cui sono state fatte e un consunto delle varie tipologie\n  !esci --> termina il client\n"
#define HELP_HELP "!help <comando> --> mostra i dettagli di un comando \n"
#define HELP_SIGNUP "!signup <username> <password> --> crea un nuovo utente\n"
#define HELP_LOGIN "!login <username> <password> --> autentica un utente\n"
#define HELP_INVIA_GIOCATA "!invia_giocata g --> invia una giocata g al server, le ruote devono essere scritte con lettere tutte minuscole\n"
#define HELP_VEDI_GIOCATE "!vedi_giocate tipo --> visualizza le giocate precedenti dove tipo = {0,1}  e permette di visualizzare le giocate passate ‘0’ oppure le giocate attive ‘1’ (ancora non estratte)\n"
#define HELP_VEDI_ESTRAZIONE "!vedi_estrazione <n> <ruota> --> mostra i numeri delle ultime n estrazioni sulla ruota specificata\n"
#define HELP_VEDI_VINCITE "!vedi_vincite --> visualizza tutte le vincite, l'estrazione in cui sono state fatte e un consunto delle varie tipologie\n"
#define HELP_ESCI "!esci --> termina il client\n"

//matrice contenente tutti i vari comandi riconosciuti dal client
char comandi[][ MAX_DIM_COMANDI ]={"!help",
                   					"!signup",
                   					"!login",
                   					"!invia_giocata",
                   					"!vedi_giocate",
                   					"!vedi_estrazione",
                   					"!vedi_vincite",
									"!esci"};
									
//matrice contenente tutti le varie ruote
char matrice_ruote[][ MAX_DIM_RUOTE ]={"bari",
										"cagliari",
										"firenze",
										"genova",
										"milano",
										"napoli",
										"palermo",
										"roma",
										"torino",
										"venezia",
										"nazionale"};

int socket_server, fdmax, stato;	//variabili per contenere una il fd del socket del server e l'altra per contenere l'fd maggiore nella lista della select,
									//stato conterrà il numero che identificherà qual è stato l'ultimo comando fatto dal client da cui si aspetta una risposta dal server
									//il valore di stato identifica il comando di posizione comandi[stato]
													
fd_set set_lista, set_temp;			//2 liste di fd per la select, una temporanea che verrà modificata dalla select
char utente_client[MAX_DIM_USPSW];	//user collegato al client
char sessionID[DIM_SESSION_ID+1];	//sessionID collegato al client 
uint16_t lmsg;						//variabile che verrà usata per inviare la lunghezza del messaggio che si vuole inviare al server

struct schedina s;					//schedina che verrà compilata come richiesto e inviata

//formato della schedina
struct schedina
{
	char user[MAX_DIM_USPSW];		//user del client che ha giocato la schedina
	int attiva;						//valore che è ad '1' se la schedina è in attesa prossima estrazione, '0' giocata relativa ad estrazione già avvenuta
	time_t orarioGiocata;			//valore che conterrà la data in cui viene giocata la schedina
	int ruote [NUM_RUOTE];			//se posizione i ha il valore '1' vuol dire che la giocata è sulla ruota di posizione i della matrice delle ruote
	int numeri [MAX_NUM_GIOCATI];				//sono i numeri giocati nella schedina, sono massimo 10 e vanno da 1 a 90 compresi
	float importi_giocata [MAX_NUM_SCOMMESSE];			//rappresentano le puntate effettuate sull'uscita di: estratto, ambo, terna, quaterna, cinquina
	float vincite [MAX_NUM_SCOMMESSE];			//rappresentano le vincite ottenute con questa schedina con: estratto, ambo, terna, quaterna, cinquina. I valori delle vincite hanno senso solo se il campo attiva della stessa schedina è = 0, che indica estrazione avvenuta e controllo per la vincita e aggiornamento dei valori già avvenuto.
};

//inizio funzioni
int controllo_correttezza_puntate_schedina (struct schedina s)		//funzione che controlla la correttezza delle puntate, ritorna 1 se corretto 0 se scorretto
{		//uno non può scommettere sull'ambo se ha giocato un solo numero, non può scommettere sulla terna se ha puntato meno di 3 numeri e così via
	int i,num_giocati,num_tipologie;
	
	num_giocati = 0;				//inizio a 0 la variabile contaore
	for (i = 0; i < MAX_NUM_GIOCATI; i++)		//scorro numeri e conto quanti sono i numeri giocati
	{
		if (s.numeri[i] != 0)			//numero giocato
			num_giocati++;			//incremento variabile contatore
		else
			break;			//esco dal ciclo ho finito di contare i numeri giocati
	}
	
	if (num_giocati >= MAX_NUM_SCOMMESSE)			//se ho giocato più o uguale numeri del numero dei tipi delle puntate automaticamente posso fare correttamente ogni tipo di puntata
		return 1;			//giocata corretta
	
	num_tipologie = 0;
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)		//scorro importi_giocata e vedo qual'è l'indice della tipologia di giocata più alto
	{
		if (s.importi_giocata[i] != 0)			//tipologia giocata
			num_tipologie++;			//incremento variabile contatore
	}
	
	if (num_giocati >= num_tipologie)
		return 1;					//giocata corretta
	else
		return 0;					//giocata non corretta
}

void inizializza_schedina (struct schedina *s)			//mette i valori di default ad una schedina passata come parametro
{
	int i;
	for (i = 0; i < MAX_DIM_USPSW; i++)
		s->user[i] = 0;
	s->attiva = 1;				//quando viene creata la schedina sarà per forza in attesa della prossima estrazione
	s->orarioGiocata = 0;
	for (i = 0; i < NUM_RUOTE; i++)
		s->ruote[i] = 0;
	for (i = 0; i < MAX_NUM_GIOCATI; i++)
		s->numeri[i] = 0;
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)
		s->importi_giocata[i] = 0;
	for (i = 0; i < MAX_NUM_SCOMMESSE; i++)
		s->vincite[i] = 0;
}

int riconosci_ruote(char *s)		//funzione per riconoscere le ruote, ritorna la posizione che ha nella matrice_ruote
{
     int i;
     for (i=0; i< NUM_RUOTE ; i++)
          if (!strcmp(matrice_ruote[i], s))
             return i;
     return -1;										//ritorna -1 nel caso non trovi la ruota
}

int riconosci(char *s)		//funzione per riconoscere il comando da eseguire, ritorna la posizione che ha in comandi
{
     int i;
     for (i=0; i< NUM_COMANDI ; i++)
          if (!strcmp(comandi[i], s))
             return i;
     return -1;										//ritorna -1 nel caso non trovi il comando
}

void apri_connessione_server(char* ip_server, int porta_server)
{
     unsigned short int p;              
     int ret;
     struct sockaddr_in addr_server;
     
     if (porta_server<0 || porta_server>65535)					//controllo che il valore della porta passato come parametro sia valido
     {
        printf("Porta non valida: %d\n", porta_server);
        exit(1);
     }
     p=porta_server;
     
     if ((socket_server=socket(AF_INET, SOCK_STREAM, 0)) == -1)
     {
        perror("Errore nella \"socket()\": ");					//segnalazione errore apertura socket
        exit(-1);
     }
     
     memset((char*)&addr_server, 0, sizeof(addr_server));				//pulizia
     addr_server.sin_family = AF_INET;
     addr_server.sin_port = htons(p);
     if (!inet_pton(AF_INET, ip_server , &addr_server.sin_addr.s_addr))		//conversione indirizzo server da indirizzo numerico a Byte
     {
        printf("Indirizzo server non valido!\n");					//segnalazione errore indirizzo server non valido
        exit(-1);
     }
     
     ret=connect(socket_server, ( struct sockaddr *) &addr_server, sizeof(addr_server));
     if (ret<0)			//connessione al server indicato nei parametri
     {
        perror("Errore nella \"connect()\":");						//segnalazione errore di connessione
        exit(-1);
     }
}

void help(char* comando)
{
	switch (riconosci(comando))			//switch per il riconoscimento del paremetro di cui si vuole sapere 
    {
	case 0:		// !help
		{
			printf("%s\n",HELP_HELP);
			break;
		}
	case 1: // !signup
		{
			printf("%s\n",HELP_SIGNUP);
			break;
		}
	case 2: // !login
		{
            printf("%s\n",HELP_LOGIN);
			break;
		}
	case 3: // !invia_giocata
		{
            printf("%s\n",HELP_INVIA_GIOCATA);
			break;
		}
	case 4: // !vedi_giocate
		{
			printf("%s\n",HELP_VEDI_GIOCATE);
			break;
		}
	case 5: // !vedi_estrazione
		{
			printf("%s\n",HELP_VEDI_ESTRAZIONE);
			break;
		}
	case 6: // !vedi_vincite
		{
            printf("%s\n",HELP_VEDI_VINCITE);
			break;
		}
	case 7: // !esci
		{
			printf("%s\n",HELP_ESCI);			
			break;
		}
	default: //comando non passato come parametro,stampo informativa su tutti i comandi
		{
			printf("Parametro passato al comando non riconosciuto\n");
		}
	}
}	

void signup(char* user, char* psw)			//funzione che manda username e psw al server per registrarsi
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	int len,ret,controllo_lunghezza;		//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	int utente_corretto;					//sarà = 0 se non è corretto, = 1 se corretto
	char *res;								//conterrà risultato della fgets che verrà usata per la rilettura di utente e psw in caso di errore nell'autenticazione
	
	identificativo_comando = utente_corretto = controllo_lunghezza = 0;				

	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
		
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	do 				//inizio do while 0   per l'invio dell'utente e psw fino a che l'utente non sarà corretto
	{
		//invio utente
		len = strlen(user) + 1;					//prendo lunghezza del messaggio
		lmsg = htons(len);						//converto nel formato network
		ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
		ret	= send (socket_server, (void*)user, len,0);						//invio user
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   exit(-1);
		}
		//devo ricevere conferma che l'utente sia corretto, riceverò utente_corretto
		ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 														//rinconverto in formato host
		ret = recv(socket_server, (void*)&utente_corretto, len, 0);				//ricevo utente
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
		}
		if (!utente_corretto)
		{								//l'utente inviato non è corretto devo richiedere l'utente e rinviarlo fino a quando non lo sarà
			printf("L'utente passato come parametro è già usato digitare un nuovo utente.\n");		//richiedo utente
			do
			{
				res = fgets(user, MAX_DIM_USPSW, stdin); 			//aggiorno con la nuova psw
				if (res == 0)										// errore durante la lettura oppure letti zero bytes
				{
					printf("Errore leggendo stdin.\n");
					exit (-1);
				}				//aggiorno con il nuovo utente
				user[strlen(user)-1]=0;		//tolgo \n
				if (strlen(user) > 8)		//controllo che le dimensioni siano rispettate
				{
					printf("Dimensione di utente non corretta, utente massimo 8 caratteri.\n");
					controllo_lunghezza = 1;
				}
			}while(controllo_lunghezza == 1);						//controllo che lunghezza della psw siano corrette
		}
	}while(!utente_corretto);			//fine do while 0
	//invio psw
	len = strlen(psw) + 1;					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
	ret	= send (socket_server, (void*)psw, len,0);						//invio psw
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
		exit(-1);
	}
	//attendo che arrivi mex di conferma che il signup è andato a buon fine
	ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 														//rinconverto in formato host
	ret = recv(socket_server, (void*)&utente_corretto, len, 0);				//ricevo valore che mi dice se è andato a buon fine il signup o meno
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
	}
	if (ret == 0)		//connessione chiusa
	{
		printf("Errore, conessione al server interrotta.\n");
	}
	if (utente_corretto == -1)			//errore non si è riuscito a portare a termine signup a lato server
	{
		printf("Errore, non è stato possibile aggiungere l'utente %s mancanza di spazio nel server.\n",user);
	}
	if (utente_corretto == 1)			//signup andato a buon fine
	{
		printf("L'utente %s è stato aggiunto correttamente al server.\n",user);
		strcpy(utente_client,user);//aggiorno l'utente associato a questo client
	}
}

void login (char* user, char* psw)			//funzione che manda username e psw al server per accedere
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	int len,ret,i;							//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	int tentativo;							//sarà = 0 se non è corretto e bisogna continuare il ciclo, = 1 se corretto
	int bloccato;							// = 1 ip bloccato , = 0 ip non bloccato
	char *res;								//conterrà risultato della fgets che verrà usata per la rilettura di utente e psw in caso di errore nell'autenticazione
	
	identificativo_comando = 1;				//login ha identificativo1
	tentativo = 0;				

	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	//ricevo esito del controllo se ip è bloccato, = 1 bloccato = 0 sbloccato
	ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 														//rinconverto in formato host
	ret = recv(socket_server, (void*)&bloccato, len, 0);					//ricevo bloccato
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
	}
	if (bloccato)			
	{							//ip bloccato
		printf("Errore, impossibile procedere con il login perchè il client è bloccato per ragioni di sicurezza.\n");
	}
	else 						//ip sbloccato posso continuare con la procedura. inizio else 1
	{
		for (i=0; i<3 ;i++)				//for per tentativi inserimento utente e psw. inizio for 1
		{
			//invio utente
			len = strlen(user)+1;					//prendo lunghezza del messaggio
			lmsg = htons(len);						//converto nel formato network
			ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
			ret	= send (socket_server, (void*)user, len,0);		//invio utente
			if(ret < 0)
			{
			   perror("Errore in fase di invio: \n");
			   exit(-1);
			}
			//invio psw
			len = strlen(psw)+1;					//prendo lunghezza del messaggio
			lmsg = htons(len);						//converto nel formato network
			ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
			ret	= send (socket_server, (void*)psw, len,0);		//invio psw
			if(ret < 0)
			{
			   perror("Errore in fase di invio: \n");
			   exit(-1);
			}
			//ricevo esito controllo correttezza
			ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
			len = ntohs(lmsg); 														//rinconverto in formato host
			ret = recv(socket_server, (void*)&tentativo, len, 0);					//ricevo tentativo
			if(ret < 0)				//gestione errore in fase di ricezione
			{
				perror("Errore in fase di ricezione: \n");
			}
			if (tentativo)				//utente e psw corretti
			{					
				break;
			}
			else  				//utente e psw non corretti. inizio else 2
			{
				if (i != 2)			//per evitare la terza immissione di nuovi dati
				{
					printf("L'utente e la psw inserite non sono valide, nessun riscontro nel database.\n");		//richiedo utente
					//prendo da tastiera nuovo utente 
					printf("Tentativi rimasti: %d\nInserisci un nuovo utente.\n",2-i);
					res = fgets(user, MAX_DIM_USPSW, stdin); 			//aggiorno con il nuovo utente
					if (res == 0)										// errore durante la lettura oppure letti zero bytes
					{
						printf("Errore leggendo stdin.\n");
						exit (-1);
					}
					user[strlen(user)-1]=0;		//tolgo \n
					//prendo da tastiera nuova psw
					printf("Inserisci una nuova psw.\n");			
					res = fgets(psw, MAX_DIM_USPSW, stdin); 			//aggiorno con la nuova psw
					if (res == 0)										// errore durante la lettura oppure letti zero bytes
					{
						printf("Errore leggendo stdin.\n");
						exit (-1);
					}
					psw[strlen(psw)-1]=0;		//tolgo \n
				}
				else
				{
					printf("Numeri massimi di tentativi di login raggiunti. L'ip verrà bloccato per 30 minuti.\n");
				}
			}		//fine else 2
		}			//fine for 1
		if (tentativo)				//utente e psw corretti
		{
			//ricevere session id
			ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del messaggio 
			len = ntohs(lmsg); 														//rinconverto in formato host
			ret = recv(socket_server, (void*)sessionID, len, 0);					//ricevo sessionID
			if(ret < 0)				//gestione errore in fase di ricezione
			{
				perror("Errore in fase di ricezione: \n");
			}
			printf("Login concluso con successo.\n");		//messaggio di login avvenuto con successo
			strcpy(utente_client,user);						//devo mettere l'utente con cui mi sono identificato con successo come utente del client
		}
	}			//fine else 1
}

void invia_giocata()				//funzione che invia la schedina s, che è stata precedentemente inizializzata con i valori immessi dall'utente
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	uint16_t id_valido;						//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0	
	uint16_t esito;							//conterrà l'esito dell'invio e della registrazione della giocata = 1 tutto ok, = 0 errore
	int len,ret;							//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	
	strcpy(s.user,utente_client);//metto user nella schedina
	
	//invio identificativo del comando = 2
	identificativo_comando = 2;
	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	
	//invio sessionID
	len = strlen(sessionID)+1;					//prendo lunghezza del messaggio
	lmsg = htons(len);							//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)sessionID, len,0);					//invio sessionID associato al client
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
	    exit(-1);
	}
	//ricevo valore che mi dice se il sessionID inviato è valido oppure no
	len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
	ret = recv(socket_server, (void*)&id_valido, len, 0);					//ricevo valore che mi dice se il sessionID inviato è valido o meno
	if(ret <= 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
	}
	if (id_valido)			//sessionID valido
	{	//inizio if.0  controllo sessionID
		
		//invio schedina s
		len = sizeof(s);					//prendo lunghezza del messaggio
		lmsg = htons(len);						//converto nel formato network
		ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
		ret	= send (socket_server, (void*)&s, len,0);		//invio schedina s
		if(ret < 0)
		{
			perror("Errore in fase di invio: \n");
			exit(-1);
		}
		//aspettare risultato di conferma = 1 tutto ok, = 0 errore
		len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
		ret = recv(socket_server, (void*)&esito, len, 0);						//ricevo valore che mi dice se è andato a buon fine il signup o meno
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
		}
		if (ret == 0)		//connessione chiusa
		{
			printf("Errore, conessione al server interrotta.\n");
		}
		if (esito)
			printf("Schedina inviata con successo.\n");						//schedina inviata con successo
		else
			printf("Errore, schedina non inviata con successo.\n");			//schedina non inviata con successo
	}		//fine if.0    controllo sessionID
	else  				//sessionID inviato non valido
	{
		printf("Errore, sessionID associato a questa connessione non valido.\n");			//messaggio sessionID non valido
	}
}

void vedi_giocate (int n)			//funzione che chiede al server le giocate effettuate, n può essere 0 o 1
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	uint16_t id_valido;						//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0	
	uint16_t user_valido;					//conterrà il valore che dirà se user inviato è valido = 1 o non valido = 0
	int len,ret;							//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	char risultato[MAX_DIM_RIS];			//conterrà il risultato ricevuto dal server che deve essere stampato a video
	
	strcpy(s.user,utente_client);			//metto user nella schedina
	
	//invio identificativo del comando = 3
	identificativo_comando = 3;
	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	//invio sessionID
	len = strlen(sessionID)+1;					//prendo lunghezza del messaggio
	lmsg = htons(len);							//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)sessionID, len,0);					//invio sessionID associato al client
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
	    exit(-1);
	}
	//ricevo valore che mi dice se il sessionID inviato è valido oppure no
	len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
	ret = recv(socket_server, (void*)&id_valido, len, 0);					//ricevo valore che mi dice se il sessionID inviato è valido o meno
	if(ret <= 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
	}
	if (id_valido)			//sessionID valido
	{	//inizio if.0  controllo sessionID
		//invio utente associato a questo client
		len = strlen(utente_client)+1;					//prendo lunghezza del messaggio
		lmsg = htons(len);							//converto nel formato network
		ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
		ret	= send (socket_server, (void*)utente_client, len,0);				//invio utente associato al client
		if(ret < 0)
		{
			perror("Errore in fase di invio: \n");
			exit(-1);
		}
		//ricevo controllo dell'utente passato come parametro è valido = 1 oppure no = 0
		len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
		ret = recv(socket_server, (void*)&user_valido, len, 0);					//ricevo valore che mi dice se il user inviato è valido o meno
		if(ret <= 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
		}
		if (user_valido)			//user passato come parametro valido
		{		//inizio if.1  user valido
			//invio valore di n = 1 giocate attive, = 0  giocate passate
			len = sizeof(n);							//prendo lunghezza del messaggio
			lmsg = htons(len);							//converto nel formato network
			ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
			ret	= send (socket_server, (void*)&n, len,0);						//invio n
			if(ret < 0)
			{
				perror("Errore in fase di invio: \n");
				exit(-1);
			}
			//ricevo il risultato contenente le giocate richieste
			ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
			len = ntohs(lmsg); 														//rinconverto in formato host
			ret = recv(socket_server, (void*)risultato, len, 0);					//ricevo risultato
			if(ret < 0)				//gestione errore in fase di ricezione
			{
				perror("Errore in fase di ricezione: \n");
				exit(-1);
			}
			//stampo a video il risultato ottenuto
			printf("%s",risultato);
		}		//fine if.1  user valido
		else   			//user passato come parametro non valido
		{
			printf("Errore, l'utente associato a questo client non risulta essere associato al sessionID di questo client o non rosulta registrato nel server.");
		}		
		
	}		//fine if.0    controllo sessionID
	else  				//sessionID inviato non valido
	{
		printf("Errore, sessionID associato a questa connessione non valido.\n");			//messaggio sessionID non valido
	}
}

void vedi_estrazione (int n, char* ruota)		//funzione che richiede di vedere le ultime n estrazioni della ruota
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	uint16_t id_valido;						//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0
	int len,ret,pos_ruota;					//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	char risultato[MAX_DIM_RIS];			//conterrà il risultato ricevuto dal server che deve essere stampato a video
	
	if (n <= 0)
	{
		printf("Errore, il numero di estrazioni da vedere è minore o uguale a zero.\n");
		return;
	}
	if (ruota==NULL)		//ruota non specificata richiedo tutte le ruote
	{
		pos_ruota = -1;		//setta pos_ruota con valore defaul = -1
	}
	else 					//ruote specificate, richiedo quelle ruote
	{
		//controllo che la ruota specificata come parametro sia corretta
		pos_ruota = riconosci_ruote(ruota);			
		if (pos_ruota == -1)			//ruota non riconosciuta errore
		{
			printf("Errore, ruota passata come parametro non riconosciuta.\n");
			return;
		}
	}
	//invio identificativo del comando = 4
	identificativo_comando = 4;
	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	//invio sessionID
	len = strlen(sessionID)+1;					//prendo lunghezza del messaggio
	lmsg = htons(len);							//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)sessionID, len,0);					//invio sessionID associato al client
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
	    exit(-1);
	}
	//ricevo valore che mi dice se il sessionID inviato è valido oppure no
	len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
	ret = recv(socket_server, (void*)&id_valido, len, 0);					//ricevo valore che mi dice se il sessionID inviato è valido o meno
	if(ret <= 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	if (id_valido)			//sessionID valido
	{	//inizio if.0  controllo sessionID
	
		//invio n
		len = sizeof(n);					//prendo lunghezza del messaggio
		lmsg = htons(len);						//converto nel formato network
		ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
		ret	= send (socket_server, (void*)&n, len,0);						//invio n
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   exit(-1);
		}
		//invio pos_ruota
		len = sizeof(pos_ruota);					//prendo lunghezza del messaggio
		lmsg = htons(len);						//converto nel formato network
		ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);		//invio dimensione
		ret	= send (socket_server, (void*)&pos_ruota, len,0);				//invio pos_ruota
		if(ret < 0)
		{
		   perror("Errore in fase di invio: \n");
		   exit(-1);
		}
		//ricevo il risultato contenente le giocate richieste
		ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
		len = ntohs(lmsg); 														//rinconverto in formato host
		ret = recv(socket_server, (void*)risultato, len, 0);					//ricevo risultato
		if(ret < 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
			exit(-1);
		}
		//stampo a video il risultato ottenuto
		printf("%s",risultato);
		
	}	//fine if.0  controllo sessionID
}

void vedi_vincite ()			//funzione che richiede di vedere le vincite del client
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	uint16_t id_valido;						//conterrà il valore che dirà se sessionID inviato è valido = 1 o non valido = 0
	uint16_t user_valido;					//conterrà il valore che dirà se user inviato è valido = 1 o non valido = 0
	int len,ret;							//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	char risultato[MAX_DIM_RIS];			//conterrà il risultato ricevuto dal server che deve essere stampato a video
	
	//invio identificativo del comando = 5
	identificativo_comando = 5;
	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	//invio sessionID
	len = strlen(sessionID)+1;					//prendo lunghezza del messaggio
	lmsg = htons(len);							//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)sessionID, len,0);					//invio sessionID associato al client
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
	    exit(-1);
	}
	//ricevo valore che mi dice se il sessionID inviato è valido oppure no
	len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
	ret = recv(socket_server, (void*)&id_valido, len, 0);					//ricevo valore che mi dice se il sessionID inviato è valido o meno
	if(ret <= 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	if (id_valido)			//sessionID valido
	{	//inizio if.0  controllo sessionID
		
		//invio utente associato a questo client
		len = strlen(utente_client)+1;					//prendo lunghezza del messaggio
		lmsg = htons(len);							//converto nel formato network
		ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
		ret	= send (socket_server, (void*)utente_client, len,0);				//invio utente associato al client
		if(ret < 0)
		{
			perror("Errore in fase di invio: \n");
			exit(-1);
		}
		//ricevo controllo dell'utente passato come parametro è valido = 1 oppure no = 0
		len = sizeof(uint16_t); 												//dimensione del mex di conferma che mi arriverà
		ret = recv(socket_server, (void*)&user_valido, len, 0);					//ricevo valore che mi dice se il user inviato è valido o meno
		if(ret <= 0)				//gestione errore in fase di ricezione
		{
			perror("Errore in fase di ricezione: \n");
		}
		if (user_valido)			//user passociato al client corretto
		{		//inizio if.1  user valido
			//ricevo il risultato contenente le giocate richieste
			ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
			len = ntohs(lmsg); 														//rinconverto in formato host
			ret = recv(socket_server, (void*)risultato, len, 0);					//ricevo risultato
			if(ret < 0)				//gestione errore in fase di ricezione
			{
				perror("Errore in fase di ricezione: \n");
				exit(-1);
			}
			//stampo a video il risultato ottenuto
			printf("%s",risultato);
		}		//fine if.1  user valido
		else   			//user passato come parametro non valido
		{
			printf("Errore, l'utente associato a questo client non risulta essere associato al sessionID di questo client o non rosulta registrato nel server.");
		}		
	}	//fine if.0 controllo sessionID
}

void esci()							//funzione che chiude i socket aperti e termina il client
{
	uint16_t identificativo_comando;		//conterrà l'identificativo del comando che verrà mandato al server
	int len,ret;							//variabile che conterrà la lunghezza dei messaggi da inviare e variabile per contenere i valori di ritorno delle primitive	
	char risultato[MAX_DIM_RIS];					//conterrà il risultato ricevuto dal server che deve essere stampato a video
	
	//mando mex al server che gli comunica di chiudere il socket con cui è connesso a questo client
	identificativo_comando = 6;
	//invio identificativo_comando
	len = sizeof(uint16_t);					//prendo lunghezza del messaggio
	lmsg = htons(len);						//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)&identificativo_comando, len,0);		//invio identificativo_comando
	if(ret < 0)
	{
	   perror("Errore in fase di invio: \n");
	   exit(-1);
	}
	//invio utente associato a questo client
	len = strlen(utente_client)+1;					//prendo lunghezza del messaggio
	lmsg = htons(len);							//converto nel formato network
	ret = send (socket_server, (void*)&lmsg, sizeof(uint16_t),0);			//invio dimensione
	ret	= send (socket_server, (void*)utente_client, len,0);				//invio utente associato al client
	if(ret < 0)
	{
		perror("Errore in fase di invio: \n");
		exit(-1);
	}
	
	//ricevo il risultato
	ret = recv(socket_server, (void*)&lmsg, sizeof(uint16_t), 0);			//attendo dimensione del mesaggio 
	len = ntohs(lmsg); 														//rinconverto in formato host
	ret = recv(socket_server, (void*)risultato, len, 0);					//ricevo risultato
	if(ret < 0)				//gestione errore in fase di ricezione
	{
		perror("Errore in fase di ricezione: \n");
		exit(-1);
	}
	//stampo a video il risultato ottenuto
	printf("%s",risultato);
	
	close(socket_server);		//chiude il socket 
    exit(0);					//termino il processo
}

void lettura_tastiera()				//funzione che legge il comando da tastiera, lo identifica e svolge le operazioni necessarie per soddisfarlo nel caso il comando sia riconosciuto
{
     char buf[ MAX_DIM_COMANDI + (MAX_DIM_PARAMETRI * MAX_NUM_OPZIONI) ],comando[ MAX_DIM_COMANDI ], parametro[ MAX_NUM_OPZIONI ][ MAX_DIM_PARAMETRI ];		//conterranno il comando ricevuto e le opzioni del comando
     int num_cose_lette;				//dirà quante stringhe sono state digitate intervallate da uno spazio, serve per distinguere i comandi dalle opzioni
     
     char *res = fgets(buf, MAX_DIM_COMANDI + (MAX_DIM_PARAMETRI * MAX_NUM_OPZIONI), stdin); // legge anche \n e lo aggiunge a buf
     if (res == 0)
	 {// errore durante la lettura oppure letti zero bytes (ovvero premuto ctrl+d come primo carattere)
		printf("Errore leggendo stdin\n");
		exit (-1);
	 }
	 num_cose_lette = sscanf(buf, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s", comando, parametro[0],parametro[1],parametro[2],parametro[3],parametro[4],parametro[5],parametro[6],parametro[7],parametro[8],parametro[9],parametro[10],parametro[11],parametro[12],parametro[13],parametro[14],parametro[15],parametro[16],parametro[17],parametro[18],parametro[19],parametro[20],parametro[21],parametro[22],parametro[23]);		//legge i comandi e gli eventuali parametri, il numero di %s sono uguali a NUM_MAX_OPZIONI + 1
	 
     switch(riconosci(comando))			//switch che chiama la funzione per capire qual è il comando ricevuto ed ha un caso per ognuno dei comandi riconosciuti dal client
	 {
      case 0: // !help
      {		//ha un parametro che è il nome del comando di cui si vuole sapere la descrizione
		   if (num_cose_lette > 1)		//ho letto il comando e anche il parametro
		   {
			   help(parametro[0]);				//chiamo la funzione passondogli il parametro
		   }
		   else  					//l'utente ha digitato solo il comando
		   {
			   printf("%s",HELP);
		   }
		   break;
      }
      case 1: // !signup ha 2 parametri
      {
		  char username [MAX_DIM_USPSW] , password [MAX_DIM_USPSW];				//conterranno l'username e psw digitati da tastiera
		  if (num_cose_lette == 3)			//controllo che il numero dei parametri letti sia giusto
		  {
				if ((strlen(parametro[0]) > 8) || (strlen(parametro[1]) > 20))		//controllo che le dimensioni siano rispettate
				{
					printf("Dimensione di utente o password non corrette, utente massimo 8 caratteri e psw massimo 20.\n");
				}
				else
				{
					sscanf(parametro[0], "%s", username);			//prendo username e password scritti da tastiera
					sscanf(parametro[1], "%s", password);
					signup(username,password);					//chiamo la funzione che farà il signup e gli passo i parametri letti
				}
		  }
		  else 		//numero di parametri letti sbagliato
		  {
				printf("Numero di parametri non corretto, bisogna passare <username> <password> signup\n");
		  }
          break;
      }
      case 2: // !login ha 2 parametri
      {
		  char username [MAX_DIM_USPSW] , password [MAX_DIM_USPSW];				//conterranno l'username e psw digitati da tastiera
		  if (num_cose_lette == 3)			//controllo che il numero dei parametri letti sia giusto
		  {
				sscanf(parametro[0], "%s", username);			//prendo username e password scritti da tastiera
				sscanf(parametro[1], "%s", password);
				login(username,password);					//chiamo la funzione che farà il signup e gli passo i parametri letti
		  }
		  else 		//numero di parametri letti sbagliato
		  {
				printf("Numero di parametri non corretto, bisogna passare <username> <password> login\n");
		  }
          break;
      }
      case 3: // !invia_giocata massimo ha 24 parametri contando le opzioni invia_giocata -r (11) -n (5) -i (5)
      {			//minimo ha 6 parametri invia_giocata -r (1) -n (1) -i (1)
           int i,j,pos,n;
           float n1;					
           inizializza_schedina(&s);				//inizializzo coi valori di default la schedina
           
           //creazione della schedina che poi verrà mandata al server
           if (num_cose_lette > 7 || num_cose_lette < 24)				//primo controllo sul corretto numero dei parametri
           {					//ora bisognerà controllare che la forma della giocata sia corretta
			   if (!strcmp(parametro[0], "-r\0"))			//controllo che prima opzione sia "-r"
		       {		//inizio primo if di controllo
				   i = 0; 					//servirà per l'indice dei parametri
				   //massimo l'utente può passare come parametro 11 ruote, passerà le ruote fino a che non inserirà -n
				   do   						//inizio do while per visionare le ruote
				   {
					   i++;
					   pos = riconosci_ruote(parametro[i]);			//controlla che la ruota passata sia corretta e ne restituisce l'indice
					   if (!strcmp(parametro[i], "-n\0"))		//abbiamo finito con le ruote digitate
					   {
						   break;				//usciamo dal while
					   }
					   if (!strcmp(parametro[i], "tutte\0"))					//controllo se è stato digitato 'tutte'
					   {
						   int y;
						   //devo puntare su tutte le ruote e poi uscire dal while
						   for (y = 0; y<NUM_RUOTE; y++)
								s.ruote[y] = 1;
						   pos = -2;
					   }
					   if (pos != -1)		//ruota riconosciuta
					   {
							if (s.ruote[pos] == 1)		//ruota già scelta, ripetizione di ruote
							{
								printf("Errore: ruota della giocata inserita più volte, impossibile effettuare la giocata\n");
								return;				//esco dalla funzione
							}
							//ruota non ancora scelta
							s.ruote[pos] = 1;				//faccio puntata su tale ruota
					   }
					   else if (pos != -2)				//caso di ruota non riconosciuta
					   {
							printf("Errore: ruota non riconosciuta, impossibile effettuare la giocata\n");
							return;				//esco dalla funzione
					   }
				   }while(i <= 12);			//devo controllare tutte le ruote passate come parametro, fino a che non trovo -n , fine while
				   if ( (i > 1) && (i <= 12) && (pos == -1))		//procedo con la parte di raccolta dei numeri giocati	
				   {
					   j = 0;			//inizializzo j a zero
					   pos = 0;			//azzero pos
					   do  				//massimo ci sono 10 numeri giocati
					   {
							j++;		//incremento j
							
							if (!strcmp(parametro[i+j], "-i\0"))		//abbiamo finito con i numeri digitate
							{
								pos = 1;
								break;				//usciamo dal while
							}
							n = atoi(parametro[i+j]);				//converto in interi i numeri passati da tastiera
							if (n < 1 || n> 90)					//numeri non validi
							{
								printf("Errore: i numeri passati devono essere compresi fra 1 e 90 compresi, impossibile effettuare la giocata\n");
								return;								//usciamo dalla funzione
							}
							if (j < 11)			//controllo per non fare overflow
								s.numeri[j-1] = n;						//segnamo i numeri della giocata
										
					   } while (j < 11);
					   if ((pos == 1) && (j > 1) && (j <= 11))		//numeri passati ok si prosegue con le puntate
					   {
						   i += j;		//aggiorno valore di i
						   j = -1;		//azzero j
						   
						   do  				//massimo ci sono 5 puntate, inizio while per le puntate
						   {
							    i++;		//incremento i sarà l'indice per i parametr
								j++;		//incremento j, sarà posizione in s.importi_giocata
							
								n1 = atof(parametro[i]);				//converto in float le puntate passate da tastiera
								if (n1 < 0)
								{
									printf("Errore: puntata negativa, impossibile effettuare la giocata\n");
									return;				//esco dalla funzione
								}
								s.importi_giocata[j] = n1;						//segnamo i numeri della giocata
										
					       } while ((i+2) < num_cose_lette);					//fine while per le puntate, il +2 è dato dal dover considerare il comando iniziale e -r per avere un corretto conteggio nel confronto con num_cose_lette
					       
					       if (controllo_correttezza_puntate_schedina(s) == 1)//controllo correttezza delle puntate
					       {
								invia_giocata();			//invio della schedina
						   }
						   else 				//schedina non valida
						   {
								printf("Errore: tipologia di puntata non valida in rapporto al numero dei numeri giocati, impossibile effettuare la giocata\n");
						   }
					   }
					   else  		//caso in cui si sono passati troppi numeri per le giocate o non se ne siano passati
							printf("Errore: quantità di numeri nella giocata sbagliata, devono essere da 1 a 5. Impossibile effettuare la giocata\n");
				   }
				   else  					//errore: passato -r e subito dopo -n senza inserire ruote o sono state inserite troppe ruote o errore nel digitare le ruote
						printf ("Parametri passati non corretti devono avere questa forma invia_giocata -r <ruote> -n <numeri giocati> -i <scommessa>\n");
					//prendere parametri e controllare altre opzioni
			   }		//fine primo if controllo
			   else   									//non passato il test che prima opzione sia "-r"
					printf ("Parametri passati non corretti devono avere questa forma invia_giocata -r <ruote> -n <numeri giocati> -i <scommessa>\n");
		   }
		   else
				printf ("Parametri passati non corretti devono avere questa forma invia_giocata -r <ruote> -n <numeri giocati> -i <scommessa>\n");
           break;
      }
      case 4: // !vedi_giocate ha un solo parametro
      {
           int n;
		   char s[1];
		   
           if (num_cose_lette == 2)			//numero corretto di parametri
           {
			   sscanf(parametro[0], "%s", s);		//leggo il tipo digitato da tastiera
			   n = atoi(s);							//lo converto ad intero
			   if (n == 0 || n == 1)		//il parametro passato può valere solo 0 o 1
					vedi_giocate(n);
			   else
					printf("Tipo passato come parametro errato\n");
		   }
		   else
		   {
			   printf("Numero di parametri non corretto, bisogna passare <tipo>\n");
		   }
           break;
      }
      case 5: // !vedi_estrazione ha da 1 a 2 parametri
      {
		   int n;
		   char ruota [MAX_DIM_RUOTE],s[5];
		   
           if (num_cose_lette == 2 || num_cose_lette == 3)
           {
			   if (num_cose_lette == 2)		//non è specificata la ruota, richiedo di vedere tutte le ruote
			   {
				   sscanf(parametro[0], "%s", s);
				   n = atoi(s);
				   vedi_estrazione(n,NULL);				//richiamo la funzione vedi_estrazione passando n e null
			   }
			   else  			//è specificata anche la ruota
			   {
				   sscanf(parametro[0], "%s", s);
				   sscanf(parametro[1], "%s", ruota);
				   n = atoi(s);
				   vedi_estrazione(n,ruota);			//richiamo la funzione vedi_estrazione passando n e ruota
			   }
		   }
		   else
		   {
			   printf("Numero di parametri non corretto, bisogna passare <n> <ruota>\n");
		   }
           break;
      }
      case 6: // !vedi_vincite non ha parametri
      {
		   if (num_cose_lette == 1)			//controllo che non siano stati messi parametri
		   {
				 vedi_vincite();			//invio dei dati al server
		   }
		   else 							//sono stati passati dei parametri ma la funzione non ne richiede
				printf("Errore la funzione non richiede parametri aggiuntivi al comando\n");
           break;
      }
      case 7: // !esci
      {
           esci();
           break;
      }
      default: printf("\nComando non riconosciuto.\n");			//caso in cui il comando non sia riconosciuto
     }
}

int main(int argc, char**argv)
{
	int i;				//variabile per i for

    if (argc != 3)		//controllo che siano stati passati il numero corretto di parametri
    {
       printf("Numero argomenti errato.\nUsa %s <ip server> <porta server>\n", argv[0]);
       exit(1);
    }

    apri_connessione_server(argv[1], atoi(argv[2]));		//connessione al server
	
    printf("Connessione al server avvenuta, ip %s e porta %s\n",argv[1],argv[2]);		//visualizza messaggio di connessione al server
    printf("%s\n", HELP);																//visualizza il messaggio iniziale di descrizione dei comandi
	
	//settaggio liste select
	FD_ZERO(&set_lista);					//azzero
	FD_ZERO(&set_temp);						//azzero
	FD_SET(0, &set_lista);					//aggiungo stdin
	FD_SET(socket_server , &set_lista);		//aggiungo il socket relativo alla connessione al server
	fdmax = socket_server;					//setto il valore più alto nella lista da passare alla select
	
	while (1)		//inizio while
	{	
		set_temp = set_lista;			//la lista temp prenderà stessi valori della lista organizzata prima
		switch (select (fdmax+1 , &set_temp , NULL , NULL, NULL))		//mi blocco in attesa
		{			//inizio switch
		case -1: 		// errore della select
               {
					perror("Errore nella select(): ");
                    exit(1);
               }
        case 0: 		// timer scaduto, non deve accadere coi parametri passati alla select
               {
					printf("Timer scaduto!\n");
                    exit(0);
               }
        default:		//caso di default, ci sono fd pronti
               {
					for (i=0; i<=fdmax; i++)		//scorro il set dei fd pronti
                    {
						if (FD_ISSET(i, &set_temp))		//trovato fd pronto
                        {
							if (i == 0) 		//stdin pronto
							{
                                lettura_tastiera();			//lettura del comando digitato dall'utente
                                continue ; 
                            }
                            if (i == socket_server) 	//socket relativo alla connessione col server pronto
                            {	
								int error = 0;
								socklen_t len = sizeof (error);
								int retval = getsockopt (socket_server, SOL_SOCKET, SO_ERROR, &error, &len);
								if (retval != 0) 
								{
									// there was a problem getting the error code
									printf("Errore durante il recupero del codice di errore: %s\n", strerror(retval));
								}
								if (error != 0) 		
								{
									printf("Errore socket: %s\n", strerror(error));
								}
								printf("Errore: %d\n",error);
								printf("Socket server chiuso.\n");
								exit(-1);
                                continue ;
                            }
                            // caso di default non previsto
                            printf("Situazione inprevista, caso default, switch, main.(relativo alla select)\n");
                         } /* fine if */
                     } /* fine for */
                 } /* fine default */
		}			//fine switch
	}				//fine while
	close(socket_server);				//chiusura del socket connesso al server
}
