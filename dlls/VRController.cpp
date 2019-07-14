
#include <algorithm>

#include "extdll.h"
#include "util.h"
#include "vector.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "animation.h"
#include "VRControllerModel.h"
#include "VRController.h"
#include "VRPhysicsHelper.h"
#include "VRModelHelper.h"

VRController::~VRController()
{
	if (m_hLaserSpot)
	{
		UTIL_Remove(m_hLaserSpot);
		m_hLaserSpot = nullptr;
	}

	for (auto beam : m_hDebugBBoxes)
	{
		UTIL_Remove(beam);
	}
	m_hDebugBBoxes.clear();
}



void VRController::Update(CBasePlayer *pPlayer, const int timestamp, const bool isValid, const bool isMirrored, const Vector & offset, const Vector & angles, const Vector & velocity, bool isDragging, VRControllerID id, int weaponId)
{
	// Filter out outdated updates
	if (timestamp <= m_lastUpdateClienttime && m_lastUpdateServertime >= gpGlobals->time)
	{
		return;
	}

	// Store data
	m_id = id;
	m_isValid = isValid;
	m_offset = offset;
	m_position = pPlayer->GetClientOrigin() + offset;
	m_angles = angles;
	m_velocity = velocity;
	m_lastUpdateServertime = gpGlobals->time;
	m_lastUpdateClienttime = timestamp;
	m_isDragging = m_isValid && isDragging;
	m_isMirrored = isMirrored;
	m_weaponId = weaponId;

	UpdateModel(pPlayer);

	UpdateHitBoxes();

	// TODO TEMP
	DebugDrawHitBoxes(pPlayer);

	UpdateLaserSpot();

	SendEntityDataToClient(pPlayer, id);
}

void VRController::DebugDrawHitBoxes(CBasePlayer *pPlayer)
{
	constexpr const int numlinesperbox = 6;

	size_t numlasers = m_hitboxes.size() * numlinesperbox;

	// Remove any "dead" lasers (levelchange etc.)
	m_hDebugBBoxes.erase(std::remove_if(m_hDebugBBoxes.begin(), m_hDebugBBoxes.end(), [](EHANDLE& e) { return e.Get() == nullptr; }), m_hDebugBBoxes.end());

	// Remove lasers that are "too much" (happens when switching from model with more hitboxes to model with less hitboxes)
	while (m_hDebugBBoxes.size() > numlasers)
	{
		UTIL_Remove(m_hDebugBBoxes.back());
		m_hDebugBBoxes.pop_back();
	}

	// Create any missing lasers (happens when starting new game or when switching from model with less hitboxes to model with more hitboxes)
	while (m_hDebugBBoxes.size() < numlasers)
	{
		CBeam* pBeam = CBeam::BeamCreate("sprites/xbeam1.spr", 1);
		pBeam->pev->spawnflags |= SF_BEAM_TEMPORARY;
		pBeam->Spawn();
		pBeam->PointsInit(GetPosition(), GetPosition());
		pBeam->pev->owner = pPlayer->edict();
		pBeam->SetColor(255, 0, 0);
		pBeam->SetBrightness(255);
		m_hDebugBBoxes.push_back(EHANDLE{ pBeam });
	}

	// Update laser positions for each hitbox (8 lasers coming from the origin and going to each of the current bbox's corners
	int i = 0;
	for (const HitBox& hitbox : m_hitboxes)
	{
		Vector points[8];
		points[0] = Vector{ hitbox.mins[0], hitbox.mins[1], hitbox.mins[2] };
		points[1] = Vector{ hitbox.mins[0], hitbox.mins[1], hitbox.maxs[2] };
		points[2] = Vector{ hitbox.mins[0], hitbox.maxs[1], hitbox.mins[2] };
		points[3] = Vector{ hitbox.mins[0], hitbox.maxs[1], hitbox.maxs[2] };
		points[4] = Vector{ hitbox.maxs[0], hitbox.mins[1], hitbox.mins[2] };
		points[5] = Vector{ hitbox.maxs[0], hitbox.mins[1], hitbox.maxs[2] };
		points[6] = Vector{ hitbox.maxs[0], hitbox.maxs[1], hitbox.mins[2] };
		points[7] = Vector{ hitbox.maxs[0], hitbox.maxs[1], hitbox.maxs[2] };

		for (int j = 0; j < 8; j++)
		{
			VRPhysicsHelper::Instance().RotateVector(points[j], hitbox.angles);
		}

		dynamic_cast<CBeam*>((CBaseEntity*)m_hDebugBBoxes[i * numlinesperbox + 0])->SetStartAndEndPos(hitbox.origin + points[0], hitbox.origin + points[1]);
		dynamic_cast<CBeam*>((CBaseEntity*)m_hDebugBBoxes[i * numlinesperbox + 1])->SetStartAndEndPos(hitbox.origin + points[0], hitbox.origin + points[2]);
		dynamic_cast<CBeam*>((CBaseEntity*)m_hDebugBBoxes[i * numlinesperbox + 2])->SetStartAndEndPos(hitbox.origin + points[0], hitbox.origin + points[3]);

		dynamic_cast<CBeam*>((CBaseEntity*)m_hDebugBBoxes[i * numlinesperbox + 3])->SetStartAndEndPos(hitbox.origin + points[7], hitbox.origin + points[6]);
		dynamic_cast<CBeam*>((CBaseEntity*)m_hDebugBBoxes[i * numlinesperbox + 4])->SetStartAndEndPos(hitbox.origin + points[7], hitbox.origin + points[5]);
		dynamic_cast<CBeam*>((CBaseEntity*)m_hDebugBBoxes[i * numlinesperbox + 5])->SetStartAndEndPos(hitbox.origin + points[7], hitbox.origin + points[4]);

		i++;
	}
}

void VRController::UpdateLaserSpot()
{
	if (IsWeaponWithVRLaserSpot(m_weaponId) && CVAR_GET_FLOAT("vr_enable_aim_laser") != 0.f)
	{
		if (!m_hLaserSpot)
		{
			CLaserSpot *pLaserSpot = CLaserSpot::CreateSpot();
			pLaserSpot->Revive();
			pLaserSpot->pev->scale = 0.1f;
			m_hLaserSpot = pLaserSpot;
		}

		TraceResult tr;
		UTIL_TraceLine(GetPosition(), GetPosition() + (GetAim() * 8192.f), dont_ignore_monsters, GetModel()->edict(), &tr);
		UTIL_SetOrigin(m_hLaserSpot->pev, tr.vecEndPos);
	}
	else
	{
		if (m_hLaserSpot)
		{
			UTIL_Remove(m_hLaserSpot);
			m_hLaserSpot = nullptr;
		}
	}
}

void VRController::UpdateModel(CBasePlayer* pPlayer)
{
	if (m_weaponId == WEAPON_BAREHAND)
	{
		if (pPlayer->HasSuit())
		{
			m_modelName = MAKE_STRING("models/v_hand_hevsuit.mdl");
		}
		else
		{
			m_modelName = MAKE_STRING("models/v_hand_labcoat.mdl");
		}
	}
	else
	{
		m_modelName = pPlayer->pev->viewmodel;
	}

	CVRControllerModel *pModel = (CVRControllerModel*)GetModel();
	pModel->pev->origin = GetPosition();
	pModel->pev->angles = GetAngles();
	pModel->pev->velocity = GetVelocity();
	UTIL_SetOrigin(pModel->pev, pModel->pev->origin);

	if (!FStrEq(STRING(pModel->pev->model), STRING(m_modelName)))
	{
		pModel->SetModel(m_modelName);
	}

	if (m_isValid)
	{
		pModel->TurnOn();
		pModel->pev->mins = Vector{ -m_radius, -m_radius, -m_radius };
		pModel->pev->maxs = Vector{ m_radius, m_radius, m_radius };
		pModel->pev->size = pModel->pev->maxs - pModel->pev->mins;
		pModel->pev->absmin = pModel->pev->origin + pModel->pev->mins;
		pModel->pev->absmax = pModel->pev->origin + pModel->pev->maxs;
	}
	else
	{
		pModel->TurnOff();
	}

	if (m_isDragging && m_weaponId == WEAPON_BAREHAND && pModel->pev->sequence != 1)
	{
		PlayWeaponAnimation(1, 0);
	}
}

void VRController::SendEntityDataToClient(CBasePlayer *pPlayer, VRControllerID id)
{
	extern int gmsgVRControllerEnt;
	MESSAGE_BEGIN(MSG_ALL, gmsgVRControllerEnt, NULL);
	WRITE_ENTITY(ENTINDEX(GetModel()->edict()));
	WRITE_BYTE((id == VRControllerID::WEAPON) ? 1 : 0);
	WRITE_PRECISE_VECTOR(GetModel()->pev->origin);
	WRITE_PRECISE_VECTOR(GetModel()->pev->angles);
	WRITE_BYTE(IsMirrored() ? 1 : 0);
	WRITE_BYTE(IsValid() ? 1 : 0);
	MESSAGE_END();
}

void VRController::UpdateHitBoxes()
{
	if (!IsValid())
	{
		ClearHitBoxes();
		return;
	}

	CBaseEntity *pModel = GetModel();
	void *pmodel = GET_MODEL_PTR(pModel->edict());
	if (!pmodel)
	{
		ClearHitBoxes();
		return;
	}

	int numhitboxes = GetNumHitboxes(pmodel);
	if (numhitboxes <= 0)
	{
		ClearHitBoxes();
		return;
	}

	if (m_hitboxes.size() == numhitboxes && FStrEq(STRING(m_hitboxModelName), STRING(pModel->pev->model)) && gpGlobals->time < (m_hitboxLastUpdateTime + (1.f/25.f)))
		return;

	m_hitboxes.clear();
	m_radius = 0.f;
	std::vector<StudioHitBox> studiohitboxes;
	studiohitboxes.resize(numhitboxes);
	if (GetHitboxes(pModel->pev, pmodel, pModel->pev->sequence, pModel->pev->frame, studiohitboxes.data(), IsMirrored()))
	{
		for (const auto& studiohitbox : studiohitboxes)
		{
			float l1 = studiohitbox.mins.Length();
			float l2 = studiohitbox.maxs.Length();

			// Skip hitboxes too far away or too big
			if (l1 > 256.f || l2 > 256.f)
			{
				int fjskajfes = 0;	// to set breakpoint in debug mode
				continue;
			}

			m_hitboxes.push_back(HitBox{ studiohitbox.origin, studiohitbox.angles, studiohitbox.mins, studiohitbox.maxs });
			m_radius = max(m_radius, l1);
			m_radius = max(m_radius, l2);
		}
	}
	m_hitboxFrame = pModel->pev->frame;
	m_hitboxSequence = pModel->pev->sequence;
	m_hitboxLastUpdateTime = gpGlobals->time;
}

CBaseEntity* VRController::GetModel() const
{
	if (!m_hModel)
	{
		CVRControllerModel *pModel = CVRControllerModel::Create(m_modelName ? STRING(m_modelName) :"models/v_hand_labcoat.mdl", GetPosition());
		if (IsValid())
		{
			pModel->TurnOn();
		}
		m_hModel = pModel;
	}
	return m_hModel;
}

void VRController::PlayWeaponAnimation(int iAnim, int body)
{
	if (!IsValid())
		return;

	CVRControllerModel *pModel = (CVRControllerModel*)GetModel();
	auto& modelInfo = VRModelHelper::GetInstance().GetModelInfo(pModel);
	if (modelInfo.m_isValid && iAnim < modelInfo.m_numSequences)
	{
		// ALERT(at_console, "VRController::PlayWeaponAnimation: Playing sequence %i of %i with framerate %.0f for %s (%s)\n", iAnim, modelInfo.m_numSequences, modelInfo.m_sequences[iAnim].framerate, STRING(m_modelName), STRING(pModel->pev->model));
		pModel->SetSequence(iAnim);
	}
	else
	{
		// ALERT(at_console, "VRController::PlayWeaponAnimation: Invalid sequence %i of %i for %s (%s)\n", iAnim, modelInfo.m_numSequences, STRING(m_modelName), STRING(pModel->pev->model));
		pModel->SetSequence(0);
	}
}

void VRController::PlayWeaponMuzzleflash()
{
	if (!IsValid())
		return;

	CBaseEntity *pModel = GetModel();
	pModel->pev->effects |= EF_MUZZLEFLASH;
}

bool VRController::AddTouchedEntity(EHANDLE hEntity) const
{
	if (m_touchedEntities.count(hEntity) != 0)
		return false;

	m_touchedEntities.insert(hEntity);
	return true;
}

bool VRController::RemoveTouchedEntity(EHANDLE hEntity) const
{
	if (m_touchedEntities.count(hEntity) == 0)
		return false;

	m_touchedEntities.erase(hEntity);
	return true;
}

bool VRController::AddDraggedEntity(EHANDLE hEntity) const
{
	if (m_draggedEntities.count(hEntity) != 0)
		return false;

	m_draggedEntities.insert(hEntity);
	return true;
}

bool VRController::RemoveDraggedEntity(EHANDLE hEntity) const
{
	if (m_draggedEntities.count(hEntity) == 0)
		return false;

	m_draggedEntities.erase(hEntity);
	return true;
}

bool VRController::IsDraggedEntity(EHANDLE hEntity) const
{
	return m_draggedEntities.count(hEntity) != 0;
}

bool VRController::AddHitEntity(EHANDLE hEntity) const
{
	if (m_hitEntities.count(hEntity) != 0)
		return false;

	m_hitEntities.insert(hEntity);
	return true;
}

bool VRController::RemoveHitEntity(EHANDLE hEntity) const
{
	if (m_hitEntities.count(hEntity) == 0)
		return false;

	m_hitEntities.erase(hEntity);
	return true;
}

const Vector VRController::GetGunPosition() const
{
	Vector pos;
	if (VRModelHelper::GetInstance().GetAttachment(GetModel(), VR_MUZZLE_ATTACHMENT, pos))
	{
		return pos;
	}
	else
	{
		return GetPosition();
	}
}

const Vector VRController::GetAim() const
{
	Vector pos1;
	Vector pos2;
	bool result1 = VRModelHelper::GetInstance().GetAttachment(GetModel(), VR_MUZZLE_ATTACHMENT, pos1);
	bool result2 = VRModelHelper::GetInstance().GetAttachment(GetModel(), VR_MUZZLE_ATTACHMENT + 1, pos2);
	if (result1 && result2 && pos2 != pos1)
	{
		return (pos2 - pos1).Normalize();
	}
	UTIL_MakeAimVectors(GetAngles());
	return gpGlobals->v_forward;
}
