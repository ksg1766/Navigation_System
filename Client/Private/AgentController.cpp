#include "stdafx.h"
#include "AgentController.h"
#include "GameInstance.h"
#include "GameObject.h"
#include "Terrain.h"
#include "DebugDraw.h"
#include "NSHelper.h"

constexpr auto EPSILON = 0.001f;

CAgentController::CAgentController(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
	:Super(pDevice, pContext)
	, m_vLinearSpeed(Vec3(50.0f, 50.0f, 50.0f))
	, m_vMaxLinearSpeed(Vec3(100.0f, 100.0f, 100.0f))
{
}

CAgentController::CAgentController(const CAgentController& rhs)
	:Super(rhs)
	, m_vLinearSpeed(rhs.m_vLinearSpeed)
	, m_vMaxLinearSpeed(rhs.m_vMaxLinearSpeed)
{
}

HRESULT CAgentController::Initialize_Prototype()
{
	return S_OK;
}

HRESULT CAgentController::Initialize(void* pArg)
{
	m_pTransform = GetTransform();

	if (nullptr != pArg)
	{
		m_pCurrentCell = static_cast<CellData*>(pArg);
	}

#pragma region DebugDraw
	m_pBatch = new PrimitiveBatch<VertexPositionColor>(m_pContext);

	m_pEffect = new BasicEffect(m_pDevice);
	m_pEffect->SetVertexColorEnabled(true);

	const void* pShaderByteCodes = nullptr;
	size_t		iLength = 0;
	m_pEffect->GetVertexShaderBytecode(&pShaderByteCodes, &iLength);

	if (FAILED(m_pDevice->CreateInputLayout(VertexPositionColor::InputElements, VertexPositionColor::InputElementCount, pShaderByteCodes, iLength, &m_pInputLayout)))
	{
		Safe_Delete(m_pBatch);
		Safe_Delete(m_pEffect);
		Safe_Release(m_pInputLayout);
		return E_FAIL;
	}
#pragma endregion DebugDraw

	//m_vLinearSpeed = m_vMaxLinearSpeed;

	return S_OK;
}

void CAgentController::Tick(_float fTimeDelta)
{
	Move(fTimeDelta);

	if (true == CanMove(m_pTransform->GetPosition()))
	{
		for (_int i = 0; i < m_dqPath.size(); ++i)
		{
			if (m_pCurrentCell == m_dqPath[i].first)
			{
				for (_int j = 0; j <= i; ++j)
				{
					m_dqPath.pop_front();
				}
			}
		}
	}
	else
	{
		m_pTransform->SetPosition(m_vPrePos);
		// sliding = move - (move · 충돌 edge normal) * 충돌 edge normal
		/*Vec3 vDir = m_pTransform->GetPosition() - m_vPrePos;
		Vec3 vPassedLine = m_pNavMeshAgent->GetPassedEdgeNormal(m_pTransform->GetPosition());

		m_pTransform->SetPosition(m_vPrePos + vDir - vDir.Dot(vPassedLine) * vPassedLine);*/
	}

}

void CAgentController::LateTick(_float fTimeDelta)
{
	m_vPrePos = m_pTransform->GetPosition();
}

_bool CAgentController::IsIdle()
{
	return !IsMoving();
}

_bool CAgentController::IsMoving()
{
	return m_isMoving;
}

void CAgentController::ForceHeight()
{
	m_pTransform->Translate(Vec3(0.f, GetHeightOffset(), 0.f));
}

_float CAgentController::GetHeightOffset()
{
	Vec3 vPos(m_pTransform->GetPosition());

	const array<Vec3, POINT_END>* vPoints = &m_pCurrentCell->vPoints;

	Plane vPlane = Plane(
		m_pCurrentCell->vPoints[POINT_A],
		m_pCurrentCell->vPoints[POINT_B],
		m_pCurrentCell->vPoints[POINT_C]
	);

	return -((vPlane.x * vPos.x + vPlane.z * vPos.z + vPlane.w) / vPlane.y + vPos.y);
}

void CAgentController::Move(_float fTimeDelta)
{
	if (false == IsMoving()) { return; }

	Vec3 vDistance = Vec3::Zero;
	Vec3 vMoveAmount = Vec3::Zero;
	Vec3 vDirection = Vec3::Zero;

	// waypoint는 임시로 무게중심.
	//if (1 >= m_vecPath.size()) // already in destcell or no pathfinding
	if (1 >= m_dqWayPoints.size()) // already in destcell or no pathfinding
	{
		vDistance = m_vDestPos - m_pTransform->GetPosition();
		vDistance.Normalize(OUT vDirection);

		vMoveAmount = fTimeDelta * m_vLinearSpeed * vDirection;
		if (vMoveAmount.LengthSquared() > vDistance.LengthSquared())
		{	// end
			vMoveAmount = vDistance;
			//m_vecPath.pop_back();
			m_dqWayPoints.pop_front();
			m_isMoving = false;
		}
	}
	else
	{
		//vDistance = m_vecPath[m_vecPath.size() - 2].first->GetCenter() - m_pTransform->GetPosition();
		vDistance = m_dqWayPoints[1] - m_pTransform->GetPosition();
		vDistance.Normalize(OUT vDirection);

		vMoveAmount = fTimeDelta * m_vLinearSpeed * vDirection;
		if (vMoveAmount.LengthSquared() > vDistance.LengthSquared())
		{	// to next waypoint
			m_dqWayPoints.pop_front();
		}
	}
	
	m_pTransform->Translate(vMoveAmount);
}

_bool CAgentController::CanMove(Vec3 vPoint)
{
	CellData* pNeighbor = nullptr;

	if (true == m_pCurrentCell->IsOut(vPoint, pNeighbor))
	{
		if (nullptr != pNeighbor)
		{
			while (true)
			{
				if (nullptr == pNeighbor)
					return false;

				if (false == pNeighbor->IsOut(vPoint, pNeighbor))
				{
					m_pCurrentCell = pNeighbor;
					break;
				}
			}
			return true;
		}
		else
			return false;
	}
	else
		return true;
}

_bool CAgentController::AStar()
{
	priority_queue<PQNode, vector<PQNode>, greater<PQNode>> pqOpen;
	map<CellData*, pair<CellData*, LINES>> mapPath;
	map<CellData*, _float> mapCost;

	m_dqPath.clear();
	m_dqPortals.clear();
	m_dqWayPoints.clear();

	{	// start node
		Vec3 vStartPos = m_pTransform->GetPosition();

		_float g = 0.0f;
		Vec3 vDistance = m_vDestPos - vStartPos;
		_float h = vDistance.Length();

		pqOpen.push(PQNode{ g + h, g, m_pCurrentCell });
		mapCost[m_pCurrentCell] = 0.0f;
		mapPath[m_pCurrentCell] = pair(nullptr, LINE_END);
	}

	while (false == pqOpen.empty())
	{
		PQNode tNode = pqOpen.top();

		if (tNode.pCell == m_pDestCell)
		{
			pair<CellData*, LINES> pairCell(m_pDestCell, mapPath[m_pDestCell].second);
			
			m_dqPortals.push_front(pair(m_vDestPos, m_vDestPos));
			while (m_pCurrentCell != pairCell.first)
			{
				m_dqPath.push_front(pairCell);
				m_dqPortals.push_front(pair(pairCell.first->vPoints[pairCell.second], pairCell.first->vPoints[(pairCell.second + 1) % 3]));
				pairCell = mapPath[pairCell.first];
			}
			m_dqPortals.push_front(pair(m_pTransform->GetPosition(), m_pTransform->GetPosition()));

			return true;
		}

		pqOpen.pop();

		for (uint8 i = LINE_AB; i < LINE_END; ++i)
		{
			CellData* pNeighbor = tNode.pCell->pNeighbors[i];	// parent의 인접셀이 nullptr이 아니라면
			if (nullptr != pNeighbor)
			{
				_float g = tNode.g + CellData::CostBetween(tNode.pCell, pNeighbor);
				auto closed = mapCost.find(pNeighbor);
				
				if (mapCost.end() == closed || g < closed->second)	// 갱신해야 한다면	// mapCost도 안쓰도록 수정 필요. g를 쓰는 이유가 없음.
				{
					mapCost[pNeighbor] = g;
					mapPath[pNeighbor] = pair(tNode.pCell, (LINES)i);	// key : parent
																		// value : parent의 neighbor index
					pqOpen.push(PQNode{ g + CellData::HeuristicCost(pNeighbor, m_vDestPos), g, pNeighbor });
				}
			}
		}
	}

	// TODO: 가장 가까운 노드 반환하도록
	return false;
}

void CAgentController::SSF()
{
	Vec3 vPortalApex = m_pTransform->GetPosition();		// 초기 상태
	Vec3 vPortalLeft = m_pTransform->GetPosition();
	Vec3 vPortalRight = m_pTransform->GetPosition();

	_int iApexIndex = 0;
	_int iLeftIndex = 0;
	_int iRightIndex = 0;

	m_dqWayPoints.push_back(vPortalApex);

	for (_int i = 1; i < m_dqPortals.size(); ++i)
	{
		const Vec3& vLeft = m_dqPortals[i].first;
		const Vec3& vRight = m_dqPortals[i].second;

		if (TriArea2x(vPortalApex, vPortalRight, vRight) <= 0.0f)
		{
			if (vPortalApex == vPortalRight || TriArea2x(vPortalApex, vPortalLeft, vRight) > 0.0f)
			{
				vPortalRight = vRight;					// funnel 당기기
				iRightIndex = i;
			}
			else
			{
				m_dqWayPoints.push_back(vPortalLeft);	// Right가 Left를 넘었다면 Left를 waypoint에 추가				

				vPortalApex = vPortalLeft;				// L을 새로운 시작점으로
				iApexIndex = iLeftIndex;

				vPortalRight = vPortalApex;				// 초기화
				iRightIndex = iApexIndex;

				i = iApexIndex;							// 재시작 인덱스
				continue;
			}
		}

		if (TriArea2x(vPortalApex, vPortalLeft, vLeft) >= 0.0f)
		{
			if (vPortalApex == vPortalLeft || TriArea2x(vPortalApex, vPortalRight, vLeft) < 0.0f)
			{
				vPortalLeft = vLeft;
				iLeftIndex = i;
			}
			else
			{
				m_dqWayPoints.push_back(vPortalRight);

				vPortalApex = vPortalRight;
				iApexIndex = iRightIndex;

				vPortalLeft = vPortalApex;
				iLeftIndex = iApexIndex;

				i = iApexIndex;
				continue;
			}
		}
	}

	// Append last point to path.
	// TODO : 개선할 순 없는가...
	/*if ((false == m_dqPath.empty()
		&& false == m_dqWayPoints.empty())
		&& (m_dqWayPoints[m_dqWayPoints.size() - 1] == m_dqPath[m_dqPath.size() - 1].first->vPoints[m_dqPath[m_dqPath.size() - 1].second]
		|| m_dqWayPoints[m_dqWayPoints.size() - 1] == m_dqPath[m_dqPath.size() - 1].first->vPoints[(m_dqPath[m_dqPath.size() - 1].second + 1) % 3]))
	{
		m_dqWayPoints.pop_back();
	}*/

	m_dqWayPoints.push_back(m_vDestPos);
}



CellData* CAgentController::FindCellByPosition(const Vec3& vPosition)
{	// 일단 brute force로 구현 후 개선.
	CellData* pCell = nullptr;

	for (_int i = 0; i < m_pCells->size(); ++i)
	{
		if (false == (*m_pCells)[i]->IsOut(vPosition, pCell))
		{
			return (*m_pCells)[i];
		}
	}

	return nullptr;
}

_bool CAgentController::Pick(CTerrain* pTerrain, _uint screenX, _uint screenY)
{
	_float fDistance = 0.0f;
	Vec3 vPickedPos;
	if (false == pTerrain->Pick(screenX, screenY, vPickedPos, fDistance, pTerrain->GetTransform()->WorldMatrix()))
	{
		return false;
	}

	m_pDestCell = FindCellByPosition(vPickedPos);

	if (nullptr == m_pDestCell)
	{
		return false;
	}

	m_vDestPos = vPickedPos;

	if (true == AStar())
	{
		//if (m_vecPath.size() > 1)
		{
			SSF();
		}
		m_isMoving = true;
		return true;
	}
	
	m_vDestPos = m_pTransform->GetPosition();
	m_pDestCell = nullptr;

	return false;
}

void CAgentController::Input(_float fTimeDelta)
{
}

void CAgentController::DebugRender()
{
	if (false == m_dqPath.empty())
	{
		m_pBatch->Begin();
		for (_int i = m_dqPath.size() - 1; i >= 0; --i)
		{
			for (uint8 j = LINE_AB; j < LINE_END; ++j)
			{
				if (j == m_dqPath[i].second)
				{
					m_pBatch->DrawLine(
						VertexPositionColor(m_dqPath[i].first->vPoints[j], Colors::Blue),
						VertexPositionColor(m_dqPath[i].first->vPoints[(j + 1) % POINT_END], Colors::Blue));
				}
				else
				{
					m_pBatch->DrawLine(
						VertexPositionColor(m_dqPath[i].first->vPoints[j], Colors::Cyan),
						VertexPositionColor(m_dqPath[i].first->vPoints[(j + 1) % POINT_END], Colors::Cyan));
				}
			}
		}
		m_pBatch->End();
	}

	if (false == m_dqWayPoints.empty())
	{
		m_pBatch->Begin();
		for (size_t i = 0; i < m_dqWayPoints.size() - 1; ++i)
		{
			m_pBatch->DrawLine(
				VertexPositionColor(m_dqWayPoints[i], Colors::Coral),
				VertexPositionColor(m_dqWayPoints[i + 1], Colors::Coral));
		}
		m_pBatch->End();
	}	
}

CAgentController* CAgentController::Create(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	CAgentController* pInstance = new CAgentController(pDevice, pContext);

	if (FAILED(pInstance->Initialize_Prototype()))
	{
		MSG_BOX("Failed to Created : CAgentController");
		Safe_Release(pInstance);
	}

	return pInstance;
}

CComponent* CAgentController::Clone(CGameObject* pGameObject, void* pArg)
{
	CAgentController* pInstance = new CAgentController(*this);
	pInstance->m_pGameObject = pGameObject;

	if (FAILED(pInstance->Initialize(pArg)))
	{
		MSG_BOX("Failed To Cloned : CAgentController");
		Safe_Release(pInstance);
	}
	return pInstance;
}

void CAgentController::Free()
{
	// DebugDraw
	Safe_Delete(m_pBatch);
	Safe_Delete(m_pEffect);
	Safe_Release(m_pInputLayout);

	Super::Free();
}
