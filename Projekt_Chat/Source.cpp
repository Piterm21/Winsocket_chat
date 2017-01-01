#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <queue>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

#define BUFFER_LENGTH 1024
#define COMMAND_SOCKET_ERROR -1
#define COMMAND_DISCONNECT 1
#define COMMAND_CHANGE_NAME 2
#define COMMAND_CREATE_CHANNEL 3
#define COMMAND_LIST_CHANNELS 4
#define COMMAND_JOIN_CHANNEL 5
#define COMMAND_LIST_USERS_ON_CHANNEL 6
#define COMMAND_LEAVE_CHANNEL 7
#define COMMAND_CHECK_ALIVE 8
#define MAX_CONNECTIONS 5

struct channel
{
	string name;
	int currentClients;
	string* clientNames;
};

struct client
{
	string id;
	channel* channel;
	SOCKET socket;
	int channelIndex;
};

struct message
{
	string msg;
	int command;
	int length;
};

int readServerCommand(bool &running)
{
	while (running) {
		string serverInput = "";
		getline(cin, serverInput);
		if (serverInput == "close") {
			running = false;
		}
	}
	return 0;
}

int handleSendMessage(SOCKET receivers[], int numberOfReceivers, string msg, int commandPassedValue = 0)
{
	int characterCount = msg.length();
	if (characterCount > BUFFER_LENGTH - 6) {
		cout << "Wiadomosc za dluga" << endl;
		return 0;
	}
	if (characterCount == 0) {
		return 0;
	}
	int i = 0;
	string command = string(12, '\0');
	command = "";
	while (i < 12 && i < msg.length() && msg[i] != ' ') {
		command += msg[i];
		i++;
	}
	int commandValue = 0;
	int msgOffset = 0;
	if (command == "BYE") {
		commandValue = COMMAND_DISCONNECT;
	} else if (command == "NICK") {
		msgOffset = 5;
		commandValue = COMMAND_CHANGE_NAME;
	} else if (command == "CREATE") {
		msgOffset = 7;
		commandValue = COMMAND_CREATE_CHANNEL;
	} else if (command == "LISTCHANNELS") {
		commandValue = COMMAND_LIST_CHANNELS;
	} else if (command == "JOINCHANNEL") {
		msgOffset = 12;
		commandValue = COMMAND_JOIN_CHANNEL;
	} else if (command == "LISTUSERS") {
		commandValue = COMMAND_LIST_USERS_ON_CHANNEL;
	} else if (command == "LEAVE") {
		commandValue = COMMAND_LEAVE_CHANNEL;
	}
	characterCount -= msgOffset;
	int lengthOfCharacterCount = 1;
	string msgToSend = string(BUFFER_LENGTH, '\0');
	msgToSend = "";
	while (characterCount > 9) {
		characterCount /= 10;
		lengthOfCharacterCount++;
	}
	characterCount = msg.length() - msgOffset;
	for (int i = 0; i < 4 - lengthOfCharacterCount; i++) {
		msgToSend += "a";
	}
	msgToSend += to_string(characterCount);
	if (commandPassedValue == 0) {
		msgToSend += to_string(commandValue);
	} else {
		msgToSend += to_string(commandPassedValue);
	}
	characterCount = msg.length();
	for (int i = msgOffset; i < characterCount; i++) {
		msgToSend += msg[i];
	}
	for (i = 0; i < numberOfReceivers; i++) {
		int sendResult = send(receivers[i], msgToSend.c_str(), characterCount + 5 - msgOffset, 0);
		int lastError = WSAGetLastError();
		if (numberOfReceivers == 1 && sendResult == SOCKET_ERROR && lastError != WSAEWOULDBLOCK) {
			return -1;
		}
	}
	return 1;
}

int checkSocketsForDisconnect(int numberOfSockets, vector<client> &clients, queue<message> &messages, bool &canWrite, bool &running)
{
	while (running) {
		Sleep(4999);
		for (int i = 0; i < numberOfSockets; i++) {
			if (clients[i].socket != INVALID_SOCKET) {
				SOCKET receivers[1] = { clients[i].socket };
				int sendResult = handleSendMessage(receivers, 1, "CHECK_ALIVE", COMMAND_CHECK_ALIVE);
				if (sendResult == -1) {
					while (!canWrite) {
					}
					canWrite = false;
					message socketDisconnectedMessage;
					socketDisconnectedMessage.command = -1;
					messages.push(socketDisconnectedMessage);
					canWrite = true;
				}
			}
		}
	}
	return 0;
}

int checkClientsForDisconnect(vector<client> &clients, queue<message> &messages, bool &canWrite, bool &running)
{
	return checkSocketsForDisconnect(MAX_CONNECTIONS, clients, messages, canWrite, running);
}

int checkServerForDisconnect(vector<client> &clients, queue<message> &messages, bool &canWrite, bool &running)
{
	return checkSocketsForDisconnect(1, clients, messages, canWrite, running);
}

void handleRecvMessage(SOCKET listeningSocket, queue<message> &messages, bool &canWrite)
{
	Sleep(500);
	char receivingBuffer[BUFFER_LENGTH] = {};
	int recvResult = recv(listeningSocket, receivingBuffer, BUFFER_LENGTH, 0);

	int lastError = WSAGetLastError();

	if (recvResult <= 0) {
		if (lastError != WSAEWOULDBLOCK && lastError != 0) {
			message msg;
			msg.command = -1;
			msg.length = 0;
			msg.msg = "";
			while (!canWrite) {
			}
			canWrite = false;
			messages.push(msg);
			canWrite = true;
		}
		return;
	}
	int msgLength = 0;
	int charactersRead = 0;
	int currentIndex = 0;
	int endValue = currentIndex + 4;
	while (msgLength == 0 || charactersRead < recvResult) {
		for (currentIndex; currentIndex < endValue; currentIndex++) {
			if (receivingBuffer[currentIndex] != 'a') {
				msgLength += ((receivingBuffer[currentIndex] - '0') * (int)pow(10, (3 - currentIndex)));
			}
		}
		int command = 0;
		endValue = currentIndex + 1;
		for (currentIndex; currentIndex < endValue; currentIndex++) {
			command = receivingBuffer[currentIndex] - '0';
		}
		message msg = { "", 0, 0 };
		msg.length = msgLength;
		msg.command = command;
		for (currentIndex; currentIndex < charactersRead + msgLength + 5; currentIndex++) {
			msg.msg += receivingBuffer[currentIndex];
		}
		charactersRead += msgLength + 5;
		endValue = charactersRead + 4;

		while (!canWrite) {
		}
		canWrite = false;
		messages.push(msg);
		canWrite = true;
	}
}

void handleChannelChange(client &currentClient, vector<channel> &channels, int newChannelIndex, bool create)
{
	string msg = string(BUFFER_LENGTH, '\0');
	msg = "";

	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (currentClient.channel->clientNames[i] == currentClient.id) {
			currentClient.channel->clientNames[i] = "";
			i = MAX_CONNECTIONS;
		}
	}
	currentClient.channel->currentClients--;
	if (currentClient.channel->currentClients <= 0 && currentClient.channel->name != "Global") {
		for (int i = 0; i < MAX_CONNECTIONS; i++) {
			currentClient.channel->clientNames[i] = "";
		}
		currentClient.channel->currentClients = 0;
		currentClient.channel->name = "";
	}
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (channels[newChannelIndex].clientNames[i] == "") {
			channels[newChannelIndex].clientNames[i] = currentClient.id;
			i = MAX_CONNECTIONS;
		}
	}
	currentClient.channel = &channels[newChannelIndex];
	currentClient.channel->currentClients++;
	currentClient.channelIndex = newChannelIndex;
	if (create) {
		msg = "Utwozono i polaczono do kanalu: " + channels[newChannelIndex].name;
	} else {
		msg = "Poprawnie polaczono do konalu: " + channels[newChannelIndex].name;
	}
	SOCKET recevicers[1] = { currentClient.socket };
	handleSendMessage(recevicers, 1, msg);
	msg = "================";
	handleSendMessage(recevicers, 1, msg);
}

void fixChannelPointers(vector<client> &clients, vector<channel> &channels)
{
	for (int i = 0; i < clients.size(); i++) {
		if (clients[i].id != "")
			clients[i].channel = &channels[clients[i].channelIndex];
	}
}

SOCKET handleServerInitialization()
{
	addrinfo serverData;
	addrinfo *ptrServerData = &serverData;

	ZeroMemory(&serverData, sizeof(serverData));
	serverData.ai_family = AF_INET;
	serverData.ai_socktype = SOCK_STREAM;
	serverData.ai_protocol = IPPROTO_TCP;
	serverData.ai_flags = AI_PASSIVE;
	getaddrinfo("127.0.0.1", "8001", &serverData, &ptrServerData);

	SOCKET serverSocket = socket(ptrServerData->ai_family, ptrServerData->ai_socktype, ptrServerData->ai_protocol);
	::bind(serverSocket, ptrServerData->ai_addr, (int)ptrServerData->ai_addrlen);
	listen(serverSocket, SOMAXCONN);
	freeaddrinfo(ptrServerData);

	return serverSocket;
}

void cleanUpClient(client &currentClient, vector<client>&clients)
{
	string msg = "Client: " + currentClient.id + " Disconnected";
	cout << msg << endl;

	closesocket(currentClient.socket);
	SOCKET receivers[MAX_CONNECTIONS] = {};
	int numberOfReceivers = 0;
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (currentClient.channel->clientNames[i] == currentClient.id) {
			currentClient.channel->clientNames[i] = "";
			i = MAX_CONNECTIONS;
		}
	}
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (currentClient.id == clients[i].id && clients[i].id != "") {
			closesocket(clients[i].socket);
			clients[i].channel->currentClients--;
			if (clients[i].channel->currentClients <= 0 && clients[i].channel->name != "Global") {
				clients[i].channel->name = "";
				clients[i].channel->currentClients = 0;
			}
			clients[i].socket = INVALID_SOCKET;
			clients[i].id = "";
		} else {
			if (clients[i].socket != INVALID_SOCKET) {
				receivers[numberOfReceivers] = clients[i].socket;
				numberOfReceivers++;
			}
		}
	}
	handleSendMessage(receivers, numberOfReceivers, msg);
}

int handleClientListing(client &currentClient, vector<client>&clients, thread &thread, int &currentConnections, vector<channel> &channels, queue<message> &messages, bool &canWrite, bool &running)
{
	currentClient.channel = &channels[0];
	currentClient.channelIndex = 0;
	string nameArray[MAX_CONNECTIONS];

	while (running) {
		handleRecvMessage(currentClient.socket, messages, canWrite);

		while (messages.size()) {
			SOCKET receivers[MAX_CONNECTIONS] = {};
			int numberOfReceivers = 0;
			string msg = string(BUFFER_LENGTH, '\0');
			msg = "";
			switch (messages.front().command) {
			case (COMMAND_DISCONNECT):
			case (COMMAND_SOCKET_ERROR): {
				cleanUpClient(currentClient, clients);
				currentConnections--;
				running = false;
			} break;
			case (COMMAND_CHANGE_NAME): {
				string newNick = string(messages.front().msg);
				if (newNick != "") {
					bool nickFree = true;
					for (int j = 0; j < MAX_CONNECTIONS; j++) {
						if (newNick == clients[0].id) {
							nickFree = false;
							j = MAX_CONNECTIONS;
						}
					}
					receivers[0] = currentClient.socket;
					numberOfReceivers = 1;
					if (nickFree) {
						int j = 0;
						while (j < MAX_CONNECTIONS - 1 && currentClient.channel->clientNames[j] != currentClient.id) {
							j++;
						}
						currentClient.channel->clientNames[j] = newNick;
						currentClient.id = newNick;
						msg = "Nick zmieniony poprawnie na : " + newNick;
					} else {
						msg = "Nick nie jest dostepny";
					}
				}
			} break;
			case (COMMAND_CREATE_CHANNEL): {
				string channelName = messages.front().msg;
				if (channelName != "") {
					bool channelNameUnique = true;
					int j = 0;
					while (channels.size() > j) {
						if (channels[j].name == channelName) {
							channelNameUnique = false;
							j = channels.size();
						}
						j++;
					}
					if (channelNameUnique) {
						for (int i = 0; i < MAX_CONNECTIONS; i++) {
							nameArray[i] = "";
						}
						channel  newChannel = { channelName, 0, nameArray };
						int channelIndex = 0;
						bool foundEmpty = false;
						for (j = 0; j < channels.size(); j++) {
							if (channels[j].name == "") {
								channels[j].name = channelName;
								newChannel = channels[j];
								channelIndex = j;
								foundEmpty = true;
							}
						}
						if (!foundEmpty) {
							int currentChannelIndex = 0;
							while (channels[currentChannelIndex].name != currentClient.channel->name) {
								currentChannelIndex++;
							}
							channels.push_back(newChannel);
							fixChannelPointers(clients, channels);
							currentClient.channel = &channels[currentChannelIndex];
							channelIndex = channels.size() - 1;
						}
						handleChannelChange(currentClient, channels, channelIndex, 1);
					}
				}
			} break;
			case (COMMAND_LIST_CHANNELS): {
				for (int j = 0; j < channels.size(); j++) {
					if (channels[j].name != "") {
						msg = channels[j].name + ": " + to_string(channels[j].currentClients) + " urzytkownikow";
						receivers[0] = { currentClient.socket };
						handleSendMessage(receivers, 1, msg);
					}
				}
			} break;
			case (COMMAND_JOIN_CHANNEL): {
				string channelToJoin = messages.front().msg;
				if (channelToJoin != "") {
					bool channelChanged = false;
					for (int j = 0; j < channels.size(); j++) {
						if (channels[j].name == channelToJoin) {
							channelChanged = true;
							handleChannelChange(currentClient, channels, j, 0);
						}
					}
					if (!channelChanged) {
						receivers[0] = { currentClient.socket };
						numberOfReceivers = 1;
						msg = "Kanal o podanej nazwie nie istnieje";
					}
				}
			} break;
			case (COMMAND_LIST_USERS_ON_CHANNEL): {
				for (int j = 0; j < currentClient.channel->currentClients; j++) {
					receivers[0] = { currentClient.socket };
					msg = currentClient.channel->clientNames[j];
					handleSendMessage(receivers, 1, msg);
				}
			} break;
			case (COMMAND_LEAVE_CHANNEL): {
				handleChannelChange(currentClient, channels, 0, 0);
			} break;
			case (COMMAND_CHECK_ALIVE): {
			} break;
			default: {
				msg = "[" + currentClient.channel->name + "]" + currentClient.id + ": " + messages.front().msg;
				cout << msg.c_str() << endl;

				for (int j = 0; j < MAX_CONNECTIONS; j++) {
					if (clients[j].socket != INVALID_SOCKET) {
						if (currentClient.id != clients[j].id && currentClient.channel->name == clients[j].channel->name) {
							receivers[numberOfReceivers] = clients[j].socket;
							numberOfReceivers++;
						}
					}
				}
			} break;
			}
			messages.pop();
			handleSendMessage(receivers, numberOfReceivers, msg);
		}
	}
	return 0;
}

void runServer(SOCKET listenSocket)
{
	int currentConnections = 0;
	int unsigned newConnectionFreeIndex = 0;
	vector<client> clients(MAX_CONNECTIONS);
	vector<channel> channels;
	string nameArray[MAX_CONNECTIONS] = { string(BUFFER_LENGTH, '\0') };
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		nameArray[i] = "";
	}
	channel globalChannel = { string(BUFFER_LENGTH, '\0'), 0, nameArray };
	globalChannel.name = "Global";
	channels.push_back(globalChannel);

	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		clients[i] = { string(BUFFER_LENGTH, '\0'), &channels[0], INVALID_SOCKET };
		clients[i].id = "";
	}

	thread workerThreads[MAX_CONNECTIONS];
	bool running = true;
	bool canWrite = true;
	thread readServerCommandThread = thread(readServerCommand, ref(running));
	queue<message> messages;
	thread checkIfClientsAreAlive = thread(checkClientsForDisconnect, ref(clients) ,ref(messages), ref(canWrite), ref(running));

	while (running) {
		string msg = string(BUFFER_LENGTH, '\0');
		msg = "";
		Sleep(500);
		SOCKET newConnectionSocket = accept(listenSocket, NULL, NULL);
		if (newConnectionSocket != INVALID_SOCKET) {
			u_long iMode = 1;
			ioctlsocket(newConnectionSocket, FIONBIO, &iMode);
			SOCKET receivers[1] = { newConnectionSocket };
			if (currentConnections < MAX_CONNECTIONS) {
				int firstEmpty = 0;
				while (clients[firstEmpty].socket != INVALID_SOCKET) {
					firstEmpty++;
				}
				currentConnections++;
				clients[firstEmpty].id = "Anon#" + to_string(newConnectionFreeIndex);
				cout << "Client: " + clients[firstEmpty].id + " Connected" << endl;
				clients[firstEmpty].socket = newConnectionSocket;
				msg = "NICK " + clients[firstEmpty].id;
				channels[0].currentClients++;
				int i = 0;
				while (channels[0].clientNames[i] != "" && i < MAX_CONNECTIONS) {
					i++;
				}
				channels[0].clientNames[i] = clients[firstEmpty].id;

				workerThreads[firstEmpty] = thread(handleClientListing, ref(clients[firstEmpty]), ref(clients), ref(workerThreads[firstEmpty]), ref(currentConnections), ref(channels), ref(messages), ref(canWrite), ref(running));
				newConnectionFreeIndex++;
			} else {
				msg = "Serwer jest pelny";
			}
			handleSendMessage(receivers, 1, msg);
		}
	}
	readServerCommandThread.join();
	checkIfClientsAreAlive.join();
	closesocket(listenSocket);
	for (int i = 0; i < MAX_CONNECTIONS; i++)
	{
		if (workerThreads[i].joinable()) {
			workerThreads[i].join();
		}
		if (clients[i].socket != INVALID_SOCKET) {
			closesocket(clients[i].socket);
		}
	}
	WSACleanup();
}

int initializeAndRunServer() 
{
	SOCKET listenSocket = handleServerInitialization();
	u_long iMode = 1;
	ioctlsocket(listenSocket, FIONBIO, &iMode);
	if (listenSocket != INVALID_SOCKET) {
		runServer(listenSocket);
	}

	cout << WSAGetLastError() << endl;

	return 0;
}

client handleClientInitialization()
{
	client user;
	addrinfo serverData;
	addrinfo *ptrServerData = &serverData;

	ZeroMemory(&serverData, sizeof(serverData));
	serverData.ai_family = AF_INET;
	serverData.ai_socktype = SOCK_STREAM;
	serverData.ai_protocol = IPPROTO_TCP;
	getaddrinfo("127.0.0.1", "8001", &serverData, &ptrServerData);
	string nameArray[MAX_CONNECTIONS] = { string(BUFFER_LENGTH, '\0') };
	channel defChannel = { string(BUFFER_LENGTH, '\0'), 0, nameArray };
	defChannel.name = "Global";

	user = { string(BUFFER_LENGTH, '\0'), &defChannel, INVALID_SOCKET };
	user.id = "";
	addrinfo* ptr;

	for (ptr = ptrServerData; ptr != NULL && (user.socket == INVALID_SOCKET); ptr = ptr->ai_next) {
		user.socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		u_long iMode = 1;
		ioctlsocket(user.socket, FIONBIO, &iMode);

		int connectResult = connect(user.socket, ptr->ai_addr, (int)ptr->ai_addrlen);
		int lastError = WSAGetLastError();

		if (connectResult == SOCKET_ERROR && lastError != WSAEWOULDBLOCK) {
			closesocket(user.socket);
			user.socket = INVALID_SOCKET;
		} else {
			fd_set socketsToCheck;
			socketsToCheck.fd_array[0] = user.socket;
			socketsToCheck.fd_count = 1;
			timeval time;
			time.tv_sec = 1;
			time.tv_usec = 0;
			int socketCheckResult = select(0, NULL, &socketsToCheck, NULL, &time);
			if (socketCheckResult == 0) {
				closesocket(user.socket);
				user.socket = INVALID_SOCKET;
			}
		}
	}
	freeaddrinfo(ptrServerData);

	return user;
}

int listenToServer(client &user, bool &running, bool &canWrite)
{
	queue<message> messages;
	
	while (running) {
		handleRecvMessage(user.socket, messages , canWrite);
		while (messages.size()) {
			switch (messages.front().command) {
			case (COMMAND_SOCKET_ERROR):
			case (COMMAND_DISCONNECT):
			{
				running = false;
			} break;
			case (COMMAND_CHANGE_NAME):
			{
				user.id = messages.front().msg;
			} break;
			case (COMMAND_CREATE_CHANNEL):
			{
				user.channel->name = messages.front().msg;
			} break;
			case (COMMAND_CHECK_ALIVE):
			{
			} break;
			default: 
			{
				cout << messages.front().msg << endl;
			} break;
			}
			messages.pop();
		}
	}
	return 0;
}

int initizalizeAndRunClient()
{
	client newUser = handleClientInitialization();
	bool running = true;
	bool canWrite = true;
	if (newUser.socket != INVALID_SOCKET) {
		cout << "Polaczono poprawnie do socketa" << endl;
		queue<message> messages;
		handleRecvMessage(newUser.socket, messages, canWrite);

		while (messages.size()) {
			switch (messages.front().command) {
			case (COMMAND_SOCKET_ERROR):
			{
				if (running) {
					cout << "Polaczenie nieudane" << endl;
					WSACleanup();
					return 1;
				}
			} break;
			case (COMMAND_CHANGE_NAME):
			{
				newUser.id = "";
				for (int i = 5; i < messages.front().msg.length(); i++) {
					newUser.id += messages.front().msg[i];
				}
				thread listeningThread = thread(listenToServer, ref(newUser), ref(running), ref(canWrite));
				vector<client> server;
				server.push_back(newUser);
				thread checkForServerDisconnect = thread(checkServerForDisconnect, ref(server), ref(messages), ref(canWrite), ref(running));
				while (running) {
					string msg = string(BUFFER_LENGTH, '\0');
					msg = "";
					SOCKET receivers[1] = { newUser.socket };
					getline(cin, msg);
					if (msg != "" && running) {
						running = handleSendMessage(receivers, 1, msg);
					}
				}
				listeningThread.join();
				checkForServerDisconnect.join();
			} break;
			default:
			{
				cout << messages.front().msg << endl;
			}break;
			}
			messages.pop();
		}
	}
	cout << "Rozlaczanie socketa" << endl;
	closesocket(newUser.socket);
	WSACleanup();
	return 0;
}


int main() {
	WSADATA wsaData;

	int initializationResult;
	initializationResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (initializationResult != 0) {
		printf("Inicjalizacja winsocket nie powiodla sie: %d\n", initializationResult);
		return 1;
	}

	string mode = "";

	cout<<"Aby uruchomic aplikacje w trybie sewera prosze wpisac \"Serv\", natomiast aby uruchomic w trybie klienta jaka klowiek inna kombinacje znakow"<<endl;
	getline(cin, mode);

	if (mode == "Serv") {
		initializeAndRunServer();
	} else {
		initizalizeAndRunClient();
	}

	system("pause");
	return 0;
}