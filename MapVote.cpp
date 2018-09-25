// MapVote.cpp : Defines the entry point for the DLL application.
//

#include "bzfsAPI.h"
#include "plugin_utils.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>

using std::map;
using std::set;
using std::vector;
using std::string;
using std::ifstream;

const string MAPVOTE_NOMAP = "NOMAP";
const double RESTART_WAIT_TIME = 5.0; // Seconds

class MapVoteHandler: public bz_EventHandler
{
private:
	// List of maps
	map<string, string> mapList;
	
	// Tally of votes
	bool votingOpen;
	map<string, int> voteTally;
	set<int> playersVoted;
	
	// How long to wait before restarting server
	double restartTime;
	
	// Map to load when server restarts
	string nextMap;
	
	// Slash-command handling
	void handleCommand(bz_UnknownSlashCommandEventData* event);
	
	// Map changing/loading
	void loadMap(bz_GenerateWorldEventData* event);
	
	// Game over handling
	void gameEnded();
	
	// Command actions
	void startMapVote(string slashCommand, int requestorID);
	void endMapVote(int requestorID);
	void cancelMapVote(int requestorID);
	void changeMap(string slashCommand, int requestorID);
	void printMapList(int playerID) const;
	void printVotes(int playerID) const;
	void voteMap(string slashCommand, int voterID);
	
	// Utility
	bool verifyAdminOp(int playerID) const;
	bool verifyNotObserver(int playerID) const;
	bool verifyVotingOpen(int playerID) const;
	void restartTimeReached();
	void resetVoting();
	
public:
	// Constructor
	MapVoteHandler();
	
	// Maplist loading
	bool loadMaplist(const char* path);
	
	// Main event handling function
	virtual void process(bz_EventData* event);
};

string getCallsign(int playerID)
{
	// Get the player record
	bz_PlayerRecord* player = bz_getPlayerByIndex(playerID);
	
	// Retrieve the callsign
	string callsign(player->callsign.c_str());
	
	// Free the player record
	bz_freePlayerRecord(player);
	
	// Return the callsign
	return callsign;
}

MapVoteHandler::MapVoteHandler():
	mapList(), votingOpen(false), voteTally(), playersVoted(),
	restartTime(0.0), nextMap(MAPVOTE_NOMAP)
{
	// Seed RNG
	srandom(time(NULL));
}

bool MapVoteHandler::loadMaplist(const char* path)
{
	// Attempt to open the maplist file
	ifstream maplistFile(path);
	if (!maplistFile)
		return false;
	
	string mapPath, mapName;
	unsigned int index;
	while (maplistFile >> mapPath)
	{
		// Try to find the last directory separator in the path
		// try POSIX-style first
		index = mapPath.find_last_of("/");
		if (index == string::npos)
		{
			// Fall back on Window-style
			index = mapPath.find_last_of("\\");
			if (index == string::npos)
			{
				// Give up..
				maplistFile.close();
				return false;
			}
		}
		
		// Get the map name by truncating everything before the last
		// directory separator in the path
		mapName = mapPath.substr(index + 1);
		
		// Clip the file extension
		index = mapName.find_last_of(".");
		mapName = mapName.erase(index);
		
		// Add the map (and it's path) to the map list
		mapList[mapName] = mapPath;
	}
	
	return true;
}

const string VOTEMAP_COMMAND =		"/votemap";
const string LISTVOTES_COMMAND = 	"/listvotes";
const string LISTMAPS_COMMAND =		"/listmaps";
const string STARTVOTE_COMMAND =	"/startmapvote";
const string ENDVOTE_COMMAND =		"/endmapvote";
const string CANCELVOTE_COMMAND =	"/cancelmapvote";
const string CHANGEMAP_COMMAND =	"/changemap";

void MapVoteHandler::handleCommand(bz_UnknownSlashCommandEventData* event)
{
	// Retrieve the command as typed from the event data
	string command(event->message.c_str());
	
	// Attempt to determine the exact command
	if (command.substr(0, VOTEMAP_COMMAND.size()) == VOTEMAP_COMMAND)
	{
		// Vote for a map
		this->voteMap(command, event->from);
		event->handled = true;
	}
	else if (command == LISTVOTES_COMMAND)
	{
		// Print the vote tally
		this->printVotes(event->from);
		event->handled = true;
	}
	else if (command == LISTMAPS_COMMAND)
	{
		// Print the map list
		this->printMapList(event->from);
		event->handled = true;
	}
	else if (command.substr(0, STARTVOTE_COMMAND.size()) == STARTVOTE_COMMAND)
	{
		// Initiate voting
		this->startMapVote(command, event->from);
		event->handled = true;
	}
	else if (command.substr(0, ENDVOTE_COMMAND.size()) == ENDVOTE_COMMAND)
	{
		// Finalize voting and switch maps
		this->endMapVote(event->from);
		event->handled = true;
	}
	else if (command.substr(0, CANCELVOTE_COMMAND.size()) == CANCELVOTE_COMMAND)
	{
		// Cancel voting
		this->cancelMapVote(event->from);
		event->handled = true;
	}
	else if (command.substr(0, CHANGEMAP_COMMAND.size()) == CHANGEMAP_COMMAND)
	{
		// Change maps manually
		this->changeMap(command, event->from);
		event->handled = true;
	}
}

void MapVoteHandler::loadMap(bz_GenerateWorldEventData* event)
{
	// Check that we have map specified
	// (the server will use a randomly-generated map if not)
	if (nextMap != MAPVOTE_NOMAP)
	{
		// Convert nextMap to bzApiString
		bzApiString path(nextMap.c_str());
		
		// Set the value in the event
		event->worldFile = path;
	}
	
	// Reset map name
	nextMap = MAPVOTE_NOMAP;
}

void MapVoteHandler::gameEnded()
{
	// Have the server start a new map poll
	this->startMapVote("", BZ_SERVER);
}

void MapVoteHandler::startMapVote(string command, int requestor)
{
	// Check that the player is an admin or op (or the server)
	if ((requestor != BZ_SERVER) && !this->verifyAdminOp(requestor))
		return;
	
	// If there is already a poll open...
	if (votingOpen)
	{
		// If the server is making the request, leave the existing poll alone
		if (requestor == BZ_SERVER)
			return;
		// Otherwise, restart the vote
		else
			this->resetVoting();
	}
	
	string message;
	
	// Look for spaces (to indicate a list of maps)
	unsigned int index = command.find_first_of(" ");
	
	// If no maps are listed, create a poll with all available maps
	if (index == string::npos)
	{	
		// Add each map name to the poll
		map<string,string>::iterator iter;
		for (iter = mapList.begin(); iter != mapList.end(); iter++)
		{
			voteTally[iter->first] = 0;
		}
		
		// Open the polls
		votingOpen = true;
		
		// Inform players of the vote
		if (requestor != BZ_SERVER)
			message = "MapVote: " + getCallsign(requestor) + " initiated a new map poll with all maps.";
		else
			message = "MapVote: initiating new map poll with all maps.";
		bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, message.c_str());
		
		return;
	}
	
	// Iterate through the list of maps
	unsigned int nextIndex;
	string mapName;
	while (index != string::npos)
	{
		index++;
		
		// Find the next space in the command
		nextIndex = command.find_first_of(" ", index);
		
		// Get the mapname
		if (nextIndex != string::npos)
			mapName = command.substr(index, (nextIndex - index));
		else
			mapName = command.substr(index);
		
		
		
		// Check that the map is in the map list
		if (mapList.find(mapName) == mapList.end())
		{
			message = "MapVote Error: Could not find map named \'" + mapName + "\'";
			bz_sendTextMessage(BZ_SERVER, requestor, message.c_str());
		}
		else
		{
			// Add the map to the poll
			voteTally[mapName] = 0;
		}
		
		// Advance to the map name
		index = nextIndex;
	}
	
	// Before we open voting, check that there are valid maps
	if (voteTally.size() > 1)
	{
		// Open voting
		votingOpen = true;
		
		// Inform players of the vote
		message = "MapVote: " + getCallsign(requestor) + " initiated a new map vote with the following maps:";
		bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, message.c_str());
		
		// List the maps in the poll
		map<string,int>::iterator iter;
		for (iter = voteTally.begin(); iter != voteTally.end(); iter++)
		{
			bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, iter->first.c_str());
		}
	}
	else
	{
		// Inform the admin that the poll is invalid
		bz_sendTextMessage(BZ_SERVER, requestor, "MapVote Error: Polls must contain at least two valid maps.");
		bz_sendTextMessage(BZ_SERVER, requestor, "               Use /listmaps for a list of valid maps.");
		
		// Clear any voting data
		this->resetVoting();
	}
}

void MapVoteHandler::endMapVote(int requestor)
{
	// If the vote is being ended by the server, skip these checks:
	if (requestor != BZ_SERVER)
	{
		// Check that the player is an admin or op
		if (!this->verifyAdminOp(requestor))
			return;
	
		// Check that there is a vote open
		if (!this->verifyVotingOpen(requestor))
			return;
	}
	
	// Tally the votes
	int maxVotes = -1;
	vector<string> topMaps;
	map<string,int>::iterator iter;
	for (iter = voteTally.begin(); iter != voteTally.end(); iter++)
	{
		// Check: is this the highest vote count?
		if (iter->second > maxVotes)
		{
			maxVotes = iter->second;
			topMaps.clear();
			topMaps.push_back(iter->first);
		}
		// Check: is this tied for highest?
		else if (iter->second == maxVotes)
		{
			topMaps.push_back(iter->first);
		}
	}
	
	// Get the map name (if there are more than one tied for top, choose at random)
	string mapName = topMaps[random() % topMaps.size()];
	
	// Inform players of map switch
	string message;
	if (requestor != BZ_SERVER)
	{
		message = "MapVote: " + getCallsign(requestor) + " ended the map voting.";
		bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, message.c_str());
	}
	message = "MapVote: " + mapName + " has won the map vote! Server will restart in %d seconds.";
	bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, message.c_str(), int(RESTART_WAIT_TIME));
	
	// Translate the map name into a path
	nextMap = mapList[mapName];
	
	// Reset voting state
	this->resetVoting();
	
	// Set the timer to restart the server
	restartTime = bz_getCurrentTime() + RESTART_WAIT_TIME;
	
	// Register with the server to recieve tick events (oh god, don't look)
	bz_registerEvent(bz_eTickEvent, this);
}

void MapVoteHandler::cancelMapVote(int requestor)
{
	// Check that the player is an admin or op
	if (!this->verifyAdminOp(requestor))
		return;
	
	// Check that there is a vote open
	if (!this->verifyVotingOpen(requestor))
		return;
	
	// Reset the poll
	this->resetVoting();
	
	// Inform players
	string message = "MapVote: " + getCallsign(requestor) + " cancelled the map vote.";
	bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, message.c_str());
}

void MapVoteHandler::changeMap(string command, int requestor)
{
	// Check that the player requesting the change is an admin or op
	if (!this->verifyAdminOp(requestor))
		return;
	
	// Look for the space between the command name and the map name
	unsigned int index = command.find_first_of(" ");
	if (index == string::npos)
	{
		// If there is no space in the command name, print an error
		bz_sendTextMessage(BZ_SERVER, requestor, "MapVote Error: You must specify a map to switch to!");
		bz_sendTextMessage(BZ_SERVER, requestor, "               Use /listmaps for a list of maps.");
		return;
	}
	
	// Retrieve the map name from the command
	string mapName = command.substr(index + 1);
	
	// Determine whether the requested map is in the map list
	string message;
	map<string,string>::iterator iter = mapList.find(mapName);
	if (iter == mapList.end())
	{
		// If not found, print an error message
		message = "MapVote Error: Could not find map named \'" + mapName + "\'.";
		bz_sendTextMessage(BZ_SERVER, requestor, message.c_str());
		bz_sendTextMessage(BZ_SERVER, requestor, "               Use /listmaps for a list of maps.");
		return;
	}
	
	// Otherwise, set the map to load
	nextMap = iter->second;
	
	// Inform players of map change
	message = "MapVote: " + getCallsign(requestor) + " has changed the map to " + mapName + "!";
	bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, message.c_str());
	bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "         Server will restart in %d seconds.", int(RESTART_WAIT_TIME));
	
	// Reset voting state
	this->resetVoting();
	
	// Set the timer to restart the server
	restartTime = bz_getCurrentTime() + RESTART_WAIT_TIME;
	
	// Register with the server to recieve tick events (oh god, don't look)
	bz_registerEvent(bz_eTickEvent, this);
}

void MapVoteHandler::printMapList(int playerID) const
{
	bz_sendTextMessage(BZ_SERVER, playerID, "MapVote Map Rotation:");
	
	// Print each map name in the list of maps
	map<string,string>::const_iterator iter;
	string message;
	for (iter = mapList.begin(); iter != mapList.end(); iter++)
	{
		message = "  " + iter->first;
		bz_sendTextMessage(BZ_SERVER, playerID, message.c_str());
	}
}

void MapVoteHandler::printVotes(int playerID) const
{
	// Check that there is a poll open
	if (!this->verifyVotingOpen(playerID))
		return;
	
	bz_sendTextMessage(BZ_SERVER, playerID, "MapVote: Current Voting Status:");
	
	// FIXME: sort by number of votes!
	
	map<string,int>::const_iterator iter;
	string message;
	for (iter = voteTally.begin(); iter != voteTally.end(); iter++)
	{
		// Print the map, and the number of votes it currently has
		message = "%d";
		if (iter->second == 1)
			message += " vote  - ";
		else
			message += " votes - ";
		message += iter->first;
		bz_sendTextMessagef(BZ_SERVER, playerID, message.c_str(), iter->second);
	}
}

void MapVoteHandler::voteMap(string command, int voterID)
{
	// Check that the player is not an observer
	if (!this->verifyNotObserver(voterID))
		return;
	
	// Check that there is a poll open
	if (!this->verifyVotingOpen(voterID))
		return;
	
	// Check that the player has not already voted
	if (playersVoted.find(voterID) != playersVoted.end())
	{
		bz_sendTextMessage(BZ_SERVER, voterID, "MapVote Error: You may only vote once!");
		return;
	}
	
	// Find the first space in the command
	unsigned int index = command.find_first_of(" ");
	
	// Check that there is a space present
	if (index == string::npos)
	{
		bz_sendTextMessage(BZ_SERVER, voterID, "MapVote Error: You must specify a map to vote for!");
		bz_sendTextMessage(BZ_SERVER, voterID, "               Use /listvotes for a list of options.");
		return;
	}
	
	// Get the name of the map
	string mapName = command.substr(index + 1);
	string message;
	
	// Check that the map is in the poll
	map<string,int>::iterator iter = voteTally.find(mapName);
	if (iter == voteTally.end())
	{
		message = "MapVote Error: Could not find map named \'" + mapName + "\'";
		bz_sendTextMessage(BZ_SERVER, voterID, message.c_str());
		bz_sendTextMessage(BZ_SERVER, voterID, "               Use /listvotes for a list of options.");
		return;
	}
	
	// Place the vote
	iter->second++;
	
	// Add the player to the list of players who have voted
	playersVoted.insert(voterID);
	
	// Inform players that the vote has been placed
	message = "MapVote: " + getCallsign(voterID) + " has voted for " + mapName + "!";
	bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, message.c_str());
	
	// Determine the number of non-observer players in the game
	bzAPIIntList* playerList = bz_newIntList();
	bz_getPlayerIndexList(playerList);
	int numPlayers = playerList->size() - bz_getTeamCount(eObservers);
	bz_deleteIntList(playerList);
	
	// Check if all players have voted (or if the current map has more than half the total votes)
	if ((int(playersVoted.size()) == numPlayers) || (iter->second > (numPlayers / 2)))
	{
		this->endMapVote(BZ_SERVER);
		return;
	}
	
	// Print the voting status to the voter
	this->printVotes(voterID);
}

bool MapVoteHandler::verifyAdminOp(int playerID) const
{
	// Get the player record
	bz_PlayerRecord* player = bz_getPlayerByIndex(playerID);
	
	// Determine admin/op status
	bool isAdminOrOp = (player->admin || player->op);
	
	// Free player record
	bz_freePlayerRecord(player);
	
	// If the player is not an admin or operator, print an error message
	if (!isAdminOrOp)
		bz_sendTextMessage(BZ_SERVER, playerID, "You must be an administrator or operator to use that command.");
	
	// Return player's admin/op status
	return isAdminOrOp;
}

bool MapVoteHandler::verifyNotObserver(int playerID) const
{
	// Get the player record
	bz_PlayerRecord* player = bz_getPlayerByIndex(playerID);
	
	// Determine observer status
	bool isObserver = (player->team == eObservers);
	
	// Free player record
	bz_freePlayerRecord(player);
	
	// If the player is an observer, print an error message
	if (isObserver)
		bz_sendTextMessage(BZ_SERVER, playerID, "MapVote Error: Sorry, observers may not vote.");
	
	// Return observer status
	return !isObserver;
}

bool MapVoteHandler::verifyVotingOpen(int playerID) const
{
	// If the vote is not open, print an error message to the player who issued the command
	if (!votingOpen)
		bz_sendTextMessage(BZ_SERVER, playerID, "MapVote Error: There is no map vote active.");
	
	return votingOpen;
}

void MapVoteHandler::restartTimeReached()
{
	// De-register for tick events
	bz_removeEvent(bz_eTickEvent, this);
	
	// Restart the server
	bz_restart();
}

void MapVoteHandler::resetVoting()
{
	// Close voting
	votingOpen = false;
	
	// Clear the tally of votes
	voteTally.clear();
	
	// Clear the list of voters
	playersVoted.clear();
}

/* ---------------------- BZFlag API Calls Begin Here ----------------------- */

// Global singleton MapVoteHandler object
MapVoteHandler voteHandler;

BZ_GET_PLUGIN_VERSION

BZF_PLUGIN_CALL int bz_Load(const char* arguments)
{
	// Attempt to load the maplist
	if (!voteHandler.loadMaplist(arguments))
	{
		bz_debugMessage(0, "MapVote Error: could not load maplist!");
		return 1;
	}
	
	bz_registerEvent(bz_eUnknownSlashCommand, &voteHandler);
	bz_registerEvent(bz_eGetWorldEvent, &voteHandler);
	bz_registerEvent(bz_eGameEndEvent, &voteHandler);
	
	bz_debugMessage(4,"MapVote plugin loaded");
	return 0;
}

BZF_PLUGIN_CALL int bz_Unload(void)
{
	bz_removeEvent(bz_eUnknownSlashCommand, &voteHandler);
	bz_removeEvent(bz_eGetWorldEvent, &voteHandler);
	bz_removeEvent(bz_eGameEndEvent, &voteHandler);
	
	bz_debugMessage(4,"MapVote plugin unloaded");
	return 0;
}

void MapVoteHandler::process(bz_EventData* event)
{
	switch (event->eventType)
	{
		case bz_eUnknownSlashCommand:
			this->handleCommand((bz_UnknownSlashCommandEventData*)event);
			break;
		case bz_eGetWorldEvent:
			this->loadMap((bz_GenerateWorldEventData*)event);
			break;
		case bz_eGameEndEvent:
			this->gameEnded();
			break;
		case bz_eTickEvent:
			if (((bz_TickEventData*)event)->time >= restartTime)
				this->restartTimeReached();
			break;
		default:
			break;
	}
}

// Local Variables: ***
// mode:C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8

