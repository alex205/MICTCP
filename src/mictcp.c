#include <mictcp.h>
#include <api/mictcp_core.h>
#include <stdint.h>
#include <inttypes.h>
#define WINDOW_SIZE 1000
#define LOSS_RATE 300
#define ADMISSIBLE_LOSS_RATE 97

/**
* This is MICTCP version 3
* @author Alexis GIRARDI
* @author Guilhem CICHOCKI
* Stop and Wait OK
* Phase de connexion OK
* Fiabilité partielle OK
* Fermeture de connexion OK
* Bugs éventuels :
* - une erreur peut éventuellement survenir si le serveur ne reçoit pas le 2ème
*   ACK lors de la fermeture de connexion
* Bugs corrigés :
* - reprise des pertes sur le ACK de la phase de connexion
* Reste à implémenter
* - négociation du taux de pertes admissibles lors de la phase de connexion
*/

int setting = 1;
mic_tcp_sock notre_socket;
mic_tcp_header header;
unsigned short expected_seq_num = 0;
/* On utilise cette variable pour activer le mode debug
* qui rend notre programme plus verbeux
*/
unsigned short debug = 0;
/* pour faire le ratio du nombre de pertes */
// pour l'instant écrit en dur
double admissible_loss_rate = 0;
int admissible_loss_rate_initial = 0;
int admissible_loss_rate_other = 0;
uint64_t total_packets = 0;

short window[WINDOW_SIZE] = {0};

/* pour le temps du timer avant timeout */
int timeOutLimit = 1000;

// Permet de créer un socket entre l’application et MIC-TCP
// Retourne le descripteur du socket ou bien -1 en cas d'erreur
int mic_tcp_socket(start_mode sm)
{
  printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

  if(initialize_components(sm) == -1) return -1; // Appel obligatoire    return -1;

  //set_loss_rate(LOSS_RATE);
  notre_socket.fd = 0;
  notre_socket.state = IDLE;
  header.seq_num = 0; // parce qu'on peut pas le faire dans l'espace global
  return notre_socket.fd; //FD de notre socket => 1
}

int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
// Permet d’attribuer une adresse à un socket. Retourne 0 si succès, et -1 en cas d’échec
{
  printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
  printf("On affecte le port %d\n", addr.port);
  memcpy(&notre_socket.addr, &addr, sizeof(mic_tcp_sock_addr));
  return (notre_socket.addr.ip_addr != NULL ? 0 : -1);
}

int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
// Met l’application en état d'acceptation d’une requête de connexion entrante
// Retourne 0 si succès, -1 si erreur
{
  printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
  if(notre_socket.state != IDLE)
  return -1;

  //Affectation du taux de pertes du serveur
  admissible_loss_rate_initial = 98;
  //On a appelé accept, on passe en WAIT_FOR_SYN
  notre_socket.state = WAIT_FOR_SYN;

  // c'est process_received_PDU qui gère l'arrivée des paquets, pendant ce temps on met en pause
  // le thread principal
  while(notre_socket.state != ESTABLISHED_SERVER) {}
  if(debug)
    printf("Reprise du thread principal\n");
  return 0;
}

int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
// Permet de réclamer l’établissement d’une connexion
// Retourne 0 si la connexion est établie, et -1 en cas d’échec
{
  printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
  //Affectation du taux de perte du client
  admissible_loss_rate_initial = ADMISSIBLE_LOSS_RATE;

  size_t taille_envoi = 0;
  size_t taille_ack = 0;
  int return_value = 0;
  int connection_attempt = 0;
  //On a appelé connect, on passe dans l'état CONNECTING
  notre_socket.state = CONNECTING;
  // ENVOI DU SYN
  //FABRICATION DU PAQUET
  mic_tcp_pdu pdu;

  //CREATION DE L'ENTÊTE
  header.source_port =  notre_socket.addr.port;
  header.dest_port = notre_socket.addr.port;
  header.seq_num = 0;
  header.syn = 1; // LE SYN !!!
  header.ack = 0;
  header.fin = 0;
  header.ack_num = admissible_loss_rate_initial;

  //CREATION DU PAYLOAD
  mic_tcp_payload pl;
  pl.data = "";
  pl.size = 0;

  //ASSEMBLAGE DU PACKET
  pdu.hd = header;
  pdu.payload = pl;

  //on envoie
  if(debug)
  {
    printf("Paquet à envoyer pour le SYN\n");
    dissector(pdu);
  }

  //Construction du packet en retour

  mic_tcp_payload packet_ack;
  packet_ack.data = malloc(15); //
  mic_tcp_sock_addr addr_ack;

  while(notre_socket.state == CONNECTING)
  {
    printf("envoi de SYN\n");
    send_syn:
    taille_envoi = IP_send(pdu, notre_socket.addr);
    //On attend le SYN_ACK
    taille_ack = IP_recv(&packet_ack, NULL, timeOutLimit);

    if(taille_ack == -1)
    {
      if(connection_attempt <= 15)
      {
	      goto send_syn;
      }
      else
      {
      	printf("Connection timed out ...\n");
      	notre_socket.state = IDLE;
      	return_value = -1;
      }
    }

    //On regarde si on a bien le SYN_ACK
    mic_tcp_header ack_hd = get_header(packet_ack.data);
    if(ack_hd.syn && ack_hd.ack)
    {
      if(debug)
        printf("SYN_ACK reçu\n");
      //On envoie le ACK
      pdu.hd.syn = 0;
      pdu.hd.ack = 1;

      if(debug)
      	printf("On a reçu en perte %d\n", ack_hd.ack_num);
      //pour le taux de perte
      admissible_loss_rate = ((double) ack_hd.ack_num) / 100.0;
      if(debug)
      	printf("Le taux admissible %lf\n", admissible_loss_rate);

      if(debug)
      {
        //printf("Le paquet à envoyer pour le ACK\n");
        //dissector(pdu);
      }
      IP_send(pdu, notre_socket.addr);
      // on a envoyé notre ACK, on passe en connecté !
      notre_socket.state = ESTABLISHED_CLIENT;
    }
  }

  return return_value;
}

int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
// Permet de réclamer l’envoi d’une donnée applicative
// Retourne la taille des données envoyées, et -1 en cas d'erreur
{
  printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
  //FABRICATION DU PAQUET
  mic_tcp_pdu pdu;

  //CREATION DE L'ENTÊTE
  header.source_port =  notre_socket.addr.port;
  header.dest_port = notre_socket.addr.port;
  header.seq_num = expected_seq_num;
  header.syn = 0;
  header.ack = 0;
  header.fin = 0;

  //CREATION DU PAYLOAD
  mic_tcp_payload pl;
  pl.data = mesg;
  pl.size = mesg_size;

  //ASSEMBLAGE DU PACKET
  pdu.hd = header;
  pdu.payload = pl;

  if(debug)
  {
    printf("Packet construit avec le numéro de séquence : %d\n", header.seq_num);

    printf("Le paquet construit :\n");
    dissector(pdu);
  }
  //MISE À JOUR DU NUMÉRO DE SÉQUENCE ATTENDU
  expected_seq_num = (expected_seq_num + 1) % 2;

  if(debug)
    printf("On s'attend à recevoir le numéro %d\n", expected_seq_num);

  size_t taille_envoi = 0;
  mic_tcp_payload packet_ack;
  packet_ack.data = malloc(15); //
  mic_tcp_sock_addr addr_ack;

  size_t taille_ack = 0;

  total_packets++;
  send:
  if(debug)
    printf("On envoie le paquet\n");
  taille_envoi = IP_send(pdu, notre_socket.addr);
  //printf("IP RECV\n");
  taille_ack = IP_recv(&packet_ack, NULL, timeOutLimit);
  /*printf("la taille ack : %d\n", taille_ack);
  printf("IP RECV après\n");
  printf("la data reçue : %s\n", packet_ack.data);
  printf("sa taille : %d\n", packet_ack.size);*/
  if(taille_ack == -1)
  {
    if(debug)
      printf("Pas reçu l'ACK car timeout\n");
    if(renvoi())
      goto send;
    else
      goto end;
  }
  else
  {
    mic_tcp_header ack_hd = get_header(packet_ack.data);
    while(ack_hd.syn && ack_hd.ack && !ack_hd.fin)
    {
      //si on reçoit un SYN_ACK c'est que le serveur n'est pas connecté car le ACK
      //s'est perdu, par conséquent on renvoie un ACK et on reboucle sur l'envoi du
      //paquet pour pouvoir le passer

      //on reconstruit un paquet pour ne pas détruire celui à envoyer
      //if(debug)
        printf("Le serveur semble ne pas être connecté, on lui renvoi un ACK !\n");

      mic_tcp_pdu connect_ack_pdu;
      mic_tcp_header connect_ack_hd;
      mic_tcp_payload connect_ack_pl;

      connect_ack_hd.source_port =  notre_socket.addr.port;
      connect_ack_hd.dest_port = notre_socket.addr.port;
      connect_ack_hd.seq_num = 0;
      connect_ack_hd.syn = 0;
      connect_ack_hd.ack = 1;
      connect_ack_hd.fin = 0;

      connect_ack_pl.data = "";
      connect_ack_pl.size = 0;

      connect_ack_pdu.hd = connect_ack_hd;
      connect_ack_pdu.payload = connect_ack_pl;

      taille_envoi = IP_send(connect_ack_pdu, notre_socket.addr);
      goto send;
    }
    
    if(!ack_hd.syn && !ack_hd.ack && ack_hd.fin)
    {
        //si on a reçu un FIN c'est que le serveur veut clôturer la connexion
        mic_tcp_pdu fin_ack_pdu;
        mic_tcp_header fin_ack_hd;
        mic_tcp_payload fin_ack_pl;
        
      fin_ack_hd.source_port =  notre_socket.addr.port;
      fin_ack_hd.dest_port = notre_socket.addr.port;
      fin_ack_hd.seq_num = 0;
      fin_ack_hd.syn = 0;
      fin_ack_hd.ack = 1;
      fin_ack_hd.fin = 0;

      fin_ack_pl.data = "";
      fin_ack_pl.size = 0;

      fin_ack_pdu.hd = fin_ack_hd;
      fin_ack_pdu.payload = fin_ack_pl;

      taille_envoi = IP_send(fin_ack_pdu, notre_socket.addr);
      
      printf("Connection ended from server\n");
      notre_socket.state = IDLE;
      return 0;
      }
        


    if(ack_hd.ack) // s'il s'agit d'un ACK
    {
      if(debug)
        printf("On a reçu un ACK avec le seq num %d, on s'attend à %d\n", ack_hd.seq_num, expected_seq_num);
      if(ack_hd.seq_num == expected_seq_num)
      {
        //if(debug)
        printf("Tout est bon, on jimpe à la fin\n");
        window[((total_packets-1) % WINDOW_SIZE)] = 1;
        goto end;
      }
      else
      {
        //if(debug)
        printf("Le seq num est pas bon on jimpe au send\n");
        if(renvoi())
          goto send;
        else
          goto end;
      }
    }
    else
    {
      //if(debug)
      printf("Ce n'est pas un ACK, on jimpe au send\n");
      if(renvoi())
        goto send;
      else
        goto end;
    }
  }

  end:
  free(packet_ack.data);
  //    printf("la fin et taille envoi %d\n", taille_envoi);
  //    printf("-----------\n---FIN---\n----------\n");
  return (taille_envoi == 0 ? -1 : taille_envoi);
}


int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
// Permet à l’application réceptrice de réclamer la récupération d’une donnée
// stockée dans les buffers de réception du socket
// Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
// NB : cette fonction fait appel à la fonction app_buffer_get()

{
  printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
  //LECTURE DU PAYLOAD DANS LE BUFFER
  mic_tcp_payload pl;
  pl.data = mesg;
  pl.size = max_mesg_size;

  return app_buffer_get(pl);
}

int mic_tcp_close (int socket)
	// Permet de réclamer la destruction d’un socket.
	// Engendre la fermeture de la connexion suivant le modèle de TCP.
	// Retourne 0 si tout se passe bien et -1 en cas d'erreur
{
	printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	// pour éviter de boucler à l'infini
	int max_try = 30;
	int current_try = 0;
	//on prépare le paquet de FIN
	size_t taille_envoi;
	mic_tcp_pdu fin_pdu;
	mic_tcp_header fin_hd;
	mic_tcp_payload fin_pl;

	//pour la réception du ACK
	size_t taille_ack;
	mic_tcp_payload packet_ack;
	packet_ack.data = malloc(15);
	mic_tcp_header ack_hd;

	fin_hd.source_port =  notre_socket.addr.port;
	fin_hd.dest_port = notre_socket.addr.port;
	fin_hd.seq_num = 0;
	fin_hd.syn = 0;
	fin_hd.ack = 0;
	fin_hd.fin = 1;

	fin_pl.data = "";
	fin_pl.size = 0;

	fin_pdu.hd = fin_hd;
	fin_pdu.payload = fin_pl;

	if(notre_socket.state == ESTABLISHED_CLIENT)
	{
		//envoi du message de fin
envoi_fin_client:
		if(current_try >= max_try)
		{
			printf("Too many attempt for closing connexion, aborting\n");
			return -1;
		}
		taille_envoi = IP_send(fin_pdu, notre_socket.addr);
		//on attend le ACK du serveur
		taille_ack = IP_recv(&packet_ack, NULL, timeOutLimit);
		if(taille_ack == -1)
		{
			if(debug)
				printf("ACK from server timed out\n");
			current_try++;
			goto envoi_fin_client;
		}
		else
		{
			ack_hd = get_header(packet_ack.data);
			if(!ack_hd.syn && ack_hd.ack && !ack_hd.fin)
			{
				if(debug)
					printf("Bien reçu l'ack du serveur, on termine\n");
				printf("Connection ended from client.\n");
				notre_socket.state = IDLE;
				return 0;
			}
			else
			{
				current_try++;
				goto envoi_fin_client;
			}
		}
	}
	else if (notre_socket.state == ESTABLISHED_SERVER)
	{
envoi_fin_serveur:
		if(current_try >= max_try)
		{
			printf("Too many attempt for closing connexion, aborting\n");
			return -1;
		}
		taille_envoi = IP_send(fin_pdu, notre_socket.addr);
		while(notre_socket.state != FIN_ACK_RECEIVED) {}

		printf("Connection ended from server\n");
		notre_socket.state = IDLE;
		return 0;
	}
	return -1;
}

void process_received_PDU(mic_tcp_pdu pdu)
// Gère le traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
// et d'acquittement, etc.) puis insère les données utiles du PDU dans le buffer
// de réception du socket. Cette fonction utilise la fonction app_buffer_add().
{
  printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
  if(debug)
    show_protocol_state();
  //le header
  mic_tcp_header ack_hd;
  ack_hd.source_port = notre_socket.addr.port;
  ack_hd.dest_port = notre_socket.addr.port;
  ack_hd.ack_num = 0;
  //  ack_hd.seq_num = 0;
  ack_hd.syn = 0;
  ack_hd.ack = 1;
  ack_hd.fin = 0;

  //le payload
  mic_tcp_payload ack_pl;
  ack_pl.data = "";
  ack_pl.size = 0;


  //Si le protocole n'est pas connecté on utilise cette partie pour traiter la
  //réception du SYN / ACK
  if(notre_socket.state != ESTABLISHED_SERVER && notre_socket.state != CLOSING)
  {
    int taille_envoi = 0;
    //pas besoin de boucle ici, on va lancer cette fonction à chaque paquet reçu
    //le test du SYN
    if(pdu.hd.syn == 1 && pdu.hd.ack == 0 && pdu.hd.fin == 0) // les autres conditions pour être sûr qu'on a bien un SYN
    {
      //on passe notre protocole en SYN_RECEIVED
      notre_socket.state = SYN_RECEIVED;
      if(debug)
        printf("SYN OK\n");
      admissible_loss_rate_other = pdu.hd.ack_num;
      if(debug)
      	printf("Le client dit qu'il fonctionne à %d\n", admissible_loss_rate_other);

    }
    else if(pdu.hd.ack == 1 && pdu.hd.syn == 0 && pdu.hd.fin == 0)
    {
      if(debug)
        printf("ACK reçu, protocole connecté !\n");
      notre_socket.state = ESTABLISHED_SERVER;
    }

    if(notre_socket.state == SYN_RECEIVED)
    {
      //on est en SYN_RECEIVED, ce qui veut dire qu'on attend un ACK
      //donc à chaque paquet reçu on envoie un SYN_ACK tant qu'on a pas le ACK
      //on envoie un SYN_ACK ici
      ack_hd.syn = 1;
      ack_hd.ack = 1;
      ack_hd.ack_num = (admissible_loss_rate_initial > admissible_loss_rate_other ? admissible_loss_rate_initial : admissible_loss_rate_other);
      if(debug)
      	printf("On va donc travailler à %d\n", ack_hd.ack_num);
      admissible_loss_rate = ((double) ack_hd.ack_num) / 100.0;

      pdu.hd = ack_hd;
      pdu.payload = ack_pl;
      IP_send(pdu, notre_socket.addr);
    }
  }

  //Réception des paquets une fois connecté
  else
  {
    if(debug)
    {
      printf("Voilà un paquet à traiter ! \n");
      dissector(pdu);
    }
    // on gère le cas de la fermeture de connexion
    if(pdu.hd.fin)
    {
      if(debug)
        printf("J'ai reçu une demande de fin !\n");
      notre_socket.state = CLOSING;
      // on envoie le ACK de fin
      pdu.hd.syn = 0;
      pdu.hd.ack = 1;
      pdu.hd.fin = 0;

      if(debug)
        printf("Je lance un ACK ...\n");
      IP_send(pdu, notre_socket.addr);
      printf("Connection ended from client\n");
    }
    else
    {
      if(debug)
      {
        printf("On va peut-être charger le buffer, on attend le seq num %d et on reçoit %d\n", expected_seq_num, pdu.hd.seq_num);
        printf("Paquet reçu :\n");
        dissector(pdu);
      }
      if(expected_seq_num == pdu.hd.seq_num)
      {
        if(debug)
          printf("Le numéro de séquence est plutôt bon, on va envoyer un ACK\n");
        app_buffer_set(pdu.payload);
        ack_hd.seq_num = (expected_seq_num + 1) % 2;

        //assemblage de tout
        pdu.hd = ack_hd;
        pdu.payload = ack_pl;

        if(debug)
        {
          printf("On envoie le ACK avec le seq_num %d\n", ack_hd.seq_num);

          dissector(pdu);
        }
        int ret = IP_send(pdu, notre_socket.addr); // envoi de l'ACK
        if(debug)
        printf("IP send a envoyé %d unité random\n", ret);
        expected_seq_num=(expected_seq_num+1)%2;
      }
      else
      {
        if(debug)
          printf("Le numéro de séquence est pas bon\n");
        ack_hd.seq_num = expected_seq_num;
        //assemblage de tout
        pdu.hd = ack_hd;
        pdu.payload = ack_pl;

        if(debug)
          printf("On demande le renvoi du paquet\n");
        int ret = IP_send(pdu, notre_socket.addr); // on force à renvoyer le paquet !!
        if(debug)
          printf("IP send a envoyé %d unité random\n", ret);
      }
    }
  }



}

void dissector(const mic_tcp_pdu pdu)
{
  printf("BEGIN PACKET\n");
  printf("---HEADER---\n");
  printf("Source port : %d\n", pdu.hd.source_port);
  printf("Dest port : %d\n", pdu.hd.dest_port);
  printf("Seq num : %d\n", pdu.hd.seq_num);
  printf("SYN : %d\n", pdu.hd.syn);
  printf("ACK : %d\n", pdu.hd.ack);
  printf("FIN : %d\n", pdu.hd.fin);
  printf("---PAYLOAD---\n");
  printf("Data : %s\n", pdu.payload.data);
  printf("Size : %d\n", pdu.payload.size);
  printf("END PACKET\n");
}

void show_protocol_state()
{
  switch (notre_socket.state) {
    case CONNECTING:
    printf("Protocol : CONNECTING\n");
    break;

    case ESTABLISHED_CLIENT:
    printf("Protocol : ESTABLISHED_CLIENT\n");
    break;
    
    case ESTABLISHED_SERVER:
    printf("Protocol : ESTABLISHED_SERVER\n");
    break;

    case IDLE:
    printf("Protocol : IDLE\n");
    break;

    case WAIT_FOR_SYN:
    printf("Protocol : WAIT_FOR_SYN\n");
    break;

    case SYN_RECEIVED:
    printf("Protocol : SYN_RECEIVED\n");
    break;

    case CLOSING:
    printf("Protocol : CLOSING\n");
    break;
  }
}

int renvoi()
{
  int sum = 0;
  int i;
  float actual_loss_rate = 0;
  for(i=0;i<WINDOW_SIZE;i++)
  {
    sum += window[i];
  }

  actual_loss_rate = ((float)sum / (float)(total_packets <= WINDOW_SIZE ? total_packets : WINDOW_SIZE));
  printf("Actual loss rate : %lf, goal loss rate : %lf\n", actual_loss_rate, admissible_loss_rate);
  return actual_loss_rate <= admissible_loss_rate;
}
