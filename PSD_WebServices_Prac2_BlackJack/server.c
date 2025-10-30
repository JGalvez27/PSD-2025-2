#include "server.h"
#include "soapH.h"
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

  // inicializar mutex y cvs.
  pthread_mutex_init(&(game->mutex), NULL);
  pthread_cond_init(&(game->cond), NULL);
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

  int gameIndex = -1;

  // Set \0 at the end of the string
  playerName.msg[playerName.__size] = 0;

  if (DEBUG_SERVER)
    printf("[Register] Registering new player -> [%s]\n", playerName.msg);

  // Buscar huecos en juegos
  for (int i = 0; i < MAX_GAMES && gameIndex == -1; i++) {
    pthread_mutex_lock(&games[i].mutex);

    tGameState status = games[i].status;

    if (status == gameWaitingPlayer) {
      // Comprobar si el nombre ya existe en este juego
      if (strcmp(games[i].player1Name, playerName.msg) == 0) {
        *result = ERROR_NAME_REPEATED;
        pthread_mutex_unlock(&games[i].mutex);

        if (DEBUG_SERVER)
          printf("[Register] ERROR: Name already exists in game %d\n", i);

        return SOAP_OK;
      } else {
        // agregar como j2
        strcpy(games[i].player2Name, playerName.msg);
        gameIndex = i;

        initDeck(&(games[i].gameDeck));
        clearDeck(&(games[i].player1Deck));
        clearDeck(&(games[i].player2Deck));
        games[i].player1Bet = DEFAULT_BET;
        games[i].player2Bet = DEFAULT_BET;
        games[i].endOfGame = FALSE;

        // Randomly select starting player
        games[i].currentPlayer = (rand() % 2 == 0) ? player1 : player2;

        // Deal initial cards (2 cards for each player)
        for (int j = 0; j < 2; j++) {
          unsigned int card1 = getRandomCard(&(games[i].gameDeck));
          games[i].player1Deck.cards[games[i].player1Deck.__size++] = card1;

          unsigned int card2 = getRandomCard(&(games[i].gameDeck));
          games[i].player2Deck.cards[games[i].player2Deck.__size++] = card2;
        }

        // Desbloquear al otro jug y cambiar estado a ready.
        games[i].status = gameReady;
        pthread_cond_broadcast(&games[i].cond);
        if (DEBUG_SERVER)
          printf("[Register] Player %s registered in game %d as player2\n",
                 playerName.msg, i);
      }
    } else if (status == gameEmpty) {
      // agregar como j1
      strcpy(games[i].player1Name, playerName.msg);
      gameIndex = i;
      games[i].status = gameWaitingPlayer;
    }
    pthread_mutex_unlock(&games[i].mutex);
  }
  // Comprobar si no hay huecos disponibles
  if (gameIndex == -1) {
    *result = ERROR_SERVER_FULL;
    if (DEBUG_SERVER)
      printf("[Register] ERROR: Server is full\n");
  } else {
    *result = gameIndex;
  }
  return SOAP_OK;
}

int blackJackns__getStatus(struct soap *soap, blackJackns__tMessage playerName,
                           int gameId, blackJackns__tBlock *status) {
  // 1. comprobar que esta registrado

  char message[STRING_LENGTH];
  tPlayer player;
  blackJackns__tDeck *playerDeck, *rivalDeck;

  playerName.msg[playerName.__size] = 0;

  // Asignar memoria para el resultado (tBlock)
  allocClearBlock(soap, status);

  // Comprobar validez gameid
  if (gameId < 0 || gameId >= MAX_GAMES) {
    copyGameStatusStructure(status, "Invalid game ID", &(status->deck),
                            ERROR_PLAYER_NOT_FOUND);
    return SOAP_OK;
  }

  pthread_mutex_lock(&games[gameId].mutex);

  // Check if player is registered
  if (strcmp(games[gameId].player1Name, playerName.msg) == 0) {
    player = player1;
    playerDeck = &(games[gameId].player1Deck);
    rivalDeck = &(games[gameId].player2Deck);
  } else if (strcmp(games[gameId].player2Name, playerName.msg) == 0) {
    player = player2;
    playerDeck = &(games[gameId].player2Deck);
    rivalDeck = &(games[gameId].player1Deck);
  } else {
    // Player not found
    copyGameStatusStructure(status, "Player not found", &(status->deck),
                            ERROR_PLAYER_NOT_FOUND);
    pthread_mutex_unlock(&games[gameId].mutex);

    if (DEBUG_SERVER)
      printf("[GetStatus] ERROR: Player %s not found in game %d\n",
             playerName.msg, gameId);

    return SOAP_OK;
  }

  // Wait while game is not ready (waiting for second player)
  while (games[gameId].status == gameWaitingPlayer) {

    if (DEBUG_SERVER)
      printf("[GetStatus] Player %s waiting for second player in game %d\n",
             playerName.msg, gameId);

    pthread_cond_wait(&games[gameId].cond, &games[gameId].mutex);
  }

  // 2. manejar turnos

  // pthread_mutex_lock(&games[gameId].mutex);
  // while (playerName.msg == games[gameId].player1Name &&
  //            games[gameId].currentPlayer != player1 ||
  //        playerName.msg == games[gameId].player2Name &&
  //            games[gameId].currentPlayer != player2) {
  //   pthread_cond_wait(&games[gameId].cond, &games[gameId].mutex);
  // }
  //
  // Mientras no sea el turno del jugador (player), y no ha acabado el juego ->
  // Esperar
  while (!games[gameId].endOfGame && games[gameId].currentPlayer != player) {
    if (DEBUG_SERVER) {
      printf("[GetStatus] Player %s waiting for turn in game %d\n",
             playerName.msg, gameId);
    }
    pthread_cond_wait(&games[gameId].cond, &games[gameId].mutex);
  }

  // Comprobar si el juego ha terminado
  if (games[gameId].endOfGame) {
    unsigned int playerPoints = calculatePoints(playerDeck);
    unsigned int rivalPoints = calculatePoints(rivalDeck);

    if (playerPoints > GOAL_GAME) {
      sprintf(message,
              "You lose! You went over %d points. Your points: %d, Rival "
              "points: %d",
              GOAL_GAME, playerPoints, rivalPoints);
      copyGameStatusStructure(status, message, playerDeck, GAME_LOSE);
    } else if (rivalPoints > GOAL_GAME) {
      sprintf(message,
              "You win! Rival went over %d points. Your points: %d, Rival "
              "points: %d",
              GOAL_GAME, playerPoints, rivalPoints);
      copyGameStatusStructure(status, message, playerDeck, GAME_WIN);
    } else if (playerPoints > rivalPoints) {
      sprintf(message, "You win! Your points: %d, Rival points: %d",
              playerPoints, rivalPoints);
      copyGameStatusStructure(status, message, playerDeck, GAME_WIN);
    } else if (rivalPoints > playerPoints) {
      sprintf(message, "You lose! Your points: %d, Rival points: %d",
              playerPoints, rivalPoints);
      copyGameStatusStructure(status, message, playerDeck, GAME_LOSE);
    } else {
      sprintf(message, "Draw! Your points: %d, Rival points: %d", playerPoints,
              rivalPoints);
      copyGameStatusStructure(status, message, playerDeck, GAME_LOSE);
    }
  } else {
    // Es el turno de player.
    unsigned int playerPoints = calculatePoints(playerDeck);
    sprintf(message, "Your turn! Your points: %d", playerPoints);
    copyGameStatusStructure(status, message, playerDeck, TURN_PLAY);
  }

  pthread_mutex_unlock(&games[gameId].mutex);

  if (DEBUG_SERVER)
    printf("[GetStatus] Status sent to player %s in game %d\n", playerName.msg,
           gameId);

  return SOAP_OK;
}

int blackJackns__playerMove(struct soap *soap, blackJackns__tMessage playerName,
                            int gameId, int action,
                            blackJackns__tBlock *result) {
  // 1. comprobar que esta registrado

  char message[STRING_LENGTH];
  tPlayer player;
  blackJackns__tDeck *playerDeck, *rivalDeck;

  playerName.msg[playerName.__size] = 0;

  // Asignar memoria para el resultado (tBlock)
  allocClearBlock(soap, result);

  // Comprobar validez gameid
  if (gameId < 0 || gameId >= MAX_GAMES) {
    copyGameStatusStructure(result, "Invalid game ID", &(result->deck),
                            ERROR_PLAYER_NOT_FOUND);
    return SOAP_OK;
  }

  pthread_mutex_lock(&games[gameId].mutex);

  // Check if player is registered
  if (strcmp(games[gameId].player1Name, playerName.msg) == 0) {
    player = player1;
    playerDeck = &(games[gameId].player1Deck);
    rivalDeck = &(games[gameId].player2Deck);
  } else if (strcmp(games[gameId].player2Name, playerName.msg) == 0) {
    player = player2;
    playerDeck = &(games[gameId].player2Deck);
    rivalDeck = &(games[gameId].player1Deck);
  } else {
    // jug no encontrado
    copyGameStatusStructure(result, "Player not found", &(result->deck),
                            ERROR_PLAYER_NOT_FOUND);
    pthread_mutex_unlock(&games[gameId].mutex);

    if (DEBUG_SERVER)
      printf("[GetStatus] ERROR: Player %s not found in game %d\n",
             playerName.msg, gameId);

    return SOAP_OK;
  }

  // Comprobar si es el turno de este jugador (player)
  if (games[gameId].currentPlayer != player) {
    sprintf(message, "It's not your turn!");
    copyGameStatusStructure(result, message, playerDeck, TURN_WAIT);
    pthread_mutex_unlock(&games[gameId].mutex);
    return SOAP_OK;
  }

  if (DEBUG_SERVER)
    printf("[PlayerMove] Player %s action: %d in game %d\n", playerName.msg,
           action, gameId);

  // Procesar accion
  if (action == PLAYER_HIT_CARD) {
    unsigned int card = getRandomCard(&(games[gameId].gameDeck));
    playerDeck->cards[playerDeck->__size++] = card;

    unsigned int playerPoints = calculatePoints(playerDeck);

    if (playerPoints > GOAL_GAME) {
      // Player se pasa, pierde.
      sprintf(message, "You went over %d! You lose. Your points: %d", GOAL_GAME,
              playerPoints);
      copyGameStatusStructure(result, message, playerDeck, GAME_LOSE);
      games[gameId].endOfGame = TRUE;

      // Desbloquear rival para notificar victoria
      pthread_cond_broadcast(&games[gameId].cond);
    } else if (playerPoints == GOAL_GAME) {
      // Player alcanza 21
      sprintf(message, "You reached %d! You must stand. Your points: %d",
              GOAL_GAME, playerPoints);
      copyGameStatusStructure(result, message, playerDeck, TURN_PLAY);

      // Cambiar turno
      games[gameId].currentPlayer = calculateNextPlayer(player);
      pthread_cond_broadcast(&games[gameId].cond);
    } else {
      // Player continua
      sprintf(message, "You drew a card. Your points: %d", playerPoints);
      copyGameStatusStructure(result, message, playerDeck, TURN_PLAY);
    }
  } else if (action == PLAYER_STAND) {
    unsigned int playerPoints = calculatePoints(playerDeck);
    unsigned int rivalPoints = calculatePoints(rivalDeck);

    // Check if rival has also finished
    if (games[gameId].currentPlayer != player) {
      // Both players have played - determine winner
      if (playerPoints > rivalPoints && playerPoints <= GOAL_GAME) {
        sprintf(message, "You win! Your points: %d, Rival points: %d",
                playerPoints, rivalPoints);
        copyGameStatusStructure(result, message, playerDeck, GAME_WIN);
      } else if (rivalPoints > playerPoints && rivalPoints <= GOAL_GAME) {
        sprintf(message, "You lose! Your points: %d, Rival points: %d",
                playerPoints, rivalPoints);
        copyGameStatusStructure(result, message, playerDeck, GAME_LOSE);
      } else {
        sprintf(message, "Draw! Your points: %d, Rival points: %d",
                playerPoints, rivalPoints);
        copyGameStatusStructure(result, message, playerDeck, GAME_LOSE);
      }
      games[gameId].endOfGame = TRUE;
      pthread_cond_broadcast(&games[gameId].cond);
    } else {
      // Change turn to rival
      sprintf(message, "You stand with %d points. Rival's turn now.",
              playerPoints);
      copyGameStatusStructure(result, message, playerDeck, TURN_WAIT);
      games[gameId].currentPlayer = calculateNextPlayer(player);
      pthread_cond_broadcast(&games[gameId].cond);
    }
  }

  pthread_mutex_unlock(&games[gameId].mutex);

  if (DEBUG_SERVER)
    printf("[PlayerMove] Move processed for player %s in game %d\n",
           playerName.msg, gameId);

  return SOAP_OK;
}
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

  // Init soap and server environment
  soap_init(&soap);
  initServerStructures(&soap);

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

  printf("Server is ON! Listening on port %d\n", port);

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
