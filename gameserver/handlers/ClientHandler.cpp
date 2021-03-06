﻿#include "../header.h"
#include "ClientHandler.h"
#include "../../utils/Utility.h"
#include "../../protocol/client.pb.h"
#include "../scenemng-alpha/SceneMng.h"
#include "../plane_shooting/SceneMng2.h"

void ClientHandler::HandleConnect(IConnection* pConn)
{
	TRACELOG("client connect=[" << pConn << "] ip=[" << pConn->GetRemoteIp() << "], port=[" << pConn->GetRemotePort() << "]");
}

void ClientHandler::HandleDisconnect(IConnection* pConn, const BoostErrCode& error)
{   
	TRACELOG("client " << pConn->GetRemoteIp() << " disconnect, conntion=[" << pConn << "]");
	if (error)
	{
		ERRORLOG("handle disconnect error, error=[" << boost::system::system_error(error).what() << "]");
	}
	plane_shooting::SceneMng::GetInstance()->PlaneDisconnect(pConn);
}

void ClientHandler::HandleWrite(const boost::system::error_code& error, size_t bytes_transferred) 
{
	if (error)
	{
		ERRORLOG("handle write error, error=[" << boost::system::system_error(error).what() << "], bytes_transferred=[" << bytes_transferred << "]");
	}
}

void ClientHandler::HandleRecv(IConnection* pConn, const char* pBuf, uint32_t uLen)
{
	if (!pBuf || uLen == 0)
	{
		ERRORLOG("message error");
		return;
	}
	
	MessageHeader* pMsgHeader = (MessageHeader*)pBuf;

	map<uint32_t, pCmdFunc>::iterator funcIt = m_cmdFuncMap.find(pMsgHeader->uMsgCmd);
	if (funcIt == m_cmdFuncMap.end())
	{
		ERRORLOG("cannot find cmd=[0x" << hex << pMsgHeader->uMsgCmd << "] function");
		return;
	}
	static uint32_t uMsgProc = 0;
	static uint64_t uStartProcTime = cputil::GetSysTime();
	++uMsgProc;
	if (uMsgProc % 100000 == 0)
	{
		uint64_t uNow = cputil::GetSysTime();
		cout << "proc msg times=[" << uMsgProc << "], time cost=[" << uNow - uStartProcTime << "]" << endl;
		uMsgProc = 0;
		uStartProcTime = cputil::GetSysTime();
	}

	pCmdFunc& cmdFunc = funcIt->second;
	(this->*cmdFunc)(pConn, pMsgHeader);
	return;
}

void ClientHandler::_RegisterCmd(uint32_t uCmdId, pCmdFunc cmdFunc)
{
	m_cmdFuncMap.insert(make_pair(uCmdId, cmdFunc));
}

void ClientHandler::_RegisterAllCmd()
{
	// 网络基本功能测试
	//_RegisterCmd(ID_REQ_Test_PingPong,					&ClientHandler::_RequestTestPingPong);			// 测试使用的ping-pong协议, 简单的将数据包返回
	_RegisterCmd(client::ClientProtocol::REQ_ENTER_GAME,	&ClientHandler::_RequestEnterGame);
	//_RegisterCmd(client::ClientProtocol::REQ_QUERY_PATH,	&ClientHandler::_RequestQueryPath);
	_RegisterCmd(client::ClientProtocol::REQ_PLANE_MOVE,	&ClientHandler::_RequestPlaneMove);
	_RegisterCmd(client::ClientProtocol::REQ_PLANE_SHOOT,	&ClientHandler::_RequestPlaneShoot);
	return;
}

void ClientHandler::_RequestTestPingPong(IConnection* pConn, MessageHeader* pMsgHeader)
{
	pConn->SendMsg((char*)pMsgHeader, pMsgHeader->uMsgSize);
}

void ClientHandler::_RequestEnterGame(IConnection* pConn, MessageHeader* pMsgHeader)
{
	if (!pConn || !pMsgHeader)
	{
		return;
	}

	client::EnterGameReq enterGameReq;
	const char* pBuf = (const char*)pMsgHeader;
	enterGameReq.ParseFromArray(pBuf + sizeof(MessageHeader), pMsgHeader->uMsgSize - sizeof(MessageHeader));

	plane_shooting::SceneMng::GetInstance()->PlayerEnter(pConn);
	return;
}

void ClientHandler::_RequestQueryPath(IConnection* pConn, MessageHeader* pMsgHeader) 
{
	if (!pConn || !pMsgHeader)
	{
		return;
	}

	client::QueryPathReq queryPathReq;
	const char* pBuf = (const char*)pMsgHeader;
	queryPathReq.ParseFromArray(pBuf + sizeof(MessageHeader), pMsgHeader->uMsgSize - sizeof(MessageHeader));

	float startPos[3];
	float endPos[3];
	startPos[0] = queryPathReq.startpos().x();
	startPos[1] = queryPathReq.startpos().y();
	startPos[2] = queryPathReq.startpos().z();

	endPos[0] = queryPathReq.endpos().x();
	endPos[1] = queryPathReq.endpos().y();
	endPos[2] = queryPathReq.endpos().z();

	client::QueryPathAck queryPathAck;

	int path = scene_alpha::SceneMng::getInstance()->findPath(startPos, endPos);
	float* smoothPath = scene_alpha::SceneMng::getInstance()->getPath();
	for (int i = 0; i < path * 3;) {
		client::PBVector* pVector = queryPathAck.add_path();
		pVector->set_x(smoothPath[i]);
		pVector->set_y(smoothPath[i+1]);
		pVector->set_z(smoothPath[i+2]);
		i += 3;
	}

	string strResponse;
	//cputil::BuildResponseProto(queryPathAck, strResponse, client::ClientProtocol::REQ_QUERY_PATH);
	pConn->SendMsg(strResponse.c_str(), strResponse.size());

	return;
}

void ClientHandler::_RequestPlaneMove(IConnection* pConn, MessageHeader* pMsgHeader) 
{
	if (!pConn || !pMsgHeader) {
		return;
	}

	client::PlaneMoveReq planeMoveReq;
	const char* pBuf = (const char*)pMsgHeader;
	planeMoveReq.ParseFromArray(pBuf + sizeof(MessageHeader), pMsgHeader->uMsgSize - sizeof(MessageHeader));

	Plane* pPlane = plane_shooting::SceneMng::GetInstance()->GetPlaneByConn(pConn);
	if (!pPlane) {
		return;
	}
	plane_shooting::Vector2D newPos;
	newPos.x = planeMoveReq.newpos().x();
	newPos.y = planeMoveReq.newpos().y();
	plane_shooting::SceneMng::GetInstance()->PlaneMove(pPlane, newPos, planeMoveReq.fireangle(), planeMoveReq.flyangle());

	return;
}

void ClientHandler::_RequestPlaneShoot(IConnection* pConn, MessageHeader* pMsgHeader) 
{ 
	if (!pConn || !pMsgHeader) {
		return;
	}

	Plane* pPlane = plane_shooting::SceneMng::GetInstance()->GetPlaneByConn(pConn);
	if (!pPlane)
	{
		return;
	}

	plane_shooting::SceneMng::GetInstance()->PlaneShoot(pPlane);
}