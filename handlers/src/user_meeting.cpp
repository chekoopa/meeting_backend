#include "Poco/Data/RecordSet.h"
#include "Poco/Data/Session.h"
#include "Poco/Data/TypeHandler.h"
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/NumberParser.h>
#include <Poco/RegularExpression.h>
#include <handlers.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>

namespace handlers {

extern Poco::RegularExpression regexp_user_meeting_id;

using nlohmann::json;

struct Meeting {
	std::optional<int> id;
	std::string name;
	std::string description;
	std::string address;
	/*
	std::string signup_description;
	int signup_from_date;
	int signup_to_date;
	int from_date;
	int to_date;
	*/
	bool published;
};

void to_json(json &j, const Meeting &m) {
	j = json{
	    {"id", m.id.value()},
	    {"name", m.name},
	    {"description", m.description},
	    {"address", m.address},
	    /*
		{"signup_description", m.signup_description},
	    {"signup_from_date", m.signup_from_date},
	    {"signup_to_date", m.signup_to_date},
	    {"from_date", m.from_date},
	    {"to_date", m.to_date},
		*/
	    {"published", m.published}};
}

void from_json(const json &j, Meeting &m) {
	j.at("name").get_to(m.name);
	j.at("description").get_to(m.description);
	j.at("address").get_to(m.address);
	/*
	j.at("signup_description").get_to(m.signup_description);
	j.at("signup_from_date").get_to(m.signup_from_date);
	j.at("signup_to_date").get_to(m.signup_to_date);
	j.at("from_date").get_to(m.from_date);
	j.at("to_date").get_to(m.to_date);
	*/
	j.at("published").get_to(m.published);
}

class Storage {
public:
	using MeetingList = std::vector<Meeting>;
	virtual void Save(Meeting &meeting) = 0;
	virtual MeetingList GetList() = 0;
	virtual std::optional<Meeting> Get(int id) = 0;
	virtual bool Delete(int id) = 0;
	virtual ~Storage() {}
};

class MapStorage : public Storage {
public:
	void Save(Meeting &meeting) override {
		if (meeting.id.has_value()) {
			m_meetings[meeting.id.value()] = meeting;
		} else {
			int id = m_meetings.size();
			meeting.id = id;
			m_meetings[id] = meeting;
		}
	}
	Storage::MeetingList GetList() override {
		Storage::MeetingList list;
		for (auto [id, meeting] : m_meetings) {
			list.push_back(meeting);
		}
		return list;
	}
	std::optional<Meeting> Get(int id) override {
		if (MeetingInMap(id)) {
			return m_meetings[id];
		}
		return std::optional<Meeting>();
	}
	bool Delete(int id) override {
		if (MeetingInMap(id)) {
			m_meetings.erase(id);
			return true;
		}
		return false;
	}

private:
	using MeetingMap = std::map<int, Meeting>;
	MeetingMap m_meetings;

	bool MeetingInMap(int id) const {
		auto meeting_ptr = m_meetings.find(id);
		return meeting_ptr != m_meetings.end();
	}
};

using namespace Poco::Data::Keywords;
using Poco::Data::RecordSet;
using Poco::Data::Session;
using Poco::Data::Statement;

class SqliteStorage : public Storage {
public:
	SqliteStorage(const std::string &path) : m_session("SQLite", path) {}

	void Save(Meeting &meeting) override {
		// std::cerr << "Save() start" << std::endl;
		if (meeting.id.has_value()) {
			// std::cerr << "Save() update" << std::endl;
			m_session << R"(UPDATE meeting 
				SET name = :name, description = :description, 
					address = :address, published = :published
				WHERE id = :id
				)",
				use(meeting.name), use(meeting.description),
				use(meeting.address), use(meeting.published),
				use(meeting.id.value()),
				now;
		} else {
			// std::cerr << "Save() insert" << std::endl;
			m_session << R"(INSERT INTO meeting 
				(name, description, address, published)
				VALUES (:name, :description, :address, :published)
				)",
				use(meeting.name), use(meeting.description),
				use(meeting.address), use(meeting.published),
				now;
		}
	}
	Storage::MeetingList GetList() override {
		// std::cerr << "GetList() start" << std::endl;
		Storage::MeetingList list;
		Statement stmt = (m_session << "SELECT id, name, description, address, published FROM meeting;",
							limit(50));
		while (!stmt.done()) {
			stmt.execute();
		}
		RecordSet rs(stmt);

		Meeting meeting;
		// std::cerr << "GetList() got " << rs.rowCount() << std::endl;
		for (RecordSet::Iterator it = rs.begin(); it != rs.end(); ++it) {
			// std::cout << (*it) << " ";
			// std::cerr << (*it)["id"].convert<int>() << std::endl;
			meeting.id = (*it)["id"].convert<int>();
			meeting.name = (*it)["name"].convert<std::string>();
			meeting.description = (*it)["description"].convert<std::string>();
			meeting.address = (*it)["address"].convert<std::string>();
			meeting.published = (*it)["published"].convert<bool>();
			list.push_back(meeting);
		}
		return list;
	}
	std::optional<Meeting> Get(int id) override {
		// std::cerr << "Get() start" << std::endl;
		Meeting meeting;
		Poco::Nullable<int> get_id = 0;
		Statement stmt = (m_session << "SELECT id, name, description, address, published FROM meeting",
							into(get_id),
							into(meeting.name), into(meeting.description),
							into(meeting.address), into(meeting.published),
							limit(1));
		stmt.execute();
		if (meeting.id.has_value()) {
			meeting.id = get_id.value();
			return std::optional<Meeting>(meeting);
		}
		return std::optional<Meeting>();
	
	}
	bool Delete(int id) override {
		// std::cerr << "Delete() start" << std::endl;
		if (Get(id).has_value()) {
			m_session << "DELETE FROM meeting WHERE id = :id", use(id), now;
		}
		return false;
	}

private:
	Session m_session;
};

Storage &GetStorage() {
	static SqliteStorage storage("sample.db");
	return storage;
}

void UserMeetingList::HandleRestRequest(Poco::Net::HTTPServerRequest &request, Poco::Net::HTTPServerResponse &response) {
	auto &storage = GetStorage();

	nlohmann::json result = nlohmann::json::array();
	for (auto meeting : storage.GetList()) {
		result.push_back(meeting);
	}

	response.setStatus(Poco::Net::HTTPServerResponse::HTTP_OK);
	response.send() << result;
}

void UserMeetingCreate::HandleRestRequest(Poco::Net::HTTPServerRequest &request, Poco::Net::HTTPServerResponse &response) {
	nlohmann::json j = nlohmann::json::parse(request.stream());
	if (j.is_discarded()) {
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_BAD_REQUEST);
		response.send() << "Bad meeting JSON";
	}

	auto &storage = GetStorage();

	Meeting meeting;
	try {
		meeting = j;
	} catch (json::exception) {
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_BAD_REQUEST);
		response.send() << "Bad meeting parameters";
		return;
	}

	try {
		storage.Save(meeting);
	} catch (Poco::Exception &exc) {
		std::cerr << exc.displayText() << std::endl;
		std::cerr.flush();
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_BAD_REQUEST);
	}

	response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_CREATED);
	response.send() << json(meeting);
}

void UserMeetingRead::HandleRestRequest(Poco::Net::HTTPServerRequest &request, Poco::Net::HTTPServerResponse &response) {
	//response.setContentType("application/json");
	auto &meetings = GetStorage();
	auto meeting = meetings.Get(m_id);
	if (meeting.has_value()) {
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_OK);
		response.send() << json(meeting.value());
	}

	response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_NOT_FOUND);
	response.send();
}

void UserMeetingUpdate::HandleRestRequest(Poco::Net::HTTPServerRequest &request, Poco::Net::HTTPServerResponse &response) {
	response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_OK);
	auto body = nlohmann::json::parse(request.stream());
	auto &meetings = GetStorage();
	Meeting meeting = body;
	meeting.id = m_id;

	try {
		meetings.Save(meeting);
	} catch (Poco::Exception &exc) {
		std::cerr << exc.displayText() << std::endl;
		std::cerr.flush();
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_BAD_REQUEST);
	}
	response.send() << json(meeting);
}

void UserMeetingDelete::HandleRestRequest(Poco::Net::HTTPServerRequest &request, Poco::Net::HTTPServerResponse &response) {
	auto &meetings = GetStorage();
	if (meetings.Delete(m_id)) {
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_NO_CONTENT);
	} else {
		response.setStatusAndReason(Poco::Net::HTTPServerResponse::HTTP_NOT_FOUND);
	}
	response.send();
}

} // namespace handlers
