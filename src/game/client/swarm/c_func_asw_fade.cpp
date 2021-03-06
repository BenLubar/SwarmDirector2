#include "cbase.h"
#include "c_func_asw_fade.h"
#include "c_asw_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT(C_Func_ASW_Fade, DT_Func_ASW_Fade, CFunc_ASW_Fade)
	RecvPropInt(RECVINFO(m_nFadeOpacity)),
END_RECV_TABLE()

ConVar asw_fade_duration("asw_fade_duration", "0.5", FCVAR_ARCHIVE, "", true, 0, false, 0);

C_Func_ASW_Fade::C_Func_ASW_Fade()
{
	m_flInterpStart = 0;
	m_iLastControls = 1;
	m_hLastMarine = NULL;
}

void C_Func_ASW_Fade::OnDataChanged(DataUpdateType_t updateType)
{
	BaseClass::OnDataChanged(updateType);

	if (updateType == DATA_UPDATE_CREATED)
	{
		SetNextClientThink(CLIENT_THINK_ALWAYS);
		if (GetRenderMode() == kRenderNormal)
		{
			SetRenderMode(kRenderTransTexture);
		}
		m_bFaded = false;
		m_nNormalOpacity = GetRenderAlpha();
	}
}

void C_Func_ASW_Fade::ClientThink()
{
	BaseClass::ClientThink();

	C_ASW_Player *pPlayer = C_ASW_Player::GetLocalASWPlayer();
	if (!pPlayer)
	{
		return;
	}

	C_ASW_Marine *pMarine = pPlayer->GetViewMarine();

	bool bFade = pPlayer->GetASWControls() == 1 && pMarine && pMarine->GetAbsOrigin().z <= GetAbsOrigin().z;
	byte target = bFade ? m_nFadeOpacity : m_nNormalOpacity;
	byte prev = bFade ? m_nNormalOpacity : m_nFadeOpacity;
	if (bFade != m_bFaded)
	{
		m_bFaded = bFade;
		m_flInterpStart = gpGlobals->curtime - fabs((m_nFadeOpacity != m_nNormalOpacity) ? asw_fade_duration.GetFloat() * (GetRenderAlpha() - prev) / (m_nFadeOpacity - m_nNormalOpacity) : asw_fade_duration.GetFloat());
		m_flInterpStart = MAX(0, m_flInterpStart);
	}

	if ( pPlayer->GetASWControls() != m_iLastControls || pMarine != m_hLastMarine.Get() )
	{
		m_iLastControls = pPlayer->GetASWControls();
		m_hLastMarine = pMarine;
		m_flInterpStart = 0;
		SetRenderAlpha(target);
		return;
	}

	if (m_flInterpStart + asw_fade_duration.GetFloat() <= gpGlobals->curtime)
	{
		SetRenderAlpha(target);
	}
	else if (m_flInterpStart > 0)
	{
		SetRenderAlpha(Lerp((gpGlobals->curtime - m_flInterpStart) / asw_fade_duration.GetFloat(), prev, target));
	}
}
