#include "cbase.h"
#include "asw_arena.h"
#include "asw_director.h"
#include "asw_spawn_manager.h"
#include "asw_gamerules.h"
#include "asw_game_resource.h"
#include "asw_marine_resource.h"
#include "asw_marine.h"
#include "asw_weapon.h"
#include "ai_network.h"
#include "missionchooser/iasw_mission_chooser.h"
#include "missionchooser/iasw_random_missions.h"
#include "asw_objective_escape.h"
#include "asw_director_control.h"
#include "asw_mission_manager.h"
#include "../mod_player_performance.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar asw_director_debug("asw_director_debug", "0", FCVAR_CHEAT, "Displays director status on screen");

extern ConVar asw_intensity_far_range;
extern ConVar asw_spawning_enabled;

extern ConVar asw_horde_override;
extern ConVar asw_wanderer_override;
ConVar asw_horde_interval_min("asw_horde_interval_min", "45", FCVAR_CHEAT, "Min time between hordes" );
ConVar asw_horde_interval_max("asw_horde_interval_max", "65", FCVAR_CHEAT, "Min time between hordes");
ConVar asw_horde_interval_scale("asw_horde_interval_scale", "10", FCVAR_CHEAT, "actual time between hordes = interval * scale / (offset + mission difficulty)");
ConVar asw_horde_interval_offset("asw_horde_interval_offset", "5", FCVAR_CHEAT, "actual time between hordes = interval * scale / (offset + mission difficulty)");

ConVar asw_director_relaxed_min_time("asw_director_relaxed_min_time", "25", FCVAR_CHEAT, "Min time that director stops spawning aliens");
ConVar asw_director_relaxed_max_time("asw_director_relaxed_max_time", "40", FCVAR_CHEAT, "Max time that director stops spawning aliens");
ConVar asw_director_peak_min_time("asw_director_peak_min_time", "1", FCVAR_CHEAT, "Min time that director keeps spawning aliens when marine intensity has peaked");
ConVar asw_director_peak_max_time("asw_director_peak_max_time", "3", FCVAR_CHEAT, "Max time that director keeps spawning aliens when marine intensity has peaked");
ConVar asw_interval_min("asw_interval_min", "1.0f", FCVAR_CHEAT, "Director: Alien spawn interval will never go lower than this");
ConVar asw_interval_initial_min("asw_interval_initial_min", "5", FCVAR_CHEAT, "Director: Min time between alien spawns when first entering spawning state");
ConVar asw_interval_initial_max("asw_interval_initial_max", "7", FCVAR_CHEAT, "Director: Max time between alien spawns when first entering spawning state");
ConVar asw_interval_initial_scale("asw_interval_initial_scale", "10", FCVAR_CHEAT, "actual time until first wanderer = interval * scale / (offset + mission difficulty)");
ConVar asw_interval_initial_offset("asw_interval_initial_offset", "5", FCVAR_CHEAT, "actual time until first wanderer = interval * scale / (offset + mission difficulty)");
ConVar asw_interval_change_min("asw_interval_change_min", "0.9", FCVAR_CHEAT, "Director: Min scale applied to alien spawn interval each spawn");
ConVar asw_interval_change_max("asw_interval_change_max", "0.95", FCVAR_CHEAT, "Director: Max scale applied to alien spawn interval each spawn");

CASW_Director g_ASWDirector;
CASW_Director* ASWDirector() { return &g_ASWDirector; }

CASW_Director::CASW_Director( void ) : CAutoGameSystemPerFrame( "CASW_Director" )
{
	
}

CASW_Director::~CASW_Director()
{
	
}

bool CASW_Director::Init()
{
	m_bSpawningAliens = false;
	m_bReachedIntensityPeak = false;
	m_fTimeBetweenAliens = 0;
	m_AlienSpawnTimer.Invalidate();
	m_SustainTimer.Invalidate();
	m_HordeTimer.Invalidate();
	m_IntensityUpdateTimer.Invalidate();
	m_bInitialWait = true;

	m_bFiredEscapeRoom = false;
	
	m_bHordeInProgress = false;
	m_bFinale = false;

	// take horde/wanderer settings from director control
	CASW_Director_Control* pControl = static_cast<CASW_Director_Control*>( gEntList.FindEntityByClassname( NULL, "asw_director_control" ) );
	if ( pControl )
	{
		m_bWanderersEnabled = pControl->m_bWanderersStartEnabled;
		m_bHordesEnabled = pControl->m_bHordesStartEnabled;
		m_bDirectorControlsSpawners = pControl->m_bDirectorControlsSpawners;
		m_flPreSpawnAliens = pControl->m_flPreSpawnAliens;
	}
	else
	{
		m_bWanderersEnabled = false;
		m_bHordesEnabled = false;
		m_bDirectorControlsSpawners = false;
		m_flPreSpawnAliens = 0;
	}

	return true;
}

void CASW_Director::Shutdown()
{

}

void CASW_Director::LevelInitPreEntity()
{
	if ( ASWSpawnManager() )
	{
		ASWSpawnManager()->LevelInitPreEntity();
	}
}

void CASW_Director::LevelInitPostEntity()
{
	Init();

	if ( ASWSpawnManager() )
	{
		ASWSpawnManager()->LevelInitPostEntity();
	}
}

void CASW_Director::LevelShutdownPostEntity()
{
	Shutdown();

	if ( ASWSpawnManager() )
	{
		ASWSpawnManager()->LevelShutdownPostEntity();
	}
}

void CASW_Director::FrameUpdatePreEntityThink()
{

}

void CASW_Director::FrameUpdatePostEntityThink()
{
	// only think when we're in-game
	if ( !ASWGameRules() || ASWGameRules()->GetGameState() != ASW_GS_INGAME )
		return;

	//PJ - Hook in mod_player_performance
	CMOD_Player_Performance::PlayerPerformance()->FrameUpdatePostEntityThink();

	UpdateIntensity();

	if ( ASWSpawnManager() )
	{
		ASWSpawnManager()->Update();
	}

	UpdateMarineRooms();

	if ( !asw_spawning_enabled.GetBool() )
		return;

	UpdateHorde();

	UpdateSpawningState();

	bool bWanderersEnabled = m_bWanderersEnabled || asw_wanderer_override.GetBool();
	if ( bWanderersEnabled )
	{
		UpdateWanderers();
	}
}

// randomly generated levels provide data about each room in the level
// we check that here to react to special rooms
void CASW_Director::UpdateMarineRooms()
{
	CASW_Game_Resource *pGameResource = ASWGameResource();
	if ( !pGameResource || !missionchooser || !missionchooser->RandomMissions())
		return;

	for ( int i=0;i<pGameResource->GetMaxMarineResources();i++ )
	{
		CASW_Marine_Resource *pMR = pGameResource->GetMarineResource(i);
		if ( !pMR || !pMR->GetMarineEntity() || pMR->GetMarineEntity()->GetHealth() <= 0 )
			continue;

		IASW_Room_Details* pRoom = missionchooser->RandomMissions()->GetRoomDetails( pMR->GetMarineEntity()->GetAbsOrigin() );
		if ( !pRoom )
			continue;

		if ( !m_bFinale && pRoom->HasTag( "Escape" ) )
		{
			UpdateMarineInsideEscapeRoom( pMR->GetMarineEntity() );
		}
	}
}


// increase intensity as aliens are killed (particularly if they're close to the marines)
void CASW_Director::Event_AlienKilled( CBaseEntity *pAlien, const CTakeDamageInfo &info )
{
	if ( !pAlien )
		return;

	CASW_Game_Resource *pGameResource = ASWGameResource();
	if ( !pGameResource )
		return;

	//Pass event to mod_player_performance
	CMOD_Player_Performance::PlayerPerformance()->Event_AlienKilled(pAlien, info);

	bool bDangerous = pAlien->Classify() == CLASS_ASW_SHIELDBUG;       // shieldbug
	bool bVeryDangerous = pAlien->Classify() == CLASS_ASW_QUEEN;		// queen

	for ( int i=0;i<pGameResource->GetMaxMarineResources();i++ )
	{
		CASW_Marine_Resource *pMR = pGameResource->GetMarineResource(i);
		if ( !pMR )
			continue;

		CASW_Marine *pMarine = pMR->GetMarineEntity();
		if ( !pMarine || pMarine->GetHealth() <= 0 )
			continue;

		CASW_Intensity::IntensityType stress = CASW_Intensity::MILD;

		if ( bVeryDangerous )
		{
			stress = CASW_Intensity::EXTREME;
		}
		else if ( bDangerous )
		{
			stress = CASW_Intensity::HIGH;
		}
		else
		{
			float distance = pMarine->GetAbsOrigin().DistTo( pAlien->GetAbsOrigin() );
			if ( distance > asw_intensity_far_range.GetFloat() )
			{
				stress = CASW_Intensity::MILD;
			}
			else
			{
				stress = CASW_Intensity::MODERATE;
			}
		}

		pMR->GetIntensity()->Increase( stress );
	}

	ASWArena()->Event_AlienKilled( pAlien, info );
}

// increase intensity as marines take damage
void CASW_Director::MarineTookDamage( CASW_Marine *pMarine, const CTakeDamageInfo &info, bool bFriendlyFire )
{
	if ( !pMarine )
		return;

	// friendly fire doesn't cause intensity increases
	if ( bFriendlyFire )
		return;

	float flDamageRatio = info.GetDamage() / pMarine->GetHealth();

	CASW_Intensity::IntensityType stress = CASW_Intensity::MILD;
	if ( flDamageRatio < 0.2f )
	{
		stress = CASW_Intensity::MODERATE;
	}
	else if ( flDamageRatio < 0.5f )
	{
		stress = CASW_Intensity::HIGH;
	}
	else
	{
		stress = CASW_Intensity::EXTREME;
	}

	if ( pMarine->GetMarineResource() )
	{
		pMarine->GetMarineResource()->GetIntensity()->Increase( stress );
	}
}

void CASW_Director::UpdateHorde()
{
	if ( asw_director_debug.GetInt() > 0 )
	{
		if ( m_bHordeInProgress )
		{
			engine->Con_NPrintf( 7 + ASW_MAX_MARINE_RESOURCES, "Horde in progress.  Left to spawn = %d", ASWSpawnManager()->GetHordeToSpawn() );
		}
		engine->Con_NPrintf( 8 + ASW_MAX_MARINE_RESOURCES, "Next Horde due: %f", m_HordeTimer.GetRemainingTime() );

		engine->Con_NPrintf( 11 + ASW_MAX_MARINE_RESOURCES, "Awake aliens: %d\n", ASWSpawnManager()->GetAwakeAliens() );
		engine->Con_NPrintf( 12 + ASW_MAX_MARINE_RESOURCES, "Awake drones: %d\n", ASWSpawnManager()->GetAwakeDrones() );
	}

	bool bHordesEnabled = m_bHordesEnabled || asw_horde_override.GetBool();
	if ( !bHordesEnabled || !ASWSpawnManager() )
		return;

	if ( !m_HordeTimer.HasStarted() )
	{
		float flDuration = RandomFloat( asw_horde_interval_min.GetFloat(), asw_horde_interval_max.GetFloat() ) * asw_horde_interval_scale.GetFloat() / ( asw_horde_interval_offset.GetFloat() + (float) ASWGameRules()->GetMissionDifficulty() );
		if ( m_bFinale || flDuration < 5.0f || !IsFinite( flDuration ) )
		{
			flDuration = RandomFloat( 5.0f, 10.0f );
		}
		if ( asw_director_debug.GetBool() )
		{
			Msg( "Will be spawning a horde in %f seconds\n", flDuration );
		}
		m_HordeTimer.Start( flDuration );
	}
	else if ( m_HordeTimer.IsElapsed() )
	{
		if (ASWSpawnManager()->GetAwakeAliens() >= 100 || ASWSpawnManager()->GetAwakeDrones() >= 30)
		{
			if (asw_director_debug.GetBool())
			{
				Msg("Too many awake aliens. Delaying horde.\n");
			}
			m_HordeTimer.Start( 10.0f );
			return;
		}

		if ( ASWSpawnManager()->AddRandomHorde() )
		{
			m_bHordeInProgress = true;
			
			if ( ASWGameRules() )
			{
				ASWGameRules()->BroadcastSound( "Spawner.Horde" );
			}
			m_HordeTimer.Invalidate();
		}
		else
		{
			// if we failed to find a horde position, try again shortly.
			m_HordeTimer.Start( RandomFloat( 10.0f, 16.0f ) );
		}
	}
}

void CASW_Director::OnHordeFinishedSpawning()
{
	if ( asw_director_debug.GetBool() )
	{
		Msg("Horde finishes spawning\n");
	}
	m_bHordeInProgress = false;
}

void CASW_Director::UpdateSpawningState()
{
	if ( m_bFinale )				// in finale, just keep spawning aliens forever
	{
		m_bSpawningAliens = true;

		if ( asw_director_debug.GetBool() )
		{
			engine->Con_NPrintf( 3 + ASW_MAX_MARINE_RESOURCES, "%s: %f %s", m_bSpawningAliens ? "Spawning aliens" : "Relaxing",
				m_SustainTimer.HasStarted() ? m_SustainTimer.GetRemainingTime() : -1,
				"Finale" );
		}
		return;
	}

	//=====================================================================================
	// Main director rollercoaster logic
	//   Spawns aliens until a peak intensity is reached, then gives the marines a breather
	//=====================================================================================

	if ( !m_bSpawningAliens )			// not spawning aliens, we're in a relaxed state
	{
		if ( !m_SustainTimer.HasStarted() )
		{
			if ( GetMaxIntensity() < 1.0f )	// don't start our relax timer until the marines have left the peak
			{
				if ( m_bInitialWait )		// just do a short delay before starting combat at the beginning of a mission
				{
					m_SustainTimer.Start( RandomFloat( 3.0f, 16.0f ) );
					m_bInitialWait = false;
				}
				else
				{
					m_SustainTimer.Start( RandomFloat( asw_director_relaxed_min_time.GetFloat(), asw_director_relaxed_max_time.GetFloat() ) );
				}
			}
		}
		else if ( m_SustainTimer.IsElapsed() )		// TODO: Should check their intensity meters are below a certain threshold?  Should probably also not wait if they run too far ahead
		{
			m_bSpawningAliens = true;
			m_bReachedIntensityPeak = false;
			m_SustainTimer.Invalidate();
			m_fTimeBetweenAliens = 0;
			m_AlienSpawnTimer.Invalidate();
		}
	}
	else								// we're spawning aliens
	{
		if ( m_bReachedIntensityPeak )
		{
			// hold the peak intensity for a while, then drop back to the relaxed state
			if ( !m_SustainTimer.HasStarted() )
			{
				m_SustainTimer.Start( RandomFloat( asw_director_peak_min_time.GetFloat(), asw_director_peak_max_time.GetFloat() ) );
			}
			else if ( m_SustainTimer.IsElapsed() )
			{
				m_bSpawningAliens = false;
				m_SustainTimer.Invalidate();
			}
		}
		else
		{
			if ( GetMaxIntensity() >= 1.0f )
			{
				m_bReachedIntensityPeak = true;
			}
		}
	}

	if ( asw_director_debug.GetInt() > 0 )
	{
		engine->Con_NPrintf( 3 + ASW_MAX_MARINE_RESOURCES, "%s: %f %f %s", m_bSpawningAliens ? "Spawning aliens" : "Relaxing",
			m_SustainTimer.HasStarted() ? m_SustainTimer.GetRemainingTime() : -1,
			m_AlienSpawnTimer.HasStarted() ? m_AlienSpawnTimer.GetRemainingTime() : -1,
			m_bReachedIntensityPeak ? "Peaked" : "Not peaked" );
	}
}

void CASW_Director::UpdateWanderers()
{
	if ( !m_bSpawningAliens )
	{
		if ( asw_director_debug.GetInt() > 0 )
		{
			engine->Con_NPrintf( 4 + ASW_MAX_MARINE_RESOURCES, "Not spawning regular aliens" );
		}
		return;
	}

	// spawn an alien every so often
	if ( !m_AlienSpawnTimer.HasStarted() || m_AlienSpawnTimer.IsElapsed() )
	{
		if ( m_fTimeBetweenAliens == 0 )
		{
			// initial time between alien spawns
			m_fTimeBetweenAliens = RandomFloat( asw_interval_initial_min.GetFloat(), asw_interval_initial_max.GetFloat() ) * asw_interval_initial_scale.GetFloat() / ( asw_interval_initial_offset.GetFloat() + (float) ASWGameRules()->GetMissionDifficulty() );;
		}
		else
		{
			// reduce the time by some random amount each interval
			m_fTimeBetweenAliens = m_fTimeBetweenAliens * RandomFloat( asw_interval_change_min.GetFloat(), asw_interval_change_max.GetFloat() );
		}
		if (!IsFinite(m_fTimeBetweenAliens))
		{
			m_fTimeBetweenAliens = asw_interval_min.GetFloat();
		}
		else
		{
			m_fTimeBetweenAliens = MAX(asw_interval_min.GetFloat(), m_fTimeBetweenAliens);
		}

		bool bSkipWanderers = ASWSpawnManager() && ASWSpawnManager()->GetAwakeAliens() >= 100;
		if ( asw_director_debug.GetInt() > 0 )
		{
			if ( bSkipWanderers )
			{
				engine->Con_NPrintf( 4 + ASW_MAX_MARINE_RESOURCES, "Too many awake aliens. Skipping wanderers." );
			}
			else
			{
				engine->Con_NPrintf( 4 + ASW_MAX_MARINE_RESOURCES, "Regular spawn interval = %f", m_fTimeBetweenAliens );
			}
		}

		m_AlienSpawnTimer.Start( m_fTimeBetweenAliens );

		if ( ASWSpawnManager() )
		{
			if ( bSkipWanderers )
			{
				if (asw_director_debug.GetBool())
				{
					Msg("Too many awake aliens. Skipping wanderer.\n");
				}
			}
			else
			{
				ASWSpawnManager()->AddAlien();
			}
		}
	}
}

// if director is controlling alien spawns, then mapper set spawners ask permission before spawning
bool CASW_Director::CanSpawnAlien( CASW_Spawner *pSpawner )
{
	if ( !m_bDirectorControlsSpawners )
		return true;

	return m_bSpawningAliens;
}

void CASW_Director::OnMarineStartedHack( CASW_Marine *pMarine, CBaseEntity *pComputer )
{
	CASW_Game_Resource *pGameResource = ASWGameResource();
	if ( !pGameResource )
		return;

	//Msg( " Marine started hack!\n" );

	// reset intensity so we can have a big fight without relaxing immediately
	for ( int i=0;i<pGameResource->GetMaxMarineResources();i++ )
	{
		CASW_Marine_Resource *pMR = pGameResource->GetMarineResource(i);
		if ( !pMR )
			continue;

		pMR->GetIntensity()->Reset();
	}

	float flQuickStart = RandomFloat( 2.0f, 5.0f );
	if ( m_HordeTimer.GetRemainingTime() > flQuickStart )
	{
		m_HordeTimer.Start( flQuickStart );
	}

	// TODO: Instead have some kind of 'is in a big fight' state?
}

void CASW_Director::OnMarineBurnedBiomass( CASW_Marine *pMarine, CBaseEntity *pBiomass )
{
	CASW_Game_Resource *pGameResource = ASWGameResource();
	if ( !pGameResource )
		return;

	//Msg( " Marine burned biomass!\n" );

	// reset intensity so we can have a big fight without relaxing immediately
	for ( int i=0;i<pGameResource->GetMaxMarineResources();i++ )
	{
		CASW_Marine_Resource *pMR = pGameResource->GetMarineResource(i);
		if ( !pMR )
			continue;

		pMR->GetIntensity()->Reset();
	}

	float flQuickStart = RandomFloat( 2.0f, 5.0f );
	if ( m_HordeTimer.GetRemainingTime() > flQuickStart )
	{
		m_HordeTimer.Start( flQuickStart );
	}

	// TODO: Instead have some kind of 'is in a big fight' state?
}

void CASW_Director::StartFinale()
{
	m_bFinale = true;
	m_bHordesEnabled = true;
	m_bWanderersEnabled = true;
	DevMsg("Starting finale\n");

	float flQuickStart = RandomFloat( 2.0f, 5.0f );
	if ( m_HordeTimer.GetRemainingTime() > flQuickStart )
	{
		m_HordeTimer.Start( flQuickStart );
	}
}

void CASW_Director::UpdateMarineInsideEscapeRoom( CASW_Marine *pMarine )
{
	if ( m_bFiredEscapeRoom )
		return;

	// Verify rules and mission manager are available.
	if ( !ASWGameRules() || !ASWGameRules()->GetMissionManager() )
		return;

	// Don't send the escape room output if there are objectives uncompleted.
	CASW_Objective_Escape *pEscape = ASWGameRules()->GetMissionManager()->GetEscapeObjective();
	if ( pEscape && !pEscape->OtherObjectivesComplete() )
		return;

	// Tell all director controls to send an output
	if ( g_pDirectorControl )
	{
		g_pDirectorControl->OnEscapeRoomStart( pMarine );
	}

	m_bFiredEscapeRoom = true;
}

void CASW_Director::OnMissionStarted()
{
	//PJ - Hook in mod_player_performance
	CMOD_Player_Performance::PlayerPerformance()->OnMissionStarted();

	if (ASWSpawnManager())
	{
		ASWSpawnManager()->SelectSpawnSet();
	}

	if (ASWSpawnManager() && m_flPreSpawnAliens > 0 && asw_spawning_enabled.GetBool())
	{
		ASWSpawnManager()->PreSpawnAliens(m_flPreSpawnAliens);
	}
}