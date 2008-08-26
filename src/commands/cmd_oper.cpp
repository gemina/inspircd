/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2008 InspIRCd Development Team
 * See: http://www.inspircd.org/wiki/index.php/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"
#include "commands/cmd_oper.h"
#include "hashcomp.h"

bool OneOfMatches(const char* host, const char* ip, const char* hostlist)
{
	std::stringstream hl(hostlist);
	std::string xhost;
	while (hl >> xhost)
	{
		if (InspIRCd::Match(host, xhost, NULL) || InspIRCd::MatchCIDR(ip, xhost, NULL))
		{
			return true;
		}
	}
	return false;
}

extern "C" DllExport Command* init_command(InspIRCd* Instance)
{
	return new CommandOper(Instance);
}

CmdResult CommandOper::Handle (const std::vector<std::string>& parameters, User *user)
{
	char LoginName[MAXBUF];
	char Password[MAXBUF];
	char OperType[MAXBUF];
	char TypeName[MAXBUF];
	char HostName[MAXBUF];
	char ClassName[MAXBUF];
	char TheHost[MAXBUF];
	char TheIP[MAXBUF];
	char HashType[MAXBUF];
	int j;
	bool found = false;
	bool type_invalid = false;

	bool match_login = false;
	bool match_pass = false;
	bool match_hosts = false;

	snprintf(TheHost,MAXBUF,"%s@%s",user->ident.c_str(),user->host.c_str());
	snprintf(TheIP, MAXBUF,"%s@%s",user->ident.c_str(),user->GetIPString());

	for (int i = 0; i < ServerInstance->Config->ConfValueEnum(ServerInstance->Config->config_data, "oper"); i++)
	{
		ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "oper", "name", i, LoginName, MAXBUF);
		ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "oper", "password", i, Password, MAXBUF);
		ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "oper", "type", i, OperType, MAXBUF);
		ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "oper", "host", i, HostName, MAXBUF);
		ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "oper", "hash", i, HashType, MAXBUF);

		match_login = (LoginName == parameters[0]);
		match_pass = !ServerInstance->PassCompare(user, Password, parameters[1], HashType);
		match_hosts = OneOfMatches(TheHost,TheIP,HostName);

		if (match_login && match_pass && match_hosts)
		{
			type_invalid = true;
			for (j =0; j < ServerInstance->Config->ConfValueEnum(ServerInstance->Config->config_data, "type"); j++)
			{
				ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "type", "name", j, TypeName, MAXBUF);
				ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "type", "class", j, ClassName, MAXBUF);

				if (!strcmp(TypeName,OperType))
				{
					/* found this oper's opertype */
					if (!ServerInstance->IsNick(TypeName, ServerInstance->Config->Limits.NickMax))
					{
						user->WriteNumeric(491, "%s :Invalid oper type (oper types must follow the same syntax as nicknames)",user->nick.c_str());
						ServerInstance->SNO->WriteToSnoMask('o',"CONFIGURATION ERROR! Oper type '%s' contains invalid characters",OperType);
						ServerInstance->Logs->Log("OPER",DEFAULT,"OPER: Failed oper attempt by %s!%s@%s: credentials valid, but oper type erroneous.", user->nick.c_str(), user->ident.c_str(), user->host.c_str());
						return CMD_FAILURE;
					}
					ServerInstance->Config->ConfValue(ServerInstance->Config->config_data, "type","host", j, HostName, MAXBUF);
					if (*HostName)
						user->ChangeDisplayedHost(HostName);
					if (*ClassName)
					{
						user->SetClass(ClassName);
						user->CheckClass();
					}
					found = true;
					type_invalid = false;
					break;
				}
			}
		}
		if (match_login || found)
			break;
	}
	if (found)
	{
		/* correct oper credentials */
		ServerInstance->SNO->WriteToSnoMask('o',"%s (%s@%s) is now an IRC operator of type %s (using oper '%s')", user->nick.c_str(), user->ident.c_str(), user->host.c_str(), irc::Spacify(OperType), parameters[0].c_str());
		user->WriteNumeric(381, "%s :You are now %s %s",user->nick.c_str(), strchr("aeiouAEIOU", *OperType) ? "an" : "a", irc::Spacify(OperType));
		if (!user->IsModeSet('o'))
			user->Oper(OperType, LoginName);
	}
	else
	{
		char broadcast[MAXBUF];

		if (!type_invalid)
		{
			std::string fields;
			if (!match_login)
				fields.append("login ");
			else
			{
				if (!match_pass)
					fields.append("password ");
				if (!match_hosts)
					fields.append("hosts");
			}

			// tell them they suck, and lag them up to help prevent brute-force attacks
			user->WriteNumeric(491, "%s :Invalid oper credentials",user->nick.c_str());
			user->IncreasePenalty(10);
			
			snprintf(broadcast, MAXBUF, "WARNING! Failed oper attempt by %s!%s@%s using login '%s': The following fields do not match: %s", user->nick.c_str(), user->ident.c_str(), user->host.c_str(), parameters[0].c_str(), fields.c_str());
			ServerInstance->SNO->WriteToSnoMask('o',std::string(broadcast));
			ServerInstance->PI->SendSNONotice("o", std::string("OPER: ") + broadcast);

			ServerInstance->Logs->Log("OPER",DEFAULT,"OPER: Failed oper attempt by %s!%s@%s using login '%s': The following fields did not match: %s", user->nick.c_str(), user->ident.c_str(), user->host.c_str(), parameters[0].c_str(), fields.c_str());
			return CMD_FAILURE;
		}
		else
		{
			user->WriteNumeric(491, "%s :Your oper block does not have a valid opertype associated with it",user->nick.c_str());

			snprintf(broadcast, MAXBUF, "CONFIGURATION ERROR! Oper block '%s': missing OperType %s",parameters[0].c_str(),OperType);

			ServerInstance->SNO->WriteToSnoMask('o', std::string(broadcast));

			ServerInstance->Logs->Log("OPER",DEFAULT,"OPER: Failed oper attempt by %s!%s@%s using login '%s': credentials valid, but oper type nonexistent.", user->nick.c_str(), user->ident.c_str(), user->host.c_str(), parameters[0].c_str());
			return CMD_FAILURE;
		}
	}
	return CMD_SUCCESS;
}

