#include "client.h"

unsigned int readBet() {

  int isValid, bet = 0;
  xsd__string enteredMove;

  // While player does not enter a correct bet...
  do {

    // Init...
    enteredMove = (xsd__string)malloc(STRING_LENGTH);
    bzero(enteredMove, STRING_LENGTH);
    isValid = TRUE;

    printf("Enter a value:");
    fgets(enteredMove, STRING_LENGTH - 1, stdin);
    enteredMove[strlen(enteredMove) - 1] = 0;

    // Check if each character is a digit
    for (int i = 0; i < strlen(enteredMove) && isValid; i++)
      if (!isdigit(enteredMove[i]))
        isValid = FALSE;

    // Entered move is not a number
    if (!isValid)
      printf(
          "Entered value is not correct. It must be a number greater than 0\n");
    else
      bet = atoi(enteredMove);

  } while (!isValid);

  printf("\n");
  free(enteredMove);

  return ((unsigned int)bet);
}

unsigned int readOption() {

  unsigned int bet;

  do {
    printf("What is your move? Press %d to hit a card and %d to stand\n",
           PLAYER_HIT_CARD, PLAYER_STAND);
    bet = readBet();
    if ((bet != PLAYER_HIT_CARD) && (bet != PLAYER_STAND))
      printf("Wrong option!\n");
  } while ((bet != PLAYER_HIT_CARD) && (bet != PLAYER_STAND));

  return bet;
}

int main(int argc, char **argv) {

  struct soap soap;                 /** Soap struct */
  char *serverURL;                  /** Server URL */
  blackJackns__tMessage playerName; /** Player name */
  blackJackns__tBlock gameStatus;   /** Game status */

  unsigned int playerMove;  /** Player's move */
  int resCode, gameId;      /** Result and gameId */
  int registered = FALSE;   /** Registration flag */
  int gameFinished = FALSE; /** Game finished flag */

  // Check arguments
  if (argc != 2) {
    printf("Usage: %s http://server:port\n", argv[0]);
    exit(0);
  }

  // Init gSOAP environment
  soap_init(&soap);

  // Obtain server address
  serverURL = argv[1];

  // Allocate memory
  allocClearMessage(&soap, &(playerName));
  allocClearBlock(&soap, &gameStatus);

  // Read player name
  printf("Enter your player name: ");
  fgets(playerName.msg, STRING_LENGTH - 1, stdin);
  playerName.msg[strlen(playerName.msg) - 1] = 0; // Remove newline
  playerName.__size = strlen(playerName.msg);

  printf("\n");

  // Registration loop
  while (!registered) {

    printf("Registering player %s...\n", playerName.msg);

    // Call register service
    if (soap_call_blackJackns__register(&soap, serverURL, "", playerName,
                                        &resCode) == SOAP_OK) {

      if (resCode >= 0) {
        // Registration successful
        gameId = resCode;
        registered = TRUE;
        printf("Successfully registered! Game ID: %d\n", gameId);
        printf("Waiting for another player to join...\n\n");
      } else {
        // Registration failed
        printf("Registration failed: ");
        showCodeText(resCode);

        if (resCode == ERROR_NAME_REPEATED) {
          printf("Please choose a different name.\n");
          printf("Enter your player name: ");
          fgets(playerName.msg, STRING_LENGTH - 1, stdin);
          playerName.msg[strlen(playerName.msg) - 1] = 0;
          playerName.__size = strlen(playerName.msg);
        } else if (resCode == ERROR_SERVER_FULL) {
          printf("Server is full. Please try again later.\n");
          soap_destroy(&soap);
          soap_end(&soap);
          soap_done(&soap);
          return 1;
        }
      }
    } else {
      // SOAP error
      printf("Error calling register service:\n");
      soap_print_fault(&soap, stderr);
      soap_destroy(&soap);
      soap_end(&soap);
      soap_done(&soap);
      return 1;
    }
  }

  // Main game loop
  while (!gameFinished) {

    // Get game status
    if (soap_call_blackJackns__getStatus(&soap, serverURL, "", playerName,
                                         gameId, &gameStatus) == SOAP_OK) {

      // Check if player was found
      if (gameStatus.code == ERROR_PLAYER_NOT_FOUND) {
        printf("Error: Player not found in game!\n");
        printStatus(&gameStatus, DEBUG_CLIENT);
        break;
      }

      // Print game status
      printf("\n=== Game Status ===\n");
      printStatus(&gameStatus, DEBUG_CLIENT);
      printf("===================\n\n");

      // Check game result
      if (gameStatus.code == GAME_WIN) {
        printf("\n*** CONGRATULATIONS! YOU WON! ***\n\n");
        gameFinished = TRUE;
      } else if (gameStatus.code == GAME_LOSE) {
        printf("\n*** YOU LOST! BETTER LUCK NEXT TIME! ***\n\n");
        gameFinished = TRUE;
      } else if (gameStatus.code == TURN_WAIT) {
        printf("Waiting for rival's move...\n");
        // Continue loop to call getStatus again
      } else if (gameStatus.code == TURN_PLAY) {
        // Player's turn - loop to handle multiple moves
        int turnFinished = FALSE;

        while (!turnFinished && !gameFinished) {

          // Read player's move
          playerMove = readOption();

          // Call playerMove service
          if (soap_call_blackJackns__playerMove(&soap, serverURL, "",
                                                playerName, gameId, playerMove,
                                                &gameStatus) == SOAP_OK) {

            // Print result of the move
            printf("\n=== Move Result ===\n");
            printStatus(&gameStatus, DEBUG_CLIENT);
            printf("===================\n\n");

            // Check game result
            if (gameStatus.code == GAME_WIN) {
              printf("\n*** CONGRATULATIONS! YOU WON! ***\n\n");
              gameFinished = TRUE;
              turnFinished = TRUE;
            } else if (gameStatus.code == GAME_LOSE) {
              printf("\n*** YOU LOST! BETTER LUCK NEXT TIME! ***\n\n");
              gameFinished = TRUE;
              turnFinished = TRUE;
            } else if (gameStatus.code == TURN_WAIT) {
              printf("You finished your turn. Waiting for rival...\n");
              turnFinished = TRUE;
            } else if (gameStatus.code == TURN_PLAY) {
              // Player can continue playing
              printf("You can make another move.\n");
            }

          } else {
            // SOAP error
            printf("Error calling playerMove service:\n");
            soap_print_fault(&soap, stderr);
            turnFinished = TRUE;
            gameFinished = TRUE;
          }
        }
      }

    } else {
      // SOAP error
      printf("Error calling getStatus service:\n");
      soap_print_fault(&soap, stderr);
      break;
    }
  }

  printf("Game ended. Thank you for playing!\n");

  // Cleanup
  soap_destroy(&soap);
  soap_end(&soap);
  soap_done(&soap);

  return 0;
}
