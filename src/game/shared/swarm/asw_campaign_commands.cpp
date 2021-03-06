#include "cbase.h"
#include "filesystem.h"
#include "missionchooser/iasw_mission_chooser.h"
#include "missionchooser/iasw_mission_chooser_source.h"
#include "../missionchooser/asw_mission_chooser_source_local.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Alien Swarm: Infested Campaign Console Commands

// Resume playing a savegame - launches to the campaign lobby with the specified save data loaded
void ASW_LaunchSaveGame(const char *szSaveName, bool bMultiplayer, bool bChangeLevel)
{
	IASW_Mission_Chooser_Source* pSource = missionchooser ? missionchooser->LocalMissionSource() : NULL;
	if (!pSource)
		return;

	int iMissions = pSource->GetNumMissionsCompleted(szSaveName);
	if (iMissions == -1)	// invalid save
		return;

	const char *pszLobby = pSource->GetCampaignSaveIntroMap(szSaveName);
	if (!pszLobby)
		return;

	// launch the campaign lobby map
	char buffer[512];
	if (bChangeLevel)		
		Q_snprintf(buffer, sizeof(buffer), "changelevel %s campaign %s\n", pszLobby, szSaveName);
	else
		Q_snprintf(buffer, sizeof(buffer), "map %s campaign %s\n", pszLobby, szSaveName);

#ifdef CLIENT_DLL
	engine->ClientCmd(buffer);
#else
	engine->ServerCommand(buffer);
#endif
}

class CASW_StartNewCampaign : public ICommandCallback, public ICommandCompletionCallback
{
	virtual void CommandCallback( const CCommand &command )
	{
		IASW_Mission_Chooser_Source *pSource = missionchooser ? missionchooser->LocalMissionSource() : NULL;
		if ( !pSource )
		{
			return;
		}

		if ( command.ArgC() < 4 || ( V_stricmp( command.Arg( 3 ), "MP" ) &&  V_stricmp( command.Arg( 3 ), "SP" ) ) )
		{
			Msg( "Usage: ASW_StartCampaign [CampaignName] [SaveName] [SP|MP]\n   Use 'auto' for SaveName to have an autonumbered save.\n" );
			return;
		}

		// create a new savegame at the start of the campaign
		char buffer[MAX_PATH];
		if ( !V_stricmp( command.Arg( 2 ), "auto" ) )
		{
			buffer[0] = '\0';
		}
		else
		{
			V_strncpy(buffer, command.Arg( 2 ), sizeof(buffer));
		}

		// Use an arg to detect MP (would be better to check maxplayers here, but we can't?)
		bool bMulti = !V_stricmp( command.Arg( 3 ), "MP" );

		if ( !pSource->ASW_Campaign_CreateNewSaveGame( buffer, sizeof( buffer ), command.Arg( 1 ), bMulti, NULL ) )
		{
			Msg( "Unable to create new save game.\n" );
			return;
		}

		// go to the load savegame code
		ASW_LaunchSaveGame( buffer, bMulti, command.ArgC() == 5 ); // if 4th parameter specified, do a changelevel instead of launching a new map.
	}

	virtual int CommandCompletionCallback( const char *partial, CUtlVector<CUtlString> &commands )
	{
		CASW_Mission_Chooser_Source_Local *pSource = missionchooser ? assert_cast<CASW_Mission_Chooser_Source_Local *>( missionchooser->LocalMissionSource() ) : NULL;
		if ( !pSource )
		{
			return 0;
		}

		const char *base = IsClientDll() ? "ASW_StartCampaign " : "ASW_Server_StartCampaign ";

		// using strlen instead of V_strlen so hopefully the compiler can precompute it.

		if ( V_strncmp( partial, base, strlen( base ) ) )
		{
			// No space after the command.
			return 0;
		}

		const char *pszCampaign = partial + strlen( base );

		if ( V_strstr( pszCampaign, pszCampaign[0] == '"' ? "\"" : " " ) )
		{
			// Campaign name finished - we're done.
			return 0;
		}

		if ( pszCampaign[0] == '"' )
		{
			// Skip opening quote.
			pszCampaign++;
		}

		int nSearchLength = V_strlen( pszCampaign );
		FOR_EACH_VEC( pSource->m_CampaignList, i )
		{
			if ( !V_strnicmp( pSource->m_CampaignList[i].szMapName, pszCampaign, nSearchLength ) )
			{
				CUtlString command( base );
				command += pSource->m_CampaignList[i].szMapName;
				command.SetLength( command.Length() - strlen( ".txt" ) );
				command += " ";
				commands.AddToTail( command );
			}
		}

		return commands.Count();
	}
};
static CASW_StartNewCampaign ASW_StartNewCampaign_cc;
#ifdef CLIENT_DLL
ConCommand ASW_StartCampaign( "ASW_StartCampaign", &ASW_StartNewCampaign_cc, "Starts a new campaign game", FCVAR_DONTRECORD, &ASW_StartNewCampaign_cc );
#else
ConCommand ASW_Server_StartCampaign( "ASW_Server_StartCampaign", &ASW_StartNewCampaign_cc, "Server starts a new campaign game", FCVAR_DONTRECORD, &ASW_StartNewCampaign_cc );
#endif

// con command for loading a savegame
void ASW_LoadCampaignGame_cc( const CCommand &args )
{
	if ( args.ArgC() < 3 )
	{
		Msg("Usage: ASW_LoadCampaign [SaveName] [SP|MP]\n" );
		return;
	}

	// Use an arg to detect MP (would be better to check maxplayers here, but we can't?)
	bool bMulti = (!Q_strnicmp( args[2], "MP", 2 ));
	ASW_LaunchSaveGame(args[1], bMulti, args.ArgC() == 4);	// if 2nd parameter specified, do a changelevel instead of launching a new
}
#ifdef CLIENT_DLL
ConCommand ASW_LoadCampaignGame( "ASW_LoadCampaign", ASW_LoadCampaignGame_cc, "Loads a previously saved campaign game", FCVAR_DONTRECORD );
#else
ConCommand ASW_Server_LoadCampaignGame( "ASW_Server_LoadCampaign", ASW_LoadCampaignGame_cc, "Server loads a previously saved campaign game", FCVAR_DONTRECORD );
#endif