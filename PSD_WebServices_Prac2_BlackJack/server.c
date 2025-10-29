#include "server.h"
#include <pthread.h>

/** Shared array that contains all the games. */
tGame games[MAX_GAMES];

void initGame(tGame *game) {

  // Init players' name
  memset(game->player1Name, 0, STRING_LENGTH);
  memset(game->player2Name, 0, STRING_LENGTH);

  // Alloc memory for the decks
  clearDeck(&(game->player1Deck));
  clearDeck(&(game->player2Deck));
  initDeck(&(game->gameDeck));

  // Bet and stack
  game->player1Bet = 0;
  game->player2Bet = 0;
  game->player1Stack = INITIAL_STACK;
  game->player2Stack = INITIAL_STACK;

  // Game status variables
  game->endOfGame = FALSE;
  game->status = gameEmpty;
}

void initServerStructures(struct soap *soap) {

  if (DEBUG_SERVER)
    printf("Initializing structures...\n");

  // Init seed
  srand(time(NULL));

  // Init each game (alloc memory and init)
  for (int i = 0; i < MAX_GAMES; i++) {
    games[i].player1Name = (xsd__string)soap_malloc(soap, STRING_LENGTH);
    games[i].player2Name = (xsd__string)soap_malloc(soap, STRING_LENGTH);
    allocDeck(soap, &(games[i].player1Deck));
    allocDeck(soap, &(games[i].player2Deck));
    allocDeck(soap, &(games[i].gameDeck));
    initGame(&(games[i]));
  }
}

void initDeck(blackJackns__tDeck *deck) {

  deck->__size = DECK_SIZE;

  for (int i = 0; i < DECK_SIZE; i++)
    deck->cards[i] = i;
}

void clearDeck(blackJackns__tDeck *deck) {

  // Set number of cards
  deck->__size = 0;

  for (int i = 0; i < DECK_SIZE; i++)
    deck->cards[i] = UNSET_CARD;
}

tPlayer calculateNextPlayer(tPlayer currentPlayer) {
  return ((currentPlayer == player1) ? player2 : player1);
}

unsigned int getRandomCard(blackJackns__tDeck *deck) {

  unsigned int card, cardIndex, i;

  // Get a random card
  cardIndex = rand() % deck->__size;
  card = deck->cards[cardIndex];

  // Remove the gap
  for (i = cardIndex; i < deck->__size - 1; i++)
    deck->cards[i] = deck->cards[i + 1];

  // Update the number of cards in the deck
  deck->__size--;
  deck->cards[deck->__size] = UNSET_CARD;

  return card;
}

unsigned int calculatePoints(blackJackns__tDeck *deck) {

  unsigned int points = 0;

  for (int i = 0; i < deck->__size; i++) {

    if (deck->cards[i] % SUIT_SIZE < 9)
      points += (deck->cards[i] % SUIT_SIZE) + 1;
    else
      points += FIGURE_VALUE;
  }

  return points;
}

void copyGameStatusStructure(blackJackns__tBlock *status, char *message,
                             blackJackns__tDeck *newDeck, int newCode) {

  // Copy the message
  memset((status->msgStruct).msg, 0, STRING_LENGTH);
  strcpy((status->msgStruct).msg, message);
  (status->msgStruct).__size = strlen((status->msgStruct).msg);

  // Copy the deck, only if it is not NULL
  if (newDeck->__size > 0)
    memcpy((status->deck).cards, newDeck->cards,
           DECK_SIZE * sizeof(unsigned int));
  else
    (status->deck).cards = NULL;

  (status->deck).__size = newDeck->__size;

  // Set the new code
  status->code = newCode;
}

void *processRequest(void *soap) {

  pthread_detach(pthread_self());

  printf("Processing a new request...");

  soap_serve((struct soap *)soap);
  soap_destroy((struct soap *)soap);
  soap_end((struct soap *)soap);
  soap_done((struct soap *)soap);
  free(soap);

  return NULL;
}

int blackJackns__register(struct soap *soap, blackJackns__tMessage playerName,
                          int *result) {

  int gameIndex;

  // Set \0 at the end of the string
  playerName.msg[playerName.__size] = 0;
  tGameState status;

  *result = -1;
  size_t i;

  while (*result == -1 && i < MAX_GAMES) {

    pthread_mutex_lock(&games[i].mutex);
    status = games[i].status;

    if (status == gameWaitingPlayer) {
      if (games[i].player1Name == playerName.msg) {
        *result = ERROR_NAME_REPEATED;
      } else {
        games[i].player2Name = playerName.msg;
        *result = gameIndex;

        games[i].status = gameReady;
      }
    } else if (status == gameEmpty) {
      games[i].player1Name = playerName.msg;
      *result = gameIndex;
      games[i].status = gameWaitingPlayer;
    }
    pthread_mutex_unlock(&games[i].mutex);

    i++;
  }

  if (i == MAX_GAMES && result < 0) {
    *result = ERROR_SERVER_FULL;
  }

  if (DEBUG_SERVER)
    printf("[Register] Registering new player -> [%s]\n", playerName.msg);

  return SOAP_OK;
}

int blackJackns__getStatus(struct soap *soap, blackJackns__tMessage playerName,
                           int gameId, blackJackns__tBlock *status) {
  // 1. comprobar que esta registrado
  // 2. manejar turnos

  pthread_mutex_lock(&games[gameId].mutex);
  while (playerName.msg == games[gameId].player1Name &&
             games[gameId].currentPlayer != player1 ||
         playerName.msg == games[gameId].player2Name &&
             games[gameId].currentPlayer != player2) {
    pthread_cond_wait(&games[gameId].cond, &games[gameId].mutex);
  }

  pthread_mutex_unlock(&games[gameId].mutex);
}

int blackJackns__playerMove(struct soap *soap, blackJackns__tMessage playerName,
                            int gameId, int action,
                            blackJackns__tBlock *result);
int main(int argc, char **argv) {

  struct soap soap;
  struct soap *tsoap;
  pthread_t tid;
  int port;
  SOAP_SOCKET m, s;

  // Check arguments
  if (argc != 2) {
    printf("Usage: %s port\n", argv[0]);
    exit(0);
  }

  // Init soap environment
  soap_init(&soap);

  // Configure timeouts
  soap.send_timeout = 60;     // 60 seconds
  soap.recv_timeout = 60;     // 60 seconds
  soap.accept_timeout = 3600; // server stops after 1 hour of inactivity
  soap.max_keep_alive = 100;  // max keep-alive sequence

  // Get listening port
  port = atoi(argv[1]);

  // Bind
  m = soap_bind(&soap, NULL, port, 100);

  if (!soap_valid_socket(m)) {
    exit(1);
  }

  printf("Server is ON!\n");

  while (TRUE) {

    // Accept a new connection
    s = soap_accept(&soap);

    // Socket is not valid :(
    if (!soap_valid_socket(s)) {

      if (soap.errnum) {
        soap_print_fault(&soap, stderr);
        exit(1);
      }

      fprintf(stderr, "Time out!\n");
      break;
    }

    // Copy the SOAP environment
    tsoap = soap_copy(&soap);

    if (!tsoap) {
      printf("SOAP copy error!\n");
      break;
    }

    // Create a new thread to process the request
    pthread_create(&tid, NULL, (void *(*)(void *))processRequest,
                   (void *)tsoap);
  }

  // Detach SOAP environment
  soap_done(&soap);
  return 0;
}
