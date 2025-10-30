
#SSL_LIBS=-lssl -lcrypto
#SSL_FLAGS=-DWITH_OPENSSL

SSL_LIBS=
SSL_FLAGS=

CFLAGS=-w
LDFLAGS=
ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer -g

all: soapC.c client server

soapC.c:
	soapcpp2 -b -c blackJack.h

client:
	gcc $(SSL_FLAGS) $(CFLAGS) -o client client.c soapC.c soapClient.c game.c -lgsoap $(SSL_LIBS) -L$(GSOAP_LIB) -I$(GSOAP_INCLUDE)

server:	
	gcc $(SSL_FLAGS) $(CFLAGS) -o server server.c soapC.c soapServer.c game.c -lgsoap $(SSL_LIBS) -L$(GSOAP_LIB) -I$(GSOAP_INCLUDE)

# Extra: compilacion con AddressSanitizer para hacer debug y detectar segfaults.
asan: clean soapC.c
	gcc $(SSL_FLAGS) $(CFLAGS) $(ASAN_FLAGS) -o client client.c soapC.c soapClient.c game.c -lgsoap $(SSL_LIBS) -L$(GSOAP_LIB) -I$(GSOAP_INCLUDE)
	gcc $(SSL_FLAGS) $(CFLAGS) $(ASAN_FLAGS) -o server server.c soapC.c soapServer.c game.c -lgsoap $(SSL_LIBS) -L$(GSOAP_LIB) -I$(GSOAP_INCLUDE)

clean:	
	rm -f client server *.xml *.nsmap *.wsdl *.xsd soapStub.h soapServerLib.* soapH.h soapServer.* soapClientLib.* soapClient.* soapC.*
