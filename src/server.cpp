#include "Poco/Data/Session.h"
#include "Poco/Data/SQLite/Connector.h"
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/ServerSocketImpl.h>
#include <handlers/factory.hpp>
#include <iostream>
#include <vector>
#include <server.hpp>

namespace {

using namespace Poco::Data::Keywords;
using Poco::Data::Session;
using Poco::Data::Statement;

using Poco::Net::Socket;

class ServerSocketImpl : public Poco::Net::ServerSocketImpl {
public:
	using Poco::Net::SocketImpl::init;
};

class ServerSocket : public Socket {
public:
	ServerSocket(const std::string &address, const int port) : Poco::Net::Socket(new ServerSocketImpl()) {
		const Poco::Net::SocketAddress socket_address(address + ":" + std::to_string(port));
		ServerSocketImpl *socket = static_cast<ServerSocketImpl *>(impl());
		socket->init(socket_address.af());
		socket->setReuseAddress(true);
		socket->setReusePort(false);
		socket->bind(socket_address, false);
		socket->listen();
	}
};

} // anonymous namespace

int Server::main(const std::vector<std::string> & args) {
	auto *parameters = new Poco::Net::HTTPServerParams();
	parameters->setTimeout(10000);
	parameters->setMaxQueued(100);
	parameters->setMaxThreads(1);

	const Poco::Net::ServerSocket socket(ServerSocket("127.0.0.1", 8080));

	Poco::Net::HTTPServer server(new handlers::Factory(), socket, parameters);

	Poco::Data::SQLite::Connector::registerConnector();
	Session session("SQLite", "sample.db");
	session << "DROP TABLE IF EXISTS meeting;", now;
	session << "CREATE TABLE meeting ("
		<< "id INTEGER PRIMARY KEY AUTOINCREMENT,"
		<< "name TEXT UNIQUE NOT NULL," 
		<< "description TEXT NOT NULL, " 
		<< "address TEXT NOT NULL, " 
		<< "published INTEGER NOT NULL);", now;

	server.start();
	waitForTerminationRequest();
	server.stopAll();

	return 0;
}
